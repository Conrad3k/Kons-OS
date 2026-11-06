# Komponent: ekran — obraz i okna (`/rdzen/system/ekran`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2–M4) — `scripts/test-m2.sh`, `scripts/test-m4.sh` (zrzuty), testy w systemie (klatka, złośliwe IPC, restart) |
| **Kod** | `src/ekran/main.c` (kompozytor, wejście), `src/ekran/drm.c` (KMS), renderer `src/grafika/k1_grafika.c`, protokoły `src/wspolne/k1_zdarzenie.c`, `k1_komunikat.c` |
| **Decyzje** | ADR 0006, ADR 0007 (klatka, kanały), ADR 0008 (okna), ADR 0010 (simpledrm); wygląd: `docs/WYGLAD.md` |

## Cel
Jedyny proces z dostępem do karty graficznej (DRM/KMS) i kompozytor okien: ustawia tryb, rysuje
tapetę (jeden z 4 wariantów), składa okna aplikacji i panele powłoki z ich pamięci współdzielonej,
dodaje cienie okien i kursor, robi page flip. Kieruje wejście (mysz do powierzchni pod kursorem,
klawiatura do okna z fokusem, Super do powłoki), zarządza fokusem, kolejnością, przeciąganiem
i minimalizacją okien, przechowuje ustawienia i rozsyła je klientom.

## Granica zaufania
Ufa jądru (DRM) i nadzorcy (kanał sterujący, UID 0 sprawdzany). **Nie ufa** klientom: każdy
komunikat sprawdzany pole po polu, UID nadawcy każdego pakietu porównywany z tożsamością od
nadzorcy, role egzekwowane (panele i cudze okna — tylko powłoka; ustawienia — tylko Ustawienia),
powierzchnia przyjmowana tylko jako memfd z pieczęciami SHRINK/GROW/SEAL i dokładnym rozmiarem,
mapowana tylko do odczytu; po 50 błędach klient jest rozłączany. Treść pikseli klientów jest
tylko kopiowana i mieszana (bez interpretacji).

## Uprawnienia
UID/GID 101, bez capabilities, `NO_NEW_PRIVS`, seccomp (baza + `ioctl` typu `'d'` = DRM),
Landlock bez żadnej ścieżki, cgroup `ekran` (128 MiB, 1 proces). Kartę otwiera nadzorca (to
otwarcie jest DRM masterem) i przekazuje jako fd 4.

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — `KLIENT(rola, aplikacja, uid)` + koniec kanału klienta.
- Kanały klientów: wejście (`k1_zdarzenie` 16 B, do 4 źródeł), powłoka i aplikacje (`k1_komunikat`
  32 B, ADR 0008), razem do 12.
- Karta: dwa bufory „dumb” XRGB8888 w trybie preferowanym (QEMU `bochs` 1280×800; simpledrm — tryb
  z firmware), scena i tapeta w RAM.
- Log: tryb, pierwsza klatka, okna (powstanie, przesunięcie, minimalizacja, zamknięcie), ustawienia,
  odrzucone komunikaty (z powodem, ≤ 8 na klienta), stan kursora ≤ 2×/s. **Bez kodów klawiszy.**

## Zachowanie
- Scena składana tylko w uszkodzonych prostokątach (do 16, potem prostokąt obejmujący), osobno
  dla każdego z dwóch buforów; kursor programowy odtwarzany ze sceny.
- Nowe okno: na środku z przesunięciem kaskadowym, dostaje fokus; kliknięcie okna — fokus
  i wierzch; okno nie wchodzi pod górny pasek, pasek tytułu zostaje w zasięgu.
- Prędkość myszy: 0,5× / 1× / 2× (ustawienie). Bez okna z fokusem strzałki przesuwają kursor o 20 px.
- Zmiana tapety: ponowne liczenie (ok. 2 s w emulacji TCG) i przerysowanie.

## Zależności
musl (statycznie, w tym libm), jądro: DRM + `bochs` (QEMU) albo `simpledrm` (firmware). Bez libdrm.

## Wywołania systemowe
`ioctl` (DRM: GET_CAP, MODE_GETRESOURCES/GETCONNECTOR/GETENCODER, CREATE_DUMB, ADDFB, MAP_DUMB,
SETCRTC, PAGE_FLIP, DIRTYFB), `mmap` (bufory, powierzchnie — bez `PROT_EXEC`), `munmap`, `fcntl`
(`F_GET_SEALS`), `fstat`, `recvmsg`/`sendmsg`, `read`, `poll`, `close`, `clock_gettime`, `write` (log).

## Obsługa błędów
Brak karty/monitora/trybu → `k1_koniec()` (nadzorca uruchomi ponownie). Zamknięty kanał klienta →
usunięcie jego powierzchni (powłoka dostaje „okno zamknięte”). Zamknięty kanał sterujący → koniec.

## Powierzchnia ataku
Parsery komunikatów 16 B i 32 B (testy + fuzzing), zdarzenia DRM (sprawdzana długość), deskryptory
od klientów (tylko memfd z pieczęciami), piksele klientów (kopiowanie z przycinaniem). Sterownik DRM
w jądrze (ioctl).

## Testy
| Test | Rodzaj |
|---|---|
| `test-m2.sh`: tryb, pierwsza klatka, mysz, strzałki, przycisk — log + zrzuty | funkcjonalny |
| `test-m4.sh`: okna, przeciąganie, fokus, minimalizacja, motyw, tapeta — log + zrzuty | funkcjonalny |
| w systemie: drugi proces nie przejmie ekranu (`SET_MASTER` → EBUSY) | negatywny |
| w systemie: złośliwe zdarzenia wejścia i obcy nadawca (`odrzucone 5`) | negatywny (IPC) |
| w systemie: powierzchnia bez memfd / bez pieczęci / zły rozmiar, panel od aplikacji, ustawienia od aplikacji, sterowanie cudzym oknem, okno od obcego UID — odrzucone; poprawne przyjęte (RAMKA) | negatywny (IPC) |
| w systemie: klatka, próba w klatce; awaria ekranu → wraca z klientami | negatywny |
| host: `test_komunikat`, `fuzz_komunikat`, `test_zdarzenie`, `fuzz_zdarzenie`, renderer z ASan/UBSan | jednostkowy / fuzzing |

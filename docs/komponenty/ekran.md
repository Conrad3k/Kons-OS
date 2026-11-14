# Komponent: ekran — obraz i okna (`/rdzen/system/ekran`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2–M4) — `scripts/test-m4.sh` (zrzuty), testy w systemie (klatka, złośliwe IPC, restart) |
| **Kod** | `src/ekran/main.c` (start, kanał sterujący, pętla, zmiana trybu), `scena.c` (składanie, wyjście), `okna.c` (klienci, okna, fokus, komunikaty), `powierzchnie.c` (pamięć, nowa powierzchnia, zatwierdzanie; 0.9.0), `wskaznik.c` (mysz, klawiatura), `drm.c` (KMS, lista trybów), renderer `src/grafika/k1_grafika.c`, protokoły `src/wspolne/k1_zdarzenie.c`, `k1_komunikat.c` |
| **Decyzje** | ADR 0006, ADR 0007 (klatka, kanały), ADR 0008 (okna), ADR 0010 (simpledrm), ADR 0012 (przegląd, tryby); wygląd: `docs/WYGLAD.md` |

## Cel
Jedyny proces z dostępem do karty graficznej (DRM/KMS) i kompozytor okien: ustawia tryb, rysuje
tapetę (jeden z 5 wariantów), składa okna aplikacji i panele powłoki z ich pamięci współdzielonej,
dodaje cienie okien i kursor, robi page flip. Kieruje wejście (mysz do powierzchni pod kursorem,
klawiatura do okna z fokusem, Super do powłoki), zarządza fokusem, kolejnością, przeciąganiem
i minimalizacją okien, przechowuje ustawienia i rozsyła je klientom.

## Granica zaufania
Ufa jądru (DRM) i nadzorcy (kanał sterujący, UID 0 sprawdzany). **Nie ufa** klientom: każdy
komunikat sprawdzany pole po polu, UID nadawcy każdego pakietu porównywany z tożsamością od
nadzorcy, role egzekwowane (panele i cudze okna — tylko powłoka; ustawienia — tylko Ustawienia),
powierzchnia przyjmowana tylko jako memfd z pieczęciami SHRINK/GROW/SEAL i dokładnym rozmiarem,
mapowana tylko do odczytu, łącznie najwyżej 2 pełne ekrany na klienta (aplikacja i powłoka — od 0.7.5 z warstwą pulpitu; ekran hasła: 1); obszar
zatwierdzenia przycinany do własnej powierzchni; po 50 błędach klient jest rozłączany. Treść pikseli klientów jest
tylko kopiowana i mieszana (bez interpretacji).

## Uprawnienia
UID/GID 101, bez capabilities, `NO_NEW_PRIVS`, seccomp (baza + `ioctl` typu `'d'` = DRM + `memfd_create` —
ustawienia i zdjęcie tapety dla skarbca),
Landlock bez żadnej ścieżki, cgroup `ekran` (176 MiB — z miejscem na zdjęcie na tapetę, 0.8.0; 1 proces). Kartę otwiera nadzorca (to
otwarcie jest DRM masterem) i przekazuje jako fd 4.

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — `KLIENT(rola, aplikacja, uid)` + koniec kanału klienta.
- Rola `LOGOWANIE` (0.5.7, ADR 0017): panele jak powłoka; dopóki taki klient jest połączony, dostaje
  każdy klawisz (także Super).
- fd 5 (0.5.6, opcjonalny): kanał do skarbca — przy starcie odczyt `K1_OB_USTAWIENIA` (≤ 1,5 s; tapeta,
  motyw, mysz, sekundy, rozdzielczość jako szer.×wys.; każda wartość sprawdzana jak `USTAW`, zła →
  domyślna), po każdej zmianie zapis nowej wersji (`src/ekran/zapis.c`). Rozdzielczość przywracana po
  pierwszej klatce, jeśli karta ją ma. Format zapisu: `K1U3` (0.8.2: miejsce na domyślne aplikacje 32 typów plików —
  GIF, BMP i WebP dały 9 typów, a `K1U2` mieścił 8); ekran czyta też `K1U2` (0.8.0–0.8.1) i `K1U1` (0.5.6–0.7.6) —
  typy, których stary zapis nie ma, zostają domyślne. Od 0.8.2 krok 4 zapis `K1U4`: w `K1U1`–`K1U3` aplikacją zdjęć
  mogły być tylko Pliki (nie wybór użytkownika), więc dla typów rodziny „obrazy” ekran bierze wtedy domyślną —
  Podgląd (ADR 0037).
- Zdjęcie na tapetę (0.8.0, ADR 0032, `src/ekran/zdjecie.c`): przyjmuje je tylko od aplikacji z tabeli nadawców —
  Plików i (od 0.8.2, ADR 0037) Podglądu (`TAPETA_ZDJECIE`:
  piksele z dekodera, do 2560×1600, zapieczętowany memfd dokładnie tego rozmiaru — pliku zdjęcia ekran nie widzi).
  Rysuje je „wypełniając” ekran (wyśrodkowane, przycięte po bokach albo u góry i dołu; `k1_skaluj`) przy każdej
  rozdzielczości. W skarbcu jako własne obiekty: `K1_OB_ZDJECIE` („K1Z1”, szerokość, wysokość — zapisywany na końcu)
  i do 12 kawałków po 1 MiB (3 bajty na piksel); przy starcie wczytane tylko w całości. `K1_UST_ZDJECIE` (0/1) mówi
  klientom, czy zdjęcie jest — ustawia go tylko ekran; `USTAW` tapety = zdjęcie bez zdjęcia jest odrzucany.
- Kanały klientów: wejście (`k1_zdarzenie` 16 B, do 4 źródeł), powłoka i aplikacje (`k1_komunikat`
  32 B, ADR 0008), razem do 12.
- Karta: dwa bufory „dumb” XRGB8888 w trybie preferowanym (QEMU `bochs` 1280×800; simpledrm — tryb
  z firmware; vmwgfx — VirtualBox VMSVGA/VMware), scena i tapeta w RAM. Przy `simpledrm` (0.5.4,
  rozpoznany przez `DRM_IOCTL_VERSION`): jeden bufor i `DIRTYFB` tylko ze zmienionymi prostokątami
  (≤ 17) — page flip w simpledrm kopiuje cały ekran do pamięci firmware, co na prawdziwym sprzęcie
  (ThinkPad X230) zacinało pulpit. Lista trybów (różne
  rozmiary 800×600…1920×1200 + preferowany, do 18) → Ustawienia (`K1_KOM_TRYB`); `K1_UST_ROZDZIELCZOSC` przełącza
  tryb między klatkami (nowe bufory → `SETCRTC` → zwolnienie starych; błąd = poprzedni tryb), po
  czym klienci dostają `WITAJ` z nowym rozmiarem.
- Log: tryb, pierwsza klatka, okna (powstanie, przesunięcie, minimalizacja, zamknięcie), ustawienia,
  odrzucone komunikaty (z powodem, ≤ 8 na klienta), stan kursora ≤ 2×/s. **Bez kodów klawiszy.**

## Komunikaty (kanały klientów, `k1_komunikat` 32 B, ADR 0008)
Pola: `obiekt` = numer powierzchni klienta 1…8 (o ile nie podano inaczej), `a`…`e` jak w `k1_komunikat.h`.

| Komunikat | Kierunek | Kto może | Znaczenie |
|---|---|---|---|
| `POWIERZCHNIA` | klient → ekran | okno: aplikacja; panel: powłoka i logowanie; pulpit: tylko powłoka, jeden | nowe okno, panel albo warstwa pulpitu (0.7.5): szerokość, wysokość, rodzaj, położenie panelu; + memfd z pieczęciami |
| `ZATWIERDZ` | klient → ekran | właściciel powierzchni | gotowa połowa bufora i zmieniony prostokąt |
| `POKAZ` | klient → ekran | powłoka, logowanie | pokaż / ukryj panel |
| `PRZESUN` | klient → ekran | powłoka, logowanie | nowe położenie panelu |
| `PRZECIAGNIJ` | klient → ekran | właściciel okna | zacznij przesuwać okno wciśniętym przyciskiem myszy |
| `MINIMALIZUJ` | klient → ekran | właściciel okna | schowaj okno (wraca przez powłokę) |
| `USUN` | klient → ekran | właściciel powierzchni | usuń powierzchnię |
| `AKTYWUJ_OKNO` | powłoka → ekran | tylko powłoka | `obiekt` = numer okna: pokaż, na wierzch, fokus |
| `MINIMALIZUJ_OKNO` | powłoka → ekran | tylko powłoka | `obiekt` = numer okna: schowaj |
| `ZAMKNIJ_OKNO` | powłoka → ekran | tylko powłoka | `obiekt` = numer okna: ekran prosi aplikację o zamknięcie (0.7.5, „Zamknij okno” na dolnym pasku) |
| `USTAW` | Ustawienia → ekran | Ustawienia; klucze `K1_UST_OTWIERANIE` + typ także Pliki i powłoka (0.8.2, „Zawsze otwieraj tą aplikacją”; `ustawia()` w `okna.c`) | klucz `K1_UST_*` i wartość (sprawdzana); nie `K1_UST_ZDJECIE` |
| `TAPETA_ZDJECIE` | Pliki, Podgląd → ekran | tabela nadawców (`zdjecie.c`) | zdjęcie na tapetę (0.8.0): szerokość, wysokość; + memfd z pikselami od dekodera |
| `WITAJ` | ekran → klient | — | rozmiar ekranu, rola, aplikacja; ponownie po zmianie rozdzielczości |
| `WSKAZNIK` | ekran → klient | — | mysz nad powierzchnią klienta (0 = poza nią): x, y, ruch / przycisk, który przycisk, e = wciśnięte Shift / Ctrl / Alt / AltGr (`K1_MOD_*`, 0.8.2 krok 6: Ctrl+klik i Shift+klik w Plikach) |
| `KLAWISZ` | ekran → klient | — | klawisz dla okna z fokusem (kod, w dół / w górę / powtórzenie, modyfikatory); nigdy w logu |
| `FOKUS` | ekran → klient | — | okno dostało / straciło klawiaturę |
| `RAMKA` | ekran → klient | — | ostatnie zatwierdzenie jest na ekranie, można rysować drugą połowę |
| `ZAMKNIJ` | ekran → aplikacja | — | zamknij to okno — jak przycisk × (0.7.5; Notatki najpierw zapisują) |
| `OKNO` | ekran → powłoka | — | okno aplikacji powstało / zmieniło stan / zniknęło (dolny pasek); okno strefy (0.10.0): `a` = 0, `c` = numer strefy |
| `USTAWIENIE` | ekran → wszyscy | — | nowa wartość ustawienia (motyw, tapeta, mysz, sekundy, rozdzielczość, czy jest zdjęcie na tapetę, domyślne aplikacje) |
| `TRYB` | ekran → Ustawienia | — | jeden z trybów ekranu (rozmiar, Hz, czy zalecany) |

**Rola `STREFA` (0.10.0, ADR 0042; `strefy.c`).** VMM strefy jest klientem z jednym oknem (bez paneli, ustawień,
tapety). Ekran rysuje mu **ramkę** (D1: szkic B2): obwódkę 3 px w kolorze strefy z `K1_STREFY` na zewnątrz okna — przed
obrazem strefy, więc strefa nie może jej zasłonić — i **znaczek** z ikoną strefy na lewym górnym rogu, po obrazie strefy;
kursor na znaczku rozwija **plakietkę** (nazwa · „osobna maszyna”, minimalizuj, zamknij; zostaje otwarta do 40 px od
niej). Wskaźnik na ramce, znaczku i plakietce nie trafia do strefy: wciśnięcie znaczka przesuwa okno (górna krawędź
okna nie wyżej niż pasek + 8 px, żeby znaczek było widać), minimalizuj chowa okno, zamknij wysyła VMM `ZAMKNIJ`.
Log: `strefa: plakietka okna N (minimalizuj X,Y, zamknij X,Y)` (testy klikają według niego).

Kanał sterujący (fd 3) od nadzorcy: `KLIENT` (rola `STREFA`: `b` = numer strefy). Złe pole, zła rola albo obcy UID → komunikat odrzucony
z wpisem w logu (≤ 8 na klienta), po 50 — klient rozłączony.

## Doręczanie (0.8.5 krok 1)
Ekran nigdy nie czeka na klienta. Gdy kanał aplikacji jest pełny (aplikacja akurat długo liczy), to, co ekran
do niej wysyła, czeka w ekranie (`zalegle.c`, do 64 komunikatów na klienta) i idzie, gdy kanał znów przyjmuje
(`poll` z `POLLOUT` tylko dla klientów z zaległymi), w tej samej kolejności. Zasady:
- `RAMKA` czeka najwyżej raz na powierzchnię (druga mówiłaby to samo) i **nigdy nie przepada** — bez niej okno
  już nigdy by się nie odświeżyło;
- ruch wskaźnika zastępuje ruch tuż przed nim (liczy się ostatnie położenie), `USTAWIENIE` — wcześniejsze o tym
  samym kluczu;
- wciśnięcia i puszczenia przycisków, klawisze, `FOKUS`, `ZAMKNIJ` — w kolejności nadania;
- klient, który w ogóle nie odbiera (kolejka pełna): najpierw przepadają najstarsze ruchy i powtórzenia klawisza,
  potem wciśnięcia; `PUSZCZENIE` i puszczony klawisz — dopiero gdy nic innego nie zostało. Log: „klient … nie
  odbiera: porzucono n ruchów i klawiszy, m innych” (liczby, bez kodów klawiszy, ≤ 2 wiersze/s).

Ile mieści sam kanał: kanały to pary gniazd `SOCK_SEQPACKET` (`socketpair`). Limit
`net.unix.max_dgram_qlen` (domyślnie 10) **nie dotyczy** pary połączonych gniazd — `unix_dgram_sendmsg`
w `net/unix/af_unix.c` (jądro 6.18) sprawdza pełną kolejkę odbiorcy tylko wtedy, gdy odbiorca nie jest połączony
z nadawcą (`unix_peer(other) != sk`). Granicą jest bufor nadawcy (`SO_SNDBUF`, domyślnie `net.core.wmem_default`
= 212 992 B): komunikat 32 B zajmuje w nim ok. 760 B, więc kanał mieści ok. 278 komunikatów (zmierzone:
`socketpair` + `send(MSG_DONTWAIT)` do `EAGAIN`). Przy myszy 125–1000 zdarzeń/s to 0,3–2 s ruchów — dopiero
potem działa kolejka w ekranie.

## Zachowanie
- Scena składana tylko w uszkodzonych prostokątach (do 16, potem prostokąt obejmujący), osobno
  dla każdego z dwóch buforów; kursor programowy odtwarzany ze sceny.
- Nowe okno: na środku z przesunięciem kaskadowym. Fokus (ochrona klawiatury, ADR 0012) dostaje,
  gdy żadne okno nie ma fokusu, okno z fokusem należy do tej samej aplikacji albo to pierwsze okno
  aplikacji połączonej ≤ 10 s temu; inaczej otwiera się tuż pod oknem z fokusem. Kliknięcie okna — fokus
  i wierzch; okno nie wchodzi pod górny pasek, pasek tytułu zostaje w zasięgu.
- Prędkość myszy: 0,5× / 1× / 2× (ustawienie). Bez okna z fokusem strzałki przesuwają kursor o 20 px.
- Zmiana tapety: ponowne liczenie (ok. 2 s w emulacji TCG) i przerysowanie.

## Zależności
musl (statycznie, w tym libm), jądro: DRM + `bochs` (QEMU) albo `simpledrm` (firmware). Bez libdrm.

## Wywołania systemowe
`ioctl` (DRM: GET_CAP, MODE_GETRESOURCES/GETCONNECTOR/GETENCODER, CREATE_DUMB, ADDFB, MAP_DUMB,
SETCRTC, PAGE_FLIP, DIRTYFB, RMFB, DESTROY_DUMB, VERSION), `memfd_create` (zapis ustawień, 0.5.6), `mmap` (bufory, powierzchnie — bez `PROT_EXEC`), `munmap`, `fcntl`
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
| `test-m4.sh` (początek): tryb, pierwsza klatka, mysz, strzałki, przycisk — log + zrzuty | funkcjonalny |
| `test-m4.sh`: okna, przeciąganie, fokus, minimalizacja, motyw, tapeta — log + zrzuty | funkcjonalny |
| w systemie: drugi proces nie przejmie ekranu (`SET_MASTER` → EBUSY) | negatywny |
| w systemie: złośliwe zdarzenia wejścia i obcy nadawca (`odrzucone 5`) | negatywny (IPC) |
| w systemie: powierzchnia bez memfd / bez pieczęci / zły rozmiar, panel od aplikacji, ustawienia od aplikacji, sterowanie cudzym oknem, okno od obcego UID — odrzucone; poprawne przyjęte (RAMKA) | negatywny (IPC) |
| w systemie: klatka, próba w klatce; awaria ekranu → wraca z klientami | negatywny |
| w systemie (M4.1): późniejsze okno innej aplikacji nie dostaje fokusu; trzecie pełnoekranowe okno klienta odrzucone (limit pamięci) | negatywny |
| host `test_zalegle`: RAMKA raz na powierzchnię, scalanie ruchów i ustawień, kolejność ważnych, przepełnienie (ruchy i powtórzenia najpierw, RAMKA nigdy) | jednostkowy (ASan/UBSan) |
| w systemie (0.8.5): klient, który nie odbiera, gdy mysz wysyła 1500 ruchów, dostaje potem RAMKA i PUSZCZENIE; inny klient w tym czasie rysuje normalnie | negatywny |
| `test-m4.sh` (M4.1): zmiana rozdzielczości z Ustawień — nowy tryb, zrzut w nowym rozmiarze, panele na nowych miejscach | funkcjonalny |
| host: `test_komunikat`, `fuzz_komunikat`, `test_zdarzenie`, `fuzz_zdarzenie`, renderer z ASan/UBSan | jednostkowy / fuzzing |

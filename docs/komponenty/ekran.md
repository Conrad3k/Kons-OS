# Komponent: ekran (`/rdzen/system/ekran`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2, M3) — `scripts/test-m2.sh` (zrzuty ekranu), testy w systemie (klatka, IPC, restart) |
| **Kod** | `src/ekran/main.c`, renderer `src/grafika/k1_grafika.c`, protokół `src/wspolne/k1_zdarzenie.c` |
| **Decyzje** | ADR 0006, ADR 0007 (klatka, kanały); wygląd: `docs/WYGLAD.md` |

## Cel
Jedyny proces z dostępem do karty graficznej (DRM/KMS): ustawia tryb obrazu, składa klatki
(tapeta z logo + kursor), robi page flip. Od M4 będzie też składał okna rysowane przez `gui`.

## Granica zaufania
Ufa jądru (sterownik DRM). **Nie ufa** komunikatom z kanału wejścia — każde pole jest sprawdzane
(`k1_zdarzenie_sprawdz`), błędne są liczone i odrzucane. Nie przyjmuje innych danych z zewnątrz.

## Uprawnienia
**M3 (ADR 0007):** UID/GID 101, bez capabilities, `NO_NEW_PRIVS`, seccomp (baza + `ioctl` tylko typu
`'d'` = DRM), Landlock bez żadnej ścieżki, cgroup `ekran` (128 MiB, 1 proces). Kartę otwiera nadzorca
(to otwarcie jest DRM masterem) i przekazuje jako fd 4.

## Wejścia i wyjścia
- fd 3: kanał sterujący od nadzorcy — komunikaty `KLIENT(rola, aplikacja, uid)` z końcem kanału
  klienta (`SCM_RIGHTS`); ekran sprawdza, że nadawcą jest root (nadzorca).
- kanały klientów roli WEJSCIE (do 4): komunikaty `k1_zdarzenie` (16 B) — ruch myszy, przyciski,
  klawisze; UID nadawcy każdego pakietu (`SCM_CREDENTIALS`) musi być tym, który podał nadzorca.
- Karta graficzna: dwa bufory „dumb” XRGB8888 w trybie preferowanym przez monitor (QEMU: 1280×800).
- Log: tryb, czas pierwszej klatki, stan kursora (pozycja, przycisk, liczniki zdarzeń) ≤ 2×/s.
  **Bez kodów klawiszy.**

## Zachowanie (M2)
Mysz przesuwa kursor; strzałki — o 20 px; lewy przycisk myszy zmienia kolor kursora na akcent.
Kursor jest programowy: przed każdą klatką z tła w RAM odtwarzany jest fragment spod poprzedniej
pozycji kursora w danym buforze, potem kursor rysowany na nowo i page flip (czekanie na zdarzenie
zakończenia przed kolejną klatką).

## Zależności
musl (statycznie, w tym libm), jądro: DRM + `bochs` (QEMU stdvga). Bez libdrm.

## Wywołania systemowe
`open`, `ioctl` (DRM: GET_CAP, MODE_GETRESOURCES, MODE_GETCONNECTOR, MODE_GETENCODER, MODE_CREATE_DUMB,
MODE_ADDFB, MODE_MAP_DUMB, MODE_SETCRTC, MODE_PAGE_FLIP, MODE_DIRTYFB), `mmap`, `read`, `poll`, `fcntl`,
`clock_gettime`, `nanosleep`, `write` (log), `brk`/`mmap` (malloc tła), `exit_group`.

## Obsługa błędów
Brak karty/monitora/trybu → `k1_koniec()`: komunikat i wyjście z kodem 1 (nadzorca zapisuje to
w logu i uruchamia ekran ponownie). Zamknięty kanał wejścia → źródło usunięte. Zamknięty kanał
sterujący → koniec.
Błędne komunikaty → odrzucone i policzone. Nieprawidłowe zdarzenia DRM (długość) → pominięte.

## Powierzchnia ataku
Parser komunikatów wejścia (mały, testy + fuzzing), parser zdarzeń DRM (dane z jądra, sprawdzana
długość), sterownik DRM w jądrze (ioctl). Renderer nie przetwarza danych zewnętrznych (tylko rozmiar
trybu, ograniczony do 8192×8192).

## Testy
| Test | Rodzaj |
|---|---|
| `test-m2.sh`: tryb, pierwsza klatka, zrzut (logo, tapeta, kursor) | funkcjonalny |
| `test-m2.sh`: ruch myszy, strzałki, przycisk — log + zrzuty | funkcjonalny |
| w systemie: drugi proces nie przejmie ekranu (`SET_MASTER` → EBUSY) | negatywny |
| w systemie: 4 złośliwe komunikaty odrzucone, poprawny przyjęty, poprawny od obcego UID odrzucony | negatywny (IPC) |
| w systemie: awaria ekranu (SIGSEGV) nie zatrzymuje systemu, ekran wraca | negatywny + funkcjonalny |
| w systemie: klatka (UID, capabilities, seccomp, cgroup, deskryptory) i próba w klatce | negatywny |
| host: `test_zdarzenie` (32 przypadki), `fuzz_zdarzenie`, renderer z ASan/UBSan | jednostkowy / fuzzing |

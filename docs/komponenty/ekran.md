# Komponent: ekran (`/rdzen/system/ekran`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M2) — `scripts/test-m2.sh` (zrzuty ekranu), testy w systemie |
| **Kod** | `src/ekran/main.c`, renderer `src/grafika/k1_grafika.c`, protokół `src/wspolne/k1_zdarzenie.c` |
| **Decyzje** | ADR 0006; wygląd: `docs/WYGLAD.md` |

## Cel
Jedyny proces z dostępem do karty graficznej (DRM/KMS): ustawia tryb obrazu, składa klatki
(tapeta z logo + kursor), robi page flip. Od M4 będzie też składał okna rysowane przez `gui`.

## Granica zaufania
Ufa jądru (sterownik DRM). **Nie ufa** komunikatom z kanału wejścia — każde pole jest sprawdzane
(`k1_zdarzenie_sprawdz`), błędne są liczone i odrzucane. Nie przyjmuje innych danych z zewnątrz.

## Uprawnienia
**M2:** root, wszystkie capabilities (RYZYKA R3). Potrzebne naprawdę: odczyt/zapis
`/rdzen/jadro/dev/dri/card0` i bycie DRM master (pierwszy, kto otworzy kartę). M3: osobny UID,
dostęp tylko do karty i kanału wejścia, seccomp, Landlock.

## Wejścia i wyjścia
- fd 3: komunikaty `k1_zdarzenie` (16 B) od `wejscie` — ruch myszy, przyciski, klawisze.
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
w logu; system działa dalej — test). Zamknięty kanał wejścia → obraz zostaje, sterowanie wyłączone.
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
| w systemie: 4 złośliwe komunikaty odrzucone, poprawny przyjęty, ekran działa | negatywny (IPC) |
| w systemie: awaria ekranu (SIGSEGV) nie zatrzymuje systemu | negatywny |
| host: `test_zdarzenie` (32 przypadki), `fuzz_zdarzenie`, renderer z ASan/UBSan | jednostkowy / fuzzing |

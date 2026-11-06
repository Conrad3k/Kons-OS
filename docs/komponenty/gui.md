# Komponent: gui — powłoka pulpitu (`/rdzen/system/gui`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4) — `scripts/test-m4.sh` (klikanie, zrzuty), testy w systemie (klatka, próba, restart) |
| **Kod** | `src/gui/main.c`, przybornik `src/przybornik/`, czcionki `narzedzia/czcionki.c` |
| **Decyzje** | ADR 0008 (pulpit), ADR 0009 (czcionki), ADR 0007 (klatka); wygląd: `docs/WYGLAD.md` |

## Cel
Rysuje panele pulpitu i obsługuje je myszą:
- **górny pasek** (cała szerokość, 32 px): nazwa aktywnej aplikacji, ikona dźwięku (okienko
  „Dźwięk — ta wersja nie obsługuje jeszcze karty dźwiękowej”), data i godzina (czas polski;
  z sekundami — w Ustawieniach),
- **dolny pasek** wyśrodkowany (jak Windows 11): przycisk Start (znak K1) i aplikacje Zegar,
  Notatki, Ustawienia; kropka = uruchomiona, kreska w kolorze akcentu = aktywna; kliknięcie:
  uruchom / pokaż / zminimalizuj,
- **menu Start**: aplikacje z opisami, „Uruchom ponownie”, „Wyłącz”; klawisz Super otwiera/zamyka.

## Granica zaufania
Klient `ekran` w roli **powłoki** i jedyny klient nadzorcy (kanał fd 4). Nie widzi treści okien
aplikacji (składa je ekran), nie ma plików ani urządzeń. Ufa tylko komunikatom, które sprawdza
(`k1_komunikat_sprawdz`).

## Uprawnienia
UID/GID 102, klatka (ADR 0007): seccomp = baza + `memfd_create`, `ftruncate`; Landlock bez żadnej
ścieżki; cgroup `gui` (48 MiB, 1 proces); `RLIMIT_FSIZE` 16 MiB (powierzchnie memfd).

## Wejścia i wyjścia
- fd 3: kanał do ekranu — powitanie, ustawienia, wskaźnik/klawisz Super, lista okien (`OKNO`).
- fd 4: kanał do nadzorcy — `URUCHOM(aplikacja)`, `WYLACZ(0 = wyłącz, 1 = uruchom ponownie)`.
- Powierzchnie (panele): górny pasek, dolny pasek, menu, okienko dźwięku (memfd, ADR 0008).
- Log: położenie przycisków (dla testów), uruchamiane aplikacje. Bez treści.

## Obsługa błędów
Zamknięty kanał do ekranu → koniec (nadzorca uruchomi ponownie). Awaria → restart; okna aplikacji
zostają, nowa powłoka dostaje ich listę (test).

## Powierzchnia ataku
Komunikaty 32 B od ekranu. Przejęta powłoka może: przestawiać i minimalizować okna, prosić
o uruchomienie jednej z trzech aplikacji, wyłączyć komputer (RYZYKA R27).

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh`: menu Start, uruchamianie z paska i z menu, minimalizacja, Start → Wyłącz, zrzuty | funkcjonalny |
| w systemie: klatka (UID, capabilities, seccomp, cgroup, deskryptory) | funkcjonalny |
| w systemie: próba w klatce `gui` (22 zabronione działania, pamięć wykonywalna) | negatywny |
| w systemie: awaria gui → wraca, okna zostają | negatywny |

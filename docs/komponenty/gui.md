# Komponent: gui — powłoka pulpitu (`/rdzen/system/gui`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4) — `scripts/test-m4.sh` (klikanie, zrzuty), testy w systemie (klatka, próba, restart) |
| **Kod** | `src/gui/main.c`, przybornik `src/przybornik/`, czcionki `narzedzia/czcionki.c` |
| **Decyzje** | ADR 0008 (pulpit), ADR 0009 (czcionki), ADR 0007 (klatka); wygląd: `docs/WYGLAD.md` |

## Cel
Rysuje panele pulpitu i obsługuje je myszą:
- **górny pasek** (cała szerokość, 32 px): nazwa aktywnej aplikacji, bateria (tylko na laptopie,
  0.5.3: błyskawica przy ładowaniu, procent, ikona z wypełnieniem; ≤ 15 % — akcent), ikona dźwięku (okienko
  „Dźwięk — ta wersja nie obsługuje jeszcze karty dźwiękowej”), data i godzina (czas polski;
  z sekundami — w Ustawieniach),
- **dolny pasek** wyśrodkowany (jak Windows 11): przycisk Start (znak K1) i aplikacje Zegar,
  Notatki, Ustawienia, Zasoby; kropka = uruchomiona, kreska w kolorze akcentu = aktywna; kliknięcie:
  uruchom / pokaż / zminimalizuj,
- **menu Start**: aplikacje z opisami, „Wyloguj” (0.6.3; od 0.7.2 tylko w K1 zainstalowanym na dysku — `SESJA`), „Uruchom ponownie”, „Wyłącz”; klawisz Super otwiera/zamyka.

## Granica zaufania
Klient `ekran` w roli **powłoki** i jedyny klient nadzorcy (kanał fd 4). Nie widzi treści okien
aplikacji (składa je ekran), nie ma plików ani urządzeń. Ufa tylko komunikatom, które sprawdza
(`k1_komunikat_sprawdz`).

## Uprawnienia
UID/GID 102, klatka (ADR 0007): seccomp = baza + `memfd_create`, `ftruncate`; Landlock bez żadnej
ścieżki; cgroup `gui` (48 MiB, 1 proces); `RLIMIT_FSIZE` 16 MiB (powierzchnie memfd).

## Wejścia i wyjścia
- fd 3: kanał do ekranu — powitanie, ustawienia, wskaźnik/klawisz Super, lista okien (`OKNO`).
- fd 4: kanał do nadzorcy — `URUCHOM(aplikacja)`, `WYLACZ(0 = wyłącz, 1 = uruchom ponownie, 2 = wyloguj)`.
- fd 5 (0.5.3, ADR 0014): od nadzorcy, jednokierunkowo — `BATERIA` (jest, procent, stan, zasilacz) i `SESJA` (0.7.2: a=1 — K1-DANE w użyciu na dysku wewnętrznym, czyli K1 zainstalowany; nadzorca odrzuca też wylogowanie poza nim);
  nadawca musi mieć UID 0, każde pole sprawdzane; po zamknięciu kanału gui przestaje go czytać.
  Log przy zmianie obecności, stanu, zasilacza lub co 5 % („bateria 73%, rozładowywanie, …”).
- Powierzchnie (panele): górny pasek, dolny pasek, menu, okienko dźwięku (memfd, ADR 0008).
- Zmiana rozdzielczości (M4.1): ekran wysyła ponownie `WITAJ` z nowym rozmiarem → gui zamyka menu,
  tworzy górny pasek na nową szerokość, przesuwa pozostałe panele i loguje nowe położenia.
- Log: położenie przycisków (dla testów), uruchamiane aplikacje. Bez treści.

## Obsługa błędów
Zamknięty kanał do ekranu → koniec (nadzorca uruchomi ponownie). Awaria → restart; okna aplikacji
zostają, nowa powłoka dostaje ich listę (test).

## Powierzchnia ataku
Komunikaty 32 B od ekranu i od nadzorcy (bateria). Przejęta powłoka może: przestawiać i minimalizować okna, prosić
o uruchomienie jednej z trzech aplikacji, wyłączyć komputer (RYZYKA R27).

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh`: menu Start, uruchamianie z paska i z menu, minimalizacja, Start → Wyłącz, Start → Wyloguj (z hasłem: ekran hasła wraca), zrzuty | funkcjonalny |
| w systemie: klatka (UID, capabilities, seccomp, cgroup, deskryptory) | funkcjonalny |
| w systemie: próba w klatce `gui` (22 zabronione działania, pamięć wykonywalna) | negatywny |
| w systemie: awaria gui → wraca, okna zostają | negatywny |
| `test-m5.sh`: bateria ACPI w QEMU (73 %, ładowanie) → log i zrzut z ikoną; `test-m1.sh` start: „brak baterii” | funkcjonalny |
| `test_komunikat`: `BATERIA` — zakresy, kierunek, brak baterii z procentem | negatywny |

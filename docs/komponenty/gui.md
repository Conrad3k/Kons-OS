# Komponent: gui — powłoka pulpitu (`/rdzen/system/gui`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4; pulpit z ikonami 0.7.5) — `scripts/test-m4.sh` (klikanie, zrzuty), testy w systemie (klatka, próba, restart), `test_rozmieszczenie` + fuzzing |
| **Kod** | `src/gui/` (0.7.5: moduły opisane w `gui.h`): `main.c` (start, pętla, ułożenie paneli), `okna.c` (lista okien, uruchamianie i zamykanie aplikacji), `pasek_gorny.c` (górny pasek, bateria, okienko dźwięku), `pasek_dolny.c`, `menu_start.c`, `pulpit.c` (warstwa ikon), `pliki.c` (Kosz i pliki folderu Pulpit, 0.8.0), `kontekst.c` (menu pod prawym przyciskiem), `zapis.c` (rozmieszczenie w skarbcu), `rozmieszczenie.c` (reguły, bez rysowania); przybornik `src/przybornik/`, czcionki `narzedzia/czcionki.c` |
| **Decyzje** | ADR 0008 (pulpit), ADR 0009 (czcionki), ADR 0007 (klatka), ADR 0026 i 0028 (ikony, menu kontekstowe, rozmieszczenie); wygląd: `docs/WYGLAD.md` |

## Cel
Rysuje pulpit i jego panele i obsługuje je myszą:
- **ikony na pulpicie** (0.7.5, szkic A „Kafelki”): warstwa pulpitu nad tapetą, pod oknami; siatka komórek
  84×92 od lewego górnego rogu (kolumnami); kafelek 52 px w kolorze okien (jasny / ciemny motyw) z ikoną
  liniową, pod nim nazwa (krem z cieniem). Kliknięcie zaznacza, podwójne kliknięcie otwiera, przeciągnięcie
  przenosi do innej komórki (ikona, która tam leżała, zamienia się miejscami), prawy przycisk — menu.
  Przy pierwszym starcie: Kosz w lewym górnym rogu i wszystkie aplikacje oprócz Instalatora (kolumna `pasek`
  w `K1_APLIKACJE`),
- **Kosz i pliki na pulpicie** (0.8.0, ADR 0029, `pliki.c`): ikona Kosza (pusty / z kulką papieru), pliki
  i foldery z folderu Pulpit (nowe dostają pierwszą wolną komórkę, znikają razem z plikiem); podwójne kliknięcie
  otwiera folder albo Kosz w Plikach (`URUCHOM` z miejscem), plik — w jego domyślnej aplikacji, a typ bez aplikacji
  (PDF, ZIP, „Plik”) — komunikat przy ikonie: typ, „K1 nie ma jeszcze aplikacji do tego typu pliku — możesz go
  przechowywać, przenosić i usuwać.”, „Pokaż w Plikach” (0.8.2 krok 5),
- **menu pod prawym przyciskiem** (menu kontekstowe; od 0.8.2 rysuje je i układa wiersze pliku przybornik — `k1_menu`,
  to samo menu co w Plikach): pulpit — „Ułóż ikony”, „Tapeta i wygląd…”; ikona —
  „Otwórz”, „Przypnij do paska” / „Odepnij od paska”, „Usuń z pulpitu”; Kosz — „Otwórz”, „Opróżnij Kosz”;
  plik — „Otwórz”, „Otwórz za pomocą ›” (obok lista aplikacji typu), „Pokaż w Plikach”, „Zmień nazwę” (Pliki otwierają
  się przy pliku z polem nazwy), „Usuń” (do Kosza); folder — bez „Otwórz za pomocą”; aplikacja na pasku — jej nazwa
  (otwiera), przypnij / odepnij, „Zamknij okno” (gdy działa); aplikacja w menu Start — „Otwórz”, przypnij /
  odepnij, „Dodaj do pulpitu” / „Usuń z pulpitu”. Kolory z motywu,
- **górny pasek** (cała szerokość, 32 px): nazwa aktywnej aplikacji, bateria (tylko na laptopie,
  0.5.3: błyskawica przy ładowaniu, procent, ikona z wypełnieniem; ≤ 15 % — akcent), ikona dźwięku (okienko
  „Dźwięk — ta wersja nie obsługuje jeszcze karty dźwiękowej”), data i godzina (czas polski;
  z sekundami — w Ustawieniach),
- **dolny pasek** wyśrodkowany (jak Windows 11): przycisk Start (znak K1), przypięte aplikacje w kolejności
  właściciela (przeciągnięcie zmienia kolejność), a za kreską uruchomione aplikacje spoza paska (dopóki
  działają); kropka = uruchomiona, kreska w kolorze akcentu = aktywna; kliknięcie: uruchom / pokaż /
  zminimalizuj; szerokość paska zależy od liczby aplikacji,
- **menu Start**: aplikacje z opisami (bez okien samego K1 — „Wybór pliku”, kolumna -1 listy aplikacji), „Wyloguj” (0.6.3; od 0.7.2 tylko w K1 zainstalowanym na dysku — `SESJA`), „Uruchom ponownie”, „Wyłącz”; klawisz Super otwiera/zamyka.

## Granica zaufania
Klient `ekran` w roli **powłoki** i jedyny klient nadzorcy (kanał fd 4). Nie widzi treści okien
aplikacji (składa je ekran), nie ma plików ani urządzeń. Ufa tylko komunikatom, które sprawdza
(`k1_komunikat_sprawdz`).

## Uprawnienia
UID/GID 102, klatka (ADR 0007): seccomp = baza + `memfd_create`, `ftruncate`; Landlock bez żadnej
ścieżki; cgroup `gui` (48 MiB, 1 proces); `RLIMIT_FSIZE` 16 MiB (powierzchnie memfd).

## Wejścia i wyjścia
- fd 3: kanał do ekranu — powitanie, ustawienia, wskaźnik/klawisz Super, lista okien (`OKNO`).
- fd 4: kanał do nadzorcy — `URUCHOM(aplikacja)` (Pliki: także miejsce — plik / folder albo Kosz), `WYLACZ(0 = wyłącz, 1 = uruchom ponownie, 2 = wyloguj)`.
- fd 6 (0.7.5, ADR 0028): kanał do skarbca — właściciel = profil gui; jeden obiekt `K1_OB_ROZMIESZCZENIE`
  (0.8.0: `"K1R2"`, aplikacje na pasku, ikony: klucz u32 — aplikacja, Kosz albo plik — kolumna, wiersz; zapis
  0.7.5 `"K1R1"` dalej czytany), czytany przy starcie (≤ 1,5 s; błąd = domyślne), zapisywany po każdej zmianie.
  Bez skarbca — rozmieszczenie tylko do końca działania gui. Od 0.8.0 ten sam kanał: `DOK_SPIS` → `DOK_LISTA`
  (tylko folder Pulpit — tak mówi tabela praw w skarbcu), `DOK_KOSZ`, `DOK_OPROZNIJ`, `DOK_ZMIANA`; od 0.8.0
  (ADR 0030) `DOK_OTWORZ` — plik z pulpitu w domyślnej aplikacji (podwójne kliknięcie; ustawienie
  `K1_UST_OTWIERANIE` + typ od ekranu) albo w wybranej (menu pod prawym przyciskiem: „Otwórz za pomocą ›”).
  Folder i plik do otwarcia w Plikach — jak dotąd przez `URUCHOM` z miejscem (c = `K1_PLIKI_*`: 1 Kosz, 2 pole nazwy).
  „Zawsze otwieraj tą aplikacją” (gdy typ ma co najmniej dwie) — `USTAW` do ekranu (tylko klucze `K1_UST_OTWIERANIE`).
- fd 5 (0.5.3, ADR 0014): od nadzorcy, jednokierunkowo — `BATERIA` (jest, procent, stan, zasilacz), `SESJA` (0.7.2: a=1 — K1-DANE w użyciu na dysku wewnętrznym, czyli K1 zainstalowany; nadzorca odrzuca też wylogowanie poza nim)
  i `POKAZ_APL` (0.8.0: aplikacja dostała plik do otwarcia — jej okno na wierzch, jeśli już jest);
  nadawca musi mieć UID 0, każde pole sprawdzane; po zamknięciu kanału gui przestaje go czytać.
  Log przy zmianie obecności, stanu, zasilacza lub co 5 % („bateria 73%, rozładowywanie, …”).
- Powierzchnie: warstwa pulpitu (`K1_POW_PULPIT`, cały ekran, zatwierdzane tylko zmienione prostokąty),
  panele: górny pasek, dolny pasek, menu Start, okienko dźwięku, menu kontekstowe (memfd, ADR 0008).
- Do ekranu (0.7.5): `ZAMKNIJ_OKNO` — „Zamknij okno” z paska; ekran prosi aplikację (`ZAMKNIJ`).
- Zmiana rozdzielczości (M4.1): ekran wysyła ponownie `WITAJ` z nowym rozmiarem → gui zamyka menu,
  tworzy górny pasek na nową szerokość, przesuwa pozostałe panele i loguje nowe położenia.
- Log: położenie przycisków, ikon i wierszy menu kontekstowego (dla testów: „pulpit gotowy”, „pasek
  zmieniony”, „ikony na pulpicie”, „menu kontekstowe”), uruchamiane aplikacje, zmiany rozmieszczenia. Bez treści.

## Obsługa błędów
Zamknięty kanał do ekranu → koniec (nadzorca uruchomi ponownie). Awaria → restart; okna aplikacji
zostają, nowa powłoka dostaje ich listę (test).

## Powierzchnia ataku
Komunikaty 32 B od ekranu, od nadzorcy (bateria) i od skarbca; rozmieszczenie ze skarbca (parser: test na hoście
i fuzzing), od 0.8.0 lista plików folderu Pulpit (`k1_dok_lista_czytaj`, fuzzing). Przejęta powłoka może:
przestawiać, minimalizować i prosić o zamknięcie okien, prosić o uruchomienie aplikacji, wyłączyć komputer, zmienić
swoje rozmieszczenie, poznać nazwy plików z folderu Pulpit i przenieść je do Kosza albo opróżnić Kosz (nie czyta
treści ani innych folderów; RYZYKA R27, R48).

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh`: menu Start, uruchamianie z paska i z menu, minimalizacja, Start → Wyłącz, Start → Wyloguj (z hasłem: ekran hasła wraca), zrzuty | funkcjonalny |
| w systemie: klatka (UID, capabilities, seccomp, cgroup, deskryptory) | funkcjonalny |
| w systemie: próba w klatce `gui` (22 zabronione działania, pamięć wykonywalna) | negatywny |
| w systemie: awaria gui → wraca, okna zostają | negatywny |
| `test-m5.sh`: bateria ACPI w QEMU (73 %, ładowanie) → log i zrzut z ikoną; `test-m1.sh` start: „brak baterii” | funkcjonalny |
| `test_komunikat`: `BATERIA` — zakresy, kierunek, brak baterii z procentem | negatywny |
| `test_rozmieszczenie` (host, ASan/UBSan): pierwszy start (Kosz w rogu), klucze ikon, przypinanie i kolejność, komórki, pliki, „Ułóż ikony”, mniejszy ekran, zapisane bajty i uszkodzone, zapis 0.7.5; `fuzz_rozmieszczenie` | jednostkowy / fuzzing |
| `test-m4.sh` (0.8.0): Kosz na pulpicie, plik z folderu Pulpit jako ikona, podwójne kliknięcie otwiera Pliki, „Usuń” z menu pliku, Kosz pełny / pusty | funkcjonalny |
| `test-m4.sh` (0.7.5): ikony na zrzucie, podwójne kliknięcie, przeciągnięcie ikony, menu kontekstowe (odepnij, zamknij okno, usuń i dodaj do pulpitu), rozmieszczenie po ponownym starcie | funkcjonalny |
| w systemie (0.7.5): warstwa pulpitu i zamykanie cudzych okien od aplikacji — odrzucone | negatywny |

# Komponent: gui — powłoka pulpitu (`/rdzen/system/gui`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4; pulpit z ikonami 0.7.5) — `scripts/test-m4.sh` (klikanie, zrzuty), testy w systemie (klatka, próba, restart), `test_rozmieszczenie` + fuzzing |
| **Kod** | `src/gui/` (0.7.5: moduły opisane w `gui.h`): `main.c` (start, pętla, ułożenie paneli), `okna.c` (lista okien, uruchamianie i zamykanie aplikacji), `pasek_gorny.c` (górny pasek, ikona sieci — 0.9.0, bateria, okienko dźwięku), `pasek_dolny.c`, `menu_start.c`, `pulpit.c` (warstwa ikon), `pliki.c` (Kosz i pliki folderu Pulpit, 0.8.0), `kontekst.c` (menu pod prawym przyciskiem), `zapis.c` (rozmieszczenie w skarbcu), `rozmieszczenie.c` (reguły, bez rysowania); przybornik `src/przybornik/`, czcionki `narzedzia/czcionki.c` |
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
- **przeciąganie plików** (0.8.2 krok 6, L24 faza A, decyzje D6 i D8): plik albo folder przeciągnięty nad Kosz albo
  folder z Pulpitu — komórka celu z jasnym obrysem, przy kursorze półprzezroczysty kafelek i znak („Do Kosza”,
  „Przenieś do „Zdjęcia””); decyduje tabela reguł z przybornika (`k1_przeciaganie`, ta sama co w Plikach). Upuszczony
  idzie tam (`DOK_KOSZ`, `DOK_PRZENIES`); folder na Kosz — najpierw **okienko** z pytaniem (D8: pulpit nie widzi wnętrza
  folderów, więc pyta przy każdym folderze), zajęta nazwa w folderze — „Zachowaj oba” (`DOK_PRZENIES` b = 1) / „Pomiń” /
  „Anuluj”, inna odmowa skarbca — okienko z powodem i „OK” (`kontekst_okienko`: ten sam panel co menu). Nad pustą
  komórką albo ikoną aplikacji — jak dotąd: ikona zmienia miejsce. Między oknami (Pliki ↔ pulpit) — po 0.8.2 (D6),
- **„Jeszcze n”** (0.8.2 krok 6, L19): gdy plików w folderze Pulpit jest więcej niż miejsc w siatce (albo ponad
  `ROZM_IKON_MAKS` = 96 ikon), ostatnia wolna komórka (kolumnami) pokazuje kafelek „+n” z napisem „Jeszcze n” —
  podwójne kliknięcie otwiera Pliki w folderze Pulpit; gdy żadnej komórki nie ma, ikona ostatniego pliku ją zwalnia
  (`rozm_pliki`, `rozm_ostatnia_wolna`; kafelek nie jest zapisywany w rozmieszczeniu),
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
- **opis ekranu dla testów** (0.10.2 krok 2, ADR 0045): panele zgłaszają swoje elementy przez przybornik (`k1_el`) —
  „Pasek” (ikona „Start” i aplikacje po nazwach; wybrana = ma okno), „Menu Start” (wiersze aplikacji, „Wyloguj”,
  „Uruchom ponownie”, „Wyłącz”), „Pulpit” (ikony: aplikacje, „Kosz”, pliki jako „plik N”), „Menu” (wiersze menu pod prawym
  przyciskiem; nazwy sesji jako „sesja N” — `k1_menu_el` z kopii menu), „Okienko” (kafelki „sesja N”, „Anuluj”);
  od 0.10.2 krok 3 „Pasek górny” („Dźwięk”) i **miejsca zmienne** (`K1_EL_ZMIENNE`) dla zrzutów wzorcowych: „Stan i
  zegar” na górnym pasku (sieć, bateria, zegar — przesuwają się z szerokością zegara), „Wersja” w menu Start.
- **menu Start**: aplikacje z opisami (bez okien samego K1 — „Wybór pliku”, kolumna -1 listy aplikacji), „Wyloguj” (0.6.3; od 0.7.2 tylko w K1 zainstalowanym na dysku — `SESJA`), „Uruchom ponownie”, „Wyłącz”; klawisz Super otwiera/zamyka.

## Granica zaufania
Klient `ekran` w roli **powłoki**; prośby do nadzorcy wysyła kanałem fd 4. Nie widzi treści okien
aplikacji (składa je ekran), nie ma plików ani urządzeń. Ufa tylko komunikatom, które sprawdza
(`k1_komunikat_sprawdz`).

## Uprawnienia
UID/GID 102, klatka (ADR 0007): seccomp = baza + `memfd_create`, `ftruncate`; Landlock bez żadnej
ścieżki; cgroup `gui` (128 MiB, 1 proces); `RLIMIT_FSIZE` 72 MiB (powierzchnie memfd: warstwa pulpitu na cały
ekran do 4096×2160 w dwóch połowach, 0.7.5).

## Wejścia i wyjścia
- fd 3: kanał do ekranu — powitanie, ustawienia, wskaźnik/klawisz Super, lista okien (`OKNO`).
- fd 4: kanał do nadzorcy — `URUCHOM(aplikacja)` (Pliki: także miejsce — plik / folder albo Kosz), `WYLACZ(0 = wyłącz, 1 = uruchom ponownie, 2 = wyloguj)`.
- fd 6 (0.7.5, ADR 0028): kanał do skarbca — właściciel = profil gui; jeden obiekt `K1_OB_ROZMIESZCZENIE`
  (0.8.0: `"K1R2"`, aplikacje na pasku, ikony: klucz u32 — aplikacja, Kosz albo plik — kolumna, wiersz; zapis
  0.7.5 `"K1R1"` dalej czytany), czytany przy starcie (≤ 1,5 s; błąd = domyślne), zapisywany po każdej zmianie.
  Bez skarbca — rozmieszczenie tylko do końca działania gui. Od 0.8.0 ten sam kanał: `DOK_SPIS` → `DOK_LISTA`
  (tylko folder Pulpit — tak mówi tabela praw w skarbcu), `DOK_KOSZ`, `DOK_OPROZNIJ`, `DOK_ZMIANA`, od 0.8.2 (krok 6)
  `DOK_PRZENIES` pliku Pulpitu do folderu Pulpitu (plik upuszczony na folder); od 0.8.0
  (ADR 0030) `DOK_OTWORZ` — plik z pulpitu w domyślnej aplikacji (podwójne kliknięcie; ustawienie
  `K1_UST_OTWIERANIE` + typ od ekranu) albo w wybranej (menu pod prawym przyciskiem: „Otwórz za pomocą ›”).
  Folder i plik do otwarcia w Plikach — jak dotąd przez `URUCHOM` z miejscem (c = `K1_PLIKI_*`: 1 Kosz, 2 pole nazwy).
  „Zawsze otwieraj tą aplikacją” (gdy typ ma co najmniej dwie) — `USTAW` do ekranu (tylko klucze `K1_UST_OTWIERANIE`).
- fd 5 (0.5.3, ADR 0014): od nadzorcy, jednokierunkowo — `BATERIA` (jest, procent, stan, zasilacz), `SESJA` (0.7.2: a=1 — K1-DANE w użyciu na dysku wewnętrznym, czyli K1 zainstalowany; nadzorca odrzuca też wylogowanie poza nim)
  i `POKAZ_APL` (0.8.0: aplikacja dostała plik do otwarcia — jej okno na wierzch, jeśli już jest), `SIEC_STAN`
  (0.9.0: ikona sieci na górnym pasku — log „sieć: połączono (ikona w x,y)” przy zmianie; `komponenty/siec.md`; od
  0.9.1 bez kabla z kartą Wi-Fi: ikona „Wachlarz” — `k1_wachlarz.c` w przybornik, stany „Wi-Fi wyłączone”, „Wi-Fi nie
  połączono”; od 0.9.1 krok 2: „Wi-Fi łączę” — kropka w terakocie, „Wi-Fi połączono” — wypełniona część wachlarza = siła
  sygnału, 1–4 kreski z `SIEC_STAN` e; log „sieć: Wi-Fi połączono, kreski 4 (ikona w x,y)”);
  od 0.10.0 `STREFA_STAN` (strefa nie ruszyła: brak wirtualizacji, pamięci albo obrazu — okienko odmowy „<tytuł> nie może
  ruszyć” z wyjaśnieniem i „Rozumiem”, log „strefa N nie ruszyła (powód P): okienko”; od 0.10.0 krok 3 także strefa,
  która zamknęła się sama: „<tytuł> zamknęła się” z powodem — brak pamięci w strefie albo w komputerze, awaria programu,
  błąd strefy, od 0.10.1 krok 1 także „Strefa zużyła całą pamięć, którą K1 jej daje” (limit jej klatki) — log
  „strefa N zamknęła się (powód P): okienko”);
  nadawca musi mieć UID 0, każde pole sprawdzane; po zamknięciu kanału gui przestaje go czytać.
- Strefy (0.10.0, ADR 0042): prośba `URUCHOM_STREFE` (fd 4). Od kroku 2 (ADR 0043) wiersz `K1_STREFY` z linią menu
  (sesja „Ogólna”) jest w menu Start jako „Przeglądarka” (ikona `K1_IK_PRZEGLADARKA` — globus, D1: A) po aplikacjach; przyciski
  stopki menu mają numery od 100 (ponad wszystkimi pozycjami). Okno strefy na pasku — jak w kroku 1.
  tylko w testach), w kroku 2 — Przeglądarka. Okno strefy (`OKNO` z `a` = 0, `c` = strefa) jest pozycją paska
  `K1_APL_MAKS + strefa` (`POZYCJA_STREFY`, tytuł i ikona z `K1_STREFY`): na dolnym pasku wśród działających, kliknięcie
  pokazuje albo chowa okno, menu „Zamknij okno”; nie da się jej przypiąć ani położyć na pulpicie.
  Od kroku 5 (D2): prawy przycisk na „Przeglądarce” w menu Start — „Otwórz” i pod napisem „Sesje” każda sesja przeglądarki
  (Ogólna, Bank, Prywatna — nazwy od nadzorcy, `STREFA_NAZWA` na fd 5), kliknięcie otwiera tę sesję; w logu menu sesje
  jako „sesja N” (nazwa to tekst użytkownika).
  Od 0.10.1 krok 4 (D4 właściciela: szkic B; `sesje.c`): kliknięcie „Przeglądarki” w menu Start (i „Otwórz” w jej menu —
  wszędzie, skąd przeglądarka się otwiera) pokazuje na środku ekranu okienko „Którą sesję otworzyć?”: kafelek na każdą
  strefę z tym samym obrazem (`K1_STREFY` — nowa sesja to nowy wiersz tabeli, gui się nie zmienia) — kropka w kolorze sesji,
  nazwa, „pamięta” / „zapomina” (`STREFY_STAN` a) albo „otwarta”, gdy sesja ma okno (kliknięcie wysuwa je na wierzch:
  nadzorca nie uruchamia strefy drugi raz), albo (0.10.2 krok 1) „zamyka się…”, gdy okno już zamknięto, a maszyna kończy się
  w tle (`STREFY_STAN` e; kliknięcie działa — nadzorca otworzy sesję sam, gdy stara maszyna skończy; D2 właściciela); „Anuluj” albo kliknięcie obok zamyka. Rysowane na panelu menu kontekstowego
  (`kontekst.c`, cel `K_SESJE`). Jedna sesja — bez pytania. Log „okienko (sesje): sesja 2 x,y; sesja 3 x,y; sesja 4 x,y;
  Anuluj x,y”, potem „okienko (sesje): sesja N” albo „Anuluj”.
  Od 0.10.1 krok 3 (D3 właściciela: okienko A; `pamiec.c`): gdy zmienia się aktywne okno, gui mówi nadzorcy, która strefa
  ma fokus (`STREFA_FOKUS`, fd 4). Od nadzorcy (fd 5) przydział każdej sesji (`STREFA_PRZYDZIAL`) i `MALO_PAMIECI` →
  okienko „Mało pamięci” na środku ekranu: zdanie „Komputerowi kończy się pamięć. Zamknij kartę albo całą sesję, której nie
  potrzebujesz.”, wiersz na każdą sesję (kropka w kolorze, nazwa, pamięć, „w oknie” przy sesji z fokusem, „Zamknij” —
  zamyka okno tej sesji jak „Zamknij okno”: przeglądarka zapisuje i kończy się), „Później”. Rysowane na panelu menu
  kontekstowego (`kontekst.c`: okienko innego modułu — `struct okienko_wlasne`, jak okienko sesji). Log „okienko (mało
  pamięci): sesja N (M MiB) Zamknij x,y; …; Później x,y”, potem „zamykam sesję N” albo „Później”. Okienka „nie może ruszyć” / „zamknęła się” i pierwszy wiersz menu paska
  mówią „Przeglądarka · <sesja>”.
  Log przy zmianie obecności, stanu, zasilacza lub co 5 % („bateria 73%, rozładowywanie, …”).
- Powierzchnie: warstwa pulpitu (`K1_POW_PULPIT`, cały ekran, zatwierdzane tylko zmienione prostokąty),
  panele: górny pasek, dolny pasek, menu Start, okienko dźwięku, menu kontekstowe (memfd, ADR 0008; na nim także okienka
  pytań i „Którą sesję otworzyć?”).
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
swoje rozmieszczenie, poznać nazwy plików z folderu Pulpit i przenieść je do Kosza albo do folderu Pulpitu (0.8.2)
albo opróżnić Kosz (nie czyta
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
| `test-pliki.sh` (0.8.2 krok 6): folder przeciągnięty na folder na pulpicie (przeniesiony), folder na Kosz — okienko z pytaniem, „Przenieś do Kosza”; zrzuty `pliki-11-pulpit-na-folder.png`, `pliki-12-pulpit-pytanie.png` | funkcjonalny |
| `test_rozmieszczenie` (0.8.2 krok 6, L19): więcej plików niż komórek — ostatnia wolna komórka dla „Jeszcze n”, stabilna przy kolejnych listach, przy pełnej siatce zwalnia ją ikona pliku, limit 96 ikon | jednostkowy |
| `test_dokumenty` (0.8.2 krok 6): pulpit przenosi plik Pulpitu tylko do folderu Pulpitu (inny folder, plik zamiast folderu, cudzy plik — odmowa) | negatywny |

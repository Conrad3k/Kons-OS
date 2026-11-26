# Komponent: gui — powłoka pulpitu (`/rdzen/system/gui`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4; pulpit z ikonami 0.7.5) — `scripts/test-m4.sh` (klikanie, zrzuty), testy w systemie (klatka, próba, restart), `test_rozmieszczenie` + fuzzing |
| **Kod** | `src/gui/` (0.7.5: moduły opisane w `gui.h`): `main.c` (start, pętla, ułożenie paneli), `okna.c` (lista okien, uruchamianie i zamykanie aplikacji), `pasek_gorny.c` (górny pasek, ikona sieci — 0.9.0, okienka paska — 0.11.8), `bateria.c` (bateria na górnym pasku; od 0.11.8 krok 3 osobno, z okienkiem baterii), `pasek_dolny.c`, `menu_start.c`, `pulpit.c` (warstwa ikon), `pliki.c` (Kosz i pliki folderu Pulpit, 0.8.0), `kontekst.c` (menu pod prawym przyciskiem), `wersja.c` (okienko „Nowa wersja K1 nie uruchomiła się”, 0.10.5), `przelacznik.c` (przełącznik okien — Alt+Tab, 0.10.5 krok 6), `aktualizacja.c` (okienko „Jest nowa wersja K1” i kropka przy Start, 0.10.5 krok 2 — `docs/komponenty/aktualizacja.md`), `zapis.c` (rozmieszczenie w skarbcu), `rozmieszczenie.c` (reguły, bez rysowania); przybornik `src/przybornik/`, czcionki `narzedzia/czcionki.c` |
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
  komórką albo ikoną aplikacji — jak dotąd: ikona zmienia miejsce. **Między oknami (0.11.0 krok 4, ADR 0052, D6: A)** —
  prowadzi ekran: plik z pulpitu nad oknem Plików albo sesji przeglądarki — ekran mówi `PRZECIAGANIE_GDZIE` (ikona wraca
  na swoje miejsce, rysuje tamto okno albo ekran), po puszczeniu na sesję `PRZECIAGANIE_KONIEC` → `DOK_DO_STREFY`
  „upuść”; pliki z Plików nad pulpitem (`UPUSZCZANIE`) — znak przy kursorze i obwódka celu (Kosz, folder, a wszędzie
  indziej Pulpit), po puszczeniu pulpit mówi tylko, gdzie (`PRZECIAGANIE_CEL`) — przenoszą albo z Ctrl kopiują Pliki,
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
- **górny pasek** (cała szerokość, 32 px; wygląd od 0.11.8 krok 2 — D3: A „Spokojny”, `docs/WYGLAD.md`): znak K1 i nazwa
  aktywnej aplikacji, pigułka z ikonami stanu — sieć, bateria (tylko na laptopie, 0.5.3: błyskawica przy ładowaniu,
  procent, ikona z wypełnieniem; ≤ 15 % — akcent), dźwięk — oraz data i godzina (czas polski; z sekundami — w
  Ustawieniach). Ikony stanu to **jedna tabela** `elementy` w `pasek_gorny.c` (nazwa w opisie ekranu, okienko, które
  otwiera, szerokość komórki, rysowanie); komórka z okienkiem podświetla się pod kursorem, bez okienka — nie (nic nie
  obiecuje). **Okienka paska** (T2): tabela `okienka` (nazwa, rozmiar, rysowanie, kogo powiadomić o otwarciu i
  zamknięciu, kto obsługuje wskaźnik nad nim) — „Dźwięk” (od 0.12.0 krok 1 — `dzwiek.c`, niżej) i od 0.11.8 krok 3
  „Bateria”; jedna powierzchnia `P_OKIENKO_PASKA` w rozmiarze największego
  (`OKIENKO_PASKA_SZER` × `_WYS`: szerokość dźwięku, wysokość baterii), przy otwarciu przesuwana pod ikonę; otwarte
  najwyżej jedno, zamyka je drugie kliknięcie, kliknięcie obok (pasek, pulpit, okno), dolny pasek, menu Start i klawisz
  Super; ikona, która zniknęła (bateria wyjęta), zamyka swoje. Log przy otwarciu: „okienko paska: Dźwięk w x,y
  (szer×wys)”,
- **dźwięk** (0.12.0 krok 1, ADR 0054; D2 właściciela: B — w kroku 1 część górna; `dzwiek.c`): ikona głośnika na pasku
  pokazuje stan (dwie fale — głośno, od 50 %; jedna — ciszej; × — wyciszony albo 0 %; blada i przekreślona — brak karty);
  okienko „Dźwięk” 300 × 158: nazwa wyjścia („Głośniki”), okrągły przycisk „Wycisz”, suwak „Głośność” z procentami
  (przesunięcie zdejmuje wyciszenie), „Odtwórz próbę” (D3: A — jedyny dźwięk K1; dwie nuty liczone w programie); bez karty:
  „Ten komputer nie ma karty dźwiękowej.” Kanał do usługi dźwięku od nadzorcy na fd 5 (`DZWIEK_KANAL`, od nowa po jej
  restarcie — gui podaje wtedy swoją głośność); gui jest klientem sterującym (`DZWIEK_GLOSNOSC`), próbę pisze do swojego
  pierścienia, gdy usługa zgłasza miejsce. Log: „dźwięk: karta N, głośność P%”, „próba: start / zagrana”, „okienko
  dźwięku: …”. Usługa: `docs/komponenty/dzwiek.md`. Lista „Grają teraz” — krok 3. Od 0.12.0 krok 2: kanał, stan,
  głośność i próba — wspólny `k1_dzwiek_klient` w przyborniku (także Ustawienia → Dźwięk); w okienku nazwa wyjścia, które
  gra („Głośniki” / „Słuchawki”). **Klawisze głośności** (ekran daje je tylko powłoce): `k1_dzw_klawisz` — głośniej (+5 %,
  zdejmuje wyciszenie), ciszej (−5 %), wycisz (włącz / wyłącz; trzymany — raz); potem `GLOSNOSC_POKAZ` do ekranu (pigułka,
  D4: A — `ekran.md`); bez karty pigułka mówi „Brak karty dźwiękowej”. **Pamięć głośności** (D5: A): głośność i wyciszenie
  każdego wyjścia w skarbcu (obiekt gui `K1_OB_GLOSNOSC`, rekord „K1G1”, 8 B — `k1_dzw_zapamietane_*`), zapis 2 s po
  ostatniej zmianie (także zmianie z Ustawień — gui słyszy `DZWIEK_STAN`), odczyt przy starcie gui; każda nowa usługa
  dźwięku (i nowe gui) dostaje obie głośności (`DZWIEK_GLOSNOSC` z `c` = wyjście). **Korektor** (0.12.3 krok 2, D1: A):
  pod kreską okienka (teraz 300 × 202) wiersz „Korektor” z nazwą ustawienia wyjścia, które gra („Więcej basu”, „Własne”)
  i strzałką — kliknięcie: `POKAZ_USTAWIENIA` a = KOREKTOR do nadzorcy (Ustawienia → Dźwięk → Korektor), okienko się
  zamyka (log „okienko dźwięku: Korektor”). Pasma obu wyjść gui pamięta razem z głośnością (rekord „K1G2”, 28 B: „K1G1”
  i po nim po dziesięć pasm głośników i słuchawek; stary „K1G1” czyta dalej — korektor płaski), słyszy je od usługi
  (`DZWIEK_KOREKTOR_STAN`, zmiana z Ustawień — po tym, jak usługa ma już zapamiętane), daje każdej nowej usłudze
  (`DZWIEK_KOREKTOR`); log przy starcie „dźwięk: zapamiętany korektor — głośniki: …, słuchawki: …”. Log: „dźwięk: zapamiętana głośność —
  głośniki P%, słuchawki P%”, „dźwięk: głośność zapamiętana”. **Krok 3** (D2: B, D6: A): lista **„Grają teraz”** pod
  kreską okienka — wiersz dla każdej sesji, która gra (`DZWIEK_SESJA` od usługi): kropka w kolorze sesji, jej nazwa,
  okrągły przycisk wyciszenia 24 px, suwak głośności sesji, procenty; okienko rośnie o wiersze (powierzchnia okienka paska
  robiona od nowa, gdy się nie mieści — nigdy mniejsza niż na starcie). Opis ekranu: „Wycisz sesję N”, „Głośność sesji N”
  (numer strefy — nigdy nazwa sesji). Która sesja gra i czy wyciszona — do ekranu (`STREFA_DZWIEK`: głośnik na pasku okna
  sesji, `ekran.md`); kliknięcie głośnika wraca od ekranu (`STREFA_WYCISZ`). Log: „dźwięk: grają sesje: N”, „dźwięk: sesja
  N wyciszona / gra (głośnik na pasku okna)”.
- **okienko baterii** (0.11.8 krok 3, D4 właściciela: A; `bateria.c`): kliknięcie baterii na górnym pasku → procent (duży)
  i symbol baterii, stan (te same słowa co Zasoby i Ustawienia — `k1_bateria_stan`), wiersz „Zostało” / „Do pełna” z czasem
  „ok. 3 h 20 min” (`k1_minuty`; nieznany — „nieznany”; bez ładowania i rozładowywania — „Zasilacz: podłączony /
  odłączony”), „Pobór mocy” „9,4 W” (`k1_dziesietne`) i przycisk „Ustawienia baterii” (Ustawienia na sekcji Bateria). Liczby
  liczy nadzorca — ten sam pomiar co dla Zasobów (`BATERIA_POMIAR` na fd 5), gui nic nie liczy; odświeżają się same, póki
  okienko jest otwarte. Otwarcie i zamknięcie gui mówi nadzorcy (`BATERIA_OKIENKO` na fd 4): póki jest otwarte, nadzorca
  czyta baterię co 5 s zamiast co 15 s (i raz od razu przy otwarciu). Log: „bateria na pasku: środek x,16 (szer w)” przy
  zmianie miejsca, „pomiar baterii: 73%, ładowanie, 9,0 W, do pełna 81 min” przy nowym stanie albo czasie, który stał się
  znany / nieznany, „okienko baterii: …” (te same liczby) przy otwarciu i każdej zmianie, póki jest otwarte, „okienko
  baterii: Ustawienia baterii”. Liczby to stan sprzętu, nie treść użytkownika. Opis ekranu: „Okienko paska” z przyciskiem
  „Ustawienia baterii”, miejsca zmienne „Stan baterii” (procent, symbol, stan) i „Liczby baterii” (wartości wierszy),
- **dolny pasek** wyśrodkowany (jak Windows 11): przycisk Start (znak K1), przypięte aplikacje w kolejności
  właściciela (przeciągnięcie zmienia kolejność), a za kreską uruchomione aplikacje spoza paska (dopóki
  działają); kropka = uruchomiona, kreska w kolorze akcentu = aktywna; kliknięcie: uruchom / pokaż /
  zminimalizuj; szerokość paska zależy od liczby aplikacji,
- **opis ekranu dla testów** (0.10.2 krok 2, ADR 0045): panele zgłaszają swoje elementy przez przybornik (`k1_el`) —
  „Pasek” (ikona „Start” i aplikacje po nazwach; wybrana = ma okno), „Menu Start” (wiersze aplikacji, „Zablokuj”,
  „Uruchom ponownie”, „Wyłącz”), „Pulpit” (ikony: aplikacje, „Kosz”, pliki jako „plik N”), „Menu” (wiersze menu pod prawym
  przyciskiem; nazwy sesji jako „sesja N” — `k1_menu_el` z kopii menu), „Okienko” (kafelki „sesja N”, „Anuluj”);
  od 0.10.2 krok 3 „Pasek górny” („Dźwięk”; od 0.11.8 też „Sieć” — ikona bez okienka jako `K1_EL_IKONA` — i „Bateria”,
  od kroku 3 przycisk z okienkiem),
  „Okienko paska” (0.11.8) i **miejsca zmienne** (`K1_EL_ZMIENNE`) dla zrzutów wzorcowych: „Stan i
  zegar” na górnym pasku (sieć, bateria, zegar — przesuwają się z szerokością zegara), „Wersja” w menu Start.
- **menu Start**: aplikacje z opisami (bez okien samego K1 — „Wybór pliku”, kolumna -1 listy aplikacji), „Zablokuj” (0.6.3 jako „Wyloguj”; od 0.11.5 „Zablokuj” i tylko, gdy magazyn ma hasło — `SESJA`; bez hasła zostałoby samo zamknięcie aplikacji), „Uruchom ponownie”, „Wyłącz”; klawisz Super otwiera/zamyka. Od 0.11.8 krok 1 (D1: A, D2: A) lista ma stałą wysokość —
  `MENU_WIDAC` = 7 wierszy (`menu_dopasuj`: na niskim ekranie mniej, najmniej `MENU_WIDAC_MIN` = 3; powierzchnia ma rozmiar
  najwyższego menu, niższe stoi przy jej dolnej krawędzi — przezroczyste piksele nie łapią wskaźnika); każde otwarcie
  pokazuje koniec listy, reszta chowa się wyżej. `PRZEWIN` od ekranu nad całym menu przesuwa listę w pikselach (ząbek =
  wiersz, touchpad — część wiersza; T3), wiersze rysuje się w wycinku panelu (`k1_wycinek`, przybornik), krawędź, za
  którą coś jest, zanika (`k1_zanik`); pasek przewijania z prawej da się chwycić. Opis ekranu ma tylko wiersze widoczne
  co najmniej w połowie (ich widoczna część); log „menu Start otwarte (pierwszy wiersz x,y co 56; Wyłącz …; Zablokuj …;
  lista y0–y1, widać n z m, przesunięcie p)” — „pierwszy wiersz” to miejsce, gdzie byłby wiersz 1 (testy dodają do niego
  wiersze; wiersze nad listą są schowane).

## Granica zaufania
Klient `ekran` w roli **powłoki**; prośby do nadzorcy wysyła kanałem fd 4. Nie widzi treści okien
aplikacji (składa je ekran), nie ma plików ani urządzeń. Ufa tylko komunikatom, które sprawdza
(`k1_komunikat_sprawdz`).

## Uprawnienia
UID/GID 102, klatka (ADR 0007): seccomp = baza + `memfd_create`, `ftruncate`; Landlock bez żadnej
ścieżki; cgroup `gui` (128 MiB, 1 proces); `RLIMIT_FSIZE` 72 MiB (powierzchnie memfd: warstwa pulpitu na cały
ekran do 4096×2160 w dwóch połowach, 0.7.5).

## Wejścia i wyjścia
- fd 3: kanał do ekranu — powitanie, ustawienia, wskaźnik/klawisz Super, od 0.10.5 krok 6 Tab przy wciśniętym Alt, Esc
  i puszczenie Alt w trakcie przełączania (`KLAWISZ`), lista okien (`OKNO`).
- fd 4: kanał do nadzorcy — `URUCHOM(aplikacja)` (Pliki: także miejsce — plik / folder albo Kosz), `WYLACZ(0 = wyłącz, 1 = uruchom ponownie, 2 = zablokuj)`.
- fd 6 (0.7.5, ADR 0028): kanał do skarbca — właściciel = profil gui; jeden obiekt `K1_OB_ROZMIESZCZENIE`
  (0.10.5 krok 6: `"K1R3"` — pasek: klucze u32 — aplikacja albo Przeglądarka (`ROZM_STREFA` + numer strefy z linią
  menu Start, jeden klucz na wszystkie sesje, T17); ikony: klucz u32 — aplikacja, Przeglądarka, Kosz albo plik —
  kolumna, wiersz; zapisy `"K1R2"` (0.8.0–0.10.4: na pasku bajt na aplikację) i `"K1R1"` (0.7.5) dalej czytane — bez
  dodawania Przeglądarki, D12), czytany przy starcie (≤ 1,5 s; błąd = domyślne), zapisywany po każdej zmianie.
  Bez skarbca — rozmieszczenie tylko do końca działania gui. Od 0.8.0 ten sam kanał: `DOK_SPIS` → `DOK_LISTA`
  (tylko folder Pulpit — tak mówi tabela praw w skarbcu), `DOK_KOSZ`, `DOK_OPROZNIJ`, `DOK_ZMIANA`, od 0.8.2 (krok 6)
  `DOK_PRZENIES` pliku Pulpitu do folderu Pulpitu (plik upuszczony na folder); od 0.8.0
  (ADR 0030) `DOK_OTWORZ` — plik z pulpitu w domyślnej aplikacji (podwójne kliknięcie; ustawienie
  `K1_UST_OTWIERANIE` + typ od ekranu) albo w wybranej (menu pod prawym przyciskiem: „Otwórz za pomocą ›”).
  Folder i plik do otwarcia w Plikach — jak dotąd przez `URUCHOM` z miejscem (c = `K1_PLIKI_*`: 1 Kosz, 2 pole nazwy).
  „Zawsze otwieraj tą aplikacją” (gdy typ ma co najmniej dwie) — `USTAW` do ekranu (tylko klucze `K1_UST_OTWIERANIE`).
- fd 5 (0.5.3, ADR 0014): od nadzorcy, jednokierunkowo — `BATERIA` (jest, procent, stan, zasilacz), od 0.11.8 krok 3
  `BATERIA_POMIAR` (pobór mocy w mW i minuty do pustej / pełnej, -1 = nieznane — ten sam komunikat i to samo sprawdzanie
  co dla Zasobów; tylko po zmianie, czyli najwyżej przy odczycie baterii; tylko do gui, nie do ekranu hasła), `SESJA` (0.7.2; od 0.11.5: a=1 — magazyn otwarty i ma hasło, menu Start pokazuje „Zablokuj”; przychodzi znowu po ustawieniu albo wyłączeniu hasła; nadzorca odrzuca też zablokowanie bez hasła)
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
  pokazuje albo chowa okno, menu „Zamknij okno”; okna sesji nie da się przypiąć ani położyć na pulpicie (przypina się
  Przeglądarkę — niżej, 0.10.5 krok 6).
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
  pamięci): sesja N (M MiB) Zamknij x,y; …; Później x,y”, potem „zamykam sesję N” albo „Później”.
  Od 0.10.5 krok 1 (S5 planu; wygląd jak okienko „Jest nowa wersja” z D1; `wersja.c`): `AB_NIEUDANA` od nadzorcy (fd 5,
  raz po starcie, gdy nowa wersja A/B nie wystartowała i GRUB wrócił) → okienko na środku ekranu „Nowa wersja K1 nie
  uruchomiła się”: „K1 X nie wystartowała na tym komputerze.”, „Komputer sam wrócił do poprzedniej wersji (Y). Nic nie
  zginęło.”, przyciski „Pokaż w Ustawieniach” (`OTWORZ_AKTUALIZACJE` do nadzorcy, fd 4 — Ustawienia → Aktualizacje) i
  „Rozumiem”; kliknięcie obok zamyka. Log „okienko (nieudana próba nowej wersji): Rozumiem x,y; Pokaż w Ustawieniach x,y”. Okienka „nie może ruszyć” / „zamknęła się” i pierwszy wiersz menu paska
  mówią „Przeglądarka · <sesja>”.
  Od 0.10.5 krok 6 (D12 właściciela: szkic A „ikona + okna sesji”; T17): **Przeglądarka na pasku i pulpicie** — wiersz
  `K1_STREFY` z linią menu Start to w gui osobna pozycja (`POZYCJA_MENU_STREFY`, obok okien sesji `POZYCJA_STREFY`), w
  rozmieszczeniu klucz `ROZM_STREFA` + numer strefy: menu Start, przypięta ikona i ikona pulpitu. Prawy przycisk na niej w
  menu Start — „Otwórz”, sesje, „Przypnij do paska” / „Odepnij od paska”, „Dodaj do pulpitu” / „Usuń z pulpitu”; ikona na
  pulpicie — „Otwórz”, przypnij / odepnij, „Usuń z pulpitu”; kliknięcie przypiętej ikony, podwójne kliknięcie na pulpicie
  i „Otwórz” — okienko „Którą sesję otworzyć?” (jedna sesja — bez pytania). Kropka „uruchomiona” pod przypiętą ikoną,
  gdy któraś sesja ma okno (`okno_pozycji`); okna sesji zostają po kresce, każde z kropką w kolorze sesji. Przy pierwszym
  starcie Przeglądarka jest na końcu paska i na pulpicie za aplikacjami (`rozm_domyslne`); stary zapis rozmieszczenia
  (`K1R2`) zostaje bez niej. Przeciąganie na pasku i „Ułóż ikony” (za aplikacjami, przed plikami) — jak aplikacja. Opis
  ekranu: przypięta ikona „Przeglądarka”, okno sesji na pasku „Przeglądarka · sesja N”.
  Od 0.10.5 krok 6 (D11 właściciela: szkic A „kafelki”; T16; `przelacznik.c`): **przełącznik okien (Alt+Tab)** — ekran
  oddaje powłoce Tab przy wciśniętym Alt (z Shift — wstecz), Esc i puszczenie Alt; pierwsze Tab pokazuje na środku
  ekranu kafelek każdego okna (także zminimalizowanego i okna sesji — kropka w jej kolorze, nazwa sesji pod tytułem),
  ostatnio używane najpierw (`okna_ostatnio_uzyte`: licznik przy każdym oknie, które staje się aktywne), wybrane jest
  poprzednie; Tab / Shift+Tab — następny / poprzedni kafelek, Esc — zamyka bez zmiany, puszczenie Alt — wybrane okno na
  wierzch (`AKTYWUJ_OKNO`; zminimalizowane wraca). Szybkie Alt+Tab = poprzednie okno; jedno okno albo żadne — nic.
  Kliknięcie kafelka też pokazuje okno, kliknięcie obok zamyka. Rysowany na panelu menu kontekstowego (`okienko_wlasne`
  z nazwą „Przełącznik okien” w opisie ekranu; panel ma od tego kroku co najmniej 6 × 3 kafelki). Log: „przełącznik okien:
  Ustawienia x,y; Zegar x,y; sesja 3 x,y (wybrane: Zegar)”, „wybrane …”, „pokazuję …”, „Anuluj” — sesje jako „sesja N”.
  Log przy zmianie obecności, stanu, zasilacza lub co 5 % („bateria 73%, rozładowywanie, …”).
- Powierzchnie: warstwa pulpitu (`K1_POW_PULPIT`, cały ekran, zatwierdzane tylko zmienione prostokąty),
  panele: górny pasek, dolny pasek, menu Start, okienka paska (0.11.8; do niej okienko dźwięku), menu kontekstowe (memfd, ADR 0008; na nim także okienka
  pytań i „Którą sesję otworzyć?”).
- Do nadzorcy (fd 4) od 0.11.8 krok 3: `BATERIA_OKIENKO` — a = 1 okienko baterii otwarte, 0 zamknięte, 2 „Ustawienia
  baterii” (nadzorca uruchamia Ustawienia, wysyła im `BATERIA_POKAZ` i pulpitowi `POKAZ_APL`, jak `OTWORZ_AKTUALIZACJE`);
  od ekranu hasła nadzorca go nie przyjmuje. Od 0.12.3 krok 2: `POKAZ_USTAWIENIA` — a = miejsce Ustawień z tabeli
  `K1_UST_MIEJSCA` (`k1_komunikat.h`; dziś KOREKTOR — wiersz „Korektor” okienka „Dźwięk”): nadzorca uruchamia Ustawienia,
  wysyła im `USTAWIENIA_MIEJSCE` i pulpitowi `POKAZ_APL`.
- Do ekranu (0.7.5): `ZAMKNIJ_OKNO` — „Zamknij okno” z paska; ekran prosi aplikację (`ZAMKNIJ`).
- Zmiana rozdzielczości (M4.1): ekran wysyła ponownie `WITAJ` z nowym rozmiarem → gui zamyka menu,
  tworzy górny pasek na nową szerokość, przesuwa pozostałe panele i loguje nowe położenia.
- Log: położenie przycisków, ikon i wierszy menu kontekstowego (dla testów: „pulpit gotowy”, „pasek
  zmieniony”, „ikony na pulpicie”, „menu kontekstowe”), uruchamiane aplikacje, zmiany rozmieszczenia. Bez treści.

  Od 0.10.5 krok 2 (ADR 0046, D1 znak A, D13; `aktualizacja.c`): `AKT_STAN` i `AKT_OPIS` od nadzorcy (fd 5) — gdy nowa
  wersja czeka na „Zainstaluj”, raz na start pulpitu (i po pobraniu) okienko „Jest nowa wersja K1” (wersja, najnowszy opis
  — ogłoszenie sprawdzone jeszcze raz `k1_ogloszenie_czytaj`, „Cały opis: Ustawienia → Aktualizacje”, „Później” i
  „Zainstaluj” → `AKT_PROSBA` a = `ZAINSTALUJ`, jedyna prośba o aktualizację, jaką nadzorca przyjmie od gui), a przy
  przycisku Start kropka w kolorze akcentu, dopóki wersja czeka.

  Od 0.11.0 krok 2 (ADR 0052, D2 „A”; `pobranie.c`): `POBRANIE` od nadzorcy (fd 5, z nazwą w memfd — sprawdzoną jeszcze
  raz) — okienko „<sesja> pobrała plik” (kropka w kolorze sesji, nazwa, rozmiar, „Zapisać go w Plikach → Pobrane?”,
  „Zostaw w przeglądarce” / „Zapisz w Pobranych” → `POBRANIE_DECYZJA`); plik ponad 4 GB — „Jest za duży dla Plików”, tylko
  „OK”; po „Zapisz” bez zapisu (`POBRANIE_WYNIK`: za mało miejsca, przerwane) — „<sesja> · nie zapisano pliku” z powodem.
  Kilka plików — po kolei (do 8). Kliknięcie obok nie zamyka okienka (odpowiedź jest gestem użytkownika w oknie K1, ADR 0027
  p. 2); menu otwarte nad nim — okienko wraca, gdy menu się zamknie. Log: „okienko (pobranie): sesja N, plik M; Zapisz
  w Pobranych x,y; Zostaw w przeglądarce x,y” — nigdy nazwa pliku.
  Od 0.11.0 krok 3 (ADR 0052): `POBRANIE` z e = 1 — zmieniona wersja pliku, który K1 dał sesji: „<sesja> zmieniła plik”, nazwa
  oryginału, „Zapisać zmiany w oryginale?” / „„Zapisz”: poprzednia wersja trafi do Kosza.”, przyciski „Zapisz”, „Zapisz jako
  nowy plik”, „Nie” (`POBRANIE_DECYZJA` b = `K1_POB_*`); przyciski każdego rodzaju okienka z jednej tabeli. Menu pliku na
  pulpicie: „Wyślij do” i sesje w „Otwórz za pomocą” → `DOK_DO_STREFY` do skarbca.

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
| `test-m5.sh` (0.11.8 krok 3): okienko baterii — liczby wyliczone z tablicy ACPI (73 %, ładowanie, 9,0 W, do pełna 81 min), nadzorca czyta co 5 s, drugie kliknięcie i menu Start zamykają, „Ustawienia baterii” → Ustawienia na sekcji Bateria, zrzut `m5-okienko-baterii`; `test-m1.sh`: „pomiar baterii: …, zostało 219 min” przy rozładowywaniu; `test-wzorce.sh`: `okienko-baterii-jasny/-ciemny` (osobny przebieg z baterią) | funkcjonalny |
| `test-m4.sh`: menu Start (0.11.8: 7 wierszy od końca listy, kółko przewija, ponowne otwarcie znowu od końca, prawy przycisk po przewinięciu), górny pasek (0.11.8: „Sieć” i „Dźwięk” w opisie, okienko „Dźwięk” pod ikoną, zamykane drugim kliknięciem i menu Start), uruchamianie z paska i z menu, minimalizacja, Start → Wyłącz, Start → Zablokuj (tylko z hasłem: ekran hasła wraca; bez hasła przycisku nie ma), zrzuty | funkcjonalny |
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

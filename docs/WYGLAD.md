# Wygląd K1

Decyzje o wyglądzie podejmuje właściciel; ten plik zapisuje ustalenia i ich realizację.
Zrzuty z testów: `build/testy/m4-*.png`, `build/testy/<część>-*.png` (artefakt CI `k1-<wersja>-logi`).
Podgląd tapety bez QEMU: `make build/obj/podglad_grafiki && build/obj/podglad_grafiki 1920 1080 -1 -1 0 WARIANT > tlo.ppm`
(PPM → PNG: `python3 -I scripts/ppm-na-png.py tlo.ppm tlo.png`).

## Ustalenia właściciela
- 2026-11-06 (M2): nowa tapeta i logo; logo nawiązuje do **niezależności i bezpieczeństwa**; styl
  jak **Claude Code** (ciemne, ciepłe tło, terakota, krem, prostota).
- 2026-11-06 (przed M4): **główny pasek na dole, wyśrodkowany jak w Windows 11**; na górze dźwięk,
  (w przyszłości) bateria i zegar. Cały interfejs **minimalistyczno-ciepły, w stylu Claude Code
  i Anthropic**. Aplikacje: Zegar, Notatki, **Ustawienia**.
- 2026-11-06 (M4, po zrzutach): tapeta **na wzór zdjęcia** właściciela (warstwy wygiętych
  „wstęg”), może być w kolorach systemu, **kilka wariantów**; **logo zminimalizowane**, eleganckie.
- 2026-11-06 (po pierwszym teście w VirtualBoxie): płaska tarcza „okropna — przebudować od zera”;
  z czterech kierunków (Pieczęć, Brama, Warstwy, Baszta) właściciel wybrał **C — Warstwy**.
  Rozmiary pulpitu: „bardzo fajne”.

- 2026-11-07 (przed M5): ekran hasła przy starcie **„bardzo ładny, w stylu systemu — Claude'owy”**
  (tapeta, ciepłe kolory, Plex, znak K1); szkice do wyboru przed implementacją.

- 2026-11-09 (plan 0.7.5/0.8.0, ADR 0026): pulpit **„jak w Windowsie, ale estetycznie, zgodnie ze
  stylem K1”** — ikony aplikacji (a od 0.8.0 plików i Kosza) na pulpicie, menu pod prawym przyciskiem,
  przypinanie i odpinanie na pasku. Kosz usuwa po 30 dniach albo ręcznie. Szkice do wyboru przed
  implementacją.

- 2026-11-09 (0.7.5): z trzech szkiców pulpitu (A „Kafelki”, B „Linie”, C „Szkło”) właściciel wybrał
  **A „Kafelki”**; „kolory pod-okien i otoczka ikon też zmieniają się z trybem” — kafelki, zaznaczenie
  i menu kontekstowe w kolorach motywu (jasny / ciemny). Na pulpicie przy pierwszym starcie: wszystkie
  aplikacje oprócz Instalatora.

- 2026-11-17 (plan 0.10.2 krok 3, decyzje D3 i D4): **zatwierdzony wygląd pilnują zrzuty wzorcowe**
  (`testy/wzorce/`, `docs/TESTY.md`) — pulpit z paskami, menu Start, okienko sesji, Ustawienia (każda sekcja), Pliki,
  Zasoby, Kalkulator i ramka strefy, w obu motywach. **Zmianę wyglądu zatwierdza właściciel (D3):** PR, który ją
  wprowadza, pokazuje obrazy „przed / po” z testu; wzorce w repo zmieniają się dopiero po jego „OK”.

- 2026-11-17 (po przeglądzie wyglądu, plan 0.10.7 „Wygląd”, `docs/archiwum/PLAN-0.10.7.txt`): **lista sekcji w Ustawieniach
  zostaje taka, jak jest** (minimalistyczna, bez grup i nagłówków); z napisów w systemie znikają **opisy tego, co
  użytkownik widzi** (podtytuły sekcji, zdania-podpisy) i **półpauzy**; nowych takich zdań nie dodajemy. **Kolor sesji
  nie obejmuje całego okna przeglądarki** („psuje estetykę”): ma być w jednym miejscu, dyskretnie (szkice w kroku 4).

- 2026-11-18 (plan 0.10.7 krok 1, decyzja D2): **napisy bez opisów i bez półpauz.** (Z2) Sekcje Ustawień mają sam
  tytuł (Plex Serif 24), bez podtytułu; treść każdej sekcji zaczyna się 32 px pod linią tytułu (`seg_y()`, ten sam
  odstęp wszędzie; w Sieci w tym miejscu stoją zakładki). Znikły też zdania-podpisy: „Każda część systemu działa
  w osobnej, zamkniętej klatce.”, „Ile miejsca zajmują Twoje pliki…” (Informacje), „Nieużywaną pamięć sesja oddaje K1
  sama…” (Zasoby), „Lista odświeża się co minutę.” (Wi-Fi). **Zostają** opisy w menu Start (decyzja właściciela) oraz
  informacje, których nie widać, a od nich zależą dane albo działanie: ostrzeżenie przy haśle, „Pliki są tylko
  w pamięci do wyłączenia komputera.” (Miejsce, na dole okna), instrukcje Instalatora, Secure Boot i Aktualizacji.
  (Z3) Zamiast półpauzy: dwa zdania („Złe hasło. Spróbuj jeszcze raz.”), przecinek albo dwukropek („Nie możesz: …”).
  Pilnuje tego `scripts/sprawdz-czytelnosc.py` (punkt 11: napisy okien K1; logi nie). Zdania pełne mają kropkę,
  krótkie napisy (nagłówki, przyciski, etykiety) nie.

## Kierunek (M4)
- **Charakter:** spokojny, rzeczowy, lekko ciepły.
- **Świat:** papier i glina — kremowy papier okien, terakota, ciemny brąz wieczoru.
- **Bogate miejsca:** tapeta (warstwy papieru) i tarcza Zegara; reszta interfejsu cicha: neutralne
  tła, włoskowate linie, jeden akcent.
- **Znak rozpoznawczy:** znak „Warstwy” — przycisk Start i nagłówek menu.

## Ekran startowy (od 0.5.8)
Na prośbę właściciela („logo przy starcie, ale bez widocznych poleceń terminala”): od GRUB-a do
pierwszej klatki pulpitu tylko znak K1 (88 px) i „Kons K1” (Plex Serif 24, kremowy `#EEEBE5`) na
**czarnym** tle — czerń jak ekran firmware'u przed nim, więc przejście jest gładkie. Obraz PNG
1320×780 rysuje przy budowie `narzedzia/ekran_startowy.c` tym samym kodem co K1; GRUB rozciąga go do
trybu ekranu (najwyżej ~5 % zniekształcenia), jądro go zostawia (simpledrm, bez konsoli tekstowej),
komunikaty „EFI stub” wyciszone (`quiet`). Kursor GRUB-a jest czarny, więc niewidoczny.

## Znak K1 (od 0.4.2): „Warstwy”
Trzy arkusze papieru leżące jeden na drugim — ta sama myśl co tapeta: warstwy ochrony, każda
osobno. Ćwiartki koła wychodzące z prawego dolnego rogu zaokrąglonego kwadratu, w trzech odcieniach
terakoty (`#B55C3E`, `#D97757`, `#E9A88B`), płasko, bez obrysu i poświaty. Rysowany w kodzie
(`k1_rysuj_znak` w `src/grafika/k1_grafika.c`) w każdym rozmiarze; czytelny od 20 px.
Wcześniej: tarcza z blankami i monogramem K1 (M2–0.4.1) — porzucona na prośbę właściciela.
Szkice kierunków: `build/podglad/logo-kierunki.png` (generowane skryptem w sesji, nie w repo).

## Tapety (od M4)
Warstwy długich, pochylonych arkuszy papieru: krawędź każdego schodzi z góry, zakręca i wychodzi
w prawym dolnym rogu; tam, gdzie krawędź jest zwrócona do patrzącego, widać ciepło oświetloną
ściankę i miękki cień na arkuszu niżej; powierzchnie odbijają „niebo” z delikatnymi chmurami. Krawędź jest matowa:
od 0.10.7 (D5) bez jasnej, „świecącej” linii, która do 0.10.5 biegła po krawędzi każdego arkusza.
Liczone w kodzie (`k1_rysuj_tlo`), w połowie rozdzielczości i powiększane, z ditheringiem.
Wybór: **Ustawienia → Wygląd → Tapeta** (miniatury z tego samego kodu).

| Nr | Nazwa | Kolory |
|---|---|---|
| 0 | **Glina** (domyślna) | kolory systemu: terakota i krem na ciemnym brązie |
| 1 | Zmierzch | jak zdjęcie właściciela: fiolet, błękit, róż, złoto |
| 2 | Noc | indygo i morski |
| 3 | Świt | jasna: brzoskwinia i lawenda |
| 4 | Las (0.7.6) | mech i szałwia na ciemnej sośnie, złote krawędzie |

## Pulpit (M4)
- **Górny pasek** (32 px, cała szerokość): przyciemnienie tapety (czarny 59%), kremowy tekst
  IBM Plex Sans Medium 13: z lewej nazwa aktywnej aplikacji (albo „Kons K1”), z prawej ikona
  głośnika i data z godziną („pt. 6 lis   19:42”; sekundy — w Ustawieniach). Na laptopie (0.5.3)
  przed głośnikiem bateria: [błyskawica przy ładowaniu] „73%” + kontur baterii 22×12 (zaokrąglenie 3,
  linia 1,25) z kremowym wypełnieniem; ≤ 15 % przy rozładowywaniu wypełnienie `#D97757`.
- **Ekran hasła (M5e)** — wybrany szkic B „Zegar” (2026-11-07): tapeta (w szkicu Zmierzch) lekko
  przyciemniona (czarny 27%), w lewym górnym rogu znak K1 22 px + „Kons K1”, z lewej na wysokości
  połowy ekranu zegar Plex Serif Light 64 i data (Serif 24), na dole pośrodku przezroczyste pole hasła
  w kształcie pigułki 340×48 (krem 18% + obrys krem 47%) z kropkami i okrągłym przyciskiem ze strzałką
  w kolorze akcentu, nad nim „Hasło”, pod nim „Enter: odblokuj · zapomniane hasło = nowe konto”;
  w prawym dolnym rogu bateria i wyłączanie. Szkice wariantów (A, B, C) były w
  `testy/host/szkice_logowania.c` do 0.7.0 (historia git).
- **Ustawienia → Bateria** (0.5.5): „73 %” (Plex Serif 24) + stan; wykres jak w Zasobach (pole 8 px,
  linia akcentu 2 px, wypełnienie akcentu 14 %, linia 50 %), pod nim pasek 3 px (tekst2) = zasilacz,
  oś: godzina startu … „teraz”.
- **Dolny pasek** (unosi się 8 px nad krawędzią, zaokrąglenie 16, tło okien 94%, włoskowaty obrys,
  miękki cień rzucany 4 px w dół, tylko poza panelem — od 0.5.0 bez „podstawy” pod paskiem): Start (znak K1 „Warstwy”), separator, ikony aplikacji 24 px w polach 44 px. Uruchomiona —
  kropka pod ikoną, aktywna — kreska 16×3 w kolorze akcentu.
- **Menu Start** (360 px): znak + „Kons K1” (Plex Serif 24) i wersja; wiersze aplikacji 56 px
  (ikona, nazwa, opis); stopka: „Uruchom ponownie”, „Wyłącz”.
- **Okna:** zaokrąglenie 12, pasek tytułu 44 px w kolorze okna (tytuł Plex Sans Medium 15,
  przyciski minimalizuj i zamknij po prawej), włoskowaty obrys, miękki cień rzucany przez ekran
  (mocniejszy dla aktywnego okna).

## Pulpit z ikonami (0.7.5, szkic A „Kafelki”)
- **Siatka:** komórki 84×92 od (16, 48), kolumnami z góry na dół; dół ekranu (100 px) zostaje dla paska.
- **Ikona:** kafelek 52×52 (zaokrąglenie 14) w kolorze tła okien (motyw: `#FAF9F6` / `#262523`, krycie
  96%), włoskowaty obrys, miękki cień 3 px w dół; ikona liniowa 28 px w kolorze tekstu. Pod kafelkiem
  nazwa — Plex Sans Medium 13, kremowa, na miękkiej ciemnej pigułce (czarny ok. 35 %, zaokrąglenie 6, 19 px wysokości,
  6 px z boków, zawsze w komórce) — czytelna na każdej tapecie w obu motywach (0.10.7, D6 — szkic A; do 0.10.5 kremowa
  z ciemnym cieniem 1 px, na jasnych łukach Gliny i na Świcie słabo czytelna).
- **Zaznaczenie i najechanie:** prostokąt komórki z zaokrągleniem 10 — jasny motyw: krem 18% (zaznaczona)
  / 9% (najechana), ciemny motyw: noc 43% / 24%; zaznaczona ma kremowy obrys 1,25 px.
- **Menu kontekstowe:** 232 px, zaokrąglenie 12, tło okien 98%, włoskowaty obrys, cień 6 px w dół;
  wiersze 36 px, tekst Plex Sans 15 (pierwszy — „Otwórz” albo nazwa aplikacji — Medium), kreska między
  grupami; najechanie jak w menu Start; „Zamknij okno” w kolorze akcentu. Na pulpicie i w menu Start
  otwiera się przy wskaźniku, na pasku — nad paskiem.
- **Menu pliku (0.8.2 krok 5, decyzja D7, szkic zaakceptowany 2026-11-12):** ten sam wygląd na pulpicie i w Plikach
  (w Plikach rysowane w oknie) — `k1_menu` w przyborniku. „Otwórz” Medium; „Otwórz za pomocą” z „›” (dwie kreski
  1,5 px) przy prawej krawędzi — podmenu 280 px obok (po prawej, bez miejsca — po lewej, nachodzi 4 px), rozwija się
  po najechaniu albo kliknięciu, wiersz-rodzic zostaje podświetlony; aplikacja, która otwiera typ teraz — ✓ w kolorze
  akcentu przed nazwą; „Zawsze otwieraj tą aplikacją” z polem 16 px (zaznaczone: wypełnione akcentem z ✓). Wiersz
  wyszarzony (`tekst3`) z powodem pod spodem (Plex Sans 13, zawijany). Komunikat przy ikonie (typ bez aplikacji):
  panel 280 px — typ (Medium 15), zdanie (Plex Sans 13, `tekst2`, zawijane), kreska, „Pokaż w Plikach”.
- **Przeciąganie (0.8.2 krok 6, szkic zaakceptowany 2026-11-12):** w Plikach przy kursorze **karteczka** — tło `pole`
  z kryciem 84 %, zaokrąglenie 10, włoskowaty obrys, cień 4 px w dół; ikona 18 px na kwadracie 26 px (`najechanie`),
  nazwa Plex Sans Medium 13 (najwyżej 240 px), przy więcej niż jednym elemencie **liczba** w kółku 22 px w kolorze
  akcentu na prawym górnym rogu. Pod nią **znak** — pigułka 24 px: białe kółko z symbolem i tekst Medium 13 na tle
  ciemnym `#1F1E1C` (przenieś: →), zielonym `#2F6B4F` (kopiuj — import: +) albo ciemnoczerwonym `#7A2E1C` (nie można: ⦸) —
  te trzy kolory poza motywem, te same w obu motywach (znak leży na liście albo na tapecie). Cel: obrys 1,5 px i tło
  w kolorze akcentu (krycie 13 %) na wierszu albo miejscu z lewej; przeciągane wiersze przygaszone (krycie 43 %).
  Zaznaczenie wielu: tło `zaznaczenie` i pionowa kreska 3 px w kolorze akcentu. Na pulpicie: komórka celu z kremowym
  obrysem 2 px i kremowym tłem 27 %, przy kursorze kafelek pliku z kryciem 70 % i ten sam znak.
- **Okienko pytania (0.8.2 krok 6):** panel menu (`k1_menu`) 300 px przy miejscu upuszczenia: pytanie Plex Sans Medium 15
  (zawijane, wiersz 21 px), pod nim zdanie Plex Sans 13 (`tekst2`), kreska, wiersze odpowiedzi — pierwszy Medium
  („Przenieś do Kosza”, „Zachowaj oba”, „OK”), „Anuluj” zwykły.
- **„Jeszcze n” (0.8.2 krok 6, L19):** kafelek jak ikona, ale z kryciem 55 %, w środku „+12” Plex Sans Medium 15.
- **Dolny pasek:** szerokość według liczby aplikacji, wyśrodkowany; przypięte, kreska, uruchomione spoza
  paska.

## Pliki i Kosz (0.8.0, ADR 0029)
- Okno Plików jak Eksplorator w Windows 11, w stylu K1 (do oceny właściciela na zrzutach): z lewej miejsca (stałe
  foldery z ikonami liniowymi, kreska, Kosz), nad listą przycisk „w górę” i ścieżka „Dokumenty › Projekt”, pod nią
  przyciski miejsca (zwykłe, z obrysem; „Tak, usuń” w kolorze akcentu), lista wierszami po 32 px (ikona folderu
  w kolorze akcentu, plików — szara), z prawej szczegóły (do 0.8.2 „podgląd”) na polu w kolorze `pole` motywu. Zmiana nazwy: pole z obrysem
  w kolorze akcentu, zaznaczona nazwa podświetlona akcentem.
- **Tylko to, czego można teraz użyć** (0.10.7 krok 3, decyzja właściciela D4 z 2026-11-18): stale widać tylko „Nowy folder”
  (w Koszu „Opróżnij Kosz”, na pendrivie „Importuj wszystkie”); „Otwórz”, „Zmień nazwę”, „Usuń” („Przywróć”, „Usuń na
  zawsze”; „Importuj”) pojawiają się po zaznaczeniu, za stałymi, więc stałe się nie przesuwają (tabela `PRZYCISKI`
  w `przyciski.c`, kolumna „zawsze”). Szczegóły z prawej tylko przy zaznaczeniu; bez niego lista zajmuje całą szerokość.
  Napisy w szczegółach zawijane, nigdy ucięte (`k1_zawin` z przybornika; nazwa pod miniaturą zdjęcia — jedna linia, bo
  tuż pod nią jest „Ustaw jako tapetę”). Pusty folder: znak „Warstwy” w odcieniach szarości (64 px, `tekst3` i jego dwa
  rozjaśnienia ku tłu), pod nim „Ten folder jest pusty” (`tekst2`) i przycisk „Nowy folder”, na środku listy; pusty Kosz
  i pusty folder na pendrivie — to samo bez przycisku. Do 0.10.7 wszystkie przyciski były zawsze, nieaktywne tylko
  jaśniejsze, a szczegóły bez zaznaczenia mówiły „Wybierz plik, aby zobaczyć szczegóły” (ucięte).
- Kosz na pulpicie: kafelek jak aplikacje; pusty — kosz z dwoma żebrami, pełny — z kulką papieru w środku.
- Pliki na pulpicie: kafelek z ikoną typu (folder, tekst, obraz, inny plik), nazwa ucięta „…”, gdy za długa.

## Podgląd (0.8.2, ADR 0037, szkic C)
Decyzja właściciela D5 (2026-11-12) z trzech szkiców (A — ramka motywu i pasek na dole, B — ciemne okno ze
szczegółami po prawej, C — zdjęcie na całe okno z pływającym paskiem): najpierw A, w trakcie zmieniona na **C**
(„ładny, estetyczny pasek, nie idący do samych końców”).
- Okno 960 × 640, ramka (pasek tytułu) w kolorach motywu; pod nią całe okno to ciemny obszar zdjęcia (`#1c1b19`,
  dolne rogi zaokrąglone jak okno), zdjęcie wyśrodkowane, dopasowane do okna, nigdy powiększone ponad 100 %.
- Pływający pasek na dole, wyśrodkowany, 16 px nad krawędzią: „pigułka” 44 px (promień 22, `#1f1e1c`, krycie 215):
  ‹ w kółku, „3 z 27”, › w kółku, cienka kreska, − i + w kółkach (przybliżenie, animacja 0,18 s: najpierw szybko,
  potem wolniej), cienka kreska, potem przyciski-napisy „Otwórz…”, „Obróć”, „Ustaw jako tapetę”
  (białe, Plex Sans Medium 13) i „Do Kosza” (jasna terakota). Najechany — jaśniejsze tło; niedostępny — przygaszony.
- Etykieta w lewym górnym rogu zdjęcia (pigułka 26 px, krycie 200): „nazwa · 4000 × 2667 · 2,4 MB · data ·
  pochodzenie”, ucięta „…”, gdy za długa. Komunikat (np. „Tapeta ustawiona.”) — taka sama etykieta nad paskiem.
- Bez zdjęcia / błąd / wczytywanie: ikona obrazu 48 px i jedno zdanie na środku ciemnego obszaru.

## Zasoby (M4.1)
Okno 560×520: „Procesor” i „Pamięć” — nazwa, liczba po prawej (Plex Sans Medium 15), pod spodem
wykres z ostatniej minuty na karcie (`pole`, zaokrąglenie 8, włoskowaty obrys): jedna seria w kolorze
akcentu (linia 2 px, delikatne wypełnienie pod nią), linia pomocnicza na 50%, najnowsza próbka przy
prawej krawędzi. Niżej tabela usług (procesor, pamięć) tekstem drugorzędnym. Bez drugiej osi
i bez kolorów spoza motywu.

## Ustawienia → Miejsce (0.8.5 krok 3, decyzja właściciela D1: szkic B)
Trzy szkice (A — pierścień z wolnym miejscem w środku i legenda obok, B — szeroki pasek jak w Windows 11, C — pierścień
i kafelki); wybrany **B**. Na górze „Wolne: 41,2 GB z 57,6 GB” (Plex Sans Medium 15), pod nim pasek 440 × 18 px
z zaokrąglonymi końcami: tor `tekst` przy 22/255, części od lewej, 2 px przerwy w kolorze tła; największy folder —
akcent (terakota), kolejne według wielkości: szałwia `#8a9a7b`, łupek `#7d8fa3`, piasek `#c9a27e`, wrzos `#a67f8e`,
glina `#b7a99a`; Kosz `#9c9790`, System — `tekst3`. Legenda w dwóch kolumnach (co 228 px, wiersz 24 px): kwadracik
10 px z zaokrągleniem 3, nazwa (Plex Sans 13), rozmiar do prawej (`tekst2`). Pod legendą zapas systemu i liczba plików
(`tekst3`, 13 px); przycisk „Użyj całego pendrive'a na dane” na dole sekcji. Ikona sekcji: dysk (`K1_IK_DYSK`).

## Strefy (0.10.0 krok 1, decyzja właściciela D1: ramka B2, start S1, ikona A)
Szkice narysowane częściami K1 na hoście (2026-11-14), wszystkie widoki stref naraz (także kroków 2–3): ramka A „Ramka
i pasek”, B „Pasek K1 z plakietką”, C „Zakładka”; start S1 „w oknie strefy” i S2 „karteczka K1”; ikona A „Globus”,
B „Kompas”, C „Okno z globusem”; okienka „brak wirtualizacji” i „za mało pamięci”; Ustawienia → Aplikacje → Przeglądarka.
Właściciel wybrał **B, ale bez paska nad oknem**: „plakietka ma się pokazywać po najechaniu na jej małą ikonkę w rogu;
ramki mają minimalnie tylko informować” — szkic **B2** zaakceptowany; **S1**; ikona **A** (krok 2).

**Zmiana D1 (krok 3, 2026-11-16) — ramka A „Pasek w kolorze sesji”.** Po teście przeglądarki na IdeaPadzie właściciel
chciał pełnej ramki jak u innych okien K1 (pasek do przesuwania, minimalizuj, zamknij — bez szukania plakietki). Trzy
szkice (A — pasek 40 px w kolorze sesji, B — pasek K1 44 px z kolorowym znaczkiem i obwódką, C — cienki pasek 30 px);
wybrany **A**. Znaczek i plakietka B2 zniknęły.
- **Pasek tytułu:** 40 px (`K1_STREFA_TYTUL`) nad obrazem strefy, w kolorze strefy; ikona strefy 18 px, napis
  „<tytuł> · <sesja> · osobna maszyna” (+ „· zapomina po zamknięciu”) Plex Sans Medium 15 biały; po prawej minimalizuj
  i zamknij (ikony 16 px w polach 28 px, co 32 px, 8 px od krawędzi; kursor nad przyciskiem — pole w kolorze napisu
  z przezroczystością 40). Chwycenie paska przesuwa okno (pasek nie wjedzie pod górny pasek K1).
- **Obwódka:** 3 px w kolorze strefy wokół paska i obrazu (prostokąt zaokrąglony 10 px); okno bez fokusu — kolor
  zmieszany w 45 % z tłem motywu.
- **Strefa jednorazowa:** jasny pasek — `pole` z 14 % koloru strefy — w obwódce w kolorze strefy; ikona, napis i
  przyciski w kolorze strefy przyciemnionym w stronę tekstu.
- **Zmiana 0.10.7 (krok 4, ADR 0048, decyzja właściciela D1 z 2026-11-18: szkic A „Kropka i linia”).** Kolor na całej
  ramce „psuł estetykę”. Od 0.10.7 ramka jak okna K1: tło okna motywu (także obwódka 3 px), włoskowaty obrys (`tekst`
  26/255), cień ekranu; ikona strefy (`tekst2`) i napis „<tytuł> · ● <sesja> · osobna maszyna” w kolorze `tekst` (bez
  fokusu `tekst3`); kolor strefy tylko w **kropce 10 px** przed nazwą sesji (bez sesji — przed „osobna maszyna”) i **linii
  2 px** pod paskiem, tuż nad obrazem strefy; bez fokusu przygaszony (115/255 z tłem). Przyciski jak w oknach K1 (`tekst2`,
  najechanie — pole `najechanie`). Strefa jednorazowa: dopisek „· zapomina” zamiast jasnego paska. Opis wyżej (pasek
  w kolorze strefy, obwódka, jasny pasek) — historia do 0.10.5.
- **Przeglądarka bez własnych przycisków okna** (S28): Firefox w strefie nie rysuje minimalizuj / powiększ / zamknij
  (ustawienie GTK w obrazie strefy) — robi to tylko pasek K1.
- **Start (S1):** okno w ramce od razu; w środku tło motywu, ikona strefy 40 px (`tekst3`), „Uruchamiam: <tytuł> ·
  <sesja>…” (Plex Sans 15) i pasek 160 × 4 (`tekst` 24/255), po którym odcinek 48 px w kolorze strefy przesuwa się tam i z
  powrotem (1,6 s, co 100 ms) — 0.10.2 krok 1, D1 właściciela 2026-11-17: szkic A (inne: kropki, zdanie po 20 s). Do okna
  programu strefy (przeglądarka: okno Firefoksa), bez procentów — nie wiadomo, ile zostało.
- **Kolory:** strefa próbna — piasek `#A8834A`; sesje przeglądarki (krok 2, ze szkicu): Ogólna — szałwia `#5F8A6A`, Bank —
  łupek `#4A6FA5`, Prywatna — wrzos `#8B6AA8`.
- **„Nie może ruszyć”** (brak wirtualizacji, pamięci, obrazu): okienko odmowy pulpitu (to samo co przy przeciąganiu
  plików, nie osobne okno ze szkicu): pogrubione „<tytuł> nie może ruszyć”, wyjaśnienie, kreska, „Rozumiem”.
- **„Zamknęła się”** (krok 3, S26): to samo okienko, gdy strefa skończyła się bez zamknięcia przez użytkownika —
  „<tytuł> zamknęła się” i powód (brak pamięci w strefie, brak pamięci w komputerze, awaria programu, błąd strefy).
- **Dolny pasek:** okno strefy wśród działających (po kresce), ikona strefy; okna sesji się nie przypina — przypina się
  **Przeglądarkę** (0.10.5 krok 6, decyzja właściciela D12 z 2026-11-18: szkic A „ikona + okna sesji”): jedna ikona globusa
  na wszystkie sesje (kliknięcie — okienko „Którą sesję otworzyć?”, kropka „uruchomiona”, gdy któraś sesja ma okno, kreska
  akcentu, gdy aktywna); okna otwartych sesji zostają po kresce, każde z kropką w kolorze sesji na ikonie (koło 4 px na
  krążku 5,5 px w kolorze tła paska, prawy dolny róg ikony). Na pulpicie ikona „Przeglądarka” jak aplikacja. Przy
  pierwszym starcie (nowa instalacja) przypięta na końcu paska i na pulpicie za aplikacjami; po aktualizacji zapisane
  rozmieszczenie zostaje — K1 nie dodaje jej sam.
- **Przełącznik okien — Alt+Tab (0.10.5 krok 6, decyzja właściciela D11 z 2026-11-18: szkic A „kafelki”, oba motywy):**
  panel na środku ekranu (tło paneli `tlo` z przezroczystością paneli, zaokrąglenie 16, włoskowaty obrys, cień jak panele),
  margines 16; kafelki 120 × 104 co 8 px, najwyżej 6 w rzędzie i 3 rzędy: ikona 40 px (`tekst`), nazwa pod nią (Plex Sans
  Medium 13, `tekst`), w oknie sesji pod nazwą nazwa sesji (Plex Sans 11, `tekst2`) i kropka w kolorze sesji przy ikonie
  (koło 5 px na krążku 7 px w kolorze tła kafelka). Wybrany kafelek: tło `najechanie`, obwódka 2 px w kolorze akcentu,
  zaokrąglenie 12. Bez podglądu treści okien. Kolejność: ostatnio używane najpierw; pierwsze Tab wybiera poprzednie okno.
- **Sesje — Ustawienia → Przeglądarka (0.10.1 krok 4, decyzja właściciela D5 z 2026-11-17: szkic A „karta na sesję”;
  zastępuje wiersze co 78 px z 0.10.0 krok 5 — opis wchodził na następną sesję):** sekcja przed Informacjami (ikona globusa),
  (od 0.10.7 bez opisu pod tytułem); karta na sesję: `pole`, 440 × 88, zaokrąglenie 10, włoskowaty
  obrys, co 96 px; kropka 6 px w kolorze sesji (22, 23), nazwa Plex Sans Medium 15, pod nią jedno zdanie stanu Plex Sans 13
  `tekst2`; dolny rząd: ciche „Zmień nazwę” i „Wyczyść dane…” (Plex Sans 13), po prawej przełącznik segmentowy 184 × 28
  „Pamięta | Zapomina”. Zmiana nazwy: pole 210 × 30 z ramką w kolorze akcentu w miejscu nazwy, w dolnym rzędzie „Zapisz”
  (akcent) i „Anuluj”. „Wyczyść dane…” pyta w okienku przybornika (to od przeciągania plików): pogrubione „Wyczyścić dane
  sesji <nazwa>?”, zdanie wyjaśnienia, kreska, „Wyczyść dane” (pogrubione), „Anuluj” — bez czerwieni. Uwagi (bez K1-DANE,
  bez wirtualizacji) — tylko gdy dotyczą, każda jedna linia nad listą: ikona info 16 px `tekst3` i Plex Sans 13 `tekst2`;
  bez K1-DANE przełącznik wyciszony (`pole` 130/255 na nim) i bez „Wyczyść dane…”. Wynik czyszczenia pod listą
  (Plex Sans Medium 13, `tekst2`). Okno Ustawień 680 × 480. Odrzucony szkic B „lista, wybrana sesja się rozwija”.
- **Okienko sesji (0.10.1 krok 4, decyzja właściciela D4 z 2026-11-17: szkic B „okienko na środku ekranu”):** kliknięcie
  „Przeglądarki” (menu Start, „Otwórz” w jej menu; w przyszłości też pulpit) pokazuje panel (jak menu: tło motywu
  z przezroczystością panelu, zaokrąglenie 16, cień) na środku ekranu, w 1/3 wysokości: ikona globusa 20 px `tekst2`
  i „Którą sesję otworzyć?” (Plex Sans Medium 15); kafelki 120 × 104 co 132 px (`pole`, zaokrąglenie 10, włoskowaty
  obrys; pod kursorem tło `najechanie` i obrys 1,5 px w kolorze sesji): kropka 11 px w kolorze sesji, nazwa (Medium 15),
  „pamięta” / „zapomina” / „otwarta” (Plex Sans 13 `tekst2`); kreska i „Anuluj” (Plex Sans 15 `tekst2`) po prawej na dole.
  Właściciel najpierw wybrał A („pigułki sesji w wierszu menu Start”), potem B — bo przeglądarkę będzie się otwierać też
  z innych miejsc. Odrzucone: A, C „strona sesji w menu”. Wariant „rozwija pod sobą wiersze” nie mieścił się na ekranie
  768 px.
- **Mało pamięci (0.10.1 krok 3, decyzja właściciela D3 z 2026-11-17: okienko A „na środku, z sesjami”):** panel 380 px
  (jak okienko sesji) na środku ekranu, w 1/3 wysokości: „Mało pamięci” (Plex Sans Medium 15), dwie linie Plex Sans 13
  `tekst2` „Komputerowi kończy się pamięć. Zamknij kartę albo / całą sesję, której nie potrzebujesz.”; wiersz co 44 px na
  sesję: kropka 5 px w kolorze sesji, nazwa (Medium 15), pamięć (Plex Sans 13 `tekst2`, x 150), „w oknie” (`tekst3`, x 214)
  przy sesji z fokusem, po prawej przycisk obrysowany „Zamknij” 92 × 28; kreska i ciche „Później” na dole. Odrzucone:
  okienko B (karteczka pod górnym paskiem z „Pokaż sesje”).
- **Zasoby → Sesje przeglądarki (0.10.1 krok 3, D3: szkic B „paski sesji”):** część nad „Usługi i aplikacje”: tytuł
  „Sesje przeglądarki” (Medium 15), wiersz co 46 px — kropka 4 px w kolorze sesji, nazwa (Medium 13), po prawej „1,1 GB
  z 2,8 GB” (13, `tekst2`), pod nimi tor 10 px (zaokrąglenie 5, `tekst` 22/255) z wypełnieniem w kolorze sesji; pod
  wierszami do 0.10.7 krok 1 zdanie „Nieużywaną pamięć sesja oddaje K1 sama…” (usunięte, Z2). Odrzucone: A (wiersze listy usług
  z drugą linijką „używa … · może dostać …”).
- **Menu sesji (0.10.0 krok 5, D2; zostaje obok okienka sesji):** prawy przycisk na „Przeglądarce” w menu Start — „Otwórz”
  (pogrubione; od 0.10.1 otwiera okienko sesji), kreska, napis „Sesje” i wiersz na sesję (jej nazwa), potem jak u aplikacji
  („Dodaj do pulpitu”). Okienka i menu paska: „Przeglądarka · <sesja>”.
- **Sesja, która pamięta / zapomina (krok 5):** pasek w pełnym kolorze sesji = pamięta; jasny pasek i „· zapomina po
  zamknięciu” = zapomina (według tego, jak strefa ruszyła). Ustawienia → Miejsce: część „Strefy” w szałwii `#5F8A6A`.

- **Rozmiar okna strefy (0.10.5 krok 4, decyzja właściciela D8 z 2026-11-18: szkice z rekomendacji):** na pasku tytułu
  trzy przyciski: minimalizuj, **maksymalizuj** (kwadrat 11 px, linia jak inne ikony; okno zmaksymalizowane — „przywróć”,
  dwa nachodzące kwadraty), zamknij. Kursor nad chwytem brzegu albo rogu — podwójna strzałka w stylu kursora K1 (kremowa,
  ciemny obrys, cień): ↔, ↕, ↖↘, ↗↙. W trakcie ciągnięcia — zarys nowego rozmiaru: obwódka 3 px w kolorze sesji i cienka
  biała w środku, w prawym dolnym rogu plakietka „W × H” (ciemna, biały tekst); strona dostaje rozmiar po puszczeniu.
  Najmniejszy obraz 640 × 480. Zmaksymalizowane — od dolnej krawędzi górnego paska do górnej krawędzi dolnego, bez
  bocznej obwódki i zaokrągleń, pasek tytułu tuż pod górnym paskiem. Rozmiar i maksymalizacja zapamiętane dla każdej sesji.
  Od 0.11.0 krok 1 (prośba właściciela z 2026-11-20: „nie domyka krawędzi i zostawia ciut za dużo miejsca na dole”):
  zmaksymalizowane co do piksela od lewej do prawej krawędzi (1366 na X230 i IdeaPadzie, nie 1360) i do **widocznej** górnej
  krawędzi dolnego paska (bez 16 px marginesu cienia jego powierzchni).
- **Pełny ekran przeglądarki (0.11.0 krok 1, decyzja właściciela D1 z 2026-11-21: szkic A, szkice A/B w obu motywach):** cały
  ekran, bez górnego i dolnego paska i bez paska tytułu K1. Przez 4 s u góry na środku **pigułka** K1: tło okna (alfa 245),
  obwódka 2 px i kropka 10 px w kolorze sesji, tekst „Przeglądarka · Ogólna · pełny ekran: Esc, aby wyjść” (IBM Plex Sans 15;
  w szkicu była półpauza — zasada Z3: dwukropek). Kursor przy górnej krawędzi wysuwa **pasek K1** 40 px jak pasek tytułu okna
  strefy: tło okna, linia 2 px w kolorze sesji na dole, kropka i „<tytuł> · <sesja>”, po prawej przycisk „Zakończ pełny
  ekran” (zaokrąglony 8 px, tło „najechanie”, przy kursorze „zaznaczenie”); chowa się, gdy kursor zjedzie niżej niż 64 px.

## Sieć (0.9.0 krok 7, decyzja właściciela D1: ikona C, sekcja B)
Trzy szkice ikony (A — monitor z kablem, B — trzy połączone węzły, C — wtyczka sieciowa) i trzy układy sekcji (A — jak
Bateria, B — kafelki kart, C — stan na karcie); wybrane **ikona C** i **sekcja B** (2026-11-13).
- **Ikona na górnym pasku:** wtyczka RJ45 (`K1_IK_SIEC`: obudowa 14×11 z zaokrągleniem 2, cztery styki, zatrzask,
  kabel w dół; `K1_IK_SIEC_BEZ_KABLA` — bez kabla), 16 px, 10 px na lewo od baterii (bez baterii — od dźwięku).
  Połączono: krem. Łączę: krem 110/255 i kropka 1,8 px w `#e9a88b` (terakota jasna) w ciemnej obwódce, prawy dolny róg.
  Kabel odłączony: wtyczka bez kabla 110/255 i krzyżyk 4 × 4 px w ciemnym kółku. Brak karty: 70/255 i ukośna kreska.
- **Ustawienia → Sieć:** w bocznej liście przed „Informacje”, ikona wtyczki. Kafelek na kartę: `pole`, 64 px wysokości,
  zaokrąglenie 10, włoskowaty obrys, co 76 px; ikona 24 px (wtyczka albo Wi-Fi — trzy łuki nad kropką), nazwa Plex Sans
  Medium 15, opis Plex Sans 13 `tekst2`; plakietka po prawej: zaokrąglenie 12, tło `najechanie`, kropka 4 px — szałwia
  `#8a9a7b` (połączono), akcent (łączę, brak sterownika), `tekst3` (pozostałe). Najwyżej 3 kafelki (2, gdy są szczegóły),
  reszta „…i jeszcze n”. Pod kafelkami „Szczegóły karty przewodowej” (Plex Sans Medium 13) i wiersze co 22 px: etykieta
  `tekst2`, wartość `tekst` od 140 px. (Do 0.10.0 krok 1 na dole `tekst3`: „Aplikacje nie mają jeszcze dostępu do
  internetu.” — od kroku 2 przeglądarka ma internet, napis zniknął.)

## Wi-Fi (0.9.1 krok 1, decyzja właściciela D1: ikona B, sekcja A)
Szkice narysowane częściami K1 na hoście (2026-11-14), wszystkie widoki naraz (także kroku 2): ikona A „Łuki”, B „Wachlarz”,
C „Słupki”; sekcja A „Zakładki, wiersz się rozwija”, B „Lista i okienko hasła”, C „Kafelek karty i widok sieci”. Wybrane
**ikona B** i **sekcja A**.
- **Ikona na górnym pasku** (bez kabla, z kartą Wi-Fi; kabel ma pierwszeństwo — wtyczka): pełny wachlarz 90° (16 px,
  promień 12, wierzchołek u dołu) — `k1_wachlarz.c`. Nie połączono: wachlarz krem 90/255 i krzyżyk 4 × 4 px w ciemnym
  kółku w prawym dolnym rogu (jak „kabel odłączony”). Wyłączone: krem 46/255, ukośna kreska. W kroku 2: łączę — przygaszony
  z kropką w terakocie; połączono — wypełniona część według kresek 1–4 (promień 0,1 + 0,225·kreski), reszta przygaszona.
- **Ustawienia → Sieć:** pod tytułem „Sieć” (od 0.10.7 tam, gdzie inne sekcje mają przyciski) zakładki „Wi-Fi | Karty sieciowe” (`k1_segment`, 260 × 32);
  zakładka Wi-Fi jest pierwsza, gdy komputer ma kartę Wi-Fi (także nieobsługiwaną). Na prawo od zakładek „Wi-Fi”
  (`tekst2`, 13 px) i przełącznik (36 × 20; nieaktywny, gdy radio wyłączone przełącznikiem w obudowie). Lista w karcie
  (`pole`, zaokrąglenie 10, włoskowaty obrys): wiersz 40 px, wachlarz 18 px (`tekst2`, tło 60/255), nazwa Plex Sans 15,
  kłódka 14 px (`tekst3`) przy sieci z hasłem, po prawej uwaga `tekst3` 13 px („niezabezpieczona”, „logowanie loginem”,
  „stare zabezpieczenie”); kreska między wierszami od 42 px; najwyżej 6 wierszy, reszta „…i jeszcze n”. Bez listy: dwie
  linijki (Plex Sans 15 i 13): „Szukam sieci…”, „Nie znaleziono sieci w pobliżu.”, „Wi-Fi wyłączone.”, „Wi-Fi wyłączone
  przełącznikiem w komputerze.”, „Nie znaleziono karty Wi-Fi.”, „<karta> — K1 nie potrafi jej jeszcze uruchomić.”
  W kroku 2 kliknięcie sieci rozwija jej wiersz (tło `zaznaczenie`): „Hasło sieci”, pole 230 px, „Połącz” (przycisk
  główny), pod spodem „Hasło wpisujesz raz. K1 je zapamięta.” albo „Złe hasło. Spróbuj jeszcze raz.” (`akcent_tekst`);
  połączona sieć — plakietka „Połączono” z kropką w szałwii.
  **Wykonanie w kroku 2 (0.9.1):** rozwinięty wiersz 96 px na tle `zaznaczenie`; pole hasła 230 × 36 (tło `tlo`, obrys
  akcentem, gdy aktywne albo po złym haśle), kropki zamiast znaków; „Połącz” (`k1_przycisk`, główny, 104 px; nieaktywny,
  dopóki hasło ma < 8 znaków) na prawo od pola; pod nimi jedna linijka 13 px: „Hasło wpisujesz raz — K1 je zapamięta.”,
  „Złe hasło. Spróbuj jeszcze raz.” (pogrubiona, `akcent_tekst`), „Nie udało się połączyć. Spróbuj jeszcze raz.”. Sieć
  otwarta: sam „Połącz” i „Sieć niezabezpieczona — inni w pobliżu mogą widzieć, co przesyłasz.”; zapamiętana: „Połącz”
  i „Zapomnij tę sieć” (zwykły, 160 px); połączona: „Zapomnij tę sieć”. Przy wierszu po prawej: „Połączono” (pogrubiony
  `tekst2` z kropką w szałwii `#8A9A7B`), „Łączę…” albo „zapamiętana” (`tekst3`). Z otwartym wierszem widać 4 wiersze
  listy. Ikona: łączę — przygaszony wachlarz z kropką w terakocie (jak „łączę” wtyczki); połączono — wypełnienie 70/255 tła
  i kreski krem.
- Zakładka „Karty sieciowe”: kafelki z 0.9.0 bez zmian; kafelek karty Wi-Fi, której K1 używa: „Wi-Fi · radio włączone”,
  plakietka „Włączone” (kropka w akcencie) albo „Wyłączone”.

## Aktualizacje (0.10.5 krok 1, decyzje właściciela D1, D2, D3 z 2026-11-17)
Szkice narysowane częściami K1 na hoście (2026-11-17), wszystkie widoki naraz — także kroku 2 (sprawdzanie, pobieranie,
gotowa wersja z opisem D13, brak sieci, pendrive przy nowszej wersji w sieci): układ sekcji A „Kafel wersji”, B „Wiersze”,
C „Dwa miejsca”; ikona A (dwie strzałki), B (strzałka w górę w kółku), C (dwa miejsca dysku); znak czekającej wersji A
(kropka przy Start), B (ikona na górnym pasku), C (bez znaku). Właściciel wybrał **wszystkie rekomendacje: układ A, ikona B,
znak A**, okno 720 × 520 (D2) i Instalator tylko do instalacji (D3).
- **Ustawienia → Aktualizacje:** w bocznej liście przed „Informacje”, ikona strzałki w kółku (`K1_IK_AKTUALIZACJE`) — **w dół** (uwaga właściciela 2026-11-17: w górę „dziwnie wygląda”; tak samo w kafelku, okienkach i przy wersjach pobieranych w kroku 2).
  Pod tytułem kafel wersji: `pole`, zaokrąglenie 10, obrys `tekst` 24, wysokość 72; po lewej kwadrat 40 × 40 w kolorze akcentu
  z ikoną (`na_akcencie`); obok **sam numer wersji** („0.10.5”, Plex Sans Medium 15 — uwaga właściciela 2026-11-17: bez
  „Kons K1”), pod nim mała linia „Zweryfikowano · działa z miejsca A na dysku” (uwaga właściciela 2026-11-17: „Zweryfikowano”
  zamiast „podpisana kluczem K1”; bez podpisu „wersja deweloperska”). Pod kafelkiem jedno zdanie (Medium 15; nieudana próba —
  `akcent_tekst`), jedna–dwie małe linie (`tekst2`) i najwyżej jeden przycisk: główny (terakota) dla „Zaktualizuj K1 na
  dysku do X” i „Uruchom ponownie”, zwykły dla „Rozumiem” i „Sprawdź jeszcze raz”. Postęp: pasek 8 px jak w Instalatorze.
  Krok 2 doda pod spodem „Sprawdzaj sam” z przełącznikiem.
- **Okno Ustawień 720 × 520** (D2; wcześniej 680 × 480): dwanaście sekcji w bocznej liście (od y = 52 co 36 px).
- **Okienko „Nowa wersja K1 nie uruchomiła się”** (pulpit, S5): jak okienko „Jest nowa wersja” ze szkicu (krok 2) —
  440 × 196 na panelu menu kontekstowego, ikona aktualizacji w `akcent_tekst`, tytuł Medium 15, zdanie, dwie małe linie,
  kreska, „Pokaż w Ustawieniach” (cichy) i „Rozumiem” (główny).
- **Instalator:** przy dysku z K1 karta `pole` 92 px — ikona aktualizacji, „Aktualizacja jest teraz w Ustawieniach.”, mała
  linia i link „Otwórz Ustawienia → Aktualizacje” (Medium 13, `akcent_tekst`); „Zainstaluj od nowa” zwykłym przyciskiem.
- Na krok 2 (już wybrane): okienko „Jest nowa wersja K1” na środku ekranu (opis wersji: D13), „Później” (cichy) i
  „Zainstaluj” (główny); kropka w kolorze akcentu przy przycisku Start, dopóki wersja czeka, i wiersz w menu Start.

## Okienko pobranego pliku (0.11.0 krok 2, decyzje właściciela D2 „A”, D3 „A” z 2026-11-21)
- Na środku ekranu, w stylu okienka „Jest nowa wersja K1” (panel 440 × 212, cień, obrys): kropka w kolorze sesji i
  „Przeglądarka · Ogólna pobrała plik” (pogrubione), nazwa pliku, rozmiar (mała, drugi kolor tekstu), „Zapisać go
  w Plikach → Pobrane?”, linia, przyciski „Zostaw w przeglądarce” (cichy) i „Zapisz w Pobranych” (główny, terakota).
  Plik za duży (> 4 GB): „Jest za duży dla Plików (najwyżej 4 GB).” / „Plik zostaje w przeglądarce.”, tylko „OK”.
  Nie zapisano: „… · nie zapisano pliku”, powód („Za mało miejsca na dysku.”, „Zapis się nie udał.”), „OK”.
- D3 „A”: w Plikach pochodzenie tylko w szczegółach pliku („z przeglądarki · Ogólna”), bez znaku na ikonie.
- Krok 3 (D4 „A”): to samo okienko dla zmienionej wersji — „<sesja> zmieniła plik”, nazwa oryginału, „Zapisać zmiany
  w oryginale?”, „„Zapisz”: poprzednia wersja trafi do Kosza.”, trzy przyciski: „Zapisz” (główny), „Zapisz jako nowy plik”,
  „Nie” (ciche). Menu pliku: „Wyślij do” ze strzałką podmenu (sesje „Przeglądarka · Ogólna”…), w „Otwórz za pomocą” sesje
  pod kreską po aplikacjach K1.

## Ustawienia → Ekran (M4.1; 0.10.7 krok 2)
Rozdzielczości jako przyciski 152 × 36 **w rzędach po 3, od największej** (odstęp 12 i 8): wybrana — wypełnienie
akcentem, pozostałe — `pole` z obrysem; zalecana (preferowana przez monitor/kartę) ma w przycisku mały dopisek
„zalecana” (Plex Sans 13, `tekst2`). Na dole „Wybór obowiązuje do wyłączenia komputera.”. Przy jednym trybie (obraz
z firmware) — wyjaśnienie i wskazówka dla VirtualBoxa.

## Ustawienia: sekcje w środku (0.10.7 krok 2, decyzja właściciela D3 z 2026-11-18)
- **Lista sekcji:** bez zmian (Z1), ale komputer bez baterii nie ma „Baterii” (prośba właściciela 2026-11-18: wiersz
  znika, gdy nadzorca powie „brak baterii”, wraca z baterią). Ikona Sieci: łuki Wi-Fi z kropką (`K1_IK_WIFI`; wtyczka
  RJ45 `K1_IK_SIEC` zostaje tam, gdzie chodzi o kabel: górny pasek, kafelek karty przewodowej).
- **Aplikacje:** wiersz 44 px na typ pliku, pod nim włoskowata linia (`tekst` 20/255); z lewej typ (Plex Sans 15),
  z prawej aplikacja (`tekst2`) albo „brak aplikacji” (`tekst3`). Typ z wyborem: nazwa, pod nią segment (wiersz 92 px).
- **Informacje — wizytówka:** znak „Warstwy” 72 px, pod nim „Kons K1” (Plex Serif 24) i „Wersja …” (`tekst2`); niżej
  Ekran, Jądro, Dane: etykiety w kolumnie 96 px (`tekst2`), wartości za nią (`tekst`). Jądro z `jadro/wersja.env`.
- **Hasło:** przełącznik i to, co znaczy, na jednej karcie (`pole`, obrys `tekst` 24, zaokrąglenie 10); wyjaśnienie
  8 px pod przełącznikiem. Z otwartym formularzem karta ma sam przełącznik, pola pod nią.
- **Aktualizacje:** ikona w kafelku bez terakotowego kwadratu — `tekst` na polu `zaznaczenie` 40 × 40 (zaokrąglenie 10;
  plan mówił `najechanie`, ale w ciemnym motywie nie odróżnia się od `pole`).
- **Przeglądarka:** „Zmień nazwę”, „Wyczyść dane…” — odmiana przycisku `K1_PRZ_NA_KARCIE` (przycisk na karcie:
  lekkie tło `tekst` 12/255, pod wskaźnikiem 26/255, bez obrysu).

## Motywy okien

| Rola | Jasny (domyślny) | Ciemny |
|---|---|---|
| tło okna | `#FAF9F6` | `#262523` |
| pole / karta | `#FFFFFF` | `#302F2C` |
| linia (włoskowata) | tekst 9–10% | tekst 9–10% |
| najechanie / zaznaczenie | `#F0EEE9` / `#EBE8E1` | `#32312E` / `#3A3834` |
| tekst: główny / drugorzędny / wyciszony | `#1F1E1C` / `#5E5C57` / `#8D8A84` | `#EEEBE5` / `#ABA79F` / `#7D7A73` |
| akcent (wypełnienie) / tekst na nim | `#BC5A37` / biały (kontrast 4,5:1) | `#D97757` / `#1F1E1C` |

## Typografia
IBM Plex Sans (13, 13 Medium, 15, 15 Medium) i IBM Plex Serif (24 — nagłówki; Light 64 — cyfry
zegara). Hierarchia rozmiarem i kolorem, nie grubością. ADR 0009.

## Ikony
Liniowe, jedna grubość (1,6 przy 24 px), kolor roli tekstu, rysowane w kodzie (`k1_ikona`):
zegar, notatki, ustawienia (suwaki), głośnik, zasilanie, restart, zamknij, minimalizuj, kłódka,
informacja, mysz, wygląd (obrazek), zasoby (ramka z pulsem), ekran (monitor).

## Ekran „Wyłączanie” (0.10.7 krok 6, decyzja właściciela D7: szkic B „Kropki”, 2026-11-19)
Po „Wyłącz” albo „Uruchom ponownie” od razu, do końca: tło okna motywu (papier / noc) na cały ekran, na środku trzy kropki
(promień 5 px, co 20 px, 36 px nad środkiem) w kolorze akcentu, rozjaśniane po kolei (od 25 % do 100 % akcentu, obieg
1,5 s), pod nimi „Wyłączanie” albo „Ponowne uruchamianie” (Plex Serif 24, kolor tekstu, linia bazowa 40 px pod środkiem).
Bez kursora, bez innych zdań (Z2), bez paska postępu (czasu i tak nie znamy). Wylogowanie go nie pokazuje. Szkice A
„Znak” i C „Przygaszenie” odrzucone. Rysuje ekran (`k1_wylaczanie`), wzorce `wylaczanie-jasny`, `wylaczanie-ciemny`.

## Kursor
Klasyczna strzałka: kremowe wypełnienie, ciemny obrys, lekki cień; przy wciśniętym przycisku —
wypełnienie w kolorze akcentu. Od 0.10.5 krok 4 nad chwytami ramki okna strefy — podwójna strzałka tego samego stylu
(↔, ↕, ↖↘, ↗↙).
Od 0.10.11 krok 2 (ADR 0051, decyzja D4 właściciela: A): gdy okno strefy jest na warstwie karty, kursor rysuje płaszczyzna
kursora karty — ten sam obraz kursora (strzałka, wciśnięta, podwójne strzałki), w tym samym miejscu; nie ma być widać
różnicy poza płynnością. Okno częściowo zasłonięte — K1 sam wraca do składania obrazu.

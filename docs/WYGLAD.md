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
ściankę i miękki cień na arkuszu niżej; powierzchnie odbijają „niebo” z delikatnymi chmurami.
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
  w kolorze akcentu, nad nim „Hasło”, pod nim „Enter — odblokuj · zapomniane hasło = nowe konto”;
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
  nazwa — Plex Sans Medium 13, kremowa z ciemnym cieniem 1 px (leży na tapecie w obu motywach).
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
- **Przeglądarka bez własnych przycisków okna** (S28): Firefox w strefie nie rysuje minimalizuj / powiększ / zamknij
  (ustawienie GTK w obrazie strefy) — robi to tylko pasek K1.
- **Start (S1):** okno w ramce od razu; w środku tło motywu, ikona strefy 40 px (`tekst3`), „Uruchamiam: <tytuł>…”
  (Plex Sans 15) i pasek 160 × 4 z odcinkiem w kolorze strefy — do pierwszego obrazu gościa.
- **Kolory:** strefa próbna — piasek `#A8834A`; sesje przeglądarki (krok 2, ze szkicu): Ogólna — szałwia `#5F8A6A`, Bank —
  łupek `#4A6FA5`, Prywatna — wrzos `#8B6AA8`.
- **„Nie może ruszyć”** (brak wirtualizacji, pamięci, obrazu): okienko odmowy pulpitu (to samo co przy przeciąganiu
  plików, nie osobne okno ze szkicu): pogrubione „<tytuł> nie może ruszyć”, wyjaśnienie, kreska, „Rozumiem”.
- **„Zamknęła się”** (krok 3, S26): to samo okienko, gdy strefa skończyła się bez zamknięcia przez użytkownika —
  „<tytuł> zamknęła się” i powód (brak pamięci w strefie, brak pamięci w komputerze, awaria programu, błąd strefy).
- **Dolny pasek:** okno strefy wśród działających (po kresce), ikona strefy; strefy nie przypina się.

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
- **Ustawienia → Sieć:** pod tytułem „Sieć” (zamiast opisu) zakładki „Wi-Fi | Karty sieciowe” (`k1_segment`, 260 × 32);
  zakładka Wi-Fi jest pierwsza, gdy komputer ma kartę Wi-Fi (także nieobsługiwaną). Na prawo od zakładek „Wi-Fi”
  (`tekst2`, 13 px) i przełącznik (36 × 20; nieaktywny, gdy radio wyłączone przełącznikiem w obudowie). Lista w karcie
  (`pole`, zaokrąglenie 10, włoskowaty obrys): wiersz 40 px, wachlarz 18 px (`tekst2`, tło 60/255), nazwa Plex Sans 15,
  kłódka 14 px (`tekst3`) przy sieci z hasłem, po prawej uwaga `tekst3` 13 px („niezabezpieczona”, „logowanie loginem”,
  „stare zabezpieczenie”); kreska między wierszami od 42 px; najwyżej 6 wierszy, reszta „…i jeszcze n”. Bez listy: dwie
  linijki (Plex Sans 15 i 13): „Szukam sieci…”, „Nie znaleziono sieci w pobliżu.”, „Wi-Fi wyłączone.”, „Wi-Fi wyłączone
  przełącznikiem w komputerze.”, „Nie znaleziono karty Wi-Fi.”, „<karta> — K1 nie potrafi jej jeszcze uruchomić.”
  W kroku 2 kliknięcie sieci rozwija jej wiersz (tło `zaznaczenie`): „Hasło sieci”, pole 230 px, „Połącz” (przycisk
  główny), pod spodem „Hasło wpisujesz raz — K1 je zapamięta.” albo „Złe hasło — spróbuj jeszcze raz.” (`akcent_tekst`);
  połączona sieć — plakietka „Połączono” z kropką w szałwii.
  **Wykonanie w kroku 2 (0.9.1):** rozwinięty wiersz 96 px na tle `zaznaczenie`; pole hasła 230 × 36 (tło `tlo`, obrys
  akcentem, gdy aktywne albo po złym haśle), kropki zamiast znaków; „Połącz” (`k1_przycisk`, główny, 104 px; nieaktywny,
  dopóki hasło ma < 8 znaków) na prawo od pola; pod nimi jedna linijka 13 px: „Hasło wpisujesz raz — K1 je zapamięta.”,
  „Złe hasło — spróbuj jeszcze raz.” (pogrubiona, `akcent_tekst`), „Nie udało się połączyć — spróbuj jeszcze raz.”. Sieć
  otwarta: sam „Połącz” i „Sieć niezabezpieczona — inni w pobliżu mogą widzieć, co przesyłasz.”; zapamiętana: „Połącz”
  i „Zapomnij tę sieć” (zwykły, 160 px); połączona: „Zapomnij tę sieć”. Przy wierszu po prawej: „Połączono” (pogrubiony
  `tekst2` z kropką w szałwii `#8A9A7B`), „Łączę…” albo „zapamiętana” (`tekst3`). Z otwartym wierszem widać 4 wiersze
  listy. Ikona: łączę — przygaszony wachlarz z kropką w terakocie (jak „łączę” wtyczki); połączono — wypełnienie 70/255 tła
  i kreski krem.
- Zakładka „Karty sieciowe”: kafelki z 0.9.0 bez zmian; kafelek karty Wi-Fi, której K1 używa: „Wi-Fi · radio włączone”,
  plakietka „Włączone” (kropka w akcencie) albo „Wyłączone”.

## Ustawienia → Ekran (M4.1)
Rozdzielczości jako przyciski w trzech kolumnach (136×32): wybrana — wypełnienie akcentem, pozostałe
— `pole` z obrysem; pod listą „Zalecana: …” (tryb preferowany przez monitor/kartę). Przy jednym trybie (obraz z firmware) — wyjaśnienie
i wskazówka dla VirtualBoxa.

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

## Kursor
Klasyczna strzałka: kremowe wypełnienie, ciemny obrys, lekki cień; przy wciśniętym przycisku —
wypełnienie w kolorze akcentu.

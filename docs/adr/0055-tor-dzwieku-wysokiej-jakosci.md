# 0055. Tor dźwięku wysokiej jakości: liczby zmiennoprzecinkowe w usłudze, ogranicznik, dither, 24 bity do karty, formaty strefy

- Status: Przyjęty, krok 1 wdrożony (plan 0.12.3), krok 2 (korektor, pkt 9–13) wdrożony, krok 3 (Kons Spatial Audio —
  silnik i gotowe sceny, pkt 14–19) wdrożony; krok 4 (scena do przeciągania) dopisze swoje
- Data: 2026-11-26
- Dotyczy: ADR 0054 pkt 3 (jeden format i głośność w usłudze — **zastąpiony** punktami 1–5 niżej), pkt 13–16 (dźwięk
  stref — formaty gościa, punkt 6); plan `docs/PLAN-0.12.3.txt` (krok 1, T1–T5, T8); opisy: `docs/komponenty/dzwiek.md`,
  `docs/komponenty/strefa.md`

## Kontekst
Po 0.12.0 tor dźwięku miał 16 bitów od początku do końca: klienci oddawali liczby całkowite, usługa sumowała je w 32
bitach, a głośność K1 i głośność sesji liczyła mnożeniem i dzieleniem całkowitym, które ucina w stronę zera. Pomiar
„przed” (krok 1a planu; ton 1 kHz o połowie pełnej skali, jeden strumień, karta 16-bitowa):

| głośność | błąd (kroki 16 bitów) | harmoniczne pod tonem |
|---|---|---|
| 100 % | 0 | — (bez zmian) |
| 50 % | 0,46 | 79 dB |
| 10 % | 0,53 | 35 dB |
| 5 % | 0,58 | 14 dB |

Błąd ucinania nie jest szumem, tylko zniekształceniem związanym z dźwiękiem (harmoniczne): przy 10 % ledwo 35 dB pod
tonem, przy 5 % — 14 dB, czyli wyraźne „ziarno” cichej muzyki. Ton o amplitudzie 3 kroków przy 50 % znikał całkiem
(wyjście: same zera). Głośne nakładające się strumienie były twardo przycinane (trzaski). Przeglądarka w strefie
przeliczała swój dźwięk (Firefox liczy w liczbach zmiennoprzecinkowych) do 16 bitów już w strefie, w alsa-lib — bez
dithera.

## Decyzja
1. **Liczby zmiennoprzecinkowe w środku (T2).** Usługa dźwięku liczy próbkę jako `float` (32 bity; ±1,0 = pełna skala):
   suma strumieni, głośność sesji, głośność K1, ogranicznik. `float` ma 24 bity dokładności przy każdej głośności, więc
   cichy dźwięk nie traci bitów aż do karty, a suma może wyjść ponad pełną skalę bez przepełnienia. Liczby całkowite
   16 bitów przechodzą do `float` dokładnie (dzielenie przez 2¹⁵), suma do 12 klientów też jest dokładna. Bez libm
   i bez `-ffast-math` — te same wyniki na hoście, w fuzzerze i w K1. Czyste funkcje: `src/wspolne/k1_dzw_tor.c`.
   Krzywa głośności bez zmian (sześcian; 50 % to dokładnie 0,125).
2. **Ogranicznik zamiast przycinania.** Gdzie ramka wyszłaby ponad pełną skalę, wzmocnienie obu kanałów schodzi do
   potrzebnego — łagodnie, zaczynając do 1 ms (48 ramek) przed głośną ramką w tym samym okresie (usługa ma cały okres,
   zanim zapisze kartę, więc to nie dodaje opóźnienia) — i wraca do 1,0 powoli (6 dB na 50 ms). Poniżej pełnej skali
   nic nie zmienia. W logu raz na minutę (gdy coś było): w ilu okresach działał.
3. **Dither TPDF przy zamianie na format karty (T3).** Próbka jest zaokrąglana do kroku karty z szumem o rozkładzie
   trójkątnym (±1 krok; dwa losowania z generatora xorshift32 — deterministyczny, te same liczby przy każdym
   uruchomieniu). Błąd staje się białym szumem bez związku z dźwiękiem, o stałej mocy ¼ kroku² (−96 dBFS przy 16
   bitach, −144 dBFS przy 24) — niezależnie od głośności. **Bez dithera w okresie, w którym każda próbka już jest
   pełnym krokiem karty:** jeden strumień 16 bitów przy 100 % idzie do karty 16-bitowej próbka w próbkę
   („przezroczystość”), a cisza zostaje cyfrową ciszą (także wyciszenie). Kształtowania szumu (noise shaping) nie ma
   — „Później”, jeśli odsłuch pokaże potrzebę.
4. **Format karty: najlepszy, jaki przyjmie (T1).** Usługa próbuje po kolei (tabela w `src/dzwiek/karta.c`):
   S32_LE z podformatem MSBITS_24 (HD Audio z przetwornikiem 24-bitowym), S32_LE z MSBITS_20, S32_LE bez podformatu
   (inny sterownik: jego `msbits`, najwyżej 24), S24_3LE, na końcu S16_LE jak dotąd. HD Audio podaje 20 i 24 bity
   tylko jako S32_LE z podformatem (`sound/hda/core/device.c`, `snd_pcm_hw_rule_subformats` w `sound/core/pcm_native.c`),
   więc pytanie o podformat mówi, ile bitów karta naprawdę gra — i do tylu bitów usługa zaokrągla z ditherem. Odmowę
   HW_PARAMS daje sam ioctl — bez nowego numeru w klatce. Log: „karta gra 24 bity (4 bajty na próbkę)”. Karta QEMU
   (`hda-output`) ogłasza tylko 16 bitów: QEMU testuje ścieżkę 16-bitową, a 24 bity — test na hoście i sprzęt
   właściciela (NIEPEWNE do pierwszego logu z X230 i IdeaPada: czy ich kodeki Realteka dają 24 bity).
5. **Głośność dalej cała w usłudze (T8).** Wzmacniacze kodeka zostają na 0 dB (ADR 0054 pkt 3). Pomiar „po”: z ditherem
   błąd przy 5 % to wciąż −96 dBFS szumu (16 bitów; −144 dBFS przy 24), harmoniczne 45 dB pod tonem przy 16 bitach
   i 94 dB przy 24 (było 14 dB), a ton 3 kroków przy 50 % wychodzi z wzmocnieniem 0,124 (idealnie 0,125; było 0).
   Przesuwanie części ściszenia do kodeka zmniejszyłoby też szum analogowy wzmacniacza, ale kroki wzmacniaczy różnią się
   między kodekami (0,75–1,5 dB), a zmiana w trakcie grania daje słyszalne stuknięcia — bez potrzeby, którą pokazałby
   odsłuch, to ryzyko się nie opłaca. Jeśli właściciel usłyszy szum przy małej głośności na X230 — „Później”.
6. **Formaty klientów i strefy (T4).** Pierścień klienta niesie jeden format z tabeli `K1_DZW_FORMATY` (`k1_dzwiek.h`):
   **S16** (gui, Ustawienia, program testowy — jak w 0.12.0) albo **F32** (VMM strefy; 8 bajtów ramka, pierścień 128 KiB);
   format jest w `DZWIEK_OTWORZ` a, rozmiar pierścienia musi się zgadzać. Próbki F32 to liczby od wrogiego gościa:
   usługa sprawdza każdą — liczba nieskończona albo NaN staje się zerem, ponad ±1,0 — ±1,0 — i liczy je (log raz na
   minutę). Strumień przy tym gra dalej: zepsuta liczba to najwyżej cisza tej próbki, a zamknięcie strumienia
   wyciszyłoby całą przeglądarkę za jedną złą stronę. Karta virtio-snd strefy ogłasza **S16, S32 i FLOAT** (tabela
   `DZW_FORMATY`, `src/strefa/dzwiek.h`); VMM przelicza je do pierścienia F32 bez straty: S16 dokładnie, S32 do 24 bitów
   `float` (tyle gra najlepsza karta), FLOAT kopiuje (sprawdza usługa). Jeden format pierścienia na VMM — strumień nie
   musi się zamykać i otwierać od nowa, gdy gość zmieni format. Log VMM: „gość gra w formacie S32” (raz na format).
   Firefox gra przez alsa-lib z jej konfiguracją domyślną; który format wybierze (`plug` przepuści FLOAT, `dmix` miesza
   w S32, gdy karta go ma) — NIEPEWNE, pokaże log na stanowisku i na sprzęcie. W obu wypadkach nie przelicza już do 16
   bitów.
7. **Częstotliwość: 48 kHz w całym torze (T5).** 44,1 kHz przelicza Firefox (jego jakość — NIEPEWNE, bez preferencji
   do zmiany w `k1.js`); 96/192 kHz — bez korzyści słyszalnej, większy koszt na X230 → „Później”.
8. **Opóźnienie (krok 1h).** Transfer wraca do gościa, gdy usługa zabrała jego ramki do okresu (ADR 0054 pkt 15), więc
   zegar przeglądarki nie widzi zapasu VMM-a (do 8192 ramek, ok. 170 ms) — ten zapas nie przesuwa obrazu względem
   dźwięku i zostaje bez zmian. Przesuwa go bufor karty: od zabrania ramek do głośnika mijają 2–4 okresy (ok. 43–85 ms),
   o tyle dźwięk jest później niż obraz — w granicach, których ucho zwykle nie zauważa przy filmie (ok. 100 ms), ale
   blisko zaleceń telewizji. Liczenie opóźnienia karty w `DZWIEK_ODTWORZONO` (np. `SNDRV_PCM_IOCTL_DELAY` — nowy numer
   w klatce) → „Później”, jeśli właściciel zobaczy rozjazd ust i głosu.

9. **Korektor systemowy (krok 2; decyzje właściciela D1: A, D2: A z 2026-11-26).** Dziesięć pasm co oktawę (31,25 Hz ·
   2ⁿ: 31 … 16 kHz; tabela `K1_KOR_PASMA`), każde −12…+12 dB co 0,5 dB — liczba całkowita półdecybeli (`int8_t` ±24).
   Pasmo = filtr szczytowy (biquad z „Audio EQ Cookbook” R. Bristowa-Johnsona, Q = √2 — oktawa między punktami 3 dB);
   w środku pasma dokładnie jego wzmocnienie (pomiar: 0,0000 dB od celu), trzy oktawy dalej najwyżej 0,13 dB. Osobne
   ustawienie dla głośników i słuchawek; gra to wyjścia, które gra. Gotowe ustawienia — tabela `K1_KOR_GOTOWE` (Płaski,
   Więcej basu, Wyraźny głos, Laptop, Słuchawki, Muzyka, Film); inne położenie — „Własne” (rozpoznawane po pasmach, bez
   osobnego numeru w komunikacie). Kod: `src/wspolne/k1_korektor.c` (czysty), `src/dzwiek/korektor.c` (usługa).
10. **Miejsce w torze i poziom.** Po sumie strumieni, przed głośnością K1 (i przyszłą sceną z kroku 3). Przed filtrami
   poziom schodzi o najgłośniejszy punkt całej krzywej (siatka 241 punktów 20 Hz–20 kHz i środki pasm): korektor **nigdy
   nie gra głośniej niż 100 %** — pomiar na gęstej siatce: najwyżej +0,028 dB dla 300 losowych ustawień, +0,007 dB dla
   gotowych (ogranicznik z pkt 2 bierze resztę). Cięcia nie podnoszą poziomu (bez „wyrównania głośności”). Płaski
   korektor nie liczy nic: przezroczystość z pkt 3 zostaje.
11. **Liczby (T6, T2).** Filtry w `double` (31 Hz przy 48 kHz w `float` dodawałby szum), postać bezpośrednia I, pamięć
   filtrów zerowana poniżej 10⁻³⁰ (bez liczb zdenormalizowanych po ciszy). Współczynniki raz po zmianie, nie na próbkę —
   własnymi szeregami: sinus i cosinus (Taylor, |x| ≤ 4), eˣ (szereg x/16 i cztery kwadraty), pierwiastek (Newton); błąd
   wobec libm < 10⁻¹² — bez libm, te same liczby na hoście, w fuzzerze i w K1. Koszt: 10 filtrów × 2 kanały × 48 000/s;
   usługa loguje raz na minutę średni czas okresu i część rdzenia (cel: < 5 % rdzenia X230 — NIEPEWNE do logu właściciela).
12. **Zmiana bez trzasku.** Nowe pasma (od klienta albo inne wyjście) przechodzą przez jeden okres (21 ms): stare i nowe
   filtry liczą ten sam okres, wynik przechodzi liniowo z jednego w drugi. Nowe filtry zaczynają z pamięcią starych
   (przeliczoną na nowy poziom) albo — po płaskim korektorze — z dwóch ostatnich ramek sumy jako wejścia i wyjścia każdego
   pasma. Pomiar: ton 100 Hz przez Płaski → Więcej basu → Laptop → Płaski — największy krok między próbkami równy krokowi
   samego tonu; po powrocie do płaskiego wyjście = wejście po jednym okresie.
13. **Komunikaty i pamięć (T4).** Komunikaty 32 B wystarczają: `DZWIEK_KOREKTOR` (klient sterujący → usługa) i
   `DZWIEK_KOREKTOR_STAN` (usługa → klienci sterujący): a = wyjście, b, c, d = dziesięć pasm po bajcie (cztery na pole,
   nieużyte bajty 0) — jedna zmiana, jeden komunikat; memfd niepotrzebny. Klient niesterujący (strefa), który je wyśle,
   traci kanał. Pamięć: gui w skarbcu, rekord „K1G2” (28 B: „K1G1” i po nim pasma obu wyjść; „K1G1” czytany dalej).
   Okienko „Dźwięk” (D1: A) pokazuje nazwę ustawienia i otwiera Ustawienia na korektorze: `POKAZ_USTAWIENIA` (gui →
   nadzorca) i `USTAWIENIA_MIEJSCE` (nadzorca → Ustawienia) z tabelą miejsc `K1_UST_MIEJSCA` (krok 4 dopisze scenę).

14. **Kons Spatial Audio (krok 3; decyzje właściciela D3: A, D5: cała lista, D7: A z 2026-11-26).** Nazwa własna funkcji
   „Kons Spatial Audio”, pod nią po polsku „dźwięk przestrzenny”. Dźwięk dzielą na **cztery pasma** zwrotnice
   Linkwitza-Rileya 4. rzędu (dwie sekcje Butterwortha) przy 120, 500 i 4000 Hz: Bas, Niski środek, Środek, Góra (tabela
   `K1_PRZ_PASMA`). Gałąź niska dostaje filtr wszechprzepustowy zwrotnicy 4 kHz, wysoka — 120 Hz, więc suma pasm to sygnał
   z obróconą tylko fazą: pomiar — najwyżej 0,0001 dB od wejścia od 20 Hz do 20 kHz. W każdym paśmie lewy i prawy kanał
   to dwa wirtualne źródła; **scena** daje pasmu **kierunek** (−180…180°, 0 — przód, + w prawo), **szerokość** (0…180°:
   kąt między jego dwoma źródłami) i **odległość** (0…100 %); kroki 2° (bajt w komunikacie). Gotowe sceny — tabela
   `K1_PRZ_SCENY` (D5): Naturalny (wszystkie pasma z przodu, 60°, 20 %), Kino (głos — Środek 50°, 15 %, Góra 90° z przodu,
   Niski środek 150° za słuchaczem — źródła po bokach i z tyłu, 40 %), Koncert (90–100°, 70 %), Pokój (40°, 5 %), Głos (Środek
   30°, 0 %, reszta 70°, 50 %); **Wyłączony** to scena wyłączona (jej miejsca zostają: po włączeniu gra jak przedtem), inne
   miejsca — „Własna”. Bas (poniżej 120 Hz) zostaje tam, gdzie jest: ucho go nie umiejscawia, a rozsunięty brzmi słabo.
   Kod: `src/wspolne/k1_przestrzen.c` (tabele, scena w liczbach, poziom), `k1_prz_licz.c` (zwrotnice, drogi, przejście),
   `src/dzwiek/przestrzen.c` (usługa).
15. **Słuchawki — model głowy (T7).** Kulista głowa (C. P. Brown, R. O. Duda 1998; promień 8,75 cm, dźwięk 343 m/s): każde
   źródło dochodzi do każdego ucha z opóźnieniem Woodwortha (z boku 656 µs między uszami — pomiar na impulsie: 667 µs,
   z własnym opóźnieniem filtra cienia) i przez **cień głowy** — filtr pierwszego rzędu, którego góra ma wzmocnienie α:
   2 (+6 dB) od strony źródła, 0,1 (−20 dB) 150° od osi ucha, zakręt przy 2c/a. Źródło z tyłu ma łagodniejszą górę (do −6 dB
   powyżej 4 kHz prosto z tyłu) — samo opóźnienie nie odróżnia przodu od tyłu. Opóźnienie ułamkowe: filtr Lagrange'a
   3. rzędu. Pomiar: źródło z lewej — 6 kHz w lewym uchu o 15,4 dB głośniej, 250 Hz o 0,7 dB (tam działa opóźnienie).
   **Odległość:** bliżej — głośniej i suszej (0 % = 0 dB, 100 % = −6 dB), dalej — **wczesne odbicia**: sześć (7–27 ms, na
   zmianę lewe i prawe ucho, łagodniejsze powyżej 5 kHz), ich udział do 0,35 przy 100 %; bez długiego pogłosu. Zbiór HRTF
   z pomiarów głowy — „Później” (T7).
16. **Głośniki laptopa (D3: A) — szerszy dźwięk.** Kierunek za plecami odbity do przodu (150° gra jak 30°), para źródeł
   przesunięta tam z równą mocą (1 na środku), boki × w (szerokość / 60° — normalne stereo, najwyżej 1,4), a szerzej niż
   normalnie — łagodne krzyżowe wygaszanie samych boków (różnica kanałów jeszcze raz, do 0,2, 3 ramki później, ze znakiem
   każdego głośnika): to, co jest takie samo w obu kanałach (głos), zostaje na swoim poziomie. Pierwsza wersja odejmowała
   drugi kanał w całości — próba w QEMU pokazała głos o 3–4 dB cichszy (plik WAV testu); poprawione przed scaleniem. Bez
   obietnicy „dźwięku z tyłu”. Pomiar: pasma z lewej — 1 kHz w prawym kanale 101 dB ciszej, w lewym tak głośno jak bez
   sceny; Kino — ton mono 2,5–3,2 dB ciszej (z tego 1,2 dB to odległość sceny 20 %).
17. **Poziom: scena nigdy nie gra głośniej niż 100 %.** Dla każdego ucha i częstotliwości (siatka 481 punktów 20 Hz–20 kHz
   i zwrotnice) usługa liczy granicę wzmocnienia: dźwięku takiego samego w obu wejściach (moduł sumy dróg) i mocy dwóch
   wejść bez związku (pierwiastek sumy kwadratów), plus odbicia z nierówności trójkąta; pasma ważone wagami zwrotnic
   (|X| pasm sumują się do 1). Gdy granica przekracza 1, cała scena ścisza się o nią (`k1_prz_moc`, `wzm`). Ściszenie
   gotowych scen: głośniki — Naturalny, Pokój, Głos 0 dB, Koncert −0,2, Kino −1,7 dB (poszerzenie dodaje mocy bokom);
   słuchawki −3,2…−5,4 dB — tam każde ucho słyszy oba źródła pasma, więc dźwięk mono byłby dwa razy głośniejszy, a
   ściszenie przywraca jego poziom. Pomiar: granica najwyżej +0,017 dB na gęstej siatce (gotowe i 100 losowych scen), ton
   w obu wejściach przez działającą scenę najwyżej −0,085 dB, szum bez związku najwyżej −1,2 dB mocy. Ogranicznik z pkt 2
   zostaje na końcu toru.
18. **Liczby, koszt, przejścia (T6).** Liczenie w `double` (jak korektor), własne sin, cos, eˣ, pierwiastek (pkt 11), stan
   filtrów zerowany poniżej 10⁻³⁰. Scena po korektorze, przed głośnością K1; osobna dla każdego wyjścia, gra ta wyjścia,
   które gra. **Wyłączona nie liczy nic** (przezroczystość z pkt 3), trzyma tylko kopię ostatniego okresu: włączona po ciszy
   zwrotnic przepuszcza najpierw ten okres (bez stuknięcia filtrów 120 Hz). Każda zmiana (inna scena, inne wyjście,
   włączenie, wyłączenie) przechodzi przez jeden okres: stara i nowa scena (albo dźwięk bez sceny) na tym samym okresie,
   płynnie; pomiar: ton 1 kHz przez Wyłączony → Kino → Głos → głośniki → Wyłączony — największy krok między próbkami równy
   krokowi tonu, po wyłączeniu wyjście = wejście po jednym okresie. Koszt na hoście (bez ASan): 179 µs na okres 21 ms
   (0,8 % rdzenia; najcięższe sceny); usługa loguje raz na minutę „scena: średnio N µs na okres” — X230 NIEPEWNE do logu.
19. **Komunikaty i pamięć (T4).** `DZWIEK_SCENA` (klient sterujący → usługa, numer 255) i `DZWIEK_SCENA_STAN` (usługa →
   klienci sterujący, 251): a = wyjście, b = włączona, c = cztery kierunki, d = cztery szerokości (bajty w krokach 2°),
   e = cztery odległości — jedna zmiana, jeden komunikat. Numery zwolnił porządek tego kroku: `BATERIA_POKAZ` (251)
   i `AKT_POKAZ` (185) to dziś wiersze tabeli `K1_UST_MIEJSCA` (Bateria, Aktualizacje) w `USTAWIENIA_MIEJSCE`; 185 czeka
   na krok 4. Pamięć: gui, rekord „K1G3” (54 B: „K1G2” i po nim scena głośników i słuchawek, po 13 B jak w komunikacie;
   „K1G2” i „K1G1” czytane dalej — sceny wyłączone). Ustawienia → Dźwięk → **Kons Spatial Audio** (trzecia zakładka):
   wyjście, przełącznik, lista gotowych scen z opisami; scena do przeciągania i przełącznik w okienku „Dźwięk” — krok 4.
20. **Scena do przeciągania, „Własna”, „Posłuchaj” (krok 4; właściciel: D4 = A — widok z góry, D6 = A — jedna
   „Własna”).** Ustawienia → Dźwięk → Kons Spatial Audio rysuje scenę z góry (`src/aplikacje/ustawienia/scena.c`): głowa
   w środku (nos — przód), trzy pierścienie odległości, każde pasmo z kierunkiem jako łuk swojej szerokości w swoim kolorze
   (kolumna tabeli `K1_PRZ_PASMA`) z punktem w środku (przeciąganie: kierunek i odległość) i uchwytami na końcach
   (szerokość, najmniej 20° przy przeciąganiu); pasma w tym samym miejscu mają osobne tory (12 px dalej każde), więc się nie
   zakrywają. Bas to pierścień wokół głowy — tylko odległość (usługa i tak trzyma go w środku, pkt 15). Na głośnikach tył
   jest przygaszony z napisem „na głośnikach tył gra z przodu” (D3: A). Klawiatura: Tab — następne pasmo, strzałki w lewo /
   w prawo — kierunek o 10°, w górę / w dół — odległość o 10 %, + / − — szerokość o 10°. Każdy ruch idzie od razu do usługi
   (`DZWIEK_SCENA`, scena się włącza); koniec ruchu — jeśli to nie gotowa scena — także jako **„Własna”** wyjścia:
   `DZWIEK_SCENA` z a = wyjście + `K1_DZW_SCENA_WLASNA` (16; b = 1 jest, 0 jeszcze nie ma) — usługa ją trzyma (nie gra),
   mówi klientom sterującym (`DZWIEK_SCENA_STAN` z tym samym a), gui pamięta (rekord „K1G4”, 80 B: „K1G3” i po nim „Własna”
   głośników i słuchawek po 13 B; „K1G3”…„K1G1” czytane dalej — bez „Własnej”). Wybór gotowej sceny „Własnej” nie kasuje,
   kafelek „Własna” ją przywraca; kolejne przesunięcie ją nadpisuje. Bez nowego numeru komunikatu (185 dalej wolny).
   **„Posłuchaj”** to druga próba K1 (D3 planu 0.12.0: jedyne dźwięki K1 to próby): szum różowy (filtr P. Kelleta na szumie
   z xorshift32, stałe ziarno) przez te same zwrotnice co scena, po kolei Bas, Niski środek, Środek, Góra po 0,9 s z 0,1 s
   ciszy (`src/wspolne/k1_prz_proba.c`, czyste); poziom w paśmie −24 dBFS (bas −21), szczyt najwyżej jak próba. Robi go
   klient (przybornik, jak „Odtwórz próbę”), więc usługa kładzie go na scenę jak każdy dźwięk; Ustawienia pokazują pasmo,
   które gra (z ramek zabranych przez usługę). Gra na wyjściu, które gra teraz — dla drugiego przycisk mówi „Podłącz
   słuchawki” / „Odłącz słuchawki”. Pomiar (`test_przestrzen`): pasma −21,5 / −24,1 / −24,0 / −24,0 dBFS, inne pasma
   najwyżej −11 dB (zbocza zwrotnic). Okienko „Dźwięk” na górnym pasku: wiersz pod „Korektor” — przełącznik Kons Spatial
   Audio (scena wyjścia, które gra) i nazwa sceny ze strzałką; reszta wiersza otwiera Ustawienia na scenie (nowy wiersz
   `K1_UST_MIEJSCA`: PRZESTRZEN).

## Skutki
- Cichy dźwięk bez „ziarna”, głośne nakładające się dźwięki bez trzasków; na kartach 24-bitowych szum toru 48 dB niżej.
- Jeden strumień 16 bitów przy 100 % dalej przechodzi bez żadnej zmiany — pilnuje tego `test-dzwiek.sh` (plik WAV od QEMU
  porównany z próbą próbka w próbkę) i `test_dzw_tor`.
- Pierścień strefy jest dwa razy większy (128 KiB); pamięć klatki usługi bez zmian (16 MiB).
- Nowy format od strefy to nowa powierzchnia ataku: liczby zmiennoprzecinkowe od gościa (RYZYKA R77, R108):
  `test_dzw_tor`, `test_virtio_dzwiek`, `fuzz_dzwiek`, `fuzz_virtio_dzwiek`.
- Testy: `test_dzw_tor` (pomiary z tego ADR jako granice), `test_dzwiek` (formaty pierścienia, `DZWIEK_OTWORZ` a),
  `test_virtio_dzwiek` (formaty gościa, próbki do `float`), `t_dzwiek` (pierścień F32: zły rozmiar odrzucony),
  `test-dzwiek.sh` (karta QEMU — 16 bitów, przezroczystość, ton strefy w S32).
- Krok 2: korektor — `test_korektor` (liczby z pkt 9–12 jako granice), `fuzz_korektor`, `test_dzwiek` (komunikaty,
  „K1G2”), `t_dzwiek` (NEGATYWNE: pasmo ponad +12 dB, nieistniejące pasmo), `test-dzwiek.sh` (pasmo 1 kHz +12 dB:
  w pliku WAV nuta 660 Hz −7,0 dB i 880 Hz −1,7 dB wobec próby bez korektora); RYZYKA R109.
- Krok 3: Kons Spatial Audio — `test_przestrzen` (liczby z pkt 14–18 jako granice), `fuzz_przestrzen`, `test_dzwiek`
  (komunikaty sceny, „K1G3”), `t_dzwiek` (scena ustawiona i powiedziana z powrotem; NEGATYWNE: włączona = 2, kierunek 182°,
  odległość 101 %), `test-dzwiek.sh` (Ustawienia → Dźwięk → Kons Spatial Audio po nazwach, „Kino”; pasma z lewej — w pliku
  WAV prawy kanał co najmniej 20 dB ciszej, lewy jak bez sceny ±1 dB); RYZYKA R110. Odsłuch („z tyłu”, „szerzej”) — tylko
  właściciel na sprzęcie.
- Krok 4: scena do przeciągania — `test_przestrzen` („Posłuchaj”: ten sam co raz, każde pasmo na swoim poziomie, inne
  pasma najwyżej −10 dB, cisza między nimi, nic po końcu), `test_dzwiek` (komunikaty „Własnej”, rekord „K1G4”),
  `t_dzwiek` („Własna” zapamiętana i powiedziana z powrotem; NEGATYWNY: „Własna” nieistniejącego wyjścia),
  `test-dzwiek.sh` (punkty pasm po nazwach, przeciągnięcie „Środka”, „Kino” i powrót „Własnej” w to samo miejsce,
  przełącznik i wiersz „Scena” w okienku; „Posłuchaj” w pliku WAV: bas w obu kanałach równo, pozostałe pasma z lewej —
  Niski środek +17 dB (jego zbocze poniżej 120 Hz to bas), Środek i Góra +50 dB, każde pasmo wyżej), wzorce zakładki
  i okienka w obu motywach (przyjęte po „OK” właściciela 2026-11-26).

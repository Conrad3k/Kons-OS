# Komponent: dzwiek (usługa dźwięku — jedyny proces z kartą dźwiękową)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.12.0 krok 1: usługa, kanał klientów, okienko „Dźwięk” w gui); PRZETESTOWANE na hoście (`test_dzwiek`, `fuzz_dzwiek`, `test_uevent`, `test_komunikat`), w systemie w QEMU bez karty (`t_dzwiek`: klatka, „brak karty”, wrogi klient, restart) i w QEMU z kartą Intel HD Audio (`scripts/test-dzwiek.sh`: dźwięk zapisany do pliku WAV). Prawdziwy głośnik X230 i IdeaPada — sprawdza właściciel. Krok 3 (dźwięk stref): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_virtio_dzwiek`, `fuzz_virtio_dzwiek`, `test_dzwiek`) i w `test-dzwiek.sh` (strefa próbna gra ton, głośnik na pasku, wyciszenie sesji, złośliwy gość); Firefox w strefie (ALSA) — stanowisko i właściciel na sprzęcie. Krok 2: słuchawki (gniazdo z kontrolek karty — ZAIMPLEMENTOWANE, test na hoście nazw gniazd; QEMU gniazda nie udaje: sprawdza właściciel), osobna głośność głośników i słuchawek, klient Ustawienia — PRZETESTOWANE w `test-dzwiek.sh` (klawisze, Ustawienia → Dźwięk, pamięć głośności po nowym pulpicie); dźwięk ze stref — krok 3; **0.12.3 krok 1** (ADR 0055, tor wysokiej jakości): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_dzw_tor` — pomiary, ogranicznik, dither, formaty karty; `test_virtio_dzwiek`, `fuzz_dzwiek`, `fuzz_virtio_dzwiek`), w systemie (`t_dzwiek`: pierścień F32) i w `test-dzwiek.sh` (karta QEMU — 16 bitów, przezroczystość próby, ton strefy w S32); 24 bity i odsłuch — właściciel na X230 i IdeaPadzie; **0.12.3 krok 2** (korektor): ZAIMPLEMENTOWANE, PRZETESTOWANE (`test_korektor`, `fuzz_korektor`, `t_dzwiek`, `test-dzwiek.sh`); **krok 3** (Kons Spatial Audio — silnik i gotowe sceny): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_przestrzen` — zwrotnice, model głowy, głośniki, poziom, przejścia; `fuzz_przestrzen`, `test_dzwiek`), w systemie (`t_dzwiek`) i w `test-dzwiek.sh` (głośniki: pasma z lewej w pliku WAV); słuchawki i odsłuch („z tyłu”, „szerzej”) — właściciel na sprzęcie |
| **Pliki** | `src/dzwiek/main.c` (kanały, klienci i ich strumienie, mieszanie, stan), `karta.c` (węzeł odtwarzania: ALSA bez alsa-lib), `kontrolki.c` (wyjścia kodeka na 0 dB, nazwa karty do logu), `dzwiek.h`; `src/wspolne/k1_dzwiek.{h,c}` (formaty pierścienia `K1_DZW_FORMATY`, tabela klientów `K1_DZW_KLIENCI`, liczenie pierścienia, próba), `k1_dzw_tor.{h,c}` (0.12.3: tor dźwięku — mieszanie, głośność, ogranicznik, dither, format karty), `k1_korektor.{h,c}` (krok 2: korektor), `k1_przestrzen.{h,c}` + `k1_prz_licz.c` (krok 3: Kons Spatial Audio), `k1_kom_dzwiek.c` (komunikaty); `src/dzwiek/korektor.c`, `przestrzen.c` (korektor i scena w usłudze); `src/nadzorca/dzwiek.c` (karta z sysfs i zdarzeń jądra, kanały klientów), `profile.c` (`KLATKA_DZWIEK`); `src/gui/dzwiek.c` (ikona na pasku, okienko „Dźwięk”, próba); `jadro/k1.config` (sekcja „0.12.0 Dźwięk”) |
| **Decyzje** | ADR 0054 (plan 0.12.0: T1–T4, T7, T8; właściciel: D1 = A, D2 = B, D3 = A; krok 2: D4 = A, D5 = A; krok 3: D6 = A, T5, T6 = A); ADR 0055 (plan 0.12.3 krok 1: T1–T5, T8 — tor wysokiej jakości; krok 2: D1 = A, D2 = A, T6 — korektor; krok 3: D3 = A, D5 = cała lista, D7 = A, T7 — Kons Spatial Audio) |

## Cel
Laptop gra dźwięk K1 i (od kroku 3) przeglądarki, a kartę dźwiękową — jak obraz — ma tylko jeden mały proces w klatce.
Aplikacje i strefy nigdy nie dotykają karty: dostają kanał do usługi i oddają przez niego próbki do zagrania.

## Granica zaufania
Klienci są wrodzy (od kroku 3 VMM strefy mówi w imieniu gościa). Usługa:
- przyjmuje kartę tylko od nadzorcy (fd 3, nadawca UID 0), klientów — tylko z kanałów od nadzorcy, każdy z UID, który
  nadzorca podał (`DZWIEK_KLIENT`); komunikat od kogoś innego jest odrzucany;
- sprawdza każdy komunikat pole po polu (`k1_kom_dzwiek.c`) i stan klienta: jeden strumień naraz, ramki tylko przy
  otwartym strumieniu i najwyżej tyle, ile mieści pierścień (`k1_dzw_zapisano`); głośność tylko od klienta sterującego;
- pierścień przyjmuje tylko o dokładnym rozmiarze i z pieczęciami (bez zmniejszania i powiększania — zmniejszony
  zakończyłby usługę sygnałem SIGBUS), mapuje go tylko do odczytu i **kopiuje** ramki przed mieszaniem; od klienta nie
  bierze żadnego indeksu ani pozycji — liczy sama;
- klient, który łamie zasady albo wysyła 16 złych komunikatów, traci kanał; zalew czeka na swoją kolej (najwyżej 32
  komunikaty klienta na obieg pętli) — inni klienci i karta są obsługiwani dalej.
Sterownik HD Audio w jądrze jest w zasięgu tylko usługi (RYZYKA R107). W logu tylko liczby i nazwy karty i kodeka (z jądra),
nigdy próbki.

## Uprawnienia
| UID | seccomp (poza bazą) | Landlock | cgroup i limity | deskryptory |
|---|---|---|---|---|
| 111 | `ioctl` tylko 14 numerów ALSA: PCM — INFO, HW_PARAMS, HW_FREE, SW_PARAMS, PREPARE, WRITEI_FRAMES, DROP; kontrolki — CARD_INFO, ELEM_LIST, ELEM_INFO, ELEM_READ, ELEM_WRITE, TLV_READ, SUBSCRIBE_EVENTS (krok 2: zdarzenia gniazda słuchawek) | brak ścieżek | `dzwiek`: 16 MiB, 1 proces, 24 deskryptory, `RLIMIT_FSIZE` 0 | fd 3: kanał od nadzorcy (jednokierunkowy); od nadzorcy: węzeł odtwarzania `pcmC<k>D<u>p` i kontrolki `controlC<k>` karty, końce kanałów klientów |

Węzły `/dev/snd` otwiera tylko nadzorca i daje je tylko tej usłudze (rozszerzenie niezmiennika „jeden właściciel zasobu”,
ADR 0054 pkt 1); swoje kopie zamyka od razu. Bez `open`, `socket`, `memfd_create` (pierścienie robią klienci).

## Przebieg
1. **Start** (przy starcie systemu, ponownie po awarii): usługa czeka na kartę; stan „szukam” (`K1_DZW_KARTA_SZUKA`).
2. **Karta** (nadzorca `dzwiek.c`): pierwsza karta z odtwarzaniem z sysfs `class/sound` (najniższe numery karty
   i urządzenia), szukana co 2 s i od razu po zdarzeniu jądra „sound”. Węzeł odtwarzania (`O_NONBLOCK`) → `DZWIEK_KARTA`
   ODTWARZANIE, kontrolki → `DZWIEK_KARTA` KONTROLKI. Usługa pyta węzeł o nazwę (log: „karta 0, urządzenie 0: odtwarzanie
   „ALC269VC Analog””) i włącza wyjścia kodeka (`kontrolki.c`: przełączniki „… Playback Switch” i głośność „… Playback
   Volume” wyjść o nazwach z parsera HDA — na 0 dB ze skali dB karty, bez niej na największą; nazwy w logu). 5 s po starcie
   bez karty: `DZWIEK_BEZ_KARTY` → stan „brak karty”. Karta znaleziona później dalej trafia do usługi.
3. **Klienci** — tabela `K1_DZW_KLIENCI` (`k1_dzwiek.h`): gui (sterujący), program testowy (sterujący; tylko obraz
   testowy, `TEST_DZWIEK`), od kroku 2 Ustawienia (sterujący; kanał w `DZWIEK_KANAL` na ich fd 4 — sekcja „Dźwięk”)
   i od kroku 3 **strefa** (VMM strefy z oknem — nie steruje; kanał w `DZWIEK_KANAL` na fd 7 VMM-a, `DZWIEK_KLIENT` c = numer
   strefy). Klientów naraz najwyżej 12. Nadzorca robi parę gniazd, końcówkę usługi wysyła w `DZWIEK_KLIENT` (wiersz, UID), gui swoją
   dostaje na fd 5 (`DZWIEK_KANAL`); ekran hasła — nie. Po restarcie usługi albo gui — kanał od nowa. Usługa od razu
   wysyła klientowi stan (`DZWIEK_STAN`: karta, głośność, wyciszenie, wyjście).
4. **Strumień:** `DZWIEK_OTWORZ` (a = format pierścienia, 0.12.3: S16 albo F32) + pierścień (16384 ramek: 64 KiB S16,
   128 KiB F32 — rozmiar musi się zgadzać z formatem). Bez karty — od razu `DZWIEK_ODTWORZONO` a = 0, b = 1
   (koniec). Z kartą usługa budzi kartę: HW_PARAMS (48 kHz, 2 kanały, okres 1024, 4 okresy; format — od 0.12.3 pierwszy,
   który karta przyjmie: S32_LE z 24 bitami (HD Audio: podformat MSBITS_24), z 20, S32_LE, S24_3LE, S16_LE; log „karta
   gra 24 bity (4 bajty na próbkę)”), SW_PARAMS (start po dwóch okresach), PREPARE. Klient pisze ramki i podaje ich liczbę (`DZWIEK_ZAPISANO`); co 1024 zabrane ramki (od kroku 3; wcześniej co 4096) dostaje
   `DZWIEK_ODTWORZONO` z ich liczbą (tyle miejsca odzyskał). `DZWIEK_ZAMKNIJ` po odegraniu: strumień kończy się, gdy
   pierścień się opróżni (`ODTWORZONO` b = 1).
5. **Mieszanie (tor dźwięku, od 0.12.3 — ADR 0055)** — za każdym razem, gdy karta ma miejsce na okres (`poll` POLLOUT,
   `avail_min` = okres): z każdego strumienia kopia do 1024 ramek, zamiana na liczby zmiennoprzecinkowe (S16 dokładnie;
   F32 sprawdzone — NaN i nieskończoność → 0, ponad ±1,0 → ±1,0, liczone), suma (sesja strefy z głośnością sesji),
   od 0.12.3 krok 2 **korektor** wyjścia, które gra (niżej; płaski — nic nie liczy), od kroku 3 **scena** Kons Spatial Audio
   tego wyjścia (niżej; wyłączona — nic nie liczy), głośność K1 (`k1_dzw_wzmocnienie`: sześcian, 50 % = 0,125 ≈ −18 dB; wyciszenie = 0), **ogranicznik** (nigdy ponad pełną
   skalę: łagodnie w dół do 1 ms przed głośną ramką, z powrotem 6 dB na 50 ms), zamiana na format karty z **ditherem**
   TPDF (±1 krok; bez niego, gdy każda próbka okresu już jest pełnym krokiem — przezroczystość 100 % i cisza), zapis okresu
   (`WRITEI_FRAMES`). Raz na minutę gry (gdy coś było): „ostatnia minuta: ogranicznik w N okresach, złe próbki od
   klientów: M”. Brakujące ramki strumienia to cisza; „niedobór” karty (EPIPE) →
   PREPARE i dalej (licznik przerw w logu).
6. **Cisza (T8):** bez strumieni karta gra ciszę jeszcze 5 s, potem DROP + HW_FREE (log: „cisza: karta uśpiona”, liczba
   przerw, okresów z ogranicznikiem i złych próbek). Usypianie kodeka przez PM — decyzja kroku 2: **jeszcze nie** (niżej).
7. **Słuchawki (krok 2):** wśród kontrolek karty usługa szuka gniazd słuchawek (`k1_dzw_gniazdo_sluchawek`: kontrolka karty
   typu tak/nie, „Headphone Jack”, „Front Headphone Jack”…, nigdy mikrofonu); są — trzyma kontrolki otwarte, prosi jądro
   o ich zdarzenia (SUBSCRIBE_EVENTS) i po każdym czyta gniazda od nowa. Coś w gnieździe — **wyjście „Słuchawki”**, puste —
   „Głośniki” (log: „wyjście: Słuchawki”). Głośniki wycisza samo jądro (automatyczne wyciszanie ogólnego parsera HDA),
   usługa tylko mówi, co gra, i bierze głośność tego wyjścia: każde wyjście ma własną głośność i wyciszenie
   (`DZWIEK_GLOSNOSC` z `c` = wyjście; zapamiętuje je gui — D5). Bez gniazda (QEMU) — zawsze głośniki.

**Energia (T8, decyzja kroku 2):** `power_save` kodeka wymaga w jądrze `PM`, a `PM` zmienia zachowanie także USB, i915
i kart sieciowych (usypianie urządzeń). Bez pomiaru na laptopach właściciela (kontener nie ma baterii) i przy zamrożonym
zakresie wersji K1 zostaje bez `PM`: karta po 5 s ciszy nie gra (DROP + HW_FREE), kodek nie usypia. Pomiar i decyzja —
w „Później” (`DO_ZROBIENIA.txt`).

## Korektor (0.12.3 krok 2, ADR 0055 pkt 9–13; D1: A, D2: A)
Korektor systemowy zmienia barwę całego dźwięku K1: **dziesięć pasm** co oktawę (31, 62, 125, 250, 500 Hz, 1, 2, 4, 8,
16 kHz — tabela `K1_KOR_PASMA`), każde −12…+12 dB co 0,5 dB, osobno dla **głośników** i **słuchawek** (gra korektor wyjścia,
które gra; po podłączeniu słuchawek przechodzi sam). **Gotowe ustawienia** — tabela `K1_KOR_GOTOWE` (Płaski, Więcej basu,
Wyraźny głos, Laptop, Słuchawki, Muzyka, Film); każde inne położenie suwaków to „Własne”. Liczenie (`src/wspolne/k1_korektor.c`,
czyste): pasmo = filtr szczytowy (biquad, „Audio EQ Cookbook”, Q = √2 — oktawa) w liczbach `double`; przed filtrami poziom
schodzi o najgłośniejszy punkt całej krzywej (siatka 20 Hz–20 kHz i środki pasm), więc korektor **nigdy nie gra głośniej
niż 100 %** (ogranicznik z kroku 1 i tak zostaje na końcu toru). Współczynniki liczone raz po zmianie, własnymi szeregami
(sin, cos, eˣ, pierwiastek — bez libm, te same liczby na hoście, w fuzzerze i w K1). Zmiana (także wyjścia) przechodzi
przez jeden okres: stare i nowe filtry na tym samym okresie, płynnie z jednych w drugie (bez trzasku); filtry startujące
z płaskiego korektora zaczynają od dwóch ostatnich ramek sumy. Płaski korektor nie liczy nic — tor zostaje przezroczysty
próbka w próbkę. Usługa (`src/dzwiek/korektor.c`): pasma od klienta sterującego (`DZWIEK_KOREKTOR`) sprawdzone pole po
polu, wszystkim klientom sterującym `DZWIEK_KOREKTOR_STAN` (nowemu — obu wyjść); w logu nazwa ustawienia („korektor
(Głośniki): Więcej basu”), najwyżej dwa razy na sekundę (przeciągany suwak wysyła wiele zmian), raz na minutę gry koszt
(„korektor: średnio N µs na okres (P % jednego rdzenia)” — T6: cel < 5 % rdzenia X230). Pamięta je gui (rekord „K1G2”,
`gui.md`) i daje każdej nowej usłudze; zmienia je Ustawienia → Dźwięk → Korektor (`aplikacje.md`), a okienko „Dźwięk”
pokazuje nazwę ustawienia wyjścia, które gra (wiersz „Korektor”, D1: A).

## Kons Spatial Audio (0.12.3 krok 3, ADR 0055 pkt 14–19; D3: A, D5: cała lista, D7: A)
Dźwięk przestrzenny K1 — nazwa własna „Kons Spatial Audio”, pod nią „dźwięk przestrzenny”. Usługa dzieli sumę (po
korektorze) na **cztery pasma** zwrotnicami Linkwitza-Rileya przy 120, 500 i 4000 Hz (Bas, Niski środek, Środek, Góra —
tabela `K1_PRZ_PASMA`; suma pasm = wejście, tylko faza obrócona) i stawia lewy i prawy kanał każdego pasma jako dwa wirtualne
źródła tam, gdzie mówi **scena**: kierunek (−180…180°), szerokość (0…180°), odległość (0…100 %). **Gotowe sceny** — tabela
`K1_PRZ_SCENY` (Naturalny, Kino, Koncert, Pokój, Głos); wyłączona — „Wyłączony”, inne miejsca pasm — „Własna” (krok 4:
przeciąganie). Bas zostaje tam, gdzie jest. Każde wyjście ma swoją scenę (gra ta wyjścia, które gra; po podłączeniu słuchawek
przechodzi sama):
- **słuchawki** — model głowy (Brown, Duda): opóźnienie między uszami (do 656 µs), cień głowy (góra dalszego ucha ciszej),
  łagodniejsza góra źródła z tyłu; odległość: bliżej głośniej i suszej, dalej ciszej z sześcioma wczesnymi odbiciami
  (7–27 ms, bez długiego pogłosu);
- **głośniki** (D3: A) — szerszy dźwięk: boki głośniej niż środek (najwyżej 1,6 ×), łagodne krzyżowe wygaszanie, kierunek
  za plecami odbity do przodu — bez „dźwięku z tyłu”, którego dwa małe głośniki nie zagrają.
Scena **nigdy nie gra głośniej niż 100 %** (granica dźwięku mono i mocy dwóch kanałów bez związku na siatce częstotliwości —
`k1_prz_moc`; ogranicznik zostaje na końcu toru). **Wyłączona nie liczy nic** — tor przezroczysty próbka w próbkę; trzyma
tylko kopię ostatniego okresu, od której zaczynają zwrotnice po włączeniu (bez stuknięcia). Każda zmiana (scena, wyjście,
włączenie) przechodzi przez jeden okres płynnie. Liczenie czyste (`k1_przestrzen.c`, `k1_prz_licz.c`; w `double`, bez libm),
usługa (`src/dzwiek/przestrzen.c`): scena od klienta sterującego (`DZWIEK_SCENA`) sprawdzona pole po polu, wszystkim klientom
sterującym `DZWIEK_SCENA_STAN` (nowemu — obu wyjść); w logu nazwa sceny („scena (Słuchawki): Kino”, „Własna”, „Wyłączony”)
najwyżej dwa razy na sekundę, raz na minutę gry koszt („scena: średnio N µs na okres (P % jednego rdzenia)”). Pamięta je gui
(rekord „K1G3”, `gui.md`) i daje każdej nowej usłudze; zmienia je Ustawienia → Dźwięk → Kons Spatial Audio (`aplikacje.md`);
przełącznik w okienku „Dźwięk” i scena do przeciągania — krok 4.

## Komunikaty
Typy i pola: `k1_komunikat.h` (sekcja „0.12.0 step 1”), sprawdzanie: `k1_kom_dzwiek.c`; obiekt zawsze 0.
| Typ | Kierunek | Pola |
|---|---|---|
| `DZWIEK_KARTA` | nadzorca → usługa (+ fd) | a = węzeł (ODTWARZANIE / KONTROLKI), b = karta 0…31, c = urządzenie 0…31 (kontrolki: 0) |
| `DZWIEK_KLIENT` | nadzorca → usługa (+ fd) | a = wiersz `K1_DZW_KLIENCI`, b = UID klienta, c = strefa (tylko klient `STREFA`, krok 3) |
| `DZWIEK_BEZ_KARTY` | nadzorca → usługa | wszystko 0 |
| `DZWIEK_OTWORZ` | klient → usługa (+ fd pierścienia) | a = format pierścienia `K1_DZW_FORMAT_*` (0.12.3: 0 S16, 1 F32), reszta 0 |
| `DZWIEK_ZAPISANO` | klient → usługa | a = nowe ramki 1…16384 |
| `DZWIEK_ZAMKNIJ` | klient → usługa | a = 0 po odegraniu / 1 od razu |
| `DZWIEK_GLOSNOSC` | klient sterujący → usługa | a = głośność 0…100, b = wyciszone 0/1, c = wyjście (krok 2; 0 — to, które gra), d = strefa (krok 3: głośność i wyciszenie jej sesji; wtedy c = 0) |
| `DZWIEK_SESJA` (krok 3) | usługa → klient sterujący | a = strefa, b = 1 jej sesja gra (ramki w ostatniej sekundzie), c = głośność sesji, d = wyciszona; po każdej zmianie, nowemu klientowi — każda inna niż na starcie |
| `DZWIEK_KOREKTOR` (0.12.3 krok 2) | klient sterujący → usługa | a = wyjście (Głośniki / Słuchawki), b, c, d = dziesięć pasm w półdecybelach −24…24, po cztery na pole (bajt na pasmo, `k1_kor_do_pol`; nieużyte bajty 0) |
| `DZWIEK_KOREKTOR_STAN` (0.12.3 krok 2) | usługa → klient sterujący | jak `DZWIEK_KOREKTOR`; nowemu klientowi obu wyjść, potem po każdej zmianie |
| `DZWIEK_SCENA` (0.12.3 krok 3) | klient sterujący → usługa | a = wyjście (Głośniki / Słuchawki), b = 1 Kons Spatial Audio włączone / 0, c = kierunki czterech pasm, d = ich szerokości (bajt na pasmo w krokach 2°: −90…90 i 0…90), e = ich odległości (bajt 0…100) — `k1_prz_do_pol` |
| `DZWIEK_SCENA_STAN` (0.12.3 krok 3) | usługa → klient sterujący | jak `DZWIEK_SCENA`; nowemu klientowi obu wyjść, potem po każdej zmianie |
| `DZWIEK_STAN` | usługa → klient | a = karta (SZUKA, BRAK, JEST, BLAD), b = głośność, c = wyciszone (wyjścia, które gra), d = wyjście (tylko z kartą: Głośniki albo — krok 2 — Słuchawki) |
| `DZWIEK_ODTWORZONO` | usługa → klient | a = ramki zabrane 0…16384, b = 1 strumień się skończył (wtedy a może być 0) |
| `DZWIEK_KANAL` | nadzorca → gui (fd 5), Ustawienia (fd 4; krok 2), VMM strefy z oknem (fd 7; krok 3) (+ fd) | wszystko 0 |
| `TEST_DZWIEK` | program testowy → nadzorca | wszystko 0 → `TEST_GNIAZDO` z kanałem |

## gui: ikona i okienko „Dźwięk” (D2 = B, część górna)
Ikona na górnym pasku: głośnik z dwiema falami (głośno, od 50 %), z jedną (ciszej), z ×  (wyciszony albo 0 %), blady
i przekreślony (brak karty albo karta przestała grać). Okienko 300 × 158: „Dźwięk” i nazwa wyjścia („Głośniki”),
okrągły przycisk wyciszenia (terakota, gdy wyciszony), suwak z procentami (przesunięcie suwaka zdejmuje wyciszenie),
„Odtwórz próbę”. Bez karty: „Ten komputer nie ma karty dźwiękowej.” Próba (D3 = A — jedyny dźwięk K1): dwie miękkie nuty,
660 i 880 Hz, po 0,35 s, liczone w programie (`k1_dzw_proba`, bez pliku dźwiękowego). Lista „Grają teraz” z suwakami
sesji — krok 3. Opis ekranu: „Wycisz”, „Głośność”, „Odtwórz próbę”. Wygląd: `docs/WYGLAD.md`.

## Dźwięk stref (krok 3, ADR 0054 pkt 13–16)
Przeglądarka w strefie (Firefox, przez ALSA — T6 = A; `media.cubeb.backend` „alsa” w `strefy/przegladarka/k1.js`) gra do
karty virtio-snd, którą udaje VMM strefy (`strefa.md`, urządzenie „dźwięk”). VMM jest klientem `STREFA`: kopiuje próbki
z kolejki gościa do swojego pierścienia — od 0.12.3 F32: karta strefy ogłasza S16, S32 i FLOAT, VMM przelicza je bez
straty, a Firefox nie musi już zamieniać dźwięku na 16 bitów w strefie (ADR 0055 pkt 6), najwyżej 8192 ramek przed tym, co usługa
zagrała, a transfer oddaje gościowi dopiero, gdy jego ramki zostały zagrane (postęp `DZWIEK_ODTWORZONO` co 1024 ramki). Usługa miesza sesję z **głośnością sesji** (sześcian jak głośność K1, start 100 %, wyciszenie), na koniec
głośność K1. Sesja **gra**, póki usługa brała jej ramki w ostatniej sekundzie (`K1_DZW_GRA_S`); stan sesji dostają klienci
sterujący (`DZWIEK_SESJA`): gui pokazuje „Grają teraz” i mówi ekranowi, który rysuje głośnik na pasku okna sesji (D6: A,
`ekran.md`); kliknięcie wraca przez gui jako `DZWIEK_GLOSNOSC` d = strefa. Gdy karta przestanie grać w trakcie, ramki,
które klient wysłał, zanim usłyszał koniec strumienia, nie łamią zasad (`zerwany`); odmowa bez karty — tak jak dotąd. Bez usługi albo karty VMM gra „w ciszę” własnym zegarem.
Strefa pobierania (bez okna) kanału nie dostaje.

## Klawisze, pigułka, Ustawienia (krok 2)
Klawisze głośności ekran daje tylko powłoce; gui zmienia głośność wyjścia, które gra (±5 %, wycisz), a ekran pokazuje
pigułkę głośności (D4: A — `ekran.md`). Ustawienia → Dźwięk (`aplikacje.md`) i okienko gui to dwaj klienci sterujący
z tym samym kodem (`src/przybornik/k1_dzwiek_klient.c`). Głośność po włączeniu (D5: A) — taka jak przy wyłączaniu: gui
zapisuje ją w skarbcu (`gui.md`) i daje każdej nowej usłudze.

## Testy
- Na hoście: `test_dzwiek` (pierścień: pełny, zawinięcie, liczniki przez 2³²; mieszanie 16 strumieni bez przepełnienia,
  obcięcie; krzywa głośności rosnąca; próba: obie nuty, długość, miękki początek i koniec, pisana kawałkami = w całości;
  komunikaty: zakresy, kierunki, deskryptory), `fuzz_dzwiek` (wrogi klient: dowolne zapisy i pobrania — liczba czekających
  ramek nigdy ponad pierścień), `test_uevent` (zdarzenie „sound”).
- W systemie (`t_dzwiek`, maszyna bez karty): klatka (UID, grupa, deskryptory), stan „brak karty”, NEGATYWNE: pierścień
  innej wielkości, ramki bez strumienia, zalew złymi komunikatami (klient traci kanał, usługa żyje); restart usługi po
  SIGKILL i nowy kanał; `t_proby` — próby w klatce.
- QEMU z kartą (`scripts/test-dzwiek.sh`): `ich9-intel-hda` + `hda-output` z `-audiodev wav` (QEMU zapisuje to, co
  zagrano); okienko po nazwach: próba przy 100 %, wyciszona próba, próba przy 50 %, zabicie usługi (program testowy przy
  drugim zdjęciu wyciszenia) i próba po restarcie; skrypt słucha pliku: trzy dźwięki (wyciszona — cisza), nuty 660 i 880 Hz,
  50 % ≈ 1/8 amplitudy 100 %; węzły karty ma tylko usługa dźwięku. Bez karty — `test-m4.sh` (okienko mówi „brak karty”).
- Krok 2, `test-dzwiek.sh`: klawisze „ciszej” ×2 (40 %, pigułka w opisie ekranu i znika sama, gui zapisuje), Ustawienia →
  Dźwięk (wyjście, próba przy 40 %), „wycisz” i „głośniej” (45 %), drugie wyciszenie — program testowy zabija pulpit, nowy
  bierze ze skarbca 45 % bez wyciszenia, próba z Ustawień; w pliku pięć dźwięków (40 % i 45 % w stosunku sześcianów do 50 %).
  `test-strefa.sh` — NEGATYWNY: klawisz głośności przy fokusie strefy daje pigułkę, gość go nie dostaje. `test_dzwiek`:
  klawisze, nazwy gniazd, rekord pamięci (złe wartości odrzucone), `GLOSNOSC_POKAZ`; `fuzz_dzwiek` — też rekord pamięci.
- Krok 3: `test_virtio_dzwiek` (prośby sterujące w stanach virtio, tylko format K1, długości, nagłówek transferu),
  `fuzz_virtio_dzwiek` (wrogi gość: dowolne prośby — odpowiedź nigdy dłuższa niż miejsce, rozmiary strumienia zawsze
  poprawne), `test_dzwiek` (klient `STREFA`, `DZWIEK_SESJA`, `DZWIEK_GLOSNOSC` d, głośność sesji na próbkach,
  `STREFA_DZWIEK`, `STREFA_WYCISZ`); `test-dzwiek.sh`: strefa próbna gra ton 480 Hz (gość `TRYB_DZWIEK` przez ALSA bez
  alsa-lib), głośnik na pasku w opisie ekranu, „Grają teraz” w okienku, kliknięcie głośnika — sesja wyciszona (w pliku
  przerwa), drugie — znów gra; gość złośliwy (`TRYB_DZWIEK_ZLY`: zły format — „nieobsługiwane”, prośba bez miejsca na
  odpowiedź — VMM kończy jego maszynę).
- 0.12.3 krok 1 (ADR 0055): `test_dzw_tor` — pomiary planu z granicami (ton 1 kHz przy 100, 50, 10, 5 % do karty 16, 20
  i 24 bitów: błąd, harmoniczne, średnia; najcichszy ton; przezroczystość; cisza), ogranicznik, próbki F32 wrogiego
  klienta, bajty formatów karty, ten sam dither przy każdym przebiegu; `test_dzwiek` — formaty pierścienia, `DZWIEK_OTWORZ`
  a; `test_virtio_dzwiek` — formaty gościa, ramki ich wielkości, próbki do `float`; `fuzz_dzwiek` — pierścień w obu
  formatach z dowolnych bajtów, suma zawsze skończona, po ograniczniku nigdy ponad ±1,0; `t_dzwiek` — pierścień F32,
  NEGATYWNY: pierścień 16 bitów zgłoszony jako F32 odrzucony; `test-dzwiek.sh` — karta QEMU przyjmuje tylko 16 bitów
  (log), pierwsza próba w pliku WAV próbka w próbkę taka jak w programie, strefa gra ton w S32 (log VMM).
- 0.12.3 krok 2 (korektor): `test_korektor` — własne sin, cos, eˣ, pierwiastek wobec libm; tabele (pasma co oktawę, gotowe
  ustawienia w ±12 dB, każde rozpoznane po nazwie); pasma w komunikacie i z powrotem (ponad ±12 dB, nieużyte bajty —
  odrzucone); każde pasmo samo: środek ±0,1 dB od celu, trzy oktawy dalej ±0,5 dB; poziom przed filtrami — żadne ustawienie
  nie jest głośniejsze niż 100 % (gęsta siatka); ton przez pasmo +6 dB wobec tonu trzy oktawy dalej; płaski —
  przezroczysty; zmiana w trakcie tonu bez skoku, powrót do płaskiego dokładny po okresie; najgorsze ustawienia na szumie
  pełnej skali — skończone, bez rozbiegania; cisza zeruje pamięć filtrów. `fuzz_korektor` (wrogi klient sterujący: dowolne
  pola, dowolna suma, zmiana w trakcie); `test_dzwiek` — komunikaty korektora, rekord „K1G2” (i „K1G1”); `t_dzwiek` —
  korektor ustawiony i powiedziany z powrotem, NEGATYWNE: pasmo ponad +12 dB, nieistniejące pasmo. `test-dzwiek.sh`:
  Ustawienia → Dźwięk → Korektor po nazwach, pasmo 1k na +12 dB, szósta próba — w pliku nuta 660 Hz o 7,0 dB i 880 Hz
  o 1,7 dB ciszej niż piąta (krzywa liczona w `k1dzwiek.py` jak w `k1_korektor.c`, ±0,75 dB), wiersz „Korektor” w okienku
  otwiera Ustawienia na korektorze, „Płaski”.
- 0.12.3 krok 3 (Kons Spatial Audio): `test_przestrzen` — tabele (pasma krawędź w krawędź, gotowe sceny rozpoznane po nazwie,
  „Wyłączony”, „Własna”), scena w komunikacie i z powrotem (poza zakresem — odrzucona); zwrotnice: suma pasm ±0,01 dB od
  wejścia, każde pasmo jak wzór; model głowy: 656 µs z boku, źródło z lewej — lewe ucho wcześniej (impuls) i głośniej w górze,
  bas zostaje; głośniki: pasma z lewej w lewym kanale, kierunek z tyłu jak z przodu; poziom: granica ≤ 1 na gęstej siatce
  (gotowe i 100 losowych scen), ton w obu wejściach i szum bez związku przez działającą scenę nie głośniej; wyłączony —
  przezroczysty; zmiana bez skoku, wyłączony dokładny po okresie; najgorsze sceny na szumie pełnej skali — skończone; cisza
  zeruje filtry. `fuzz_przestrzen` (wrogi klient sterujący: dowolne pola dwóch scen, dowolne wyjścia, zmiana w trakcie);
  `test_dzwiek` — komunikaty sceny, rekord „K1G3” (i „K1G2”, „K1G1”); `t_dzwiek` — scena ustawiona i powiedziana z powrotem,
  NEGATYWNE: włączona = 2, kierunek 182°, odległość 101 %. `test-dzwiek.sh`: Ustawienia → Dźwięk → Kons Spatial Audio po
  nazwach, „Kino” (usługa gra, gui zapamiętuje), program testowy stawia wszystkie pasma z lewej („Własna”), siódma próba —
  w pliku prawy kanał co najmniej 20 dB ciszej, lewy jak piąta ±1 dB; przełącznik — wyłączony.
- Na sprzęcie (właściciel): próba słychać na X230 i IdeaPadzie; log z nazwami karty i kodeka; krok 2 — klawisze (log
  wejścia: „z klawiszami głośności”), słuchawki w gnieździe (log „gniazdo słuchawek: …”, „wyjście: Słuchawki”).

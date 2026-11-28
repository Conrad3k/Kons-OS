# Komponent: dzwiek (usługa dźwięku — jedyny proces z kartą dźwiękową)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.12.0 krok 1: usługa, kanał klientów, okienko „Dźwięk” w gui); PRZETESTOWANE na hoście (`test_dzwiek`, `fuzz_dzwiek`, `test_uevent`, `test_komunikat`), w systemie w QEMU bez karty (`t_dzwiek`: klatka, „brak karty”, wrogi klient, restart) i w QEMU z kartą Intel HD Audio (`scripts/test-dzwiek.sh`: dźwięk zapisany do pliku WAV). Prawdziwy głośnik X230 i IdeaPada — sprawdza właściciel. Krok 3 (dźwięk stref): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_virtio_dzwiek`, `fuzz_virtio_dzwiek`, `test_dzwiek`) i w `test-dzwiek.sh` (strefa próbna gra ton, głośnik na pasku, wyciszenie sesji, złośliwy gość); Firefox w strefie (ALSA) — stanowisko i właściciel na sprzęcie. Krok 2: słuchawki (gniazdo z kontrolek karty — ZAIMPLEMENTOWANE, test na hoście nazw gniazd; QEMU gniazda nie udaje: sprawdza właściciel), osobna głośność głośników i słuchawek, klient Ustawienia — PRZETESTOWANE w `test-dzwiek.sh` (klawisze, Ustawienia → Dźwięk, pamięć głośności po nowym pulpicie); dźwięk ze stref — krok 3; **0.12.3 krok 1** (ADR 0055, tor wysokiej jakości): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_dzw_tor` — pomiary, ogranicznik, dither, formaty karty; `test_virtio_dzwiek`, `fuzz_dzwiek`, `fuzz_virtio_dzwiek`), w systemie (`t_dzwiek`: pierścień F32) i w `test-dzwiek.sh` (karta QEMU — 16 bitów, przezroczystość próby, ton strefy w S32); 24 bity i odsłuch — właściciel na X230 i IdeaPadzie; **0.12.3 krok 2** (korektor): ZAIMPLEMENTOWANE, PRZETESTOWANE (`test_korektor`, `fuzz_korektor`, `t_dzwiek`, `test-dzwiek.sh`); **krok 3** (Kons Spatial Audio — silnik i gotowe sceny): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_przestrzen` — zwrotnice, model głowy, głośniki, poziom, przejścia; `fuzz_przestrzen`, `test_dzwiek`), w systemie (`t_dzwiek`) i w `test-dzwiek.sh` (głośniki: pasma z lewej w pliku WAV); słuchawki i odsłuch („z tyłu”, „szerzej”) — właściciel na sprzęcie; **krok 4** (scena do przeciągania, „Własna”, „Posłuchaj”): ZAIMPLEMENTOWANE, PRZETESTOWANE (`test_przestrzen`, `test_dzwiek`, `t_dzwiek`, `test-dzwiek.sh`, `test-wzorce.sh`); **0.13.5 krok 1** (mikrofon, ADR 0065): ZAIMPLEMENTOWANE, PRZETESTOWANE na hoście (`test_mikrofon`, `fuzz_mikrofon`, `test_dzwiek`), w systemie bez karty (`t_mikrofon`: kto może słuchać — testy negatywne) i w QEMU z drugim kodekiem z wejściem (`test-dzwiek.sh`: ton testowy, pasek poziomu Ustawień, znak na pasku, wyciszenie, cisza przy ekranie zabezpieczeń); prawdziwy mikrofon (wbudowany, w słuchawkach) — właściciel na X230 i IdeaPadzie |
| **Pliki** | `src/dzwiek/main.c` (pętla, nadzorca, karta, okres), `klienci.c` (0.13.5: kanały klientów, ich komunikaty i strumienie, głośność, mieszanie — wydzielone z `main.c`), `karta.c` (węzeł odtwarzania: ALSA bez alsa-lib), `kontrolki.c` (wyjścia i — od 0.13.5 — nagrywanie kodeka na 0 dB, gniazda słuchawek i mikrofonu, nazwa karty do logu), `nagrywanie.c` + `mikrofon.c` (0.13.5: węzeł nagrywania, słuchacze mikrofonu), `dzwiek.h`; `src/wspolne/k1_mikrofon.{h,c}` (0.13.5: tor mikrofonu — czysty); `src/wspolne/k1_dzwiek.{h,c}` (formaty pierścienia `K1_DZW_FORMATY`, tabela klientów `K1_DZW_KLIENCI`, liczenie pierścienia, próba), `k1_dzw_tor.{h,c}` (0.12.3: tor dźwięku — mieszanie, głośność, ogranicznik, dither, format karty), `k1_korektor.{h,c}` (krok 2: korektor), `k1_przestrzen.{h,c}` + `k1_prz_licz.c` (krok 3: Kons Spatial Audio) + `k1_prz_proba.c` (krok 4: „Posłuchaj”), `k1_kom_dzwiek.c` (komunikaty); `src/dzwiek/korektor.c`, `przestrzen.c` (korektor i scena w usłudze); `src/nadzorca/dzwiek.c` (karta z sysfs i zdarzeń jądra, kanały klientów), `profile.c` (`KLATKA_DZWIEK`); `src/gui/dzwiek.c` (ikona na pasku, okienko „Dźwięk”, próba); `jadro/k1.config` (sekcja „0.12.0 Dźwięk”) |
| **Decyzje** | ADR 0065 (0.13.5: mikrofon — D1 = A, D2 = A, D3 = A, T1–T6); ADR 0054 (plan 0.12.0: T1–T4, T7, T8; właściciel: D1 = A, D2 = B, D3 = A; krok 2: D4 = A, D5 = A; krok 3: D6 = A, T5, T6 = A); ADR 0055 (plan 0.12.3 krok 1: T1–T5, T8 — tor wysokiej jakości; krok 2: D1 = A, D2 = A, T6 — korektor; krok 3: D3 = A, D5 = cała lista, D7 = A, T7 — Kons Spatial Audio; krok 4: D4 = A, D6 = A — scena do przeciągania) |

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
| 111 | `ioctl` tylko 15 numerów ALSA: PCM — INFO, HW_PARAMS, HW_FREE, SW_PARAMS, PREPARE, WRITEI_FRAMES, READI_FRAMES (0.13.5: mikrofon), DROP; kontrolki — CARD_INFO, ELEM_LIST, ELEM_INFO, ELEM_READ, ELEM_WRITE, TLV_READ, SUBSCRIBE_EVENTS (krok 2: zdarzenia gniazda słuchawek) | brak ścieżek | `dzwiek`: 16 MiB, 1 proces, 24 deskryptory, `RLIMIT_FSIZE` 0 | fd 3: kanał od nadzorcy (jednokierunkowy); od nadzorcy: węzeł odtwarzania `pcmC<k>D<u>p`, nagrywania `pcmC<k>D<u>c` (0.13.5) i kontrolki `controlC<k>` karty, końce kanałów klientów; pierścienie słuchaczy mikrofonu mapowane tylko do zapisu |

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

## Mikrofon (0.13.5 krok 1, ADR 0065; D2: A, D3: A)
Nagrywa ta sama usługa (jeden właściciel karty). Nadzorca daje jej z kartą jej pierwszy węzeł nagrywania (`DZWIEK_KARTA`
NAGRYWANIE, `O_RDONLY`); karta bez niego — „brak mikrofonu” (źródło BRAK w stanie). **Karta nagrywa tylko, gdy ktoś słucha**
(`nagrywanie.c`): HW_PARAMS (48 kHz; S32_LE, inaczej S16_LE; stereo, inaczej mono), SW_PARAMS (start przy pierwszym
odczycie), PREPARE i jeden odczyt, który ją uruchamia; potem okres po każdym `POLLIN` (`READI_FRAMES`); przepełnienie
(EPIPE) — PREPARE i dalej (licznik w logu); nikt nie słucha — DROP i HW_FREE (log „mikrofon nie nagrywa”).
- **Kto może słuchać** — kolumna `K1_DZW_KLIENCI` (`k1_dzw_klient_slucha`): pulpit — nigdy (rysuje znak, wycisza), Ustawienia —
  tylko poziom (`DZWIEK_MIKROFON` POZIOM: 0…100 co 107 ms w `DZWIEK_NAGRANO` c, nigdy próbka), program testowy — próbki,
  strefa — próbki, tylko gdy nadzorca pozwolił (`DZWIEK_KLIENT` d = 1 — krok 2: sesja z przełącznikiem „Mikrofon”). Klient,
  który prosi o więcej, traci kanał.
- **Próbki** (`DZWIEK_OTWORZ` b = SŁUCHA): pierścień klienta jak przy odtwarzaniu (dokładny rozmiar, pieczęcie przeciw
  zmniejszaniu i powiększaniu), ale mapowany **tylko do zapisu**; po każdym okresie usługa pisze 1024 ramki od swojej pozycji
  (zawija na końcu) i mówi `DZWIEK_NAGRANO` a = 1024, c = poziom. Pozycji od klienta nie bierze: wolny słuchacz traci swoje
  najstarsze ramki. `DZWIEK_ZAMKNIJ` b = SŁUCHA (a = OD_RAZU) — koniec.
- **Tor** (`k1_mikrofon.c`, czysty): ramki karty → liczby zmiennoprzecinkowe (mono do obu kanałów) → ton testowy (tylko na
  prośbę programu testowego, `TESTOWY`: 1 kHz, −12 dB — karta QEMU nagrywa ciszę) → × poziom mikrofonu (0…100: 50 = 0 dB,
  100 = +20 dB, 0 = cisza) → w granicach ±1,0 → cisza, gdy wyciszony albo wstrzymany → poziom paska (0…100 = −60…0 dBFS,
  0,6 dB na krok) → pierścienie słuchaczy (S16 zaokrąglone, F32 jak są).
- **Kontrolki** (`kontrolki.c`): „Capture Switch” włączony, „Capture Volume” na 0 dB (jak wyjścia); wzmocnienia „… Boost”
  zostają, jak ustawi jądro (NIEPEWNE na laptopach). Mikrofon w słuchawkach przełącza samo jądro (auto-mic parsera HDA);
  usługa czyta jego gniazdo (`k1_dzw_gniazdo_mikrofonu`: „Mic Jack”, „Headset Mic Jack”…, nigdy „… Phantom Jack”) i mówi
  źródło: „Wbudowany” albo „W słuchawkach” (log „wyjście: …, mikrofon: …”).
- **Stan** dla klientów sterujących — `DZWIEK_STAN` e (`k1_dzw_mik_pole`): źródło, poziom, wyciszony, wstrzymany, „K1
  słucha” (Ustawienia albo program testowy); innym klientom e = 0. Poziom i wyciszenie ustawia klient sterujący
  (`DZWIEK_MIKROFON` USTAW), pamięta je pulpit (rekord „K1G5”).
- **T4 — prywatność:** pulpit, gdy jego ekran zabezpieczeń (Ctrl+Alt+Del) jest na wierzchu, mówi `WSTRZYMAJ` b = 1 — usługa
  daje wszystkim słuchaczom ciszę (log „mikrofon: wstrzymany (ekran zabezpieczeń)”); koniec kanału pulpitu zdejmuje
  wstrzymanie. W logu kto słucha i liczba przepełnień — nigdy próbki ani poziomy w czasie.

## Korektor (0.12.3 krok 2, ADR 0055 pkt 9–13; D1: A, D2: A)
Korektor systemowy zmienia barwę całego dźwięku K1: **dziesięć pasm** co oktawę (31, 62, 125, 250, 500 Hz, 1, 2, 4, 8,
16 kHz — tabela `K1_KOR_PASMA`), każde −12…+12 dB co 0,5 dB, osobno dla **głośników** i **słuchawek** (gra korektor wyjścia,
które gra; po podłączeniu słuchawek przechodzi sam). **Gotowe ustawienia** — tabela `K1_KOR_GOTOWE` (Płaski, Więcej basu,
Wyraźny głos, Laptop, Słuchawki, Muzyka, Film); każde inne położenie suwaków to „Własne”. Liczenie (`src/wspolne/k1_korektor.c`,
czyste): pasmo = filtr szczytowy (biquad, „Audio EQ Cookbook”, Q = √2 — oktawa) w liczbach `double`; przed filtrami poziom
dobiera poziom tak, żeby **głośność była taka jak bez korektora** (0.13.0, ADR 0055 pkt 21: średnia mocy po oktawach
20 Hz–20 kHz ważona krzywą K z BS.1770 — jak LUFS; pasma podniesione grają głośniej, opuszczone ciszej, reszta prawie bez
zmian); najgłośniejszy punkt krzywej (siatka, środki pasm, 20–24 kHz) najwyżej +6 dB ponad 100 % (`K1_DZW_ZAPAS`), resztę
bierze ogranicznik z kroku 1 na końcu toru (do 0.13.0 poziom schodził o ten punkt: „Więcej basu” ściszało wszystko poza
basem o 7 dB). Współczynniki liczone raz po zmianie, własnymi szeregami
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
- **głośniki** (D3: A) — szerszy dźwięk: boki głośniej niż środek (najwyżej 1,4 ×), łagodne krzyżowe wygaszanie samych boków
  (głos zostaje na swoim poziomie), kierunek
  za plecami odbity do przodu — bez „dźwięku z tyłu”, którego dwa małe głośniki nie zagrają.
Scena **zachowuje głośność** (0.13.0, ADR 0055 pkt 21: jak korektor — waga K, pół dźwięku takiego samego w obu kanałach,
pół bez związku; gotowe sceny 0,00…−0,42 dB od dźwięku bez nich), a jej najgłośniejszy punkt (granica dźwięku mono i mocy
dwóch kanałów bez związku na siatce częstotliwości — `k1_prz_moc`) najwyżej +6 dB ponad 100 %; ogranicznik zostaje na końcu
toru. W słuchawkach środek pasma (to, co jest takie samo w obu kanałach — głos) gra od 0.13.0 z jednego źródła w kierunku
pasma, a dwa źródła tylko boki: bez grzebienia, który znosił głos (−6,6 dB przy 1 kHz). **Na start** (właściciel,
2026-11-26) gra **„Koncert” z basem na 4 %, włączony** — na obu wyjściach, dopóki pulpit nie da zapamiętanej sceny. **Wyłączona nie liczy nic** — tor przezroczysty próbka w próbkę; trzyma
tylko kopię ostatniego okresu, od której zaczynają zwrotnice po włączeniu (bez stuknięcia). Każda zmiana (scena, wyjście,
włączenie) przechodzi przez jeden okres płynnie. Liczenie czyste (`k1_przestrzen.c`, `k1_prz_licz.c`; w `double`, bez libm),
usługa (`src/dzwiek/przestrzen.c`): scena od klienta sterującego (`DZWIEK_SCENA`) sprawdzona pole po polu, wszystkim klientom
sterującym `DZWIEK_SCENA_STAN` (nowemu — obu wyjść); w logu nazwa sceny („scena (Słuchawki): Kino”, „Własna”, „Wyłączony”)
najwyżej dwa razy na sekundę, raz na minutę gry koszt („scena: średnio N µs na okres (P % jednego rdzenia)”). Pamięta je gui
(rekord „K1G3”, `gui.md`) i daje każdej nowej usłudze; zmienia je Ustawienia → Dźwięk → Kons Spatial Audio (`aplikacje.md`);
od kroku 4 (pkt 20 ADR 0055; D4: A, D6: A) scenę przeciąga się w Ustawieniach (widok z góry), przełącznik jest też
w okienku „Dźwięk”. Krok 4: każde wyjście ma też **„Własną”** — miejsca ostatniej sceny ustawionej ręką: klient sterujący
daje ją `DZWIEK_SCENA` z a = wyjście + `K1_DZW_SCENA_WLASNA`, usługa ją trzyma (nie gra, nie loguje — zmienia się przy
każdym przeciągnięciu) i mówi wszystkim klientom sterującym (nowemu — obu wyjść); gui ją pamięta („K1G4”). „Posłuchaj”
(szum w pasmach, `k1_prz_proba.c`) to strumień klienta jak próba — usługa nic o nim nie wie.

## Komunikaty
Typy i pola: `k1_komunikat.h` (sekcja „0.12.0 step 1”), sprawdzanie: `k1_kom_dzwiek.c`; obiekt zawsze 0.
| Typ | Kierunek | Pola |
|---|---|---|
| `DZWIEK_KARTA` | nadzorca → usługa (+ fd) | a = węzeł (ODTWARZANIE / KONTROLKI / — 0.13.5 — NAGRYWANIE), b = karta 0…31, c = urządzenie 0…31 (kontrolki: 0) |
| `DZWIEK_KLIENT` | nadzorca → usługa (+ fd) | a = wiersz `K1_DZW_KLIENCI`, b = UID klienta, c = strefa (tylko klient `STREFA`, krok 3), d = 1 może słuchać mikrofonu (0.13.5 krok 2: tylko `STREFA` z przełącznikiem „Mikrofon”) |
| `DZWIEK_BEZ_KARTY` | nadzorca → usługa | wszystko 0 |
| `DZWIEK_OTWORZ` | klient → usługa (+ fd pierścienia) | a = format pierścienia `K1_DZW_FORMAT_*` (0.12.3: 0 S16, 1 F32), b = `K1_DZW_KIER_GRA` / `SLUCHA` (0.13.5: mikrofon do pierścienia), reszta 0 |
| `DZWIEK_ZAPISANO` | klient → usługa | a = nowe ramki 1…16384 |
| `DZWIEK_ZAMKNIJ` | klient → usługa | a = 0 po odegraniu / 1 od razu, b = kierunek (0.13.5: SŁUCHA — tylko a = 1) |
| `DZWIEK_MIKROFON` (0.13.5) | klient → usługa | a = co: USTAW (sterujący: b = poziom 0…100, c = wyciszony), POZIOM (kto może słuchać: b = 1 słucham poziomu / 0), WSTRZYMAJ (tylko pulpit: b = 1 ekran zabezpieczeń na wierzchu / 0), SESJA (krok 2, sterujący: b = strefa, c = jej mikrofon wyciszony), TESTOWY (tylko program testowy: b = 1 ton w mikrofonie / 0); reszta 0 |
| `DZWIEK_GLOSNOSC` | klient sterujący → usługa | a = głośność 0…100, b = wyciszone 0/1, c = wyjście (krok 2; 0 — to, które gra), d = strefa (krok 3: głośność i wyciszenie jej sesji; wtedy c = 0) |
| `DZWIEK_SESJA` (krok 3) | usługa → klient sterujący | a = strefa, b = 1 jej sesja gra (ramki w ostatniej sekundzie), c = głośność sesji, d = wyciszona, e = mikrofon sesji (0.13.5 krok 2: 1 słucha, 2 wyciszony); po każdej zmianie, nowemu klientowi — każda inna niż na starcie |
| `DZWIEK_KOREKTOR` (0.12.3 krok 2) | klient sterujący → usługa | a = wyjście (Głośniki / Słuchawki), b, c, d = dziesięć pasm w półdecybelach −24…24, po cztery na pole (bajt na pasmo, `k1_kor_do_pol`; nieużyte bajty 0) |
| `DZWIEK_KOREKTOR` od usługi (0.12.3 krok 2; do 0.13.5 osobny typ `KOREKTOR_STAN`) | usługa → klient sterujący | te same pola — stan; nowemu klientowi obu wyjść, potem po każdej zmianie |
| `DZWIEK_SCENA` (0.12.3 krok 3) | klient sterujący → usługa | a = wyjście (Głośniki / Słuchawki), b = 1 Kons Spatial Audio włączone / 0, c = kierunki czterech pasm, d = ich szerokości (bajt na pasmo w krokach 2°: −90…90 i 0…90), e = ich odległości (bajt 0…100) — `k1_prz_do_pol` |
| `DZWIEK_SCENA` od usługi (0.12.3 krok 3; do 0.13.5 osobny typ `SCENA_STAN`) | usługa → klient sterujący | te same pola — stan; nowemu klientowi obu wyjść, potem po każdej zmianie |
| `DZWIEK_SCENA` z a = wyjście + 16 (0.12.3 krok 4) | w obie strony | „Własna” wyjścia (`K1_DZW_SCENA_WLASNA`): b = 1 jest, 0 jeszcze nie ma, c–e jak w scenie; usługa ją trzyma i mówi, nie gra |
| `DZWIEK_STAN` | usługa → klient | a = karta (SZUKA, BRAK, JEST, BLAD), b = głośność, c = wyciszone (wyjścia, które gra), d = wyjście (tylko z kartą: Głośniki albo — krok 2 — Słuchawki), e = mikrofon (0.13.5, `k1_dzw_mik_pole`: źródło, poziom, wyciszony, wstrzymany, K1 słucha; tylko klientom sterującym, innym 0) |
| `DZWIEK_NAGRANO` (0.13.5) | usługa → słuchacz mikrofonu | a = nowe ramki w jego pierścieniu 0…16384 (0: słucha tylko poziomu), b = 1 już nie słucha (odmowa, brak mikrofonu, koniec; wtedy a = 0), c = poziom 0…100 ostatnich okresów |
| `DZWIEK_ODTWORZONO` | usługa → klient | a = ramki zabrane 0…16384, b = 1 strumień się skończył (wtedy a może być 0) |
| `DZWIEK_KANAL` | nadzorca → gui (fd 5), Ustawienia (fd 4; krok 2), VMM strefy z oknem (fd 7; krok 3) (+ fd) | wszystko 0 |
| `TEST_DZWIEK` | program testowy → nadzorca | a = 0 → `TEST_GNIAZDO` z kanałem; 0.13.5: a = inny wiersz `K1_DZW_KLIENCI` — kanał jako ten klient (strefa: strefa 1, b = 1 może słuchać), żeby sprawdzić, czego usługa mu odmawia |

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
  w pliku prawy kanał co najmniej 20 dB ciszej, lewy jak piąta ±1 dB.
- 0.12.3 krok 4 (scena do przeciągania): `test_przestrzen` — „Posłuchaj” (ten sam co raz, w kawałkach jak w pierścieniu;
  każde pasmo na swoim poziomie ±1,5 dB, szczyt w granicy, inne pasma najwyżej −10 dB, cisza między pasmami, nic po końcu);
  `test_dzwiek` — „Własna” w komunikatach (a = wyjście + 16; inne a odrzucone), rekord „K1G4” (i „K1G3”); `t_dzwiek` —
  „Własna” słuchawek zapamiętana i powiedziana z powrotem, NEGATYWNY: „Własna” nieistniejącego wyjścia. `test-dzwiek.sh`:
  punkty pasm, lista i „Posłuchaj” po nazwach; „Posłuchaj” — w pliku ósmy dźwięk: bas w obu kanałach równo, Niski środek
  z lewej co najmniej 12 dB, Środek i Góra co najmniej 20 dB, każde pasmo wyżej; przeciągnięcie „Środka” → „Własna” (gui
  zapamiętuje), „Kino”, kafelek „Własna” — punkt wraca w to samo miejsce; okienko „Dźwięk”: przełącznik wyłącza scenę,
  wiersz „Scena” otwiera Ustawienia na scenie.
- 0.13.0 (zgłoszenie właściciela, ADR 0055 pkt 21): `test_korektor` — waga K wobec libm (100 Hz −1,1 dB, 10 kHz +4,0
  dB), każde gotowe ustawienie i każde pasmo samo tak samo głośne jak bez korektora (±0,02 dB; mocno podniesione pasmo —
  szczyt w zapasie +6 dB), najgłośniejszy punkt najwyżej +6 dB (300 losowych ustawień, do 24 kHz); `test_przestrzen` —
  start: Koncert z basem 4 %, włączony; słuchawki: ton mono bez grzebienia (−3…+4 dB, przedtem −6,6 dB przy 1 kHz); każda
  gotowa scena tak samo głośna jak bez niej (szum różowy, waga K liczona w teście, ±0,5 dB); granica i ton w zapasie;
  `test_dzwiek` — rekordy „K1G1”, „K1G2” dają Koncert włączony; `test-dzwiek.sh` — przełącznik w okienku domyślnie
  włączony (wyłączany przed próbami), szósta próba w proporcji nowego poziomu, siódma: moc obu kanałów jak w piątej.
- 0.13.5 krok 1 (mikrofon, ADR 0065): `test_mikrofon` — kolumna „słucha”, stan mikrofonu w polu e (wszystkie wartości tam
  i z powrotem, bity spoza — odrzucone), gniazda mikrofonu (nigdy „Phantom”, nigdy słuchawek), komunikaty (MIKROFON
  każdego rodzaju, NAGRANO, OTWORZ/ZAMKNIJ b, KLIENT d, KARTA NAGRYWANIE — zakresy i kierunki), wzmocnienie poziomu
  (50 = 0 dB, 100 = +20 dB, rośnie), ramki karty S16/S32/mono, poziom paska (−6 dB, −12 dB, −60 dB), ton testowy,
  pierścień (zawija, S16 w granicach, F32 bez zmian); `test_dzwiek` — rekord „K1G5” (i „K1G4”…); `fuzz_mikrofon` (dowolne
  bajty karty w każdym formacie: próbki skończone, w ±1,0, poziom 0…100, pierścień tylko w sobie); `t_mikrofon` (bez karty:
  „brak mikrofonu”, poziom i wyciszenie wracają w stanie, słuchanie kończy się od razu; NEGATYWNE: pulpit, Ustawienia
  (próbki) i strefa bez przełącznika tracą kanał, WSTRZYMAJ nie od pulpitu, TESTOWY nie od programu testowego);
  `test-dzwiek.sh` — drugi kodek QEMU z wejściem (`hda-micro`, `-audiodev none`): węzeł nagrywania u usługi, „Powiedz coś”
  w Ustawieniach (pasek ponad połowę z tonem), znak na górnym pasku i jego okienko, „Wycisz mikrofon”, Ctrl+Alt+Del —
  wstrzymany, „Zatrzymaj” — karta nie nagrywa, znak znika.
- Na sprzęcie (właściciel): próba słychać na X230 i IdeaPadzie; log z nazwami karty i kodeka; krok 2 — klawisze (log
  wejścia: „z klawiszami głośności”), słuchawki w gnieździe (log „gniazdo słuchawek: …”, „wyjście: Słuchawki”).

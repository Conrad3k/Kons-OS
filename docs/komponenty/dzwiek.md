# Komponent: dzwiek (usługa dźwięku — jedyny proces z kartą dźwiękową)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.12.0 krok 1: usługa, kanał klientów, okienko „Dźwięk” w gui); PRZETESTOWANE na hoście (`test_dzwiek`, `fuzz_dzwiek`, `test_uevent`, `test_komunikat`), w systemie w QEMU bez karty (`t_dzwiek`: klatka, „brak karty”, wrogi klient, restart) i w QEMU z kartą Intel HD Audio (`scripts/test-dzwiek.sh`: dźwięk zapisany do pliku WAV). Prawdziwy głośnik X230 i IdeaPada — sprawdza właściciel. Krok 2: słuchawki (gniazdo z kontrolek karty — ZAIMPLEMENTOWANE, test na hoście nazw gniazd; QEMU gniazda nie udaje: sprawdza właściciel), osobna głośność głośników i słuchawek, klient Ustawienia — PRZETESTOWANE w `test-dzwiek.sh` (klawisze, Ustawienia → Dźwięk, pamięć głośności po nowym pulpicie); dźwięk ze stref — krok 3 |
| **Pliki** | `src/dzwiek/main.c` (kanały, klienci i ich strumienie, mieszanie, stan), `karta.c` (węzeł odtwarzania: ALSA bez alsa-lib), `kontrolki.c` (wyjścia kodeka na 0 dB, nazwa karty do logu), `dzwiek.h`; `src/wspolne/k1_dzwiek.{h,c}` (format, tabela klientów `K1_DZW_KLIENCI`, liczenie pierścienia, mieszanie, głośność, próba), `k1_kom_dzwiek.c` (komunikaty); `src/nadzorca/dzwiek.c` (karta z sysfs i zdarzeń jądra, kanały klientów), `profile.c` (`KLATKA_DZWIEK`); `src/gui/dzwiek.c` (ikona na pasku, okienko „Dźwięk”, próba); `jadro/k1.config` (sekcja „0.12.0 Dźwięk”) |
| **Decyzje** | ADR 0054 (plan 0.12.0: T1–T4, T7, T8; właściciel: D1 = A, D2 = B, D3 = A; krok 2: D4 = A, D5 = A) |

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
   testowy, `TEST_DZWIEK`) i od kroku 2 Ustawienia (sterujący; kanał w `DZWIEK_KANAL` na ich fd 4 — sekcja „Dźwięk”). Nadzorca robi parę gniazd, końcówkę usługi wysyła w `DZWIEK_KLIENT` (wiersz, UID), gui swoją
   dostaje na fd 5 (`DZWIEK_KANAL`); ekran hasła — nie. Po restarcie usługi albo gui — kanał od nowa. Usługa od razu
   wysyła klientowi stan (`DZWIEK_STAN`: karta, głośność, wyciszenie, wyjście).
4. **Strumień:** `DZWIEK_OTWORZ` + pierścień (16384 ramek, 64 KiB). Bez karty — od razu `DZWIEK_ODTWORZONO` a = 0, b = 1
   (koniec). Z kartą usługa budzi kartę: HW_PARAMS (48 kHz, S16_LE, 2 kanały, okres 1024, 4 okresy), SW_PARAMS (start po
   dwóch okresach), PREPARE. Klient pisze ramki i podaje ich liczbę (`DZWIEK_ZAPISANO`); co 4096 zabranych ramek dostaje
   `DZWIEK_ODTWORZONO` z ich liczbą (tyle miejsca odzyskał). `DZWIEK_ZAMKNIJ` po odegraniu: strumień kończy się, gdy
   pierścień się opróżni (`ODTWORZONO` b = 1).
5. **Mieszanie** — za każdym razem, gdy karta ma miejsce na okres (`poll` POLLOUT, `avail_min` = okres): z każdego
   strumienia kopia do 1024 ramek, suma, głośność K1 (`k1_dzw_wzmocnienie`: sześcian, 50 % ≈ −18 dB; wyciszenie = 0),
   obcięcie do 16 bitów, zapis okresu (`WRITEI_FRAMES`). Brakujące ramki strumienia to cisza; „niedobór” karty (EPIPE) →
   PREPARE i dalej (licznik przerw w logu).
6. **Cisza (T8):** bez strumieni karta gra ciszę jeszcze 5 s, potem DROP + HW_FREE (log: „cisza: karta uśpiona”, liczba
   przerw i przyciętych próbek). Usypianie kodeka przez PM — decyzja kroku 2: **jeszcze nie** (niżej).
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

## Komunikaty
Typy i pola: `k1_komunikat.h` (sekcja „0.12.0 step 1”), sprawdzanie: `k1_kom_dzwiek.c`; obiekt zawsze 0.
| Typ | Kierunek | Pola |
|---|---|---|
| `DZWIEK_KARTA` | nadzorca → usługa (+ fd) | a = węzeł (ODTWARZANIE / KONTROLKI), b = karta 0…31, c = urządzenie 0…31 (kontrolki: 0) |
| `DZWIEK_KLIENT` | nadzorca → usługa (+ fd) | a = wiersz `K1_DZW_KLIENCI`, b = UID klienta |
| `DZWIEK_BEZ_KARTY` | nadzorca → usługa | wszystko 0 |
| `DZWIEK_OTWORZ` | klient → usługa (+ fd pierścienia) | wszystko 0 |
| `DZWIEK_ZAPISANO` | klient → usługa | a = nowe ramki 1…16384 |
| `DZWIEK_ZAMKNIJ` | klient → usługa | a = 0 po odegraniu / 1 od razu |
| `DZWIEK_GLOSNOSC` | klient sterujący → usługa | a = głośność 0…100, b = wyciszone 0/1, c = wyjście (krok 2; 0 — to, które gra) |
| `DZWIEK_STAN` | usługa → klient | a = karta (SZUKA, BRAK, JEST, BLAD), b = głośność, c = wyciszone (wyjścia, które gra), d = wyjście (tylko z kartą: Głośniki albo — krok 2 — Słuchawki) |
| `DZWIEK_ODTWORZONO` | usługa → klient | a = ramki zabrane 0…16384, b = 1 strumień się skończył (wtedy a może być 0) |
| `DZWIEK_KANAL` | nadzorca → gui (fd 5), Ustawienia (fd 4; krok 2) (+ fd) | wszystko 0 |
| `TEST_DZWIEK` | program testowy → nadzorca | wszystko 0 → `TEST_GNIAZDO` z kanałem |

## gui: ikona i okienko „Dźwięk” (D2 = B, część górna)
Ikona na górnym pasku: głośnik z dwiema falami (głośno, od 50 %), z jedną (ciszej), z ×  (wyciszony albo 0 %), blady
i przekreślony (brak karty albo karta przestała grać). Okienko 300 × 158: „Dźwięk” i nazwa wyjścia („Głośniki”),
okrągły przycisk wyciszenia (terakota, gdy wyciszony), suwak z procentami (przesunięcie suwaka zdejmuje wyciszenie),
„Odtwórz próbę”. Bez karty: „Ten komputer nie ma karty dźwiękowej.” Próba (D3 = A — jedyny dźwięk K1): dwie miękkie nuty,
660 i 880 Hz, po 0,35 s, liczone w programie (`k1_dzw_proba`, bez pliku dźwiękowego). Lista „Grają teraz” z suwakami
sesji — krok 3. Opis ekranu: „Wycisz”, „Głośność”, „Odtwórz próbę”. Wygląd: `docs/WYGLAD.md`.

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
- Na sprzęcie (właściciel): próba słychać na X230 i IdeaPadzie; log z nazwami karty i kodeka; krok 2 — klawisze (log
  wejścia: „z klawiszami głośności”), słuchawki w gnieździe (log „gniazdo słuchawek: …”, „wyjście: Słuchawki”).

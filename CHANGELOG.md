# Historia zmian K1

Format: każda wersja ma sekcje **Dodane**, **Zmienione**, **Naprawione**, **Usunięte** (tylko potrzebne).
Numeracja: [docs/ARCHITEKTURA.md](docs/ARCHITEKTURA.md#wersjonowanie). Tagi wersji K1: `k1-vX.Y.Z`.

## [0.13.0] — 2026-11-27

**Bezpieczny start** (wersja 0.13, plan napisany z przeglądu bezpieczeństwa — zgłoszenia #230–#238). K1 jest odporny na
to, co dotąd najłatwiej by go złamało. Nowa wersja trafia do laptopa dopiero 20 minut po wydaniu — gdyby ktoś przejął
konto GitHub, jest czas, żeby ją zatrzymać. Starsza wersja z poprawioną dziurą nie otworzy danych (TPM 2.0), a dysk
z danymi cofnięty do starszej kopii K1 zauważy i zapyta. Gdy K1 nie wstaje, jest **tryb ratunkowy** (Esc przy starcie albo
sam po dwóch nieudanych startach): poprzednia wersja i raport na pendrive. Wydanie nie pisze już nic na port szeregowy,
a błąd krytyczny widać na ekranie. Nowa klawiatura albo mysz z USB czeka na „Pozwól”. **Ctrl+Alt+Del** zawsze prowadzi do
ekranu zabezpieczeń K1 — tylko tam ustawiasz i zmieniasz hasło, więc żadna strona nie podrobi prośby K1 o hasło.

Opis w K1: Bezpieczny start: Ctrl+Alt+Del zawsze otwiera ekran K1 (tam hasło), nowa klawiatura z USB czeka na
Twoją zgodę, a gdy K1 nie wstaje, Esc przy starcie daje tryb ratunkowy z poprzednią wersją i raportem na pendrive.

### Dodane
- **Łańcuch wydań** (krok 1, PR #245, ADR 0056; Twoja decyzja D1: zwłoka 20 minut): laptop liczy zwłokę od pobrania
  i instaluje wydanie dopiero, gdy świeże ogłoszenie po 20 minutach je potwierdza; ogłoszenie formatu 2
  (`k1-ogloszenie2.bin`) ma czas podpisu i termin 7 dni, workflow „Ogłoszenie dnia” podpisuje je co dzień; terminy liczone
  według czasu z serwera HTTPS; Ustawienia → Aktualizacje mówią „Nie mogę sprawdzić…”, gdy ogłoszenie jest stare.
  K1 do 0.12.x czyta dalej stare ogłoszenie (format 1, bez zmian).
- **TPM i odwoływanie starszych wersji** (krok 2, PR #250, ADR 0057): generacja K1 w TPM 2.0 — wersja starsza niż zapisana
  generacja nie otwiera danych (okienko „Ta wersja K1 jest za stara”); stan magazynu w TPM — dane cofnięte do starszej
  kopii: okienko z „To moje dane”; sekcja `.sbat` w `k1.efi`. X230 (TPM 1.2) zostaje bez tych ochron.
- **Tryb ratunkowy** (krok 3, PR #251, ADR 0058; D2, D3: A): Esc w pierwszej sekundzie startu albo dwa starty bez obrazu
  → menu GRUB-a z trybem ratunkowym; ekran ratunkowy: „Uruchom K1 normalnie”, „Uruchom poprzednią wersję”, „Zapisz raport
  na pendrive” (FAT16/FAT32). GRUB zostaje (koniec „tymczasowego”).
- **Obraz wydania** (krok 4, PR #252, ADR 0059; D4: A, D5: A): `console=ttynull` — nic na porcie szeregowym, log w pamięci;
  ekran awarii (opis błędu krytycznego na ekranie, restart po 60 s); „Zapisz raport na pendrive” w Ustawieniach →
  Informacje; udawane karty Wi-Fi tylko w jądrze testów; testy QEMU biorą kopię testową (`k1-test.efi`, obrazy `-test`).
- **Dostęp fizyczny** (krok 5, PR #254, ADR 0060; D6: A, D7: A): klawiatura albo mysz z USB podłączona po pierwszym
  pulpicie czeka na „Pozwól” (nie odcina jedynej klawiatury); okienko „Ustaw hasło” przy pierwszym starcie bez hasła.
- **Zaufany ekran K1** (krok 6, PR #258, ADR 0061; D8: A): Ctrl+Alt+Del nie trafia do żadnego okna — ekran zabezpieczeń
  nad wszystkim (Zablokuj, Hasło, Uruchom ponownie, Wyłącz); hasło K1 ustawia się i zmienia tylko tam; Ustawienia → Hasło
  pokazują stan.
- Testy: `test-tpm` (swtpm), `test-ratunek`, `test-wydanie` (obraz wydania: port pusty, ekran awarii, raport), `test-m5`
  (zgoda USB), `test-m4` i `test-wzorce` (ekran zabezpieczeń); na hoście `test_tpm`, `test_raport`, `test_urz_wejscia`,
  `test_stopka`; fuzzing `fuzz_tpm`, `fuzz_raport`, `fuzz_stopka`; zgodność ogłoszenia formatu 1 z czytnikiem 0.12.3.

### Zmienione
- **Jądro** (krok 7, PR #260, ADR 0062 — decyzja, jądro bez zmian): RANDSTRUCT — nie (układ struktur publicznego,
  powtarzalnego wydania zna każdy); kCFI — w audycie 0.15; ochrona jądra przez hiperwizor — po 1.0. Jądro K1: TPM 2.0,
  `DRM_PANIC`, `NULL_TTY`.
- Akcje GitHuba przypięte skrótem commita (Dependabot). Kons Spatial Audio i korektor zachowują głośność (ADR 0055 pkt 21).
- PID 1 przy błędzie krytycznym kończy się, a jądro pokazuje ekran awarii (niezmiennik 5, ADR 0059).
- RYZYKA: R10, R11, R17, R21, R26, R33, R38, R40, R41, R44 i inne zmienione; nowe R111 (ogłoszenie dnia), R112 (TPM),
  R113 (tryb ratunkowy pisze na pendrive).

### Naprawione
- Robot nie wydawał (Issue #253): instalował narzędzia ze starego spisu pakietów runnera.

### Porządki
- Nadzorca: jedno otwarcie K1-EFI i jeden zapis grubenv (`k1efi_otworz`, `k1efi_zapisz_env` w `rozruch.c`) zamiast kopii
  w zatwierdzaniu wersji i trybie ratunkowym; obraz przeglądarki pyta o identyfikator wydania `rozruch.c` zamiast czytać
  go drugi raz.
- Przegląd „Tymczasowe”: GRUB — zamknięte decyzją (ADR 0058); w kodzie nie ma żadnego rozwiązania tymczasowego.
- Formularz hasła w jednym miejscu (gui) zamiast w Ustawieniach; Ustawienia podzielone przed rozbudową (`wyglad.c`), raport
  z nadzorcy wydzielony z trybu ratunkowego (`raport.c`), urządzenia wejścia z `urzadzenia.c` (`wejscia.c`).
- Plan 0.13.0 w `docs/archiwum/`; README i lista „co sprawdzić” na laptopach.

## [0.12.3] — 2026-11-26

**Dźwięk wysokiej jakości** (drugi plan tematu 0.12, ADR 0055; Twoja prośba z 2026-11-26). Dźwięk jest czystszy: K1 liczy
go z zapasem, cichy dźwięk nie ma już „ziarna”, głośne nakładające się dźwięki nie trzeszczą, a karta dostaje 24 bity,
gdy je przyjmuje. W Ustawieniach → Dźwięk jest **korektor** — dziesięć pasm i gotowe ustawienia (Więcej basu, Wyraźny głos,
Laptop…), osobno dla głośników i słuchawek. I znak firmowy K1: **Kons Spatial Audio** — dźwięk przestrzenny. Na scenie
widzisz głowę z góry i kolorowe pasma dźwięku (bas, niski środek, środek, góra): przeciągasz je, żeby zdecydować, skąd
słychać które częstotliwości, albo wybierasz gotową scenę (Naturalny, Kino, Koncert, Pokój, Głos). „Posłuchaj” puszcza
szum po kolei w każdym paśmie, żeby usłyszeć, gdzie jest. W słuchawkach dźwięk naprawdę rozkłada się wokół głowy, na
głośnikach laptopa — szerzej. Przełącznik jest też w okienku „Dźwięk” na górnym pasku.

Opis w K1: Czystszy dźwięk, korektor w Ustawieniach (Dźwięk) i Kons Spatial Audio — dźwięk przestrzenny: przeciągasz
pasma dźwięku wokół głowy albo wybierasz gotową scenę, np. Kino. Najlepiej słychać w słuchawkach.

### Dodane
- **Tor najwyższej jakości** (krok 1, PR #229): usługa dźwięku liczy w liczbach zmiennoprzecinkowych, miękki ogranicznik
  zamiast przycinania, delikatny szum maskujący (dither) przy zamianie na format karty — przy 5 % głośności zniekształcenia
  45 dB pod dźwiękiem na karcie 16-bitowej i 94 dB na 24-bitowej (było 14 dB). Karta dostaje 24 bity, gdy je przyjmuje;
  przeglądarka oddaje dźwięk w swoim formacie (32 bity, liczby zmiennoprzecinkowe). Przy 100 % jeden dźwięk przechodzi przez
  K1 bez żadnej zmiany (test porównuje plik próbka w próbkę).
- **Korektor systemowy** (krok 2, PR #239; Twoje decyzje D1: A, D2: A): Ustawienia → Dźwięk → Korektor — 10 pasm
  (31 Hz … 16 kHz, ±12 dB), gotowe ustawienia (Płaski, Więcej basu, Wyraźny głos, Laptop, Słuchawki, Muzyka, Film), osobno
  dla głośników i słuchawek; nigdy głośniej niż 100 %; w okienku „Dźwięk” wiersz „Korektor”. K1 go pamięta.
- **Kons Spatial Audio** (krok 3, PR #240; D3: A, D5, D7: A): cztery pasma, każde w swoim miejscu wokół głowy — w słuchawkach
  model głowy (opóźnienie i cień między uszami), na głośnikach szerszy dźwięk; gotowe sceny Naturalny, Kino, Koncert, Pokój,
  Głos; scena nigdy nie gra głośniej niż 100 %; wyłączona nic nie liczy.
- **Scena do przeciągania** (krok 4, PR #241; D4: A, D6: A): widok z góry z głową i pasmami w kolorach, przeciąganie
  kierunku, odległości i szerokości (mysz, touchpad, klawiatura), kafelki scen, **„Własna”** (K1 pamięta jedną własną
  scenę dla głośników i jedną dla słuchawek — gotowa scena jej nie kasuje), **„Posłuchaj”**; w okienku „Dźwięk” przełącznik
  Kons Spatial Audio i nazwa sceny.
- Komunikaty `DZWIEK_KOREKTOR`, `DZWIEK_KOREKTOR_STAN`, `DZWIEK_SCENA`, `DZWIEK_SCENA_STAN` (także „Własna”),
  `POKAZ_USTAWIENIA`, `USTAWIENIA_MIEJSCE`; format pierścienia w `DZWIEK_OTWORZ`; pamięć dźwięku w gui — rekord „K1G4”.
- Testy: na hoście `test_dzw_tor`, `test_korektor`, `test_przestrzen` (pomiary z ADR 0055 jako granice), fuzzing
  `fuzz_korektor`, `fuzz_przestrzen`; `test-dzwiek` słucha pliku WAV z QEMU: przezroczystość, odpowiedź korektora, pasma
  sceny z lewej, „Posłuchaj” pasmo po paśmie, przeciąganie i powrót „Własnej”; `test-wzorce` — Ustawienia → Dźwięk,
  Korektor, Kons Spatial Audio, okienko „Dźwięk” (zatwierdzone przez Ciebie).

### Zmienione
- Ustawienia → Dźwięk: zakładki „Głośność | Korektor | Kons Spatial Audio”; okienko „Dźwięk” wyższe (dwa nowe wiersze).
- Strefa: karta virtio-snd ogłasza też formaty 32-bitowe i zmiennoprzecinkowe; VMM przelicza je bez strat.
- RYZYKA: R108 i R77 uzupełnione o formaty od strefy, nowe R109 (korektor od klienta), R110 (scena od klienta).

### Porządki
- `BATERIA_POKAZ` i `AKT_POKAZ` to dziś wiersze tabeli miejsc Ustawień (`K1_UST_MIEJSCA`, jeden komunikat
  `USTAWIENIA_MIEJSCE`) — dwa numery komunikatów wolne.
- Okienko „Dźwięk”: oba wiersze prowadzące do Ustawień rysuje jedna funkcja; Ustawienia — jedna funkcja zakładek otwieranych
  przez nadzorcę zamiast dwóch.
- Liczenie dźwięku w osobnych, czystych plikach (`k1_dzw_tor.c`, `k1_korektor.c`, `k1_przestrzen.c`, `k1_prz_licz.c`,
  `k1_prz_proba.c`) zamiast w pętli usługi; kolory pasm — kolumna tabeli pasm.
- Przegląd „Tymczasowe”: w kodzie nie ma żadnego rozwiązania tymczasowego.

## [0.12.0] — 2026-11-26

**Dźwięk** (wersja 0.12, ADR 0054). Laptop gra. Na górnym pasku jest głośnik: kliknięcie otwiera okienko „Dźwięk”
z głośnością, wyciszeniem, „Odtwórz próbę” i listą „Grają teraz”. Klawisze głośności laptopa zmieniają głośność, a na
ekranie na chwilę pojawia się pigułka z poziomem — także nad przeglądarką na pełnym ekranie. Słuchawki w gnieździe
przejmują dźwięk same, głośniki milkną; każde wyjście ma swoją głośność, a K1 pamięta ją po wyłączeniu. Ustawienia mają
sekcję Dźwięk. Przeglądarka gra filmy i muzykę przez K1 — kartę dźwiękową ma tylko usługa dźwięku, nie strefa — a okno
sesji, która gra, ma na pasku mały głośnik: kliknięcie ją wycisza.

Opis w K1: Laptop gra. Głośnik na górnym pasku otwiera okienko z głośnością i listą tego, co gra. Klawisze głośności
działają, słuchawki przejmują dźwięk same, a przeglądarka gra filmy — sesję, która gra, wyciszasz głośnikiem na pasku
jej okna.

### Dodane
- **Usługa dźwięku** (krok 1, PR #223; ADR 0054): jedyny właściciel karty dźwiękowej (Intel HD Audio, sterownik w jądrze),
  w swojej klatce (UID 111, tylko ioctl ALSA — 14 numerów). Klienci piszą próbki do własnego pierścienia (zapieczętowany
  memfd), usługa miesza je, liczy głośność i pilnuje każdego pola komunikatu. Po 5 s ciszy zwalnia kartę.
- **Okienko „Dźwięk”** (krok 1, Twoja decyzja D2: B): głośnik na górnym pasku (ikona z poziomem), suwak, „Wycisz”,
  „Odtwórz próbę” (dwie nuty — jedyny dźwięk samego K1, D3: A); bez karty — „Ten komputer nie ma karty dźwiękowej.”
- **Klawisze głośności i pigułka** (krok 2, PR #225; D4: A): głośność o 5 %, wyciszenie; pigułkę rysuje ekran, więc
  widać ją nad pełnym ekranem; strona w przeglądarce klawiszy nie dostaje.
- **Słuchawki** (krok 2): gniazdo słuchawek z kontrolek karty — włożone przejmują dźwięk; osobna głośność dla głośników
  i słuchawek. **Pamięć głośności** (D5: A): po włączeniu komputera taka jak przed wyłączeniem.
- **Ustawienia → Dźwięk** (krok 2): wyjście, wyciszenie, suwak, „Odtwórz próbę”.
- **Dźwięk z przeglądarki** (krok 3, PR #226; D6: A): karta virtio-snd w każdej strefie (jeden strumień 48 kHz, 16 bitów,
  stereo — bez mikrofonu, D1: A), VMM oddaje próbki usłudze dźwięku; Firefox gra przez ALSA. Na pasku okna sesji, która
  gra, mały głośnik (kliknięcie — sesja cicha); w okienku „Dźwięk” lista „Grają teraz” z suwakiem każdej sesji.
- Komunikaty `DZWIEK_*`, `GLOSNOSC_POKAZ`, `DZWIEK_SESJA`, `STREFA_DZWIEK`, `STREFA_WYCISZ` (dokumenty kanałów).
- Testy: `test-dzwiek` (karta Intel HD Audio w QEMU zapisuje dźwięk do pliku WAV — próba, głośność, wyciszenie, klawisze,
  strefa próbna z tonem 480 Hz, wyciszenie sesji, gość złośliwy), na hoście `test_dzwiek`, `test_virtio_dzwiek`,
  fuzzing `fuzz_virtio_dzwiek` i komunikatów dźwięku; `test-wzorce` — okienko „Dźwięk” i Ustawienia (zatwierdzone przez
  Ciebie).

### Zmienione
- Jądro K1: sterownik Intel HD Audio i kodeki laptopów; jądro strefy: `SND_VIRTIO`.
- Górny pasek: głośnik z poziomem głośności zamiast stałej ikony; okno sesji: głośnik na pasku, gdy gra.
- RYZYKA: R107 (sterownik karty osiągalny z usługi dźwięku), R108 (dźwięk strefy), R77 uzupełnione o virtio-snd.

### Porządki
- `k1_rysuj.c` podzielony (ikony do `k1_ikony.c`) przed nowymi ikonami głośnika; wspólny klient usługi dźwięku
  w przyborniku (`k1_dzwiek_klient.c`) zamiast osobnego w gui i Ustawieniach; suwak `k1_suwak` wspólny.
- Długie funkcje skrócone przed dopisaniem dźwięku (ekran: komunikaty od powłoki, sprawdzanie komunikatów ekranu).
- Przegląd „Tymczasowe”: w kodzie nie ma żadnego rozwiązania tymczasowego (dźwięk wszedł bez nich).
- README i dokumenty komponentów: dźwięk opisany, „czego jeszcze nie ma” bez dźwięku.

## [0.11.9] — 2026-11-25

<!-- k1: poprawki -->
**Poprawki** (robot, ADR 0047): nowe wersje tego, co K1 bierze z zewnątrz — bez zmian w kodzie K1.

### Zmienione
- **Jądro Linux 7.3.6** — poprawki bezpieczeństwa i błędów (było 7.3.5).
- **Przeglądarka:** biblioteki z Debiana 13 — poprawki bezpieczeństwa (pakietów zmienionych: 7).

## [0.11.8] — 2026-11-25

**Menu Start i górny pasek** (Twoja prośba z 2026-11-23, przed 0.12). Menu Start jest niższe i nie sięga już prawie do
górnej krawędzi ekranu: widać 7 aplikacji, a te, które się nie mieszczą, chowają się za górną krawędzią listy — dojeżdżasz
do nich kółkiem myszy albo dwoma palcami. Górny pasek wygląda spokojniej: znak K1 przed nazwą aplikacji, sieć, bateria
i dźwięk w jednej „pigułce”, godzina mocniej, data lżej. A kliknięcie baterii otwiera małe okienko: procent, co bateria
robi, ile czasu zostało (albo do pełna) i pobór mocy laptopa — te same liczby co w Zasobach.

Opis w K1: Menu Start jest niższe, a listę aplikacji przewijasz kółkiem albo dwoma palcami. Górny pasek ma nowy wygląd,
a kliknięcie baterii pokazuje, ile czasu zostało i ile prądu bierze laptop.

### Dodane
- **Okienko baterii** (krok 3, Twoja decyzja D4: szkic A; `gui/bateria.c`): kliknięcie baterii na górnym pasku → okienko
  pod nią: procent, stan, „Zostało” / „Do pełna” z czasem, „Pobór mocy” w watach, przycisk „Ustawienia baterii” (otwiera
  Ustawienia na sekcji Bateria). Liczby odświeżają się same; póki okienko jest otwarte, K1 czyta baterię co 5 s zamiast co
  15 s. Liczy je nadzorca — ten sam pomiar co dla Zasobów.
- Komunikaty `BATERIA_OKIENKO` (gui → nadzorca: okienko otwarte, zamknięte, „Ustawienia baterii”) i `BATERIA_POKAZ`
  (nadzorca → Ustawienia: pokaż sekcję Bateria); test na hoście `test_komunikat`.
- Testy: `test-m5` — okienko z liczbami wyliczonymi z tablicy ACPI (73 %, ładowanie, 9,0 W, do pełna 81 min), zamykanie,
  przycisk; `test-m1` — czas przy rozładowywaniu; `test-wzorce` — osobny przebieg z baterią, wzorce `okienko-baterii-*`
  (zatwierdzone przez Ciebie).

### Zmienione
- **Menu Start niższe i przewijane** (krok 1, PR #218; D1: 7 wierszy, D2: reszta chowa się u góry): stała wysokość listy,
  przewijanie kółkiem (wiersz na ząbek) i dwoma palcami (płynnie), miękkie zanikanie przy krawędzi, cienki pasek
  przewijania; na niskim ekranie mniej wierszy (najmniej 3); każde otwarcie od końca listy.
- **Nowy górny pasek** (krok 2, PR #219; D3: „Spokojny”): pigułka z ikonami stanu (jedna tabela w `gui/pasek_gorny.c`),
  okienka pod swoimi ikonami (jedna tabela okienek paska, otwarte najwyżej jedno).
- `ZASOBY_BATERIA` nazywa się `BATERIA_POMIAR` — idzie też do gui (po zmianie, tylko do pulpitu, nie do ekranu hasła).

### Porządki
- `gui`: bateria w osobnym pliku `bateria.c` (zanim urosła o okienko); `odbierz_baterie` → `odbierz_od_nadzorcy` (obsługuje
  cały kanał od nadzorcy); stałe okienka dźwięku (`DZW_*`) zastąpiła tabela okienek paska.
- Dokumenty: ustalenia z kroku 2 (PR #220 scalił się pusty) odtworzone w planie; `robot.md` — ustawienie repozytorium,
  bez którego robot nie wydaje (Issue #216, #217).

## [0.11.5] — 2026-11-23

**„Wyloguj” to teraz „Zablokuj” — i tylko z hasłem** (Twoja prośba z 2026-11-23, przed 0.12). Konto w K1 jest jedno,
więc „wylogowanie” niczego nie zmieniało poza tym, że zamykało aplikacje i — gdy jest hasło — pokazywało ekran hasła.
Przycisk w menu Start nazywa się teraz „Zablokuj” i pokazuje się tylko wtedy, gdy masz ustawione hasło (Ustawienia →
Hasło); bez hasła go nie ma. Po ustawieniu albo wyłączeniu hasła menu Start zmienia się od razu, bez ponownego
uruchamiania.

Opis w K1: W menu Start „Wyloguj” zmienił nazwę na „Zablokuj” i jest tylko wtedy, gdy masz ustawione hasło.

### Zmienione
- **Menu Start: „Zablokuj” zamiast „Wyloguj”** (`gui/menu_start.c`): ta sama ikona kłódki i to samo działanie —
  aplikacje się zamykają, skarbiec zapomina klucz, wraca ekran hasła. Do 0.11.4 przycisk był w K1 zainstalowanym na
  dysku także bez hasła (wtedy tylko zamykał aplikacje i otwierał pulpit od nowa), a w sesji z pendrive'a nie było go
  wcale — teraz decyduje tylko hasło (także na pendrivie z K1-DANE z hasłem).
- **Komunikat `SESJA`** (nadzorca → gui, fd 5): a=1 znaczy teraz „magazyn otwarty i ma hasło” (`blokada_mozliwa()`
  z `SKARB_STAN` b), nie „K1 zainstalowany”. Skarbiec zgłasza stan nadzorcy także po ustawieniu i wyłączeniu hasła,
  więc menu Start zmienia się od razu. Nadzorca odrzuca `WYLACZ` a=2 (zablokowanie) bez hasła — „zablokowanie
  odrzucone: magazyn danych bez hasła”. Słownik: **zablokowanie**.
- Testy: `test-m4` — bez hasła nie ma „Zablokuj”, po ustawieniu hasła w Ustawieniach jest od razu, Start → Zablokuj
  daje ekran hasła; `test-m5f` — start z ISO, z dysku i z pendrive'a bez hasła: bez „Zablokuj”. Zrzut wzorcowy
  menu Start bez przycisku (wzorce testów nie mają hasła).

### Usunięte
- `zainstalowany()` w nadzorcy (jedynym powodem był dawny „Wyloguj”).

## [0.11.4] — 2026-11-23

**„Zapisz obraz jako…” w przeglądarce — okno zapisu się mieści, K1 pyta o każdy zapisany plik** (Twoje zgłoszenie
z 2026-11-23, przed 0.12). Okno zapisu Firefoksa bywało wyższe niż okno sesji na laptopie i jego góra wychodziła poza
ekran — trzeba było wcisnąć Enter na ślepo. Teraz okno zapisu zawsze mieści się w oknie sesji. A okienko K1 „Zapisz
w Pobranych” czasem się nie pokazywało: K1 patrzył tylko na folder pobranych przeglądarki, a w oknie zapisu można wybrać
inny folder (i przeglądarka potem otwiera je tam, gdzie poszedł ostatni zapis); piąty plik zapisany, zanim odpowiesz na
okienka, przepadał.

Opis w K1: Okno „Zapisz obraz jako…” w przeglądarce mieści się w oknie sesji; K1 pyta o każdy zapisany plik — także
zapisany w innym folderze niż Pobrane i kilka naraz.

### Naprawione
- **Okno zapisu ucięte u góry** (kompozytor strefy, `xdg.c`): okno dialogowe stało na środku ekranu sesji także wtedy,
  gdy było od niego wyższe — okno wyboru pliku GTK ma 1124×859 px, a okno sesji na laptopie 1366×768 ok. 640 px
  wysokości, więc ok. 70 px góry (nazwa pliku, tytuł) i 150 px dołu („Zapisz”) były poza ekranem. Teraz lewy górny róg okna
  dialogowego jest zawsze na ekranie, a większe niż ekran dostaje prośbę (`xdg_toplevel.configure`) o rozmiar ekranu —
  GTK zmniejsza się do niego (raz na rozmiar, gdy nie może mniej — zostaje przy swoim minimum). Sprawdzone na
  stanowisku z prawdziwym Firefoksem (`K1_EKRAN=1366x640 scripts/stanowisko.sh testy/stanowisko/zapis.py`).
- **Brak okienka „Zapisz w Pobranych”** (agent plików w strefie, nowy `agent/foldery.c`):
  - agent patrzy też na katalog domowy przeglądarki i jego foldery widoczne w oknie zapisu (do 3 poziomów, także
    założone później „Nowym folderem”; bez ukrytych, profilu i „Z K1”) — obraz zapisany poza Pobranymi też wywołuje
    okienko;
  - plik, któremu K1 odmówił, bo 4 inne czekają na Twoją odpowiedź, agent zgłasza znowu, gdy któryś się skończy (albo
    po 5 s) — do 0.11.3 o nim zapominał;
  - plik zapisany w miejscu (bez `.part`) zgłasza dopiero po 0,7 s bez kolejnego zapisu — raz i w całości (program, który
    pisze plik w kilku podejściach, dawał rozmiar z pierwszego, a potem „nie udało się”);
  - po utraconych zdarzeniach jądra (przepełniona kolejka inotify) przegląda pliki zmienione w ostatnich 2 min; ten sam
    niezmieniony plik nigdy nie wraca drugi raz.
  Test na hoście `test_agent_foldery` (prawdziwe foldery i inotify: pobranie, zapis w domu, w nowym folderze, w kilku
  podejściach; negatywne: pliki sprzed startu, profil, ukryte, `.part`, plik usunięty przed zgłoszeniem, za głęboko,
  usunięty folder); stanowisko: prawdziwy agent obok Firefoksa zgłasza obraz zapisany w Pobranych i w katalogu domowym.
  W logu strefy agent podaje tylko rozmiar zgłaszanego pliku, nigdy nazwy.

### Zmienione
- Stanowisko (`scripts/stanowisko.sh`): `K1_EKRAN=SZERxWYS` — inny rozmiar ekranu sesji; obok kompozytora z ASan także
  nowy agent (`K1_AGENT`); scenariusz `testy/stanowisko/zapis.py`.

## [0.11.3] — 2026-11-22

**Przeglądarka na mocniejszych komputerach i dalsze przyspieszenia** (Twoja prośba z 2026-11-22, po 0.11.2: Speedometer
3.1 na IdeaPadzie — ten sam wynik co wcześniej, ale już taki sam z szybką grafiką i bez niej). Sesja przeglądarki bierze
teraz wszystkie procesory komputera (do 254 — granica sposobu, w jaki strefa poznaje swoje procesory), a na komputerach
z dużą pamięcią — do 256 GiB pamięci (wcześniej 64 GiB, a już od 32 GB szybka grafika by się nie uruchomiła). Do tego
w strefie: duże strony także w samej strefie, krótkie czekanie zamiast usypiania procesora i oszczędniejszy zegar.

Opis w K1: Przeglądarka bierze wszystkie procesory komputera i — na komputerach z dużą pamięcią — więcej pamięci;
strefa przeglądarki działa sprawniej (duże strony, krótsze budzenie procesorów, oszczędniejszy zegar).

### Zmienione
- **Wszystkie procesory komputera dla sesji przeglądarki** (do 0.11.2: najwyżej 4): wiersz `K1_STREFY` ma
  `K1_STREFA_PROCESORY_MAKS` (254 — strefa poznaje procesory z tablicy MP, gdzie każdy ma 8-bitowy identyfikator APIC;
  255 to „wszystkie”, jeden bierze kontroler przerwań), VMM daje tyle, ile ma komputer. Tablica MP przeniesiona spod
  ostatniego KiB pamięci (mieściła 37 procesorów) do `ST_MP_TABELA` — wskaźnik zostaje tam, gdzie szuka go Linux; jądro
  strefy z `NR_CPUS=256`; zapas pamięci klatki VMM-a rośnie o 1 MiB na procesor i 2 MiB na GiB pamięci gościa (tablice
  stron KVM).
- **Do 256 GiB pamięci dla sesji** (było 64 GiB; górna granica dalej: pamięć komputera minus rezerwa K1 — na laptopie z
  8 GB ok. 7 GiB, z 16 GB ok. 15 GiB). Naprawione przy okazji: okno szybkiej grafiki leżało na stałym adresie 32 GiB
  i nachodziło na pamięć gościa od 28 GiB — na komputerze z 32 GB i więcej sesja z szybką grafiką by nie ruszyła. Teraz
  okno idzie zaraz nad pamięć gościa (`most_okno_baza`, `test_gpu_most`), a VMM pilnuje, żeby całość zmieściła się
  w adresach, które procesor daje gościowi.
- **Duże strony w strefie** (jądro strefy `TRANSPARENT_HUGEPAGE`, zawsze): w 0.11.2 duże strony miała tylko pamięć
  strefy po stronie K1 — teraz także sterta JavaScriptu i bufory przeglądarki w samej strefie, więc procesor trzyma
  w TLB wpisy po 2 MiB zamiast 4 KiB.
- **Krótkie czekanie przed uśpieniem procesora strefy** (jądro strefy `HALTPOLL_CPUIDLE`, `cpuidle_haltpoll.force=1`):
  przeglądarka to wiele procesów i wątków, które co chwilę budzą się nawzajem — bezczynny procesor strefy czeka teraz
  chwilę czynnie (do 200 µs, sam dopasowuje czas), zamiast od razu wychodzić z gościa i czekać na obudzenie przez K1.
  Nie w emulacji QEMU (testy).
- **Zegar TSC-deadline dla procesorów strefy**: KVM go obsługuje, ale sam go nie zgłasza (QEMU włącza go jawnie) — jądro
  strefy nastawia teraz zegar jednym zapisem.

### Naprawione
- **Pełny ekran: prośba gościa mogła nie dojść do ekranu K1** (od 0.11.2, gdy powiadomienia od gościa obsługuje osobny
  wątek): kilka szybkich zmian gościa („proszę”, „wychodzę”…) VMM sklejał w ostatnią — ekran nie
  słyszał prośby (bezpieczeństwo bez zmian: wynik i tak „bez pełnego ekranu”, ale test negatywny tego nie widział).
  VMM wysyła teraz ekranowi każdą zmianę od razu, po kolei (`okno.c`; najwyżej 4 na sekundę, jak dotąd).



**Szybsza przeglądarka** (Twoja prośba z 2026-11-22: Speedometer 3.1 na IdeaPadzie — 5 bez szybkiej grafiki, 4 z nią).
Przeglądarka dostaje teraz tyle procesorów, ile ma komputer (do 4; do tej pory 2 z 4 wątków X230 i IdeaPada), jej pamięć
leży w dużych stronach, a strefa nie zatrzymuje się już na każdym znaku dla urządzeń (obraz, sieć, karta, dysk). Najwięcej
powinno to dać przy szybkiej grafice — obsługa karty zabierała do tej pory przeglądarce jeden z jej dwóch procesorów.

Opis w K1: Szybsza przeglądarka: sesja dostaje tyle procesorów, ile ma komputer (do 4), jej pamięć leży w dużych stronach,
a obraz, sieć i karta graficzna nie zatrzymują już jej procesorów. Najwięcej zyskuje szybka grafika.

### Zmienione
- **Do 4 procesorów dla sesji przeglądarki** (Ogólna, Bank, Prywatna; wcześniej 2): liczba w wierszu `K1_STREFY` to teraz
  najwięcej, ile strefa dostaje — VMM nigdy nie daje więcej, niż ma komputer (`KVM_CAP_NR_VCPUS`). Speedometer mierzy
  głównie wątek strony, ale obok niego pracują rysowanie (procesorem albo — przy szybkiej grafice — sterownik karty
  w strefie), kompilacja JavaScriptu i odśmiecanie pamięci; na dwóch procesorach odbierały czas stronie. Jądro strefy
  z `PARAVIRT_SPINLOCKS` (procesor czekający na blokadę wstrzymanego procesora tej samej strefy śpi zamiast się kręcić).
- **Powiadomienia kolejek bez wychodzenia z KVM** (`src/strefa/powiadomienia.c`): każde „w kolejce jest coś nowego” od
  gościa (klatka obrazu, polecenie karty, pakiet przeglądarki, odczyt obrazu strefy) zatrzymywało dotąd procesor gościa —
  wyjście do VMM (z ochronami procesora przy powrocie), czekanie na wspólny zamek urządzeń i cała praca urządzenia na
  wątku tego procesora. Teraz KVM sam sygnalizuje eventfd kolejki (`KVM_IOEVENTFD`), procesor od razu wraca do strony,
  a urządzeniem zajmuje się osobny wątek VMM-a — na innym, wolnym wątku procesora komputera. Sprawdzanie łańcuchów od
  gościa bez zmian (to samo `mmio_powiadomienie`, test `test_virtio`).
- **Duże strony dla pamięci strefy**: jądro K1 z `TRANSPARENT_HUGEPAGE` (tylko na prośbę — `MADVISE`, memfd
  `SHMEM_HUGE_ADVISE`), VMM prosi o nie dla pamięci gościa (`MADV_HUGEPAGE`, jak QEMU) — mniej chybień w TLB i krótsze
  przejścia tablic stron przy każdym dostępie strony do pamięci. Usługi K1 o nic nie proszą, więc dla nich bez zmian.

## [0.11.1] — 2026-11-22

**Poprawka po 0.11.0** (Twoje zgłoszenie z 2026-11-22). „Zapisz obraz jako…” (i każde inne okno zapisu pliku
w przeglądarce, np. Ctrl+S) zamykało sesję przeglądarki. Teraz okno zapisu się otwiera, a plik trafia do folderu pobranych
sesji — K1 pyta, jak przy pobraniu, „Zapisz w Pobranych”.

Opis w K1: Poprawka: „Zapisz obraz jako…” i inne okna zapisu pliku w przeglądarce nie zamykają już sesji przeglądarki.
Zapisany plik trafia do folderu pobranych sesji, a K1 pyta, czy zapisać go w Pobranych.

### Naprawione
- **Sesja przeglądarki zamykała się przy „Zapisz obraz jako…”** (Twoje zgłoszenie: Wikipedia, Grafika Google; IdeaPad,
  z szybką grafiką i bez niej): okno wyboru pliku Firefoksa (GTK) czyta swoje ustawienia przez GSettings, a obraz strefy
  miał ich opisy bez skompilowanego spisu (`gschemas.compiled` robi skrypt pakietu, a my rozpakowujemy pakiety bez
  skryptów) — GLib od razu kończył przeglądarkę, a za nią strefę. Budowa obrazu strefy kompiluje teraz spis programem
  z pakietu GLib (`scripts/zbuduj-strefe.sh`, powtarzalnie) i przerywa, gdy go brak; przeglądarka trzyma ustawienia GTK
  w pamięci (`GSETTINGS_BACKEND=memory` — w strefie nie ma D-Bus, dconf tylko pisał ostrzeżenia).
- Scenariusz stanowiska `testy/stanowisko/menu.py`: prawy przycisk na obrazach (w linku, przy krawędziach, po wklejeniu
  tekstu z K1), „Zapisz obraz jako…” i Enter — plik w folderze pobranych, Firefox żyje; bez poprawki pada na tym samym
  błędzie co u Ciebie.
- **Testy w CI:** sprawdzenie „OTWARCIE W TRAKCIE ZAMYKANIA (D2)” w `test-strefa` czasem nie przechodziło (#159), choć
  nadzorca robił wszystko dobrze — ponownie otwarta strefa testowa żyje ok. 1,5 s, a program testowy patrzył na nią co
  3 s; teraz co 50 ms. `test-most` po awarii gościa czekał po 15 min na każdą brakującą fazę i CI ucinał cały krok testów
  bez wyniku — teraz wynik gościa albo koniec jego maszyny kończy czekanie. Sama awaria jądra gościa przy 48 połączeniach
  naraz: zgłoszenie #208.
- **CI: testy QEMU po kilka naraz, nie wszystkie** (Twoja decyzja, na stałe): `scripts/testy.sh` puszczał wszystkie 14
  testów z maszynami QEMU naraz na 4 procesorach runnera GitHuba — wszystko zwalniało kilkukrotnie (stąd losowe błędy
  stref), a dwa razy padł sam runner, bez logów. Teraz po tyle naraz, ile procesorów (`K1_TESTY_NARAZ`), najdłuższe
  pierwsze.

## [0.11.0] — 2026-11-22

**Pliki ze stref** (plan 0.11.0, kroki 1–5; ADR 0052). Pliki i tekst przechodzą między przeglądarką a K1 — zawsze przez
Pliki, zawsze jako kopia i zawsze za Twoją zgodą wyrażoną w oknie, które rysuje K1 (strona tego nie podrobi). Pobrany plik
trafia do Plików → Pobrane, plik z Plików wysyłasz do sesji albo przeciągasz na okno przeglądarki, zmieniony plik wraca do
oryginału tylko po „Zapisz”, a tekst kopiujesz i wklejasz między Notatkami, polami K1 i przeglądarką. Do tego okno
przeglądarki sięga krawędzi ekranu i ma prawdziwy pełny ekran.

Opis w K1: Pliki przechodzą między przeglądarką a Plikami K1: pobrany plik zapiszesz w Pobranych, plik z Plików wyślesz
do sesji przeglądarki albo przeciągniesz na jej okno, a zmieniony wraca do oryginału tylko za Twoją zgodą. Działa schowek:
Ctrl+C i Ctrl+V w Notatkach, polach K1 i w przeglądarce. Okno przeglądarki sięga krawędzi ekranu i ma pełny ekran.

### Dodane
- **Pełny ekran i okno do krawędzi** (krok 1, Twoja decyzja D1 „A”): zmaksymalizowane okno przeglądarki wypełnia ekran
  między paskami co do piksela; F11 albo film „na cały ekran” zajmuje cały ekran, a K1 zawsze daje drogę powrotu (pigułka
  „pełny ekran: Esc, aby wyjść”, pasek K1 przy górnej krawędzi, Alt+Tab, klawisz Start).
- **Pliki ze strefy do Plików** (krok 2, D2 i D3 „A”): po zakończonym pobraniu okienko K1 „Zapisz w Pobranych” / „Zostaw
  w przeglądarce”; w szczegółach pliku pochodzenie „z przeglądarki · Ogólna” i data.
- **Pliki do strefy** (krok 3, D4 „A”): w menu pliku „Wyślij do” i sesje w „Otwórz za pomocą” (zamkniętą sesję K1
  otwiera); kopia trafia do folderu sesji „Z K1”; zmieniony plik wraca tylko po „Zapisać zmiany w oryginale?” („Zapisz” —
  stara wersja do Kosza, „Zapisz jako nowy plik”, „Nie”).
- **Przeciąganie między oknami** (krok 4, D6 „A”): z Plików na pulpit i z pulpitu do Plików (przenosi, **z Ctrl —
  kopiuje**, także w samych Plikach); na okno sesji przeglądarki — przy kursorze znak K1 „Kopia do sesji „Ogólna””, sesja
  dostaje kopię dopiero po puszczeniu, a strona — plik jak z menedżera plików (pole „Prześlij plik” się wypełnia).
- **Schowek** (krok 5, D5 „A”): Ctrl+C / Ctrl+X / Ctrl+V w Notatkach (zaznaczanie Shift ze strzałkami, Ctrl+A), Ctrl+V
  w polach (nazwa pliku w Plikach, hasło Wi-Fi, nazwa sesji); tekst między Notatkami a przeglądarką i między sesjami.
  Schowek trzyma ekran K1 tylko w pamięci; sesja dostaje tekst dopiero przy Twoim Ctrl+V w jej oknie (strona nie
  przeczyta schowka sama), kopia z przeglądarki liczy się tylko tuż po Twoim kliknięciu albo klawiszu w jej oknie; tekst
  z innej sesji — na ramce na chwilę „Wklejono z: Bank”.

### Zmienione
- Port plików strefy (ADR 0052) ma ramki upuszczenia i schowka; kompozytor strefy tworzy własne obiekty Wayland
  (`wl_data_offer`) do upuszczenia i zaznaczenia; przeciąganie zaczęte w strefie dalej jest od razu anulowane.
- Tabela przeciągania (`k1_przeciaganie`): cel „okno sesji”, kopiowanie z Ctrl; okienka pytań w osobnym pliku.

### Porządki
- Usunięte rozwiązanie tymczasowe z terminem 0.11.0: odczyt ustawień ekranu w starych formatach K1U1–K1U3 (od 0.10.0 każdy
  otwarty skarbiec zapisał je w K1U4; starsze — ustawienia domyślne). Tymczasowe zostaje tylko GRUB (0.12).
- Podzielone przed rozbudową / przy limicie: `k1_kom_ekran.c` (sprawdzanie powierzchni i komunikatów między oknami
  osobno), `k1_aplikacja.c` (komunikat ekranu w osobnej funkcji), `ekran/wskaznik.c` (klawisz do okna z fokusem),
  `k1_przeciaganie.c` → `k1_prz_okienko.c`; nowe części w osobnych plikach (`ekran/przeciaganie.c`, `ekran/schowek.c`,
  `pliki/kopia.c`, `notatki/schowek.c`, `strefa/schowek.c`, `kompozytor/dane.c`, `kompozytor/schowek.c`,
  `agent/schowek.c`). Pliki `.c` — żaden powyżej 600 wierszy (najbliżej: `gui/pulpit.c` 588, `kompozytor/wejscie.c` 578 —
  do podziału przed następną rozbudową). Plan 0.11.0 → `docs/archiwum/`.
- Okno wyboru pliku K1 dla stron (T7) — „Później”: wymagałoby D-Bus i portalu w strefie.

## [0.10.11] — 2026-11-21

**Szybsza grafika** (plan 0.10.11, kroki 1–3; ADR 0051). Przeglądarka z szybką grafiką zacina się mniej: gotowy obraz
strony nie jest już kopiowany procesorem po drodze z karty na ekran — gdy okno przeglądarki widać w całości, karta sama
pokazuje jej obraz jako osobną warstwę. Do tego K1 obsługuje nowsze karty zintegrowane Intela (laptopy mniej więcej
z lat 2017–2024, do Meteor / Arrow Lake) tak jak IdeaPad — **niesprawdzone na sprzęcie** (Twoja decyzja D5: takiego
laptopa nie masz). Na X230 i IdeaPadzie poza płynnością przeglądarki nic się nie zmienia.

Opis w K1: Przeglądarka z szybką grafiką działa płynniej: obraz strony idzie z karty graficznej prosto na ekran, bez
kopiowania procesorem. K1 obsługuje też nowsze karty zintegrowane Intela, z laptopów z lat 2017–2024: oszczędzanie energii
obrazu, szybka grafika i Zasoby, zakładka Karta graficzna.

### Dodane
- **Obraz strefy bez kopii** (krok 2, ADR 0051, Twoja decyzja D4 „A”): w sesji z szybką grafiką bufor karty z gotową
  klatką strony idzie ze strefy do ekranu K1 bez kopiowania — strefa i VMM go nie kopiują, a gdy nic nie zasłania okna
  przeglądarki, karta pokazuje go jako warstwę (nakładkę) pod ramką K1, z kursorem na własnej płaszczyźnie. Gdy coś
  zasłania okno (inne okno, menu, panel, zmiana rozmiaru) — ekran składa okno z bufora sam. Strefa nigdy nie dostaje
  deskryptora bufora, ekran tylko pokazuje gotowy obraz (nie zleca karcie pracy).
- **Nowsze karty zintegrowane Intela** (krok 3, Twoje decyzje D5 i D6 „A”): w jądrze K1 pliki od producenta karty — DMC
  (oszczędzanie energii obrazu) od Apollo Lake po Meteor / Arrow Lake i GuC (bez niego i915 na Alder Lake P, Raptor Lake
  i Meteor / Arrow Lake uznaje kartę za zawieszoną: obraz działa, rysowanie nie). Pośrednik grafiki przyjmuje
  rozszerzenie „PAT” przy nowym buforze, które Mesa dodaje na Meteor / Arrow Lake (bez niego szybka grafika by tam nie
  wstała), i odczyt „protected content” kontekstu (iris po zawieszeniu karty). Zasoby → „Karta graficzna” czytają część
  graficzną karty (`gt/gt0`) — Meteor Lake ma drugą część (media). Tabela generacji: `docs/komponenty/jadro.md`.
  Najnowszych kart (Lunar Lake i nowsze, sterownik xe) dalej nie ma — pulpit działa na nich przez obraz z firmware.
- **Pomiar drogi obrazu** (krok 1, plan T1): co 10 s wiersze „pomiar strefy”, „pomiar VMM”, „pomiar ekranu” w logu
  (same liczby: średnia i 95. percentyl). Log K1 idzie tylko na port szeregowy — liczb z laptopa nie ma (Twoja decyzja
  D1 „B”: bez liczb w Zasobach); na laptopie ocena płynności według listy w `docs/TESTY.md`.

### Zmienione
- Kompozytor strefy nie kopiuje bufora karty przeglądarki do swojej pamięci — scena czyta go na miejscu (krok 1; jedna
  pełna kopia klatki mniej).
- Klatka (seccomp): warunki profilu na jedno wywołanie mogą być alternatywami (bazowe zostają bezwzględne); ekran może
  wołać `DMA_BUF_IOCTL_SYNC` (krok 2).
- Jądro K1 (k1.efi) większe o ok. 1,3 MB — pliki kart Intela.

### Naprawione
- Stanowisko (`scripts/stanowisko.sh`) znów się buduje (krok 1; brakowały dwie funkcje od 0.10.5).

### Porządki
- PATCH (porządki MINOR przy 0.11.0). Tymczasowe: bez zmian (K1U1–K1U3 — 0.11.0, GRUB — 0.12), nowych nie dodano.
  Pliki `.c` — żaden powyżej 600 wierszy. Plan 0.10.11 → `docs/archiwum/`.

## [0.10.10] — 2026-11-20

**Poprawka po 0.10.9** (Twoje zgłoszenie i prośba z 2026-11-20). Instalator umie zainstalować K1 od nowa na dysku, na
którym już jest K1 z Twoimi danymi — tego brakowało przy przejściu z 0.10.7 na nowy układ dysku. Zasoby mają zakładkę
„Karta graficzna”: obciążenie karty, taktowanie i pobór mocy karty oraz całego laptopa.

Opis w K1: Instalator z pendrive'a zainstaluje K1 od nowa także na dysku, na którym już jest K1, po wpisaniu USUŃ.
W Zasobach jest nowa zakładka Karta graficzna: obciążenie karty, taktowanie i pobór mocy karty oraz całego laptopa.

### Dodane
- **Zasoby → „Karta graficzna”** (krok 2, Twoje decyzje: zakładki z wykresami, pobór mocy karty i laptopa): u góry dwie
  zakładki; w nowej — obciążenie karty (liczba i wykres z ostatniej minuty: część czasu, w której karta nie spała),
  taktowanie („650 z 1050 MHz”), wykres poboru mocy z dwiema liniami — karta (licznik energii procesora, RAPL) i cały
  laptop (gdy działa na baterii) — i sesje, które rysują kartą. Dane czyta nadzorca (sterownik i915, RAPL); jądro K1
  dostało `POWERCAP` i `INTEL_RAPL`. W maszynie wirtualnej karta „nie podaje” — liczby są na laptopach.

### Naprawione
- **Nie dało się zainstalować K1 od nowa na dysku z K1** (krok 1, Twoje zgłoszenie): K1 uruchomiony z pendrive'a bierze
  dane z dysku, więc Instalator widział dysk jako „używany” i nie dawał przycisku, a K1 0.10.7 nie zaktualizuje się do
  nowego układu. Teraz przy pendrivie z K1 Instalator → dysk → „Zainstaluj od nowa” → „USUŃ” działa także dla takiego
  dysku: K1 zamyka sesje przeglądarki i dane z dysku (pulpit działa dalej, dane w pamięci), czyści cały dysk i instaluje
  się od zera. Bez pendrive'a Instalator mówi, żeby go podłączyć.

### Zmienione
- Zasoby: lista usług ma najwyżej 12 wierszy (było 14) — okno tej samej wysokości mieści zakładki.

### Porządki
- PATCH bez osobnego planu (dwa kroki w DO_ZROBIENIA). Tymczasowe: bez zmian (K1U1–K1U3 — 0.11.0, GRUB — 0.12), nowych
  nie dodano. Pliki `.c` — żaden powyżej 600 wierszy.

## [0.10.9] — 2026-11-20

**Szybka grafika** (plan 0.10.9 „Filmy i szybka grafika”, kroki 1–6; ADR 0049, 0050). Przy każdej sesji przeglądarki jest
przełącznik „Szybka grafika”: przeglądarka w tej sesji rysuje strony kartą graficzną — płynniej, z WebGL, przy mniejszym
zajęciu procesora — a strefa dalej nie steruje kartą: każdą jej prośbę sprawdza osobny proces K1 w klatce. Działa na
IdeaPadzie; karta X230 nie oddziela pracy stref, więc tam przełącznik jest wyciszony. Do tego: K1 ma sterownik karty
Intela z jądra, obraz przeglądarki leży obok systemu, nie w pamięci (ok. 109 MB więcej wolnej pamięci), a miejsce na
system jest większe. Filmy kartą wypadły z tej wersji (Twoja decyzja z 2026-11-20) — zostają w planie jako „może”.

Opis w K1: Na nowszych kartach Intela przeglądarka może rysować strony kartą graficzną: płynniej, z WebGL i mniejszym
zajęciem procesora. Przełącznik „Szybka grafika” znajdziesz przy każdej sesji w Ustawieniach, w sekcji Przeglądarka; w Ogólnej jest włączony.
K1 ma też więcej wolnej pamięci, bo obraz przeglądarki nie leży już w pamięci.

### Dodane
- **Szybka grafika** (kroki 5 i 6; Twoje decyzje D5, D7, D9): przełącznik przy każdej sesji w Ustawieniach →
  Przeglądarka, domyślnie włączony w Ogólnej, wyłączony w Banku i Prywatnej; działa od następnego otwarcia sesji.
  W sesji z przełącznikiem strefa ma prawdziwy sterownik karty (Mesa 26.2 z iris, w obrazie strefy), a jego prośby idą
  mostem „native context” do **pośrednika grafiki** — małej usługi K1 w klatce, osobnej dla każdej strefy, która sprawdza
  każdą prośbę pole po polu i dopiero wtedy daje ją karcie. Bufory karty strefa widzi w swojej pamięci (nigdy jako kod),
  pamięć karty sesji ma limit 768 MiB. Na ramce okna — znak „układ scalony” za nazwą sesji, póki sesja rysuje kartą.
  Gdy strona zawiesi kartę albo pośrednik się zakończy, tylko ta sesja wraca do rysowania procesorem; K1 i inne sesje
  działają. Na X230 przełącznik jest wyciszony z wyjaśnieniem („karta nie oddziela pracy stref”).
- **Sterownik karty Intela w jądrze** (krok 2): K1 rysuje pulpit przez i915 zamiast obrazu z firmware (X230 i IdeaPad
  — sprawdziłeś 2026-11-20). Ustawienia → Przeglądarka mówią, czy karta obsługuje szybką grafikę.
- **Dekoder filmów** (krok 2): usługa K1 w klatce, która umie rozpakowywać filmy H.264 kartą. W tej wersji tylko
  sprawdza przy starcie, co umie karta (log) — przeglądarka z niej nie korzysta (filmy kartą: „może”, punkt 3
  DO_ZROBIENIA).

### Zmienione
- **Obraz przeglądarki poza rdzeniem** (krok 4, ADR 0050): leży w miejscu A/B obok systemu i jest sprawdzany
  dm-verity, zamiast siedzieć w pamięci przez cały czas — K1 ma ok. 109 MB pamięci więcej (rdzeń 115 → 6 MB).
- **Większe miejsca na system** (krok 4b, Twoja decyzja D10 „A”): 768 MiB na wersję (było 128 MiB), K1-EFI 1600 MiB,
  na dysku 3 GiB z poczekalnią; dysk co najmniej 4 GB, pendrive co najmniej 2 GB. Obraz przeglądarki w zstd.
  **K1 zainstalowany wersją 0.10.7 albo starszą nie zaktualizuje się** — ani przez sieć, ani z pendrive'a (Ustawienia →
  Aktualizacje mówią „zainstaluj od nowa”); zainstaluj go od nowa z pendrive'a 0.10.9 — dane z dysku przepadną.
- **Format sesji K1Z2** (krok 6): sesja pamięta przełącznik szybkiej grafiki; sesje zapisane wcześniej (K1Z1) czyta bez
  zmian.
- **ISO** (krok 4): ok. 27 MB, bez przeglądarki — K1 nie ma napędu CD, więc w maszynie wirtualnej z ISO przeglądarki nie
  ma (pendrive i dysk — bez zmian).

### Naprawione
- **Ekran „Wyłączanie” nie pojawiał się na X230** bez otwartej przeglądarki (Twoje zgłoszenie z 2026-11-19, przy kroku
  4): nadzorca czeka teraz co najmniej jeden obieg kropek (1,5 s), zanim przekaże wyłączanie dalej.

### Porządki
- Filmy kartą (strona strefy dawnego kroku 3) — wypadły przed napisaniem kodu; dekoder filmów z kroku 2 zostaje uśpiony,
  linia o filmach zniknęła z Ustawień, przełącznik nazywa się „Szybka grafika”. Tymczasowe: robot w trybie próbnym
  (od kroku 4) — termin dotrzymany, od tego wydania znowu pełny tryb; K1U1–K1U3 — 0.11.0, GRUB — 0.12 bez zmian; nowych
  nie dodano. Pliki `.c` — żaden powyżej 600 wierszy. Plan 0.10.9 — w archiwum.

## [0.10.7] — 2026-11-19

**Wygląd** (plan 0.10.7, kroki 1–7; ADR 0048). Poprawki po przeglądzie wyglądu: krótsze napisy bez półpauz, Ustawienia
i Pliki ułożone porządniej, okno przeglądarki bez kolorowej obwódki, napisy pod ikonami pulpitu czytelne na każdej
tapecie, tapeta z matową krawędzią łuków. Do tego dwa błędy zgłoszone ze sprzętu: Ustawienia → Aktualizacje już nie
zamykają Ustawień, a „Wyłącz” pokazuje ekran „Wyłączanie” i zawsze dochodzi do końca.

Opis w K1: K1 wygląda spokojniej: krótsze napisy, porządniejsze Ustawienia i Pliki, okno przeglądarki z ramką jak
inne okna. Napisy pod ikonami pulpitu czytasz na każdej tapecie. Aktualizacje w Ustawieniach już się nie zamykają,
a po kliknięciu „Wyłącz” widzisz ekran „Wyłączanie”.

### Zmienione
- **Napisy** (krok 1, Twoje zasady Z2 i Z3, decyzja D2): bez podtytułów pod nazwami sekcji Ustawień i bez zdań, które
  opisują to, co widać; ok. 80 napisów bez półpauzy (dwa zdania, przecinek albo dwukropek). Opisy w menu Start, ostrzeżenie
  przy haśle i „Pliki są tylko w pamięci…” zostają. Półpauzę w napisie okna wyłapuje teraz `sprawdz-czytelnosc.py`.
- **Ustawienia** (krok 2, D3): Informacje jako wizytówka (znak, „Kons K1”, wersja, dane w kolumnie; wersja jądra z budowy,
  nie wpisana na sztywno — było „Linux 6.18”), Aplikacje wierszami, Hasło na jednej karcie, Ekran z trybami po trzy
  w rzędzie, Aktualizacje bez terakotowego kwadratu, Przeglądarka z przyciskami, ikona Sieci jako łuki Wi-Fi. Komputer
  bez baterii nie ma sekcji „Bateria”. Lista sekcji z lewej bez zmian (Z1).
- **Pliki** (krok 3, D4): stale widać tylko „Nowy folder” (w Koszu „Opróżnij Kosz”), „Otwórz”, „Zmień nazwę”, „Usuń”
  pojawiają się po zaznaczeniu; panel szczegółów tylko przy zaznaczonym pliku, napisy w nim zawijane, nie ucięte; pusty
  folder: szary znak „Warstwy”, „Ten folder jest pusty” i „Nowy folder”.
- **Okno przeglądarki** (krok 4, D1 — szkic A, ADR 0048): ramka jak okna K1, bez kolorowej obwódki; kolor sesji tylko
  w kropce przed nazwą sesji i w linii pod paskiem; sesja jednorazowa: „· zapomina”. Pasek dalej rysuje K1, poza obrazem
  strefy („osobna maszyna”).
- **Pulpit** (krok 7, D6 — szkic A): napis pod ikoną leży na miękkiej, ciemnej pigułce — czytelny na każdej tapecie
  i w obu motywach.
- **Tapeta** (krok 7, D5): łuki bez jasnej, „świecącej” linii na krawędzi; ciepła ścianka zostaje. Dotyczy wszystkich
  pięciu tapet.

### Dodane
- **Ekran „Wyłączanie”** (krok 6, D7 — szkic B „Kropki”): od kliknięcia „Wyłącz” albo „Uruchom ponownie” („Ponowne
  uruchamianie”) do końca; przykrywa pulpit i okno przeglądarki, kropki się ruszają, póki K1 czeka na zapis sesji.
  Rysuje go ekran K1 na prośbę nadzorcy (komunikat `WYLACZANIE`, tylko od nadzorcy). Wylogowanie bez zmian.

### Naprawione
- **Ustawienia → Aktualizacje zamykały Ustawienia** (krok 5, Twoje zgłoszenia z 2026-11-18): po ponownym otwarciu
  Ustawień i po „Sprawdź teraz”. Godzina „Sprawdzono o …” była liczona funkcją biblioteki C, która czyta plik strefy
  czasowej — klatka Ustawień tego zabrania. Teraz czas polski jak zegar na pasku; `sprawdz-czytelnosc.py` pilnuje, żeby
  takie funkcje nie wróciły; test w `test-aktualizacja-siec.sh` (otwórz, zamknij, otwórz, „Sprawdź teraz”).
- **Wyłączanie z otwartą przeglądarką nie wyłączało komputera** (krok 6, Twoje zgłoszenie z 2026-11-18): nadzorca co 5 s
  zapisuje w logu, na co czeka (najwyżej 30 s), a init po ostatecznym zatrzymaniu procesów czeka najwyżej okres łaski
  i i tak wyłącza. W QEMU błąd się nie powtórzył — wynik na X230 i IdeaPadzie sprawdzasz Ty (punkt 4 DO_ZROBIENIA).

### Porządki
- Tapeta: usunięty nieużywany kolor „krawędzi” z palet. Pliki `.c` — żaden powyżej 560 wierszy (najdłuższy `k1_rysuj.c`,
  552). Tymczasowe: terminów na 0.10.7 nie było, nowych nie dodano (K1U1–K1U3 — 0.11.0, GRUB — 0.12 bez zmian). Plan
  0.10.7 — w archiwum.

## [0.10.5] — 2026-11-18

**Aktualizacje przez sieć** (plan 0.10.5, kroki 1–8; ADR 0046, 0047). K1 zainstalowany na dysku sam sprawdza wydania na
GitHubie, pobiera nową wersję i pyta, zanim ją zainstaluje; wszystko o wersjach jest w Ustawieniach → Aktualizacje.
Poprawki jądra i przeglądarki wydaje odtąd sam robot. Okno przeglądarki można powiększać i maksymalizować, Alt+Tab
przełącza okna, a Przeglądarka przypina się do paska jak inne aplikacje. System działa na jądrze Linux 7.3.

Opis w K1: K1 sam sprawdza, czy jest nowa wersja, pobiera ją i pyta, zanim ją zainstaluje. Wszystko o wersjach
znajdziesz w Ustawieniach, w sekcji Aktualizacje, także powrót do poprzedniej wersji. Okno przeglądarki możesz
powiększyć, zmniejszyć i zmaksymalizować, Alt+Tab przełącza okna, a Przeglądarkę możesz przypiąć do paska i położyć
na pulpicie. K1 działa na nowym jądrze Linux 7.3.

### Dodane
- **Aktualizacje przez sieć** (krok 2, ADR 0046; Twoje decyzje D4, D5, D7 — na razie według rekomendacji): K1 na dysku
  sprawdza 30 s po połączeniu z siecią i potem co 6 godzin („Sprawdzaj sam”, domyślnie włączone), nową wersję pobiera od
  razu i pokazuje okienko **„Jest nowa wersja K1”** z opisem; instaluje dopiero po „Zainstaluj”. W Ustawieniach →
  Aktualizacje: „Sprawdź teraz”, postęp, **„Wróć do poprzedniej wersji”**. Pobiera **strefa pobierania** — mikro-maszyna
  bez okna, która łączy się tylko z serwerami wydań GitHuba; K1 ufa podpisowi kluczem K1, nie GitHubowi. Każde wydanie ma
  teraz **ogłoszenie** (`k1-ogloszenie.bin`) i **paczkę** (`k1-<wersja>.k1a`); paczka czeka w poczekalni na dysku i jest
  sprawdzana przed zapisem. K1 z pendrive'a mówi tylko, że jest nowsza wersja — nagraj nowy pendrive.
- **Robot aktualizacji** (krok 3, ADR 0047, Twoja decyzja D6): raz dziennie sprawdza nowe jądro Linux (ta sama linia,
  nowsza linia długoterminowa po 7 dniach spokoju) i poprawione pakiety przeglądarki z Debiana, a przy zmianie sam robi
  wersję PATCH, puszcza pełne testy i po zielonych ją wydaje — z opisem „Ta aktualizacja zawiera istotne poprawki
  dotyczące bezpieczeństwa i jakości.”. Zmienia tylko numery i sumy w czterech plikach. Do tego wydania działał w trybie
  próbnym; pełny tryb — osobny PR zaraz po wydaniu.
- **Okno przeglądarki: rozmiar i maksymalizacja** (krok 4, Twoja decyzja D8): brzegi i rogi ramki K1 z kursorem
  strzałek, zarys z rozmiarem w trakcie, przeglądarka dopasowuje się po puszczeniu (najmniej 640 × 480); przycisk
  maksymalizacji i podwójne kliknięcie paska tytułu — okno między górnym a dolnym paskiem; rozmiar zapamiętany dla każdej
  sesji.
- **Alt+Tab** (krok 6, Twoja decyzja D11 — szkic A): przełącznik okien z kafelkami (także zminimalizowane okna i sesje
  przeglądarki), ostatnio używane najpierw; Shift+Tab wstecz, Esc anuluje. Strefa nie dostaje Tab ani Alt z tego skrótu.
- **Przeglądarka na pasku i pulpicie** (krok 6, Twoja decyzja D12 — szkic A): jedna ikona dla wszystkich sesji,
  kliknięcie pyta o sesję, kropka „uruchomiona”; przy nowej instalacji przypięta i na pulpicie, po aktualizacji Twój
  pulpit zostaje bez zmian.
- **Raptor Bridge** (krok 5, Twoja decyzja D9): most między strefami a internetem sprawdzony od środka strefy
  (`scripts/test-most.sh`: wiele połączeń naraz, półzamknięcia, odmowy, złośliwy gość vsock) i opisany
  (`docs/komponenty/raptor-bridge.md`).

### Zmienione
- **Ustawienia → Aktualizacje** (krok 1, Twoje decyzje D1–D3): kafel wersji z „Zweryfikowano”, aktualizacja z pendrive'a,
  nieudana próba nowej wersji; okno Ustawień 720 × 520. **Instalator tylko instaluje** (i włącza Secure Boot) — przy dysku
  z K1 odsyła do Ustawień. Nieudana próba nowej wersji pokazuje okienko także na pulpicie.
- **Jądro Linux 7.3.5** dla K1 i stref (krok 7, Twoja decyzja D14) — dotąd 6.18. Nowe nazwy opcji przeniesione,
  świadomie odrzucone opcje z powodem w `jadro/*-odrzucone.txt`; `k1.efi` 125 600 KiB (zapas w miejscu A/B ok. 5 MiB).
- Budowa: inna wersja jądra buduje się od zera, `k1.efi` podpisywane przy zatrzymanym zegarze (powtarzalność).

### Naprawione
- Czarne okno przeglądarki na X230 z 8 GB (od 0.10.1): obraz z pamięci strefy powyżej 3 GiB był czytany z innego miejsca
  (krok 1).
- Raptor Bridge: zgubione odpowiedzi i kredyt połączeń, półzamknięcia, miejsce trzymane przez klienta bez bajtu,
  połączenia po resecie urządzenia (krok 5).
- `test-m5`: komunikat jądra wpisany na porcie szeregowym w środek wiersza K1 dawał fałszywy błąd („start 2 z
  pendrive'a”, CI na PR-ze kroku 8) — skrypty w powłoce czytają log przez `log_k1` (`scripts/wspolne.sh`), jak `k1qemu.py`.

### Porządki
- **Przegląd dokumentacji** (krok 8, D10 — według rekomendacji): README (stan 0.10.5, rozmiar ISO, testy na PR-ze),
  ARCHITEKTURA (mapa repo — jeden wiersz na katalog z odesłaniem do opisu części, brakujące katalogi i skrypty, CI bez
  testów na `main`), TESTY (wszystkie fuzzery, `test-skarbiec` po usunięciu formatu 1, `t_opis`, `t_most`, CI), skill
  (stan wersji tylko w DO_ZROBIENIA, testy QEMU — odesłanie do TESTY), plany 0.10.7 i 0.10.9 (robot i wydania przez sieć:
  punkt 0 „Robot i wydania”, krok 4 planu 0.10.9 z wyłączonym robotem), AGENTS (scalony krok trafia do laptopów
  z wydaniem robota), RYZYKA R96. ADR-y, CHANGELOG i archiwum bez zmian (historia).
- `scripts/sprawdz-czytelnosc.py` pilnuje na stałe: ścieżek do plików repo w dokumentach, opisu każdego skryptu, testu
  na hoście i fuzzera, wersji w „Stan:” DO_ZROBIENIA i „Co już jest” README.
- Komunikaty `INST_*` po przeniesieniu aktualizacji do Ustawień — wszystkie w użyciu; pliki `.c` blisko limitu — żaden
  powyżej 560 wierszy. Tymczasowe: terminów na 0.10.5 w kodzie nie było, nowych nie dodano; tryb próbny robota kończy
  się z tym wydaniem. Plan 0.10.5 — w archiwum.

## [0.10.2] — 2026-11-17

**Testy** (plan 0.10.2, kroki 1–3; ADR 0045). Przeglądarka pokazuje „Uruchamiam…” aż do swojego okna i znika od razu po
zamknięciu, a sesja zapisuje się w tle — także przy wyłączaniu komputera. Testy pytają K1, co jest na ekranie, i klikają
po nazwach, a wygląd okien K1 pilnują zrzuty wzorcowe, które zatwierdzasz Ty.

### Dodane
- **Zrzuty wzorcowe** (krok 3, Twoje decyzje D3 i D4): `scripts/test-wzorce.sh` porównuje co do piksela 38 obrazów
  w `testy/wzorce/` — dolny i górny pasek, ikony pulpitu, menu Start, okienko „Którą sesję otworzyć?”, każdą z 11 sekcji
  Ustawień, Pliki, Zasoby i Kalkulator, w jasnym i ciemnym motywie; ramkę strefy (2 obrazy) — `test-strefa.sh`. Różnica
  = błąd testu z obrazami „przed / po / różnica” (w CI: artefakt z logami). Zmianę wyglądu zatwierdzasz Ty: wzorce zmienia
  `scripts/przyjmij-wzorce.sh` dopiero po Twoim „OK”.
- **Miejsca zmienne** (krok 3, uzupełnienie ADR 0045): to, co zmienia się samo (zegar i stan na górnym pasku, numer
  wersji, czas, liczby i wykresy Zasobów, miejsce na pliki, karty sieciowe), program zgłasza w opisie ekranu
  (`K1_EL_ZMIENNE`) — porównanie je pomija. Górny pasek jest panelem opisu („Pasek górny”, „Dźwięk”).
- **Opis ekranu dla testów** (krok 2, ADR 0045): w obrazie testowym ekran mówi programowi testowemu, jakie okna i panele są
  na ekranie, a w nich przyciski, wiersze menu, kafelki, ikony i zakładki — z nazwą i prostokątem (format `k1_opis.h`:
  memfd, każde pole sprawdzane, `test_opis`, `fuzz_opis`). Tylko rola `TEST` (program testowy); w obrazie, który
  instalujesz, nikt nie może zapytać (`t_opis`, `sprawdz-czytelnosc`). Bez tekstu użytkownika: pliki to „plik N”, sesje
  „sesja N”. `test-m4` klika po nazwach (`q.klik_w("Menu Start", "Notatki")`).

### Zmienione
- **Ekran startu przeglądarki** (krok 1, Twoja decyzja D1 — szkic A): „Uruchamiam: Przeglądarka · Ogólna…” z odcinkiem
  w kolorze sesji, który jeździ po pasku, zostaje, aż Firefox pokaże swoje okno (dotąd znikał po sekundzie, gdy strefa
  narysowała pierwsze tło); najwyżej 5 minut.
- **Zamykanie w tle** (krok 1, Twoja decyzja D2): po „zamknij” okno sesji znika od razu, a przeglądarka zapisuje sesję
  i maszyna kończy się w tle; kafelek sesji mówi wtedy „zamyka się…”, a otwarcie tej sesji w tym czasie K1 wykona sam,
  gdy poprzednia skończy.
- **Wyłączanie i ponowne uruchamianie** z pulpitu najpierw prosi każdą działającą sesję o zamknięcie i czeka na nią
  (najwyżej 30 s) — dotąd sesje kończyły się razem z nadzorcą, a przeglądarka nie zdążyła zapisać, że jesteś zalogowany.

### Porządki
- `test_odcinki.c` (599 wierszy, tuż pod limitem) podzielony: pełny magazyn, kroki kompaktowania, powiększanie
  i 70 000 obiektów — w `test_odcinki_pelny.c`.
- Położenia z logu w testach zostają tylko tam, gdzie opisu ekranu nie ma (listy plików i notatek, karty tapet, tryby
  ekranu, Kalkulator, przesuwanie okien, pasek okna strefy — `docs/TESTY.md`).
- Tymczasowe: terminów na 0.10.2 nie było; nowych rozwiązań tymczasowych nie dodano. Plan 0.10.2 — w archiwum.

## [0.10.1] — 2026-11-17

**Balon: pamięć strefy według potrzeby** (plan 0.10.1, kroki 1–5; ADR 0044). Przeglądarka nie wysypuje się już przy
przełączaniu kart ani po minimalizacji, bierze tyle pamięci, ile potrzebuje, i oddaje K1 to, czego nie używa; przy
otwieraniu wybierasz sesję, a kółko i dwa palce przewijają strony także po otwarciu nowej karty.

### Dodane
- **Balon** (krok 2, ADR 0044, Twoja decyzja D2): sesja przeglądarki bierze tyle pamięci, ile w tej chwili potrzebuje —
  najwyżej pamięć komputera minus rezerwa K1 (768 MB na 4 GB, 1 GB od 8 GB; IdeaPad ok. 2,9 GB, X230 ok. 6,6 GB; dotąd
  na sztywno 1,5 GB). Zwolniona pamięć (zamknięta karta) wraca do K1 sama w kilka sekund.
- **Pamięć komputera dla wszystkich** (krok 3, Twoja decyzja D3): gdy K1 brakuje pamięci, najpierw po cichu zabiera
  nieużywaną pamięć sesjom w tle (sesji, w której pracujesz — na końcu); gdy to nie wystarcza — okienko **„Mało
  pamięci”** z listą sesji, ich pamięcią i „Zamknij” przy każdej; w krytycznym braku K1 sam zamyka sesję w tle z największą
  pamięcią i mówi o tym. **Zasoby → „Sesje przeglądarki”**: pasek w kolorze sesji, „1,1 GB z 2,8 GB”. Na 4 GB zmieszczą się
  dwie lekkie sesje naraz.
- **Okienko „Którą sesję otworzyć?”** (krok 4, Twoja decyzja D4): Przeglądarka w menu Start pokazuje na środku ekranu
  kafelki sesji (kolor, nazwa, „pamięta / zapomina”, „otwarta”); otwarta sesja wychodzi na wierzch, nie startuje drugi raz.
- **Prawdziwy powód końca strefy** (krok 1): nowe zdanie „Strefa zużyła całą pamięć, którą K1 jej daje”; w logu liczniki
  strefy co 5 s (pamięć, zram, grafika, połączenia) i to, który program w strefie się skończył — same liczby.

### Zmienione
- **Ustawienia → Przeglądarka** (krok 4, Twoja decyzja D5): każda sesja w swojej karcie; nic już nie nachodzi na sąsiednią
  sesję; „Wyczyść dane…” pyta w okienku; uwagi (bez K1-DANE, bez wirtualizacji) — jedna spokojna linia nad listą.
- Pamięć sesji z wiersza `K1_STREFY` (1536 MiB) to teraz „pamięć na start” — ile musi być wolne, żeby sesja ruszyła;
  procesy VMM stref mają `oom_score_adj` (w tle 800, w oknie 500) — przy braku pamięci jądro zatrzyma przeglądarkę, nie
  usługę K1.

### Naprawione
- **Przeglądarka wysypywała się przy przełączaniu kart, po minimalizacji i „później, bez minimalizacji”** (krok 1, D1):
  przyczyną był nasz kompozytor w strefie, nie pamięć — odrzucał podgląd karty pokazany drugi raz (Firefox tworzy jego
  część od nowa na tym samym miejscu, protokół Wayland na to pozwala) i odłączał przeglądarkę.
- **Przewijanie po otwarciu nowej karty** (krok 5, D6 — IdeaPad): po nowej karcie dwa palce (i precyzyjne kółko) nie
  przewijały już niczego, w żadnej karcie, aż do zamknięcia przeglądarki. Kompozytor strefy nigdy nie kończył gestu palców
  (`axis_stop`), a Firefox wysyła dalsze ruchy niezakończonego gestu do strony, na której się zaczął; precyzyjne kółko
  (części ząbka) brał za palce. Teraz VMM oznacza ramki z touchpada, a gest kończy się po 200 ms bez ruchu palców albo od
  razu przed kółkiem, kliknięciem i zmianą okna pod kursorem.
- Statystyki pamięci gościa nie docierały do K1 (krok 3): sterownik balonu powiadamia przed `DRIVER_OK`, VMM to
  odrzucał.

### Porządki
- Martwego kodu stałej pamięci strefy nie ma — kolumna pamięci `K1_STREFY` zostaje jako „pamięć na start” (słownik);
  liczby w dokumentach (1,5 GB, „na 4 GB jedna sesja”) poprawione w README, `strefa.md` i słowniku.
- **Test przeglądarki usunięty z CI** (Twoja decyzja z 2026-11-17): `test-przegladarka.sh` przez wiele dni padał na
  zachowaniu Firefoksa w CI, a ważne błędy (przewijanie po nowej karcie) i tak znajdowałeś Ty na sprzęcie. Przeglądarkę
  sprawdzasz Ty (lista w DO_ZROBIENIA), Claude odtwarza zgłoszenia na stanowisku. W CI zostają części K1 pod nią: strefa
  (`test-strefa`), łącznik i reguła sieci D3 (`test_lacznik`), dysk sesji, balon, kompozytor (`test_palce`). Limity CI
  wracają do 80 min (zadanie) i 45 min (krok testów); znika mechanizm „testów informacyjnych” (`testy.sh`) i etykieta
  `testy-przegladarka`.
- Tymczasowe: test przeglądarki informacyjny — zamknięty (test usunięty); K1U1–K1U3 — 0.11.0 bez zmian. Nowych rozwiązań
  tymczasowych 0.10.1 nie dodało.
- Nowe narzędzie: **stanowisko na hoście** (`scripts/stanowisko.sh`) — kompozytor strefy i prawdziwy Firefox z obrazu
  strefy bez maszyny wirtualnej; tak znaleziono oba błędy przeglądarki tej wersji.
- Blisko limitu 600 wierszy (dzielić przed następną rozbudową): `test_odcinki.c` (599), `k1_rysuj.c` (542), `uslugi.c`
  (540), `t_wifi_ap.c` (528), `powierzchnia.c` (527).

### Bezpieczeństwo
- Balon: każdy numer strony, blok i statystyka od gościa sprawdzane przed dotknięciem pamięci (`test_balon`,
  `fuzz_balon`); złośliwy gość kończy tylko swoją strefę; twarda granica pamięci w klatce VMM (memory.max). Nowe komunikaty
  stref sprawdzane pole po polu (`test_komunikat`, fuzzing). RYZYKA R88–R90.

## [0.10.0] — 2026-11-16

**Strefy: pierwsza strefa i przeglądarka** (plan 0.10.0, kroki 1–5; ADR 0042, 0043) — pierwsza wersja tematu 0.10 Strefy.
W menu Start jest **Przeglądarka**: prawdziwy Firefox w osobnej maszynie (strefie) z własnym jądrem Linux. Strona, która
zaatakuje przeglądarkę, zostaje w tej maszynie — nie dosięgnie Twoich plików, haseł ani reszty K1. Na laptopie trzeba raz
włączyć wirtualizację w BIOS-ie (README, punkt 8).

### Dodane
- **Pierwsza strefa** (krok 1, ADR 0042): jądro K1 z KVM; nasz mały program „strefa” (VMM) prowadzi jedną mikro-maszynę
  w swojej klatce — pamięć maszyny nigdy nie jest wykonywalna w K1, każda prośba od maszyny sprawdzana pole po polu
  (złośliwa maszyna kończy tylko siebie). Drugie, małe jądro Linux dla wnętrza stref (`jadro/strefa.config`). Bez
  wirtualizacji (albo wyłączonej w BIOS-ie) — okienko „nie może ruszyć” z radą, co włączyć.
- **Przeglądarka w strefie** (krok 2, ADR 0043): Firefox ESR z Debiana 13 w obrazie strefy (pakiety sprawdzane podpisem
  Debiana i sumami), nasz kompozytor Wayland w strefie; do internetu strefa nie ma karty sieciowej — prosi K1 o każde
  połączenie, a K1 łączy ją tylko z internetem i routerem, nigdy z resztą sieci domowej (Twoja decyzja D3). W logach
  tylko liczby — nigdy adresy stron ani wpisany tekst. Trzy sesje (D2): **Ogólna**, **Bank**, **Prywatna**, każda z własnym
  kolorem ramki.
- **Zaufana ramka** (krok 3, Twój szkic A): okno strefy ma pasek tytułu K1 w kolorze sesji („Przeglądarka · Ogólna ·
  osobna maszyna”, minimalizuj, zamknij; przesuwanie za pasek) i obwódkę — rysuje je K1, strona nie może ich podrobić
  ani zasłonić. Firefox nie rysuje już własnych przycisków okna.
- **Przewijanie** (krok 4, Twoja decyzja D8): kółko myszy i dwa palce na touchpadzie przewijają okno pod kursorem —
  strony w przeglądarce, listy w Plikach, Notatkach, Wyborze pliku, powiększone zdjęcie w Podglądzie; na touchpadzie
  kierunek naturalny. Touchpad ma teraz własny tryb (protokoły Synaptics, Elantech, ALPS): ruch, stuknięcia, przycisk.
- **Sesje, które pamiętają** (krok 5, ADR 0042 T11): Ogólna i Bank pamiętają zalogowania, zakładki i historię — po
  zamknięciu i po ponownym starcie K1; Prywatna zapomina wszystko. Dane sesji leżą zaszyfrowane w skarbcu (dysk strefy:
  2 GiB w kawałkach po 64 KiB); bez K1-DANE (dane tylko w pamięci) każda sesja zapomina. Inne sesje i aplikacje nie
  widzą danych sesji.
- **Ustawienia → Przeglądarka** (krok 5): przy każdej sesji kolor, nazwa, „Pamięta | Zapomina” (od następnego otwarcia),
  **„Zmień nazwę”**, **„Wyczyść dane”** (pyta dwa razy); na dole stan wirtualizacji. **Menu sesji**: prawy przycisk na
  Przeglądarce w menu Start — Ogólna, Bank, Prywatna. **Ustawienia → Miejsce**: część „Strefy” (ile zajmują sesje).

### Zmienione
- **Pamięć strefy** (krok 3): przeglądarka nie znika przy ciężkich stronach — pamięć kompresowana w strefie, strony
  oddają pamięć przed samą przeglądarką; gdy strefa skończy się sama, okienko mówi dlaczego (pamięć strefy, pamięć
  komputera, awaria, błąd). Pamięć według potrzeby (balon) — 0.10.1.
- Szybszy start przeglądarki (krok 3): obraz strefy rozpakowywany wprost do pamięci podręcznej i na każdym procesorze.
- Okno Ustawień: 680 × 480 (jedna sekcja więcej).
- Ustawienia ekranu zapisane dawno temu (formaty K1U1–K1U3) są przy pierwszym odczycie zapisywane od nowa (K1U4).

### Naprawione (przed wydaniem, CI)
- **Zamknięcie przeglądarki zapisuje sesję:** K1 zamyka okno Firefoksa jak krzyżyk (kompozytor strefy wysyła
  `xdg_toplevel.close`), Firefox kończy się sam i zapisuje ciasteczka i zalogowania; wcześniej dostawał SIGTERM, kończył się
  w 0,3 s bez zapisu i sesja zapominała ostatnie zmiany. SIGTERM dopiero po 15 s; bez pytań „Zamknąć karty?”.
- **Szybkie kręcenie kółkiem** przewija stronę do końca: kompozytor strefy wysyła każdy ząbek osobno (GTK robi z kilku
  ząbków w jednej porcji jeden krok).
- **Testy:** krótsze limity (test przeglądarki 35 min, krok testów 45 min — Twoja decyzja); test przeglądarki jest
  **informacyjny** — idzie zawsze, ale jego błąd nie blokuje wydania (Twoja decyzja; przeglądarkę sprawdzasz na sprzęcie;
  do 0.10.1).

### Porządki (zasada czytelności 2)
- **Usunięty format 1 skarbca** (0.5.6–0.8.1) i jego przejście na format 2 (termin 0.10.0, Twoja decyzja D7): `obszary.c`,
  `przejscie.c`, komunikat `START_POTWIERDZONY`, kompaktowanie przy otwarciu, `test_magazyn`, `test_przejscie`, tryby
  formatu 1 w `fuzz_magazyn`. Nośnik z takim magazynem K1 rozpoznaje i zostawia nietknięty (dane tylko w pamięci, rada:
  uruchom go raz z 0.9.1). Ogólne sprawdzenia (pusta partycja, obce dane, hasło) przeszły do `test_odcinki`.
- `test-skarbiec.sh`: zamiast scenariuszy formatu 1 — „stary” (magazyn formatu 1 nietknięty) i „wersje” (20 000 wersji
  jednego obiektu otwierają się od razu).
- Tymczasowe: format 1 skarbca — usunięty (termin dotrzymany); nowy termin: odczyt ustawień ekranu K1U1–K1U3 — 0.11.0.
  Komentarzy „M8/M11” w kodzie i konfiguracji jądra już nie ma.
- Statusy ADR 0020 (częściowo wdrożony), 0024 i 0025 (niewdrożone), 0042 (uzupełnienie T11); RYZYKA R26, R50, R54 —
  ramka K1 dla okien stref (R80), nowe R87 (dysk sesji).
- Blisko limitu 600 wierszy (dzielić przed następną rozbudową): `test_odcinki.c` (599), `k1_rysuj.c` (542),
  `uslugi.c` (534).

### Bezpieczeństwo
- Nowe ryzyka: R77–R80 (VMM, KVM, ataki procesora, okno udające K1), R81–R85 (obraz przeglądarki, stary Firefox,
  DNS bez szyfrowania, łącznik w VMM, router dostępny ze strefy), R86 (touchpad), R87 (dysk sesji).
- Nazwy sesji to tekst użytkownika — nigdy w logu (menu loguje „sesja N”).

## [0.9.1] — 2026-11-14

**Sieć: Wi-Fi** (plan 0.9.1, kroki 1–2; ADR 0041) — druga wersja tematu 0.9 Sieć. Laptop łączy się z domowym Wi-Fi:
widzisz listę sieci, wybierasz swoją, wpisujesz hasło raz — potem K1 łączy się sam przy każdym starcie.

### Dodane
- **Karta Wi-Fi i lista sieci** (krok 1): sterowniki kart Wi-Fi z jądra Linux (Intel, Qualcomm Atheros, Realtek,
  MediaTek — w laptopach na PCIe) i pliki od producentów kart wbudowane w podpisany K1 (Twoja decyzja D3: nikt nie może
  ich podmienić). Ustawienia → Sieć mają zakładki „Wi-Fi | Karty sieciowe” (Twój wybór D1, szkic A): przełącznik Wi-Fi
  i listę sieci w pobliżu (siła sygnału, nazwa, kłódka przy sieci z hasłem). Nazwy z powietrza są pokazywane
  bezpiecznie („?” zamiast znaków sterujących i złych bajtów) i nigdy nie trafiają do logu. Na górnym pasku — ikona
  „Wachlarz” (Twój wybór D1, szkic B). Gdy przełącznik w obudowie wyłącza radio — Ustawienia to mówią.
- **Łączenie z siecią z hasłem** (krok 2, Twoja decyzja D2): kliknięcie sieci rozwija jej wiersz z polem hasła
  i „Połącz”; złe hasło — „Złe hasło — spróbuj jeszcze raz.” pod polem. Działa z sieciami domowymi WPA2 i mieszanymi
  WPA2/WPA3 (tak działa większość routerów) oraz z sieciami otwartymi (kawiarnia, hotel) z napisem, że są
  niezabezpieczone. Logowanie (wymiana czterech wiadomości z routerem) robi usługa siec w swojej klatce; klucze do jądra
  wpisuje nadzorca. Hasło idzie z Ustawień prosto do usługi siec — nawet nadzorca go nie widzi — i nie jest nigdzie
  zapisywane: K1 pamięta tylko liczony z niego klucz sieci.
- **Zapamiętane sieci** (Twoja decyzja D4): sieć, która przyjęła hasło, jest zapamiętana w skarbcu (zaszyfrowana);
  przy starcie i po utracie zasięgu K1 łączy się sam z najsilniejszą zapamiętaną. „Zapomnij tę sieć” w Ustawieniach.
  Hasło nigdy nie jest pokazywane z powrotem. Przełącznik Wi-Fi z Ustawień też jest zapamiętany.
- **Ikona z siłą sygnału**: łączę — wachlarz z pomarańczową kropką; połączono — wypełniona część wachlarza to siła
  sygnału (1–4 kreski). Kabel i Wi-Fi naraz — używany jest kabel. Zegar z internetu działa też przez Wi-Fi.

### Zmienione
- **Jądro kompresowane zstd** (krok 1): z plikami kart Wi-Fi jądro z gzip miałoby 20 MB; zstd: 12,5 MB.
- Usługa siec ma też kanał do skarbca (zapamiętane sieci), a Ustawienia — kanał prosto do usługi siec (hasło Wi-Fi).
- Pliki kart Wi-Fi pobierane przy budowie także z lustra linux-firmware na gitlab.com, gdy kernel.org nie odpowiada
  (suma każdego pliku i tak jest sprawdzana).

### Bezpieczeństwo
- Kryptografia WPA2 (SHA-1, HMAC, PBKDF2, PRF 802.11, AES z zawijaniem klucza) — małe własne pliki sprawdzone
  oficjalnymi wektorami testowymi; każda ramka logowania sprawdzana pole po polu (test i fuzzing); ochrona przed
  powtórzeniem ramek i ponownym wpisaniem tego samego klucza (KRACK); router musi potwierdzić te same zabezpieczenia,
  które ogłosił (bez obniżania). Nowe ryzyka opisane: R74–R76.

## [0.9.0] — 2026-11-13

**Sieć: kabel** (plan 0.9.0, kroki 1–9 — w historii git; ADR 0038, 0039) — pierwsza wersja tematu 0.9 Sieć. K1 łączy
się z routerem kablem, pokazuje stan połączenia i sam ustawia zegar z internetu. Żadna aplikacja nie dostaje jeszcze
internetu (przyjdzie z przeglądarką w strefie, 0.10). Wi-Fi — w 0.9.1.

### Dodane
- **Kabel sieciowy i adres od routera.** Po włożeniu kabla K1 sam dostaje adres od routera (DHCP); po wyjęciu — oddaje
  go. Sterowniki kart z jądra Linux, włączone szeroko (Twoja decyzja D0, T6): karty przewodowe Intel (X230), Realtek
  (IdeaPad 110-15ISK), Broadcom, Qualcomm Atheros, Marvell, NVIDIA, JMicron, VIA, karty maszyn wirtualnych (QEMU,
  VMware, VirtualBox) i popularne przejściówki USB–Ethernet. Adres pobiera nowa usługa **siec** w swojej klatce (bez
  żadnych uprawnień: każdy pakiet z sieci sprawdzany pole po polu), a ustawia go nadzorca po sprawdzeniu.
- **Nic w K1 nie odpowiada z zewnątrz** (ADR 0039): bez adresu K1 wysyła do sieci tylko pytanie o adres; z adresem
  odpowiada wyłącznie „kto ma ten adres” (ARP) i „zamknięte” na próbę połączenia — żadnego pinga, żadnego ICMP, nic nie
  nasłuchuje. Tylko IPv4 (bez IPv6 i zapory). Żadna klatka nie może otworzyć gniazda sieciowego.
- **Ikona sieci na górnym pasku** (Twój wybór D1: wtyczka sieciowa): jasna wtyczka z kablem — połączono; przygaszona
  z pomarańczową kropką — łączę (czekam na adres od routera); przygaszona z krzyżykiem — kabel odłączony; przekreślona —
  brak obsługiwanej karty.
- **Ustawienia → Sieć** (Twój wybór D1: kafelki kart): kafelek na każdą kartę sieciową komputera z plakietką stanu
  (Połączono / Łączę… / Brak kabla / Brak sterownika / Później — Wi-Fi). Pod kafelkami — Twoja prośba — szczegóły karty
  przewodowej: sterownik z jądra, prędkość i dupleks łącza, adres karty (MAC), a gdy połączono — adres w sieci, maska
  i router. Karta, której K1 nie zna, pokazuje swój identyfikator (np. `10ec:8136`) — dopisanie sterownika to jedna
  linia w następnej wersji. Widać też kartę Wi-Fi (z identyfikatorem), choć Wi-Fi przyjdzie w 0.9.1.
- **Zegar z internetu** (T5): po połączeniu K1 pyta naraz do 6 serwerów czasu (te od routera i stałe adresy dwóch firm)
  i ustawia zegar tylko wtedy, gdy większość odpowiedzi się zgadza; nigdy na czas sprzed tego wydania. Ustawia też
  zegar sprzętowy — następny start ma dobrą godzinę bez sieci (dotąd po słabej bateryjce zegara bywał rok 2000).
  Skok zegara nie opróżnia Kosza: czas plików w Koszu przesuwa się o tyle samo. Ustawienia → Data i czas mówią, kiedy
  zegar przyszedł z internetu.
- **Aktualizacja wymienia też pliki startowe** (shim, GRUB, `grub.cfg` — ADR 0038): przy aktualizacji przez Instalator
  nowe pliki zapisują się obok starych, są sprawdzane z podpisanym manifestem i przełączane jednym zapisem; zanik prądu
  w środku zostawia stare albo nowe — komputer zawsze startuje.

### Zmienione
- **Nowsza biblioteka C** — własny, przypięty musl 1.2.5 z poprawką bezpieczeństwa CVE-2025-26519 i dodatkowa ochrona
  przed przepełnieniem pamięci w całym systemie (`_FORTIFY_SOURCE=3`, fortify-headers): przepełnienie zatrzymuje
  program zamiast cicho nadpisać pamięć.
- Odpowiedzi skarbca nigdy nie przepadają (dotąd przy bardzo dużym imporcie Pliki mogły „zapomnieć” o zmianie, R64).
- Aktualizacja wprost z 0.6.2–0.6.3 nie jest już możliwa (najpierw na 0.8.x); notatki z pendrive'a nagranego
  0.5.6–0.7.6 nie przechodzą do Plików (Twoja decyzja z 2026-11-12).

### Porządki
- Usługa systemowa to jeden wiersz w tabeli `K1_SYSTEM` (dotąd cztery miejsca w kodzie) — nowa usługa siec to wiersz,
  klatka i program; tabela właścicieli kanałów nie może się już zapełnić.
- Sprawdzanie komunikatów podzielone według kanałów (`k1_kom_*.c`), sieć dostała własny plik.
- Usunięte rozwiązania tymczasowe z terminem 0.9.0: manifest formatu 1 i przenoszenie starych notatek.
- Nadzorca: zapytania do jądra o sieć w osobnym pliku (`siec_jadro.c`), stan dla ekranu (`siec_stan.c`), zegar
  (`siec_zegar.c`).
- Pliki testów też mają limit 600 wierszy (zasada czytelności 1, `AGENTS.md`): `test_dokumenty.c` (1370 wierszy) i
  `test_komunikat.c` (789) podzielone na części, te same sprawdzenia. Ekran: `okna.c` podzielony (powierzchnie
  w `powierzchnie.c`) przed ramką stref w 0.10.
- CI (Twoja decyzja D4): przebiegi PR-ów (etykieta „testy”) budują jądro przyrostowo z kopii, którą zostawia build na
  `main` — ok. 7 min krócej; wydanie zawsze budowane od zera.
- „Tymczasowe”: terminy 0.9.0 załatwione; format 1 skarbca — dalej 0.10.0.

### Testy
- `test-siec.sh` (QEMU z „siecią testową” w Pythonie): z zewnątrz nic nie odpowiada, adres od routera, 5 złych
  odpowiedzi DHCP odrzuconych, kabel wyjęty i włożony; ikona w trzech stanach i Ustawienia → Sieć w jasnym i ciemnym
  motywie (zrzuty); zegar z 2001 r.: serwery milczą, odpowiadają źle, nie zgadzają się, podają czas sprzed wydania —
  zegar bez zmian; dobry czas — zegar ustawiony, Kosz przesunięty.
- Na hoście: `test_siec`, `test_czas` (pakiety czasu, zgodność źródeł), Kosz po przestawieniu zegara; fuzzing
  `fuzz_dhcp`, `fuzz_sntp`; w systemie: żadna klatka nie ustawi zegara ani adresu, nie otworzy gniazda sieciowego.

## [0.8.7] — 2026-11-13

### Zmienione
- **Zasoby: pobór mocy reaguje szybciej** (Twoja uwaga z X230 po 0.8.6: liczba spadała bardzo powoli). Nadzorca
  wygładza odczyt słabiej — bierze połowę każdego nowego odczytu zamiast 30 % — więc liczba dogania zmianę w ok.
  15 s zamiast 35 s. Może przez to trochę bardziej skakać; tak samo szacowany czas do rozładowania. Powolny spadek
  przez minutę i dłużej to już sama bateria (jej kontroler też uśrednia pobór) — tego K1 nie przyspieszy.

## [0.8.6] — 2026-11-13

**Audyt przed 0.9 Sieć** (`docs/archiwum/AUDYT-0.8.5.txt`) — przegląd porządku w kodzie, błędów i sprawności całego
systemu. Kod okazał się czysty (analizatory nie znalazły błędów pamięci), ale audyt znalazł jedną dużą rzecz dla
laptopa i kilka mniejszych; wszystko, co dało się poprawić bezpiecznie, jest w tej wersji, reszta w planie 0.9.

### Zmienione
- **Laptop: procesor oszczędza energię.** Do 0.8.5 jądro K1 nie miało żadnego zarządzania energią procesora:
  procesor budził się 250 razy na sekundę także bez pracy, spał tylko najpłycej i nie zmieniał częstotliwości —
  laptop grzał się i zużywał baterię bez potrzeby, a bez „turbo” był wolniejszy przy cięższej pracy. Teraz bezczynny
  procesor naprawdę śpi (głębokie stany uśpienia), częstotliwość dopasowuje się do pracy, a przy temperaturze
  krytycznej jądro samo wyłącza komputer. Na X230: Zasoby → pobór mocy na baterii powinien wyraźnie spaść
  (lista testów w `docs/DO_ZROBIENIA.txt`). W maszynie wirtualnej tego nie widać.
- **Płynniejszy obraz na X230:** ekran bez „page flip” (X230) składał ok. 50 klatek na sekundę zamiast 60 — zegar
  jądra miał dokładność 4 ms. Teraz zegary są dokładne.
- **Wyłączanie i restart szybsze o 3 sekundy:** po zatrzymaniu ostatniego programu system zawsze czekał jeszcze
  pełne 3 s (zmierzone: 3,1 s → 0,1 s).
- **Wyjęty pendrive znika z Plików od razu** (dotąd nawet po 10 s — nadzorca nie słuchał zdarzenia „usunięto dysk”).

### Naprawione
- **Historia baterii z ostatnich minut przed wyłączeniem** nie zapisywała się: przy wyłączaniu wszystkie programy
  dostawały sygnał naraz i magazyn danych znikał, zanim nadzorca zdążył zapisać (a nadzorca czekał na niego 1,5 s
  w pętli). Teraz najpierw kończy nadzorca — zapisuje i zatrzymuje usługi — potem reszta.
- Skarbiec albo ekran, których nie udało się uruchomić (brak pamięci na nowy proces), są uruchamiane ponownie po
  sekundzie; dotąd zostawały zatrzymane.

### Porządki
- Nadzorca: pięć funkcji i zmiennych używanych w jednym pliku nie jest już widocznych w pozostałych; tabela kanałów
  pętli bez zbędnej kolumny; pełna tabela właścicieli kanałów jest zgłaszana w logu (dotąd po cichu). Drobne:
  zbędna deklaracja w skarbcu, zmienna Plików bez deklaracji, wcięcie w składaniu obrazu.
- Dokumenty: kolejność wyłączania (`init.md`), zdarzenia dysków i historia baterii (`nadzorca.md`), zarządzanie
  energią (`jadro.md`), RYZYKA R63–R64; skill i `CLAUDE.md` — prawdziwy czas CI (ok. 30 min) i to, że w kontenerze
  Claude testy w tle idą tylko podczas czekania poleceniem.
- Do planu 0.9 (`docs/DO_ZROBIENIA.txt`): podział `k1_komunikat.c` przed komunikatami sieci, pliki testów ponad
  limit 600 wierszy, kolejka odpowiedzi skarbca, pamięć podręczna jądra w CI.

### Testy
- **Fuzzery biegną równolegle** (`make -j… fuzz`): 13 fuzzerów po kolei zajmowało 4–6,5 min każdego przebiegu testów,
  teraz ok. 1,5–2 min.
- `test-m5`: Start → Wyłącz na „laptopie” (bateria, dane na pendrivie): „historia baterii zapisana przed
  wyłączeniem”, potem wyłączenie (na 0.8.5 tego zapisu nie było).
- Analiza statyczna całego kodu K1 (gcc `-fanalyzer`, clang `--analyze`, dodatkowe ostrzeżenia): bez błędów —
  szczegóły w raporcie audytu.

## [0.8.5] — 2026-11-13

**Podgląd i Miejsce** (`docs/archiwum/PLAN-0.8.5.txt`, kroki 1–5) — poprawki tematu 0.8 Pliki przed 0.9 Sieć: okna
już nie zamarzają, Podgląd jest płynny, nowa sekcja Ustawienia → Miejsce, Secure Boot da się włączyć na K1
zainstalowanym na dysku. Wersji 0.8.4 nie ma (numer wybrałeś sam).

### Ważne przy aktualizacji
- **Secure Boot na zainstalowanym K1:** w 0.8.3 Instalator uruchomiony z dysku nie pokazywał linku „Secure Boot…”
  (Twoje zgłoszenie z X230). Zaktualizuj K1 na dysku do 0.8.5 przez Instalator z pendrive'a (dane zostają), uruchom
  z dysku bez pendrive'a → Instalator → **Secure Boot…** → **Przygotuj klucz K1** → dalej jak w README („Secure Boot
  z kluczem K1”).

### Dodane
- **Ustawienia → Miejsce** (krok 3; Twoje decyzje: szkic B, podział według folderów, miejsce na pliki): ile miejsca
  jest wolne, szeroki pasek w kolorach motywu (największy folder w kolorze akcentu) i legenda — Pulpit, Dokumenty,
  Obrazy, Muzyka, Wideo, Pobrane (z Twoimi folderami w środku), Kosz, „System”; „Zapas systemu” (zawsze wolny, żeby
  dało się zapisać zmiany) i liczba plików. Sekcja wie, czy K1 działa z pendrive'a, z dysku czy tylko z pamięci;
  przycisk **„Użyj całego pendrive'a na dane”** przeniósł się tu z Informacji. Odświeża się sama, gdy jest otwarta.

### Zmienione
- **Podgląd płynny** (krok 2): przybliżanie (− +, także szybkie klikanie) i przesuwanie przybliżonego zdjęcia liczą się
  raz na klatkę i w ruchu szybko — z mniejszej kopii zdjęcia (ok. 12 % dawnego czasu klatki); gdy zdjęcie stanie,
  w ułamku sekundy rysuje się ostro jak dotąd. Kolejne „+” w trakcie animacji idzie od ostatniego wybranego poziomu,
  wolna klatka przeskakuje zamiast wydłużać animację.
- **Każde okno rysuje tło szybciej** (krok 2): zaokrąglone prostokąty (tło okien, przyciski, karty) nie liczą już
  odległości od brzegu dla każdego piksela wnętrza — tło okna 8 × szybciej, wygląd co do piksela ten sam.
- Ustawienia → Informacje: wersja, ekran, jądro, gdzie są dane (miejsce na pliki — w sekcji Miejsce).

### Naprawione
- **Okna już nie zamarzają** (krok 1): gdy aplikacja chwilę liczyła, a mysz wysyłała dużo ruchów, ekran po cichu
  wyrzucał to, czego aplikacja nie zdążyła odebrać — także „klatka pokazana” (okno już nigdy się nie odświeżało:
  „zawieszony Podgląd”) i „puszczono przycisk” (przeciąganie się „przyklejało”). Teraz zaległe zdarzenia czekają
  w ekranie, ruchy myszy łączą się w jeden, a te ważne nigdy nie przepadają.
- **Secure Boot: „Przygotuj klucz K1”** (krok 4): Instalator pokazuje link „Secure Boot…” także wtedy, gdy K1 działa
  z dysku, na którym go zainstalowano (w 0.8.3 tylko przy starcie z pendrive'a). Klucz przygotowany wcześniej, a nie
  potwierdzony w MokManagerze: okno pokazuje „Uruchom ponownie” i mówi, co zrobić.

### Porządki
- Ustawienia: miejsce na pliki liczy i opisuje jeden moduł (`miejsce.c`) — Informacje nie mają już własnego paska ani
  własnego zapisu rozmiarów (rozmiary jak w Plikach: `k1_rozmiar_tekst`).
- Nowe części jako wiersze tabel: części miejsca (`K1_CZESCI` — z tabeli folderów + Kosz + rezerwa), sekcje Ustawień.
- „Tymczasowe” przejrzane, terminy bez zmian: stary manifest formatu 1 i przeniesienie starych notatek — 0.9.0;
  format 1 skarbca i przejście na format 2 — 0.10.0; GRUB — 0.12. Nic nowego tymczasowego w 0.8.5.

### Testy
- W systemie: klient, który przez chwilę nie odbiera (1500 ruchów myszy), dostaje potem „klatkę” i „puszczenie”, inny
  klient rysuje normalnie (na 0.8.3 test był czerwony: 278 odebranych, reszta przepadła). Host: kolejka zaległych
  ekranu, szybka droga Podglądu, zaokrąglone prostokąty bit w bit jak dawniej, podział miejsca (też NEGATYWNY: inna
  aplikacja nie dostaje liczb), nowe komunikaty skarbca. Pomiar Podglądu na hoście (`pomiar_podgladu`).
- `test-skarbiec`: najdłuższy krok przejścia na format 2 mierzony czasem procesora skarbca (praca kroku); czas zegarowy
  zostaje w logu — w CI wydania krok 0,01 s trwał raz 1,25 s, bo osiem maszyn naraz zapisywało duże obrazy na dysk.
- QEMU: Podgląd — 20 × „+” i 20 × „−” bez czekania i przeciąganie; Miejsce przed i po imporcie z pendrive'a (oba
  motywy), z pendrive'a (przycisk) i z dysku (bez przycisku); Secure Boot — K1 z dysku bez pendrive'a: link widać
  (na 0.8.3 czerwony), z kluczem wydania „Przygotuj klucz K1” zapisuje klucz dla MokManagera.

## [0.8.3] — 2026-11-12

**Pierwsze podpisane wydanie** — bez zmian w działaniu systemu. Od tej wersji wydania są podpisane Twoim kluczem
(sekret `K1_KLUCZ_PODPISU`, dodany 2026-11-12), więc działa Secure Boot z kluczem K1 (README → „Secure Boot z kluczem
K1”), a zainstalowany K1 przyjmuje już tylko wersje podpisane tym kluczem i nie starsze.

### Ważne przy aktualizacji
- Zainstaluj 0.8.3 na dysk albo zaktualizuj nią system przez Instalator (dane zostają). Od tej chwili Twój K1
  przyjmie tylko wydania podpisane Twoim kluczem — sekret trzymaj w menedżerze haseł.
- Secure Boot włączasz sam, raz: Instalator → „Secure Boot…” → „Przygotuj klucz K1” → MokManager → BIOS (README).
  Pendrive'y z wersjami deweloperskimi (do 0.8.2) zawsze wymagają wyłączonego Secure Boot.

### Zmienione
- **Dokumentacja dogoniła stan 0.8.2:** README (co K1 już ma, czego jeszcze nie, pasek, miejsce na starym pendrivie,
  spis dokumentów), pełna tabela klatek wszystkich 17 usług i aplikacji w opisie nadzorcy (limity, deskryptory,
  seccomp — z `profile.c` i `kanaly[]`), mapa repo w `ARCHITEKTURA.md` (brakujące moduły, narzędzia, skrypty,
  workflowy), `TESTY.md` (kroki `test-m1`, spis plików i próby klatek dla każdej usługi, plik 17 MB już nie „za duży”,
  czas testów), opisy komponentów (stare ścieżki testów, limity, nazwy dawnych etapów; gniazda UNIX są — to kanały
  usług), `RYZYKA.md` (stan 0.8.2), spis ADR (0036 wykonany), przewodnik `kons-dev`.

### Testy
- Fuzzing zapisuje „wolne” i błędne przypadki libFuzzera w `build/`, nie w katalogu repozytorium.
- Droga z podpisem sprawdzona przed wydaniem lokalnie z jednorazowym kluczem testowym (`test-m6`: Instalator →
  Secure Boot → MokManager → Secure Boot włączony; `test-m5f`: aktualizacja podpisanej wersji).

## [0.8.2] — 2026-11-12

**Pliki — dopracowanie, część 2** (`docs/archiwum/PLAN-0.8.2.txt`, kroki 1–7; ADR 0036, 0037) — ostatnia wersja 0.8.
Skarbiec na lata (prawie cały pendrive na pliki, szybki start przy dużych danych), pendrive z telefonu w całości,
duże zdjęcia, aplikacja Podgląd, menu pod prawym przyciskiem w Plikach i przeciąganie plików. Następna: 0.9 Sieć.

### Ważne przy aktualizacji
- Dane z 0.5.6–0.8.1 **przechodzą same na nowy format skarbca** — w tle, gdy nowa wersja przepracuje 15 s (pulpit albo
  ekran hasła). Przerwa prądu w dowolnej chwili zostawia stary albo nowy magazyn, oba pełne. Jeśli danych jest więcej,
  niż mieści wolna połowa pendrive'a, przejście czeka (magazyn działa jak dotąd), aż zwolnisz miejsce albo użyjesz
  „Użyj całego pendrive'a na dane”. Po przejściu wersja 0.8.1 tych danych już nie odczyta.

### Dodane
- **Aplikacja Podgląd** (krok 4, ADR 0037; wygląd C — Twój wybór): podwójne kliknięcie zdjęcia w Plikach albo na
  pulpicie otwiera je w osobnym oknie — zdjęcie na całe okno, na dole pływający pasek ‹ „3 z 27” › | − + | Otwórz…,
  Obróć, Ustaw jako tapetę, Do Kosza; w lewym górnym rogu nazwa, wymiary, rozmiar, data i pochodzenie.
  ← / → — poprzednie i następne zdjęcie z folderu (w kolejności Plików); − / + — przybliżanie z lekką animacją,
  przybliżone zdjęcie przesuwa się myszą; R — obrót widoku (pliku nie zmienia); Delete — do Kosza bez pytania (D2);
  Ctrl+O — okno wyboru pliku z samymi zdjęciami. Podgląd jest domyślną aplikacją zdjęć, także po aktualizacji.
  Widzi tylko zdjęcia folderów, z których coś w nim otworzysz — i nic po zamknięciu.
- **Menu pod prawym przyciskiem w Plikach** (krok 5, D7) — to samo co na pulpicie: „Otwórz”, „Otwórz za pomocą ›”
  (obok lista aplikacji), „Zmień nazwę”, „Usuń”; w Koszu „Przywróć”, „Usuń na zawsze”; na pendrivie „Importuj”.
  Na pulpicie dodatkowo „Pokaż w Plikach”, a „Zmień nazwę” otwiera Pliki z gotowym polem nazwy.
- **Zaznaczanie wielu** w Plikach (krok 6): Ctrl+klik, Shift+klik, Ctrl+A; „Usuń”, „Przywróć”, „Usuń na zawsze”,
  „Importuj” i menu działają na wszystkie zaznaczone.
- **Przeciąganie plików** (krok 6, D6, D8) w Plikach — na folder, na miejsce z lewej, na Kosz; z Kosza na folder =
  przywróć tam; z pendrive'a na folder = import tam — i na pulpicie (plik na Kosz albo na folder). Przy kursorze
  karteczka z nazwą i liczbą oraz znak, co się stanie („Przenieś do…”, „Kopiuj do… (import)”, „Nie można…”).
  Przed Koszem pytanie, gdy elementów jest więcej niż jeden albo folder ma coś w środku; zajęta nazwa — „Zachowaj
  oba” / „Pomiń” / „Anuluj”. Przeciąganie między oknami — po 0.8.2, razem ze strefami.
- **Nowe formaty zdjęć** (krok 3): GIF (pierwsza klatka), BMP, WebP (stratny i bezstratny) — miniatura, Podgląd,
  tapeta; import z pendrive'a kładzie je do Obrazów.
- Pulpit: gdy plików na Pulpicie jest więcej niż miejsc, ostatnia wolna komórka pokazuje „Jeszcze n” (podwójne
  kliknięcie otwiera Pliki).

### Zmienione
- **Skarbiec w nowym formacie** (krok 1, ADR 0036): na pliki ok. 90 % K1-DANE zamiast połowy (pendrive 64 MiB
  z 0.8.1: 23 → 50 MiB); start nie zależy od ilości danych (magazyn z filmem 300 MB otwiera się od razu — ekran
  dostaje ustawienia od początku); porządkowanie danych (kompaktowanie) małymi porcjami w przerwach, skarbiec nie
  zastyga; sufit plików 256 GB (było ok. 64 GB).
- **Pendrive z telefonu w całości** (krok 2): działa też „mały” FAT32 (niektóre aparaty i karty); folder z tysiącami
  zdjęć widać cały (do 16 384 elementów), a „Importuj wszystkie” bierze wszystkie (było: pierwsze 1024).
- **Ponowny import nie robi kopii** (D3): już zaimportowane pliki są pomijane — „Pominięto n — już zaimportowane”;
  zaimportowany plik ma datę z pendrive'a, nie datę importu. Import przerywa się z jasnym powodem (brak miejsca, za
  dużo plików).
- **Nazwy plików nie mogą udawać innych**: znaki niewidoczne, zmieniające kierunek tekstu, nietypowe spacje na brzegach
  są zakazane albo poprawiane; „ą” zapisane jako „a” + ogonek (macOS) to to samo co „ą”; „Ä” i „ä” to ta sama nazwa.
  Pliki z takimi nazwami z wcześniejszych wersji dostają poprawioną nazwę przy starcie — nic nie ginie.
- **Duże zdjęcia** (krok 3): miniatura, Podgląd i tapeta do 100 Mpx (tryb 48/50 Mpx w telefonach; było 24) i plików
  do 128 MB (było 16); za duże zdjęcie mówi, ile ma („Zdjęcie ma 150 Mpx — K1 pokazuje do 100 Mpx”).
- **Podwójne kliknięcie zawsze coś robi** (krok 5): tekst → Notatki, zdjęcie → Podgląd, plik bez aplikacji (PDF,
  ZIP) → „K1 nie ma jeszcze aplikacji do tego typu pliku”. W Plikach przycisk „Otwórz” zamiast „Otwórz w…”.
- Ustawienia → Aplikacje pokazują wszystkie typy plików i ich aplikację.
- Panel po prawej w Plikach nazywa się „szczegóły”, zdjęcie w nim — „miniatura” (D1; „Podgląd” to teraz aplikacja).
- Zasoby: najwyżej 14 wierszy usług („… i jeszcze n”) — okno mieści się na ekranie 1024×768.

### Naprawione
- Plik z pendrive'a o nazwie „    .jpg” stawał się ukrytym „.jpg” — teraz „Plik.jpg”; bardzo długa nazwa z długą
  końcówką dostaje „ (2)” zamiast odmowy.
- Pliki zawisały przy zdjęciu, którego treści skarbiec nie oddał.

### Porządki (zasada czytelności 2; koniec tematu 0.8 „Pliki”)
- Wspólne części zamiast kopii: menu pod prawym przyciskiem (`k1_menu` — pulpit i Pliki), strona klienta dekodera
  (`k1_dekodowanie` — Pliki i Podgląd), przeciąganie (`k1_przeciaganie` — jedna tabela reguł), zaznaczanie
  (`k1_zaznaczenie`), nazwy plików (`k1_nazwy.c` — skarbiec i czytnik), kolejność plików (`k1_dok_przed` — Pliki,
  Wybór pliku, Podgląd), limity dekodera w jednej tabeli (`k1_dekoder.h`); tabele klientów dekodera i nadawców
  tapety zamiast warunków w kodzie.
- Podzielone przed rozbudową: skarbiec (`obszary.c`, `odcinki.c`, `spis.c`, `przerwy.c`, `przejscie.c`), Pliki
  (`wskaznik.c`, `menu.c`, `przeciaganie.c`, `zbiorczo.c`), nazwy (`k1_nazwy.c`).
- Usunięte: przycisk „Otwórz w…” z podmianą rzędu przycisków (Pliki), „Otwórz w: Pliki” na pulpicie, Pliki jako
  „aplikacja do otwierania”, nieużywana funkcja przeciągania (`k1_prz_rozmiar`); stare nazwy („podgląd” jako panel
  Plików, „Otwórz w…”) w komentarzach i dokumentach.
- Przegląd „Tymczasowe”: manifest formatu 1 i przeniesienie starych notatek — termin 0.9.0 bez zmian (przed siecią);
  nowy wpis: odczyt i przejście formatu 1 skarbca (`obszary.c`, `przejscie.c`) — do 0.10.0, gdy żaden magazyn
  z 0.5.6–0.8.1 nie zostanie.
- Plan: zamknięty plan 0.8.2 → `docs/archiwum/`; przed usługą sieci w 0.9 — jedna tabela usług systemowych
  (`DO_ZROBIENIA.txt`).

### Testy
- Host: nowe `test_odcinki`, `test_przejscie`, `test_podglad`, `test_menu`, `test_przeciaganie` (każda para
  źródło/cel); `test_dokumenty` (próby T1/2, T1/6, prawa Podglądu i pulpitu), `test_fat` („mały” FAT32, folder 3000
  plików), `test_dekoder` (GIF, BMP, WebP, 48/100 Mpx, odmowy), `test_komunikat`, `test_rozmieszczenie`; przerwa prądu
  po każdym zapisie formatu 2 i przejścia; fuzzing `fuzz_magazyn` (tryby 3–6), `fuzz_dekoder` (nowe formaty);
  `pomiar_magazynu` (ręcznie, wyniki w ADR 0036).
- QEMU: `test-skarbiec` (przejście 64 MiB z 0.8.1 i 190 MiB, treść sprawdzona bajt w bajt), `test-pendrive`
  (mały FAT32 z folderem 1100 zdjęć, ponowny import, 48/100 Mpx, GIF/BMP/WebP, Podgląd, przeciąganie z pendrive'a),
  `test-pliki` (menu, „Otwórz za pomocą”, zaznaczanie wielu, przeciąganie, pytanie przed Koszem, pulpit);
  `k1qemu`: `przeciagnij`, `klik_z`.

## [0.8.1] — 2026-11-11

**Pliki — dopracowanie, część 1** (`docs/PLAN-0.8.1.txt`, kroki 1–4; decyzja właściciela D9: wydanie teraz, reszta —
`docs/PLAN-0.8.2.txt`). Najważniejsze dla danych: nic nie ginie, mieści się dużo więcej, widać, ile miejsca zostało, a stary
pendrive można powiększyć bez utraty danych.

### Dodane
- **Ustawienia → Informacje: miejsce na pliki** (krok 3) — „zajęte X z Y” z paskiem i „Pliki i foldery: n z 16 384”;
  bez K1-DANE: ile zostało w pamięci do wyłączenia.
- **„Użyj całego pendrive'a na dane”** (krok 3, ADR 0033) — pendrive nagrany wersją 0.5.6–0.7.6 (ok. 30 MB na pliki)
  powiększa się jednym kliknięciem w Informacjach; pliki, notatki i ustawienia zostają na miejscu, przerwa prądu w
  trakcie niczego nie psuje (start dokańcza powiększenie).
- **Pliki do 4 GB** (krok 4, ADR 0034) — np. filmy z pendrive'a; plik idzie kawałkami po 1 MB, więc K1 nie potrzebuje
  na to więcej pamięci. Przy dużym pliku stopka pokazuje postęp („120 z 300 MB”); wyjęty w trakcie pendrive nie zostawia
  śmieci.
- Pliki mówią w stopce „Mało miejsca na pliki”, gdy zostało mniej niż 10 %.

### Znane ograniczenie
- Z dużą ilością danych start trwa dłużej (skarbiec sprawdza przy starcie wszystkie dane — ok. 30 s na 1 GB
  z pendrive'a USB 2.0). Pulpit nie czeka: ustawienia i tapeta ze zdjęcia pojawiają się, gdy skarbiec skończy.
  Poprawka formatu skarbca — pierwszy krok 0.8.2 (`docs/PLAN-0.8.2.txt`, RYZYKA R58).

### Zmienione
- **Dużo więcej plików** (krok 2, ADR 0034): do 16 384 plików i folderów (było 1024) i ok. 64 GB plików na dużym
  dysku (było ok. 4 GB).
- **Start nie zwalnia z czasem**: skarbiec porządkuje dziennik (kompaktowanie) przy starcie i w wolnej chwili, zanim
  urośnie.
- **Pełny dysk nie blokuje systemu**: pliki nie zajmą ostatnich 8 MB (albo 2 %) — ustawienia, pulpit, tapeta i historia
  baterii zapisują się dalej („Brak miejsca na pliki”).
- Notatki pokazują do 256 najnowszych notatek (było 64); aplikacja dostaje najwyżej 64 pliki naraz — 65. z komunikatem.
- Plik większy niż 16 MB z pendrive'a nie jest już pomijany; podgląd zdjęcia — dalej do 16 MB.

### Naprawione
- **Uszkodzony wpis pliku kasował plik przy starcie** (krok 1) — teraz skarbiec go naprawia („Odzyskany plik 123”,
  złe znaki nazwy → „_”), a treść zostaje cała; tak samo przy złej generacji treści.
- **Zły zegar komputera opróżniał Kosz** (słaba bateria zegara: rok 2000) albo blokował go na lata — Kosz nic nie usuwa,
  gdy zegar jest wcześniejszy niż czas zbudowania wersji albo najpóźniejszy widziany; daty plików się nie cofają.
- **Notatki mogły zgubić tekst**: notatka nie do odczytu jest tylko do odczytu (nie nadpisze oryginału pustą), a gdy
  jej plik trafi do Kosza w trakcie pisania, tekst zapisuje się jako kopia.
- Folder „Notatki” mnożył się („Notatki (2)”, „(3)”) — K1 pamięta go po numerze.
- Przywrócony plik, którego folder jest w Koszu, ląduje w Dokumentach — Pliki teraz to mówią.

### Testy
- Host: `test_dokumenty` (naprawa wpisów, zegar, 16 384 wpisy, rezerwa, zapis w toku i jego przerwania), nowe
  `test_notatki`, `test_indeks`; `test_magazyn` (65 536 obiektów, długi dziennik, kompaktowanie, **powiększenie z przerwą
  prądu po każdym zapisie**), `test_komunikat`, `test_fat` (odczyt od miejsca); fuzzing `fuzz_dokument`, `fuzz_fat`.
- QEMU: nowy `test-skarbiec.sh` (długi dziennik, pełna K1-DANE, powiększenie pełnego pendrive'a przyciskiem i import
  16 zdjęć 16/16, dokończenie przerwanego powiększenia); `test-pendrive.sh` (plik 17 MB i film 300 MB z pendrive'a).
- Ekran wypisuje linię stanu także po zmianie samej liczby odrzuconych zdarzeń — test złośliwych wiadomości
  (M1) nie zależy już od ruchu kursora.
- CI: instalacja narzędzi nie zależy od jednego serwera z pakietami (11.11 serwery Ubuntu w chmurze GitHuba nie
  odpowiadały i budowa 0.8.1 dwa razy kończyła się po 10 minutach): pomija serwer, który nie odpowiada; gdy nie
  odpowiada żaden serwer Ubuntu — bierze pakiety z ich kopii na kernel.org; gdy i ta zawiedzie — instaluje z kopii
  pakietów zapisanej przez poprzednią udaną budowę (pamięć podręczna GitHuba, nie limit artefaktów).

## [0.8.0] — 2026-11-10

**Pliki** — wspólne pliki i foldery w zaszyfrowanym skarbcu, jak w Windows; zdjęcia z pendrive'a, ich podgląd i tapeta
ze zdjęcia (ADR 0026, 0029–0033). Pięć kroków, jedno wydanie.

### Dodane
- **Aplikacja Pliki** (krok 1): z lewej Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane i Kosz; własne foldery
  w środku, „w górę” i ścieżka, „Nowy folder”, „Zmień nazwę” (F2), „Usuń” (Delete, do Kosza); z prawej szczegóły
  zaznaczonego pliku: typ rozpoznany z zawartości, rozmiar, daty, pochodzenie, początek tekstu.
- **Kosz**: „Przywróć”, „Usuń na zawsze”, „Opróżnij Kosz”; pliki znikają same po 30 dniach. Kosz i pliki z folderu
  Pulpit leżą na pulpicie jako ikony.
- **Otwieranie plików** (krok 2): podwójne kliknięcie otwiera plik w jego domyślnej aplikacji, „Otwórz w…” — w wybranej
  (Pliki i pulpit); domyślne aplikacje w Ustawienia → Aplikacje. Aplikacja dostaje dostęp tylko do tego jednego pliku.
- **Okno „Wybór pliku”** rysowane przez K1: aplikacja, która prosi o plik (Notatki: „Otwórz…”, Ctrl+O), dostaje tylko
  ten, który wskażesz — nigdy listę plików.
- **Notatki na plikach**: każda notatka to plik w Dokumenty › Notatki (nazwa z pierwszego wiersza), usuwana do Kosza;
  notatki z poprzednich wersji przenoszą się same przy pierwszym uruchomieniu.
- **Zdjęcia z pendrive'a** (krok 3): włożony pendrive (FAT12/16/32, np. z telefonu albo aparatu) to miejsce „Pendrive”
  w Plikach — tylko do odczytu; „Importuj” / „Importuj wszystkie”: zdjęcia do Obrazów, reszta do Dokumentów,
  pochodzenie „z pendrive'a”. Pendrive czyta mała usługa `czytnik` w klatce — jądro dalej nie dotyka obcego systemu
  plików. exFAT i NTFS — jeszcze nie (Pliki mówią to wprost).
- **Podgląd zdjęć** (krok 4): zaznaczone zdjęcie PNG albo JPEG widać w Plikach po prawej (obrócone tak, jak trzymałeś
  telefon). Zdjęcie odczytuje usługa `dekoder` — osobny, krótko żyjący proces w klatce dla każdego zdjęcia (biblioteka
  Wuffs, ADR 0032); uszkodzony plik: „Podgląd niedostępny”.
- **Tapeta ze zdjęcia**: pod podglądem przycisk „Ustaw jako tapetę” — zdjęcie wypełnia ekran przy każdej rozdzielczości,
  zostaje po wyłączeniu komputera (zaszyfrowane w skarbcu), a w Ustawienia → Wygląd pojawia się kafelek „Zdjęcie”.
- **Cały pendrive na dane** (krok 5, ADR 0033): K1 zapisany na pendrive większym niż obraz powiększa przy pierwszym
  starcie swoją pustą partycję danych do końca pendrive'a (było 64 MB na zawsze). Pendrive z danymi nie jest ruszany.

### Zmienione
- Skarbiec przechowuje do 4096 obiektów; pliki do 16 MB (w kawałkach po 1 MiB), z nazwą, typem, czasami i pochodzeniem.
- Pulpit zna pliki z folderu Pulpit i Kosz („Otwórz”, „Otwórz w: …”); ułożenie pulpitu w nowym formacie (stary dalej
  czytany).
- Ekran: miejsce na 16 klientów, zdjęcie na tapetę (do 2560×1600); klatka ekranu z większym limitem pamięci.
- Klatka usługi może mieć limit czasu procesora i przestrzeni adresowej (SPEC §7 — dekodery).

### Naprawione
- Obrazek nakładany na okno aplikacji (dziś: miniatura w Plikach) był przezroczysty — prześwitywała przez niego tapeta.

### Porządki (zasada czytelności 2)
- Spis usług systemu w budowie i w obrazie wynika z jednej listy `K1_SYSTEM` (`scripts/uslugi-systemu.sh`) — nowa
  usługa to o dwa miejsca mniej do zmiany.
- Podzielone przed rozbudową: nadzorca (`logi.c`), Pliki (`przyciski.c`), skarbiec (`obiekty.c`, `haslo.c`).
- Kanał Plików do nadzorcy (fd 6) obsługuje prośby o pendrive i o dekoder w jednym miejscu; wspólne skalowanie obrazów
  (`k1_skaluj`) dla dekodera i ekranu.
- Słownik: usunięty podwójny wpis „okno wyboru pliku”; nowe pojęcia: dekoder (usługa), podgląd, tapeta ze zdjęcia.
- Przegląd „Tymczasowe”: manifest formatu 1 zostaje do 0.9.0 — oprócz aktualizacji z 0.6.x sprawdza on przy
  instalacji pliki startowe (shim, GRUB), więc najpierw trzeba to sprawdzanie zastąpić. Przeniesienie starych notatek —
  do 0.9.0 (bez zmian).

### Testy
- Host: `test_dokumenty`, `test_fat`, `test_dekoder` (obrazy z Pillow, uszkodzone, EXIF), `test_instalator`
  (powiększanie GPT, sfdisk czyta wynik), `test_komunikat` (wszystkie nowe komunikaty); fuzzing `fuzz_dokument`,
  `fuzz_fat`, `fuzz_pendrive`, `fuzz_dekoder`.
- QEMU: `test-pliki.sh` (foldery, Kosz, Notatki na plikach, otwieranie, okno wyboru), `test-pendrive.sh` (wirtualny
  pendrive USB ze zdjęciami: import, podgląd JPEG/PNG, uszkodzony JPEG, tapeta na zrzucie i po ponownym starcie, wyjęcie,
  NTFS), `test-m5.sh` (obraz na większym pendrivie: K1-DANE 64 → 576 MiB przy pierwszym starcie, potem bez zmian).

## [0.7.6] — 2026-11-09

Mała aktualizacja przed 0.8.0 (prośba właściciela).

### Dodane
- **Nowa tapeta „Las”** — zieleń mchu i szałwii na ciemnej sośnie, ze złotymi krawędziami arkuszy.
  Wybór: Ustawienia → Wygląd → Tapeta (miniatury w dwóch rzędach, po cztery).

### Zmienione
- **Instalator na zainstalowanym K1:** gdy system działa z dysku komputera i nie ma pendrive'a z nowszą
  wersją, okno pisze tylko „Aktualizacje są możliwe poprzez nośnik USB.” — bez instrukcji krok po kroku.
  Z podłączonym pendrive'em K1 wszystko jak dotąd („Zaktualizuj system”).

### Porządki
- Liczba tapet w jednym miejscu (`K1_TAPETY` w `k1_komunikat.h`): z niej wynika sprawdzanie ustawienia;
  kompilator pilnuje, żeby każda tapeta miała paletę i nazwę.

## [0.7.5] — 2026-11-09

Pulpit jak w Windows, w stylu K1 (ADR 0026, 0028; szkic A „Kafelki” wybrany przez właściciela).
Przed tym — porządki czytelności (decyzja właściciela z 2026-11-09, `AGENTS.md`).

### Dodane
- **Ikony aplikacji na pulpicie** — jasne kafelki w kolorze okien (w ciemnym motywie ciemne), pod nimi
  nazwa. Kliknięcie zaznacza, podwójne kliknięcie otwiera, przeciągnięcie przenosi ikonę na inne miejsce
  (jeśli tam leży inna, zamieniają się miejscami). Na start: wszystkie aplikacje oprócz Instalatora.
- **Menu pod prawym przyciskiem myszy** (w kolorach motywu): na pulpicie — „Ułóż ikony”, „Tapeta i
  wygląd…”; na ikonie — „Otwórz”, „Przypnij do paska” / „Odepnij od paska”, „Usuń z pulpitu”; na
  aplikacji na pasku — jej nazwa, przypnij / odepnij, „Zamknij okno”; na aplikacji w menu Start —
  „Otwórz”, przypnij / odepnij, „Dodaj do pulpitu” / „Usuń z pulpitu”.
- **Przypinanie do dolnego paska** i zmiana kolejności przeciąganiem; uruchomiona aplikacja spoza paska
  widoczna na nim (za kreską), dopóki działa. Pasek ma szerokość według liczby aplikacji.
- **Pulpit pamięta ułożenie** — ikony i pasek wracają po wyłączeniu komputera (zaszyfrowane w skarbcu).
- „Zamknij okno” z paska: aplikacja zamyka się sama, jak przyciskiem × — Notatki najpierw zapisują tekst.

### Zmienione
- Pulpit (gui) dostaje kanał do skarbca — widzi w nim tylko swoje rozmieszczenie (ADR 0028, RYZYKA R27).
- Ekran zna trzeci rodzaj powierzchni: warstwę pulpitu (nad tapetą, pod oknami; tylko dla pulpitu).
- Klatka gui i ekranu hasła: większe limity pamięci — duże ekrany (powyżej 1920×1080) mieściły się
  z trudem albo wcale (ekran hasła na 1920×1200).

### Porządki (zasada czytelności 2)
- **Limity wielkości sprawdzane w testach** (`scripts/sprawdz-czytelnosc.py`): plik ≤ 600 wierszy,
  funkcja ≤ 80; każda usługa i aplikacja ma opis, każdy komunikat — wpis w dokumencie swojego kanału;
  każde rozwiązanie tymczasowe — wpis w planie.
- Podzielone: pulpit (9 małych modułów), Ustawienia (Bateria, Hasło osobno), Notatki (zapis, edycja
  tekstu), Instalator — okno (każdy ekran osobno), skarbiec (nagłówek magazynu osobno); 7 długich funkcji
  (sprawdzanie komunikatów, nadzorca, ekran, bateria, initramfs) podzielonych na mniejsze.
- Dokumentacja: tabela komunikatów ekranu, kanał testowy nadzorcy, rozdział każdej aplikacji;
  nieaktualne opisy katalogu notatek usunięte. Kod formatu 1 manifestu oznaczony jako tymczasowy (0.8.0).

### Testy
- `test-m4`: ikony na zrzucie (jasne i ciemne kafelki), podwójne kliknięcie, menu kontekstowe (zamknij
  okno, ułóż ikony, odepnij, usuń i dodaj do pulpitu, przypnij), przeciąganie na pasku i na pulpicie,
  rozmieszczenie po ponownym starcie. Host: `test_rozmieszczenie`, fuzzing `fuzz_rozmieszczenie`.
  W systemie: warstwa pulpitu i zamykanie cudzych okien od aplikacji — odrzucone.

## [0.7.3] — 2026-11-09

Koniec porządków przed M7 (ADR 0023, `docs/PRZED-M7.md` — usunięty, historia w git). Dla użytkownika
system działa tak samo; zmieniły się kod i testy.

### Naprawione
- **Kliknięcie aplikacji tuż po starcie.** Przez pierwsze 5 sekund od włączenia komputera pulpit
  ignorował kliknięcie w aplikację (w menu Start i na pasku) — uznawał, że „już się uruchamia”.
  Widać to było na szybkim komputerze; wykryły to nowe, szybsze testy w CI.

### Zmienione
- Notatki: pod notatką tylko „Zapisano (zaszyfrowane)” — bez informacji, czy na pendrivie, czy na dysku
  (prośba właściciela; miejsce danych pokazują Ustawienia → Informacje).
- Kod: jedna funkcja czasu od startu (`k1_teraz`, było 5 kopii), jedna do czytania małych plików z
  sysfs (`k1_czytaj_plik`, było 5), wysokość górnego paska w jednym miejscu dla ekranu i pulpitu,
  Instalator rysuje przyciski wspólnym `k1_przycisk`, GPT zapisuje liczby przez `k1_bajty.h`;
  38 funkcji i zmiennych widocznych tylko tam, gdzie są używane (`static`); nieaktualne komentarze
  sprzed M5 poprawione.

### Testy
- Testy w QEMU bez stałych pauz: czekają na odpowiedź monitora, potwierdzenie kliknięcia w logu ekranu
  i na stan ekranu na zrzucie (`docs/TESTY.md`) — wolna maszyna nie daje fałszywych błędów, a testy
  są szybsze. Zrzuty bez pośrednich plików i powłoki (`k1qemu.py`); `zamknij_qemu` w `wspolne.sh`.
- Testy na hoście: bajty bloku układu K1-EFI zgodne z opisem formatu, wartości GPT i teksty baterii
  wpisane wprost zamiast liczone tymi samymi wzorami co kod.

## [0.7.2] — 2026-11-09

Poprawki zgłoszone przez właściciela i część porządków przed M7 (`docs/PRZED-M7.md`).

### Naprawione
- **Zainstalowany K1 nie „udaje” pendrive'a.** Instalator uruchomiony z dysku pisał „K1 działa teraz z
  obrazu .iso… uruchom komputer z pendrive'a”. Teraz mówi „Kons K1 jest zainstalowany” i jak
  zaktualizować system (pendrive z nowszą wersją → „Zaktualizuj system”).
- **„Wyloguj” tylko w zainstalowanym K1.** Uruchomiony z pendrive'a albo z .iso K1 nie pokazuje
  „Wyloguj” w menu Start (nadzorca też odrzuci taką prośbę). Zainstalowany = dane (K1-DANE) na dysku
  komputera.

### Zmienione
- Ustawienia i Zasoby opisują stan baterii tymi samymi słowami („Na baterii”, „Ładowanie”…).
- Kod: jedna funkcja do liczb szesnastkowych, jedna do cofania po znakach UTF-8 (pola tekstowe,
  Notatki), jedno źródło tekstów baterii. Pulpit dostaje od nadzorcy nowy komunikat `SESJA`.

### Testy
- `test-m4`, `test-m5f`: „Wyloguj” jest przy dysku, nie ma przy ISO i pendrivie; Instalator na
  zainstalowanym K1 nie prosi o pendrive. Testy na hoście: brak `grub-editenv`/`sfdisk` to błąd;
  zmiana `k1_test_host.h` przebudowuje testy; `test_efi` potwierdza wersję miejsca A przy
  nieczytelnym grubenv; nowe sprawdzenia `SESJA`, `k1_hex_bajty`, `k1_utf8_poczatek`, Backspace.

## [0.7.1] — 2026-11-08

Porządki w kodzie przed etapem M7 (ADR 0023) i nowa aplikacja.

### Dodane
- **Kalkulator** — działania, procenty, zmiana znaku, pamięć (MC, MR, M+, M−); liczby po polsku
  (przecinek, odstępy co trzy cyfry); obsługa myszą i klawiaturą (też numeryczną).

### Zmienione
- Kod uporządkowany: lista aplikacji i usług jest w jednym miejscu (nowa aplikacja = jeden wiersz +
  katalog); odczyt partycji systemu (wersje A/B) — jeden wspólny dla nadzorcy i Instalatora; wspólne
  kolory, ikona baterii, formatowanie liczb i czasu, przyciski i pola tekstowe; skrypty budowania bez
  powtórzeń. Działanie systemu bez zmian.
- Kolor akcentu na znaku K1 i strzałce myszy wyrównany do palety (#D97757 — był minimalnie inny).

### Naprawione
- Aplikacje odświeżały zegar dwa razy na obieg pętli; Zasoby nie mieściły listy po dodaniu aplikacji.

### Usunięte
- Stare szkice ekranu hasła i nieużywany kod.

### Testy
- Testy w QEMU na jednej wspólnej bibliotece (mniej przypadkowych błędów); testy na hoście z jednym
  wspólnym mechanizmem; nowe: `test_kalkulator`, `test_efi`, scenariusz Kalkulatora w `test-m4`.

## [0.7.0] — 2026-11-08

Etap **M6 — bezpieczne aktualizacje** zakończony: kroki M6c (A/B) i M6d (Secure Boot), ADR 0022.

### Dodane
- **Dwa miejsca na system (A/B):** na dysku są teraz dwie kopie systemu. Aktualizacja zapisuje nową
  wersję **obok** działającej i nic w działającej nie zmienia. Po ponownym uruchomieniu komputer
  próbuje nowej wersji **raz**: gdy wystartuje, K1 sam ją zatwierdza; gdy nie (uszkodzona, przerwany
  zapis, zawieszenie) — przy następnym starcie wraca poprzednia i Instalator mówi „Ostatnia
  aktualizacja nie wystartowała”. Lista dysków w Instalatorze pokazuje też wersję zapasową.
- **Secure Boot z kluczem K1:** cały system to teraz **jeden podpisany plik** (`k1.efi`). Z włączonym
  Secure Boot laptop uruchomi tylko K1 podpisany Twoim kluczem — podmieniony albo uszkodzony system się
  nie uruchomi, a na ekranie pojawi się wyjaśnienie, co zrobić.
- **Instalator → „Secure Boot…”:** stan (włączony/wyłączony, klucz wpisany albo nie) i przycisk
  „Przygotuj klucz K1”. Potem restart, niebieski ekran MokManagera (w ciągu 10 s dowolny klawisz) →
  Enroll MOK → Continue → Yes → hasło **kons** → Reboot, a na koniec w BIOS-ie (F1) Secure Boot → Enabled.
  Działa tylko w wersji podpisanej Twoim kluczem (README → „Klucz podpisu”): ten sam sekret podpisuje
  teraz i system, i Secure Boot — nie trzeba niczego więcej.

### Zmienione
- Obraz na pendrive ma 345 MB (było 113) — mieści dwa miejsca na system; plik do pobrania (`.img.xz`)
  dalej ok. 8 MB. Nagrywasz go jak dotąd (Rufus/balenaEtcher).
- **Aktualizacja do 0.7.0 z zainstalowanego 0.6.x:** jak zawsze — start z dysku, pendrive 0.7.0
  podłączony, Instalator → Zaktualizuj. Ten jeden raz zapisuje się cała partycja systemu (stary
  Instalator nie zna A/B); od 0.7.0 aktualizacje są już A/B. Dane zostają.
- Obraz ISO (do maszyn wirtualnych) też startuje z `k1.efi`.

### Bezpieczeństwo
- Podpisane jądro ma wbudowaną linię poleceń i nie przyjmuje initramfs od programu ładującego
  (sprawdzone: GRUB z podsuniętym obcym initramfs — K1 go zignorował).
- Wersja deweloperska (bez Twojego sekretu) jest podpisana publicznym kluczem testowym — K1 nigdy nie
  proponuje jego wpisania (każdy mógłby nim podpisać fałszywy system).

### Testy
- QEMU `test-m6` (nowy): Secure Boot z kluczem K1 → pulpit; bez klucza i ze zmienionym jednym bajtem
  `k1.efi` → odmowa z wyjaśnieniem; z kluczem wydania cała Twoja droga: „Przygotuj klucz K1” → MokManager
  (Enroll MOK, hasło) → Secure Boot włączony → K1 startuje.
- QEMU `test-m5f`: aktualizacja A/B do wolnego miejsca, potwierdzenie po starcie, wycofanie z
  uszkodzonego nowego miejsca, blokada starszej wersji.
- Host: układ partycji i grubenv (te same bajty co `grub-editenv`), SHA-256 (wektory FIPS), manifest
  formatu 2, nowe komunikaty, fuzzing.

## [0.6.3] — 2026-11-07

Etap **M6 — bezpieczne aktualizacje**, krok M6b (ADR 0021), oraz Twoje uwagi do 0.6.2.

### Dodane
- **System sprawdza sam siebie przy każdym starcie (dm-verity):** przed uruchomieniem pulpitu
  odczytuje cały obraz systemu i porównuje go z odciskiem zapisanym przy budowie. Zmieniony choćby
  jeden bajt (ktoś podmienił pliki na dysku albo nośnik się psuje) — system się nie uruchomi i powie
  dlaczego, zamiast wykonać obcy kod. Sprawdzanie trwa ułamek sekundy (obraz ma ok. 1 MB; w pierwszej
  wersji tej notatki pomyłkowo „30 MB”).
- **Wyloguj** w menu Start (obok „Uruchom ponownie” i „Wyłącz”): zamyka aplikacje i magazyn danych;
  z hasłem wraca ekran hasła (dane znów zamknięte), bez hasła pulpit uruchamia się od nowa.
- **Monitor zasobów → Bateria:** procent, aktualny pobór mocy (np. „pobór 9,4 W”) i szacowany czas:
  „zostało ok. 3 h 20 min” na baterii albo „do pełna ok. 1 h 10 min” przy ładowaniu. Pobór jest
  uśredniany z kilkunastu sekund (bez skakania przy każdym kliknięciu); przy otwartym Monitorze
  odczyt co 5 s.

### Naprawione
- **„System myśli, że jest na pendrivie”:** Notatki, Ustawienia → Informacje i Ustawienia → Hasło
  mówią teraz, gdzie naprawdę leżą dane — „na dysku komputera” albo „na pendrivie K1” (nadzorca wie,
  czy partycja K1-DANE jest na USB). Napis bez magazynu danych wymienia obie możliwości (instalacja
  na dysku albo pendrive z pliku `-usb.img.xz`).

### Testy
- QEMU: obraz bez odcisku, ze złym odciskiem i z jednym zmienionym bajtem → system się zatrzymuje
  z komunikatem; poprawny obraz → „obraz rdzenia sprawdzony (dm-verity…)”. Start z hasłem → Wyloguj →
  ekran hasła → hasło → pulpit. Zasoby: wiersz baterii (w QEMU: „brak baterii”).
- Host: pobór mocy i energia z różnych sterowników (moc wprost, prąd × napięcie, ładunek × napięcie),
  szacunek czasu, uśrednianie, bzdury z firmware'u; nowe komunikaty (`ZASOBY_BATERIA`, `WYLACZ` a=2,
  „dane na USB”).

## [0.6.2] — 2026-11-07

Etap **M6 — bezpieczne aktualizacje**, krok M6a (ADR 0021).

### Dodane
- **Manifest systemu:** na pendrivie i na dysku przy systemie leży „metryczka” — wersja, odcisk
  (skrót) całego systemu i podpis klucza K1.
- **Instalator sprawdza, co instaluje:** zanim cokolwiek zapisze, liczy odcisk systemu z pendrive'a;
  zmieniony albo uszkodzony system odrzuca. Gdy K1 jest zbudowany z kluczem — wymaga też ważnego
  **podpisu** (podrobiony system nie przejdzie).
- **Bez powrotu do starszej wersji:** aktualizacja ze starszego pendrive'a jest zablokowana (starsze
  wersje mogą mieć poprawione już dziury). Ta sama wersja — wolno (naprawa).
- Okno Instalatora pokazuje wersje: „Na dysku: 0.6.1 → z pendrive'a: 0.6.2 (podpisana)”.
- Wydanie na GitHubie mówi, czy jest podpisane. **Do podpisów potrzebny jest jednorazowo Twój klucz**
  (README → „Klucz podpisu”) — do tego czasu wydania są „deweloperskie”.

### Zmienione
- System plików systemu na pendrivie ma 47 MB (ostatni MB partycji to manifest).
- Najbezpieczniej aktualizować **z zainstalowanego K1**: start z dysku, pendrive z nową wersją
  podłączony, Instalator → Zaktualizuj (wtedy sprawdza Twój system, nie kod z pendrive'a).

### Naprawione
- Wydanie 0.6.1 się nie zbudowało: w CI test uznał za brakujący napis „K1: wyłączanie zasilania”, bo
  komunikat jądra wpadł w jego środek na wspólnej konsoli (system działał poprawnie, 126/126 testów
  w systemie). Sprawdzanie logów pomija teraz takie wtrącenia.

### Testy
- Host: manifest (podpis tylko z właściwym kluczem i niezmienionymi bajtami, obcy klucz, złe dane),
  fuzzing. QEMU: pendrive ze zmienionym jednym bajtem systemu → odmowa, dysk nietknięty; dysk z
  nowszą wersją → aktualizacja zablokowana.

## [0.6.1] — 2026-11-07

### Naprawione
- **Instalator: „Nie znaleziono systemu do skopiowania”** (Twój test 0.5.9): gdy K1 działa z pliku
  `.iso` nagranego na pendrive, nie ma skąd skopiować systemu — a Instalator mimo to pokazywał przycisk
  „Zainstaluj”. Teraz zamiast przycisku mówi wprost, co zrobić: pobrać `k1-…-usb.img.xz`, nagrać go
  na pendrive i uruchomić komputer z niego. Osobny komunikat, gdy podłączone są dwa pendrive'y K1.

### Testy
- QEMU: start z obrazu `.iso` z dyskiem SATA → Instalator bez przycisku, nic się nie zaczyna.

## [0.6.0] — 2026-11-07

Etap **„Fundament”** (ADR 0019) — zamyka M5. Mniej widać na ekranie, więcej pod spodem.

### Dodane
- **Historia baterii z poprzednich uruchomień** (Twoja prośba z 0.5.5): system zapisuje przebieg
  naładowania w zaszyfrowanym magazynie (od razu po starcie, potem co 5 minut i przy wyłączaniu).
  Ustawienia → Bateria pokazują pod wykresem ostatnie wcześniejsze uruchomienie: kiedy, jak długo,
  od ilu do ilu procent, z małym wykresem. Trzymamy ostatni tydzień (do 16 uruchomień).
- **System reaguje na urządzenia od razu:** podłączenie myszy, klawiatury, pendrive'a z danymi czy
  zasilacza jądro zgłasza nadzorcy natychmiast (zdarzenia „uevent”), zamiast sprawdzania co 2 sekundy.

### Zmienione
- **Nadzorca podzielony na moduły** (najważniejszy program w systemie): zamiast jednego pliku z ponad
  1500 wierszami — 8 części o jasnych zadaniach (usługi, klatki, urządzenia, skarbiec, instalator,
  bateria i zasoby, kanał testowy, pętla główna). Start systemu to teraz **jawne etapy** (dane →
  stan magazynu → hasło → pulpit), każdy zapisywany w logu.
- Jedno miejsce sprawdzania wiadomości między programami (nadawca, poprawność, deskryptory) zamiast
  kilkunastu kopii tego samego kodu.
- Testy: wspólna biblioteka do testów w emulatorze; z KVM (w CI) testy pulpitu, sprzętu i instalatora
  idą równolegle.

### Testy
- Na hoście: format historii baterii (zapis, odczyt, limity, uszkodzone dane), parser zdarzeń jądra,
  wspólny odbiór wiadomości (prawdziwe gniazda); fuzzing obu nowych parserów.
- QEMU: dwa starty z pendrive'a z baterią — pierwszy zapisuje historię, drugi ją odczytuje.

## [0.5.9] — 2026-11-07

Etap **M5f — instalator** (ADR 0018) i szybsze przesuwanie okien.

### Dodane
- **Instalator** (Twój pomysł): menu Start → Instalator → wybierasz dysk → wpisujesz **USUŃ** → K1
  instaluje się na dysku komputera: system + zaszyfrowane miejsce na dane **na całym dysku** (zamiast
  64 MB na pendrivie). Potem startujesz bez pendrive'a — szybciej, bo dysk SATA jest dużo szybszy od USB.
- **Aktualizacja bez utraty danych:** uruchamiasz nowszą wersję z pendrive'a → Instalator →
  *Zaktualizuj system*. Wymieniany jest tylko system; Twoje dane (ustawienia, notatki, hasło) zostają.
- Bezpieczeństwo instalatora: okno nie ma dostępu do dysków; zapisuje osobny, malutki program w
  ciasnej klatce, który dostaje od nadzorcy tylko dwa urządzenia. Zapis kończy się sprawdzeniem
  (odczyt i porównanie), a tablica partycji jest zapisywana na samym końcu — przerwana instalacja nie
  zostawia pół-działającego systemu. Aktualizacja fizycznie nie dostaje partycji z Twoimi danymi.
- Z podłączonym pendrive'em K1 zainstalowany system i tak używa swoich danych z dysku.

### Zmienione
- **Przesuwanie okien ~10× mniej obciąża procesor** (Twoja uwaga): w X230 (grafika z firmware) ekran
  składał klatkę przy każdym ruchu myszy, bez limitu, i liczył cień okna wolną funkcją dla każdego
  piksela. Teraz: najwyżej 60 klatek na sekundę, cień z gotowej tabeli, stare i nowe miejsce okna
  składane raz. W emulatorze: ~19 ms zamiast ~190 ms na klatkę; przeciąganie jest płynne.
- Instalator ma ikonę dysku w menu Start (na dolnym pasku zostają cztery aplikacje).

### Testy
- QEMU: pendrive + pusty dysk → instalacja → start z samego dysku → nowy magazyn → aktualizacja z
  pendrive'a → dane przetrwały; bez wpisania „USUŃ” nic się nie dzieje; tablica partycji sprawdzona
  przez `sfdisk --verify`. Na hoście: tablica GPT dla dysków 4 GB … 3 TB i sektorów 512/4096 B,
  lista dysków na sztucznym sysfs, nowe komunikaty. Próby klatek nowych programów.

## [0.5.8] — 2026-11-07

Poprawki po Twoim teście 0.5.7 na X230.

### Dodane
- **Logo przy starcie** (Twoja prośba): od ekranu ThinkPada do pulpitu widać tylko znak K1 i napis
  „Kons K1” na czarnym tle — **bez żadnego tekstu** (wyciszone komunikaty jądra „EFI stub…”).
- **Wiele notatek:** z lewej lista notatek (tytuł = pierwsza linia, pod nim druga), przycisk
  **„Nowa notatka”**, kliknięcie otwiera notatkę, **„Usuń notatkę”** z potwierdzeniem. Ostatnio
  zmieniona jest na górze; pusta notatka znika sama po przejściu do innej. Skróty: Ctrl+N (nowa),
  Ctrl+↑/↓ (poprzednia / następna). Do 32 notatek, każda zaszyfrowana osobno. Twoja dotychczasowa
  notatka zostaje jako pierwsza.
- **Ustawienia → Informacje:** gdzie są Twoje dane — „zaszyfrowane na nośniku K1” albo „tylko
  w pamięci”.

### Naprawione
- **„Wymaga nośnika K1” — nie dało się zapisać ani ustawić hasła:** system szuka partycji na dane
  także **po starcie** (przez 2 minuty). Pendrive, który pojawi się później (wolny albo — jak w X230 —
  przełączony przez kontroler USB 3 w trakcie startu), jest teraz używany: usługa danych i pulpit
  uruchamiają się ponownie, już z zapisem.
- **Długi start z pendrive'a bez partycji na dane** (np. gdy na pendrive zapisano plik `.iso`):
  system czekał do 15 s na partycję, której nie było. Teraz czeka tylko, aż pendrive się zgłosi.
- W Ustawieniach → Hasło jaśniejszy opis, gdy brak miejsca na dane: hasło i zapis działają z pendrive'a
  z plikiem `k1-…-usb.img.xz` (nie z `.iso`).

### Testy
- QEMU: pendrive z pustą partycją K1-DANE podłączony po starcie → nowy zaszyfrowany magazyn i pulpit
  od nowa; dwie notatki → wyłączenie → obie wracają (ostatnio zmieniona na górze) → otwarcie drugiej
  → usunięcie z potwierdzeniem; logo widoczne od GRUB-a do pulpitu (sprawdzone na zrzutach).

## [0.5.7] — 2026-11-07

Etap **M5e — hasło i ekran logowania** (ADR 0017).

### Dodane
- **Ustawienia → Hasło:** włączenie hasła (nowe + powtórz), zmiana (obecne + nowe) i wyłączenie
  (obecne). Hasło szyfruje klucz do Twoich danych; zapomniane hasło = nowe konto.
- **Ekran hasła przy włączaniu** (Twój wybór — szkic B „Zegar”): duży zegar i data na tapecie
  „Zmierzch”, przezroczyste pole hasła na dole, bateria i wyłączanie w rogu. Po poprawnym haśle
  pulpit startuje z Twoimi ustawieniami.
- Ochrona przed zgadywaniem: od trzeciej złej próby coraz dłuższa przerwa (5 s, 10 s … do 5 min) —
  pilnuje tego usługa danych, nie ekran hasła.
- Hasło nigdy nie trafia do logu.

### Testy
- QEMU: ustawienie hasła w Ustawieniach → ponowne uruchomienie → ekran hasła zamiast pulpitu → złe
  hasło odrzucone → dobre hasło → pulpit z zapisaną tapetą i motywem; zrzuty ekranu.
- Próba klatki nowego programu „logowanie”; nowe komunikaty — testy na hoście.

## [0.5.6] — 2026-11-07

Etap **M5c + M5d — zaszyfrowany zapis danych** (ADR 0015, ADR 0016).

### Dodane
- **Dane przetrwają wyłączenie:** tapeta, motyw, prędkość myszy, sekundy na zegarze, rozdzielczość
  i notatka wracają po ponownym uruchomieniu. Zapisuje je nowa usługa **skarbiec** — jedyna, która
  ma dostęp do partycji z danymi „K1-DANE”; wszystko jest **zaszyfrowane** (XChaCha20-Poly1305,
  biblioteka Monocypher), a zmieniony bajt na dysku jest wykrywany, nie przyjmowany po cichu.
- **Obraz na pendrive** `k1-<wersja>-usb.img.xz` (UEFI): system + pusta partycja na dane; przy
  pierwszym starcie K1 zakłada na niej zaszyfrowany magazyn. Dysk laptopa pozostaje nietknięty.
- Bez partycji danych (ISO, maszyna wirtualna bez drugiego dysku) wszystko działa jak wcześniej —
  dane tylko do wyłączenia.
- **Alarm Windows Defendera** („Trojan:Win32/Sabsik!ml” na ISO 0.5.5 — zgadywanie modelu, nie znany
  wirus): ISO i obraz USB startują teraz przez **podpisane** programy z Ubuntu (shim z podpisem
  Microsoftu, GRUB z podpisem Canonical) zamiast naszego, samodzielnie zbudowanego GRUB-a.
- ISO i obraz USB są **w pełni powtarzalne** — z tego samego kodu powstaje plik identyczny bit w bit
  (można sprawdzić, że wydanie pochodzi z repozytorium). ISO jest mniejsze (14 MB).

### Zmienione
- Notatki nie mają już dostępu do żadnych plików — notatka jest obiektem w skarbcu (ciaśniejsza klatka).
- K1 formatuje tylko pustą partycję „K1-DANE”; partycji z innymi danymi nie nadpisuje (test).
- Testy: na hoście format magazynu (62 przypadki: przerwany zapis, zmieniony bajt, hasło, brak
  miejsca…) i fuzzing; w QEMU wyłączenie i ponowny start z tym samym dyskiem, start z obrazu USB
  przez UEFI (dwa razy), próba klatki skarbca.

## [0.5.5] — 2026-11-07

### Dodane
- **Ustawienia → Bateria** (Twój pomysł): procent i stan baterii oraz minimalistyczny wykres
  naładowania od uruchomienia systemu (linia w kolorze akcentu, pod wykresem pasek — kiedy był
  podłączony zasilacz), godzina startu i najniższy poziom. Pomiar co pół minuty; po wielu godzinach
  wykres sam „zagęszcza” starsze pomiary, więc zawsze pokazuje cały czas od uruchomienia.
  Historia z poprzednich uruchomień — razem z trwałym zapisem danych (M5).
- Ikona baterii (Ustawienia).

## [0.5.4] — 2026-11-07

### Naprawione
- **Zacinanie się całego pulpitu na prawdziwym laptopie** (ThinkPad X230, Twój pierwszy test):
  kursor, zmiana tapety i cały system działały skokowo. Dwie przyczyny: (1) w jądrze brakowało obsługi
  typów pamięci procesora (MTRR/PAT), więc każdy piksel szedł do karty bez buforowania; (2) przy
  obrazie z firmware ekran przy każdej klatce przepisywał cały ekran (ok. 4 MB). Teraz przy obrazie
  z firmware jest jeden bufor, a do karty trafiają tylko zmienione miejsca (np. sam kursor).
  W QEMU tego nie było widać — emulator nie odwzorowuje typów pamięci.

## [0.5.3] — 2026-11-07

### Dodane
- **Wskaźnik baterii** na górnym pasku (Twój pomysł): na laptopie przed ikoną dźwięku procent
  naładowania i ikona baterii, błyskawica przy ładowaniu, przy niskim poziomie (≤ 15 %) wypełnienie
  w kolorze akcentu. Na komputerze bez baterii (i w maszynie wirtualnej) nic się nie pokazuje.
  Stan sprawdzany co 15 s (ADR 0014).
- Jądro: sterownik Embedded Controller (przez niego laptopy, m.in. ThinkPad X230, podają stan
  baterii), baterii i zasilacza ACPI.
- Testy: sztuczna bateria laptopa w QEMU (tablica ACPI) — test w systemie, log powłoki i zrzut
  ekranu z ikoną; test na hoście odczytu stanu baterii (także z błędnymi danymi).

## [0.5.2] — 2026-11-07

Etap **M5a — dyski i USB** (pierwszy krok M5; ADR 0013). Pierwszy laptop: ThinkPad X230.

### Dodane
- Jądro rozpoznaje **dyski SATA** (kontroler AHCI — m.in. ThinkPad X230, VirtualBox) i **NVMe**,
  tablice partycji **GPT** (partycja danych K1 będzie szukana po nazwie „K1-DANE”).
- **USB**: kontrolery USB 3 (xHCI), USB 2 (EHCI) i USB 1.1 (OHCI); **klawiatury i myszy USB**
  działają w pulpicie, **pamięci USB** (pendrive'y) są widoczne. Tylko standardowe urządzenia —
  bez sterowników producentów i bez surowego dostępu do urządzeń.
- README: jak spróbować startu z pendrive'a na ThinkPadzie X230.

### Zmienione
- Odłączona mysz lub klawiatura USB jest „zapominana”, więc ponowne podłączenie (także pod tym samym
  numerem urządzenia) działa.
- Stare tablice partycji MBR nie są czytane przez jądro (mniej kodu wystawionego na obce nośniki).
- Testy: test w systemie z dyskami SATA/NVMe, pendrive'em i USB na trzech kontrolerach (dostęp do
  dysków tylko dla roota, nikt ich nie otwiera); nowy `scripts/test-m5.sh` — maszyna bez PS/2,
  tylko klawiatura i mysz USB, podłączanie i odłączanie myszy w trakcie pracy.

## [0.5.1] — 2026-11-07

### Naprawione
- Mysz mogła nie działać po bardzo szybkim starcie (np. pod KVM): nadzorca szukał urządzeń wejścia,
  zanim jądro zgłosiło mysz, i przestawał szukać, gdy znalazł klawiaturę. Teraz co 2 s sprawdza,
  czy doszło nowe urządzenie, i uruchamia usługę wejścia ponownie z pełnym zestawem. To też pierwszy
  krok do podłączania urządzeń w trakcie pracy (M5).

### Zmienione
- CI: logi testów zawsze dostępne, niezależnie od limitu miejsca na artefakty — w konsoli zadania,
  w podsumowaniu (lista OK/BŁĄD) i w wydaniu (`k1-<wersja>-logi.tar.gz`); artefakty 7 dni, obraz jako
  artefakt tylko z PR-ów.
- Testy pulpitu i wejścia czekają, aż mysz zostanie otwarta; test pulpitu sprawdza położenie kursora
  z logu ekranu po każdym ruchu; sprawdzanie wykresu Zasobów liczy każdą kolumnę.
- Dokumentacja: wstępne rozpoznanie etapu M5 (`docs/M5-ROZPOZNANIE.txt`).

## [0.5.0] — 2026-11-06

Etap **M4.1 — porządki i audyt** (ADR 0011, ADR 0012).

### Dodane
- Aplikacja **Zasoby**: procesor i pamięć całego systemu (liczby i wykresy z ostatniej minuty) oraz
  zużycie każdej usługi i aplikacji. Dane podaje nadzorca (statystyki cgroup) przez kanał, do którego
  aplikacja nie może pisać.
- **Ustawienia → Ekran**: wybór rozdzielczości z listy, którą podaje karta graficzna; zmiana od razu,
  pulpit układa się od nowa.
- Sterownik karty graficznej **VMware SVGA** (VirtualBox „VMSVGA”, VMware) — w maszynach wirtualnych
  więcej rozdzielczości niż 1024×768 z firmware.
- Ikony: Zasoby (puls), Ekran (monitor).

### Naprawione
- Pod dolnym paskiem i pod menu dźwięku „wyjeżdżała” jaśniejsza podstawa: cień panelu miał w środku
  dziurę przesuniętą 4 px w dół. Cień jest teraz liczony tylko poza panelem.
- Bezpieczeństwo (przegląd M4.1): nowe okno innej aplikacji nie przejmuje już klawiatury (ochrona
  przed „kradzieżą fokusu”); limit pamięci okien jednego klienta; ekran przycina obszar odświeżania
  do własnego okna klienta; log usług oczyszczany także ze znaków sterujących C1 i błędnego UTF-8;
  klatka zamyka wszystkie zbędne deskryptory (`close_range`); katalog notatek bez podążania za
  dowiązaniami.

### Zmienione
- Kod: kompozytor podzielony na `main.c`, `scena.c`, `okna.c`, `wskaznik.c`, `drm.c`; program
  testowy na pliki tematyczne (`testy/system/`).
- Testy: zmiana rozdzielczości i Zasoby w `test-m4.sh`; w systemie — ochrona fokusu, limit pamięci
  okien, klatka i próba aplikacji Zasoby; na hoście — `test_log`, nowe komunikaty w `test_komunikat`.


## [0.4.2] — 2026-11-06

### Zmienione
- Nowy znak K1 **„Warstwy”** (wybór właściciela z czterech kierunków): trzy arkusze papieru
  w odcieniach terakoty, jak na tapecie — w przycisku Start i w menu. Tarcza z blankami usunięta.
- README: w VirtualBoxie mysz i klawiatura działają od razu (test właściciela, VirtualBox z EFI);
  obejście dla niskiej rozdzielczości w trybie EFI.

## [0.4.1] — 2026-11-06

### Naprawione
- CI: instalacja narzędzi zawieszała się na serwerze GitHuba (ustawienie „bez pytań” dla apt
  ginęło przy `sudo`); teraz jest przekazywane wprost, instalacja pokazuje postęp w logu i ma limit
  10 minut. Bez zmian w samym systemie — obraz ISO jak w 0.4.0.

## [0.4.0] — 2026-11-06

Etap **M4 — GUI**: pulpit, okna i pierwsze aplikacje; pierwszy obraz ISO do maszyny wirtualnej.

### Dodane
- Pulpit (ustalenia właściciela, `docs/WYGLAD.md`): górny pasek — nazwa aktywnej aplikacji, dźwięk,
  data i godzina (czas polski, letni/zimowy sam); dolny pasek wyśrodkowany jak w Windows 11 — Start
  i aplikacje ze wskaźnikami; menu Start z „Uruchom ponownie” i „Wyłącz”; klawisz Super.
- Okna: zaokrąglone, z cieniem, przeciąganie za pasek tytułu, minimalizacja (też kliknięciem na
  pasku), zamykanie, fokus po kliknięciu.
- Aplikacje: **Zegar** (tarcza analogowa, data po polsku), **Notatki** (polskie znaki przez prawy Alt,
  zapis automatyczny), **Ustawienia** (motyw jasny/ciemny, tapeta, prędkość myszy, sekundy na
  zegarze, informacje; hasło — opis na M5).
- Nowe tapety na wzór zdjęcia właściciela (warstwy wygiętego papieru), 4 warianty: Glina (domyślna,
  kolory systemu), Zmierzch, Noc, Świt; nowy, płaski znak K1 (tarcza z blankami).
- Czcionki IBM Plex Sans i Serif (bitmapy liczone przy budowie, ADR 0009).
- **Obraz ISO** (`k1-<wersja>.iso`, ~16 MB) do VirtualBoxa/VMware/QEMU — start przez UEFI albo BIOS,
  grafika z firmware (simpledrm); CI publikuje go jako wydanie (Releases).
- Bezpieczeństwo (ADR 0008): ekran jako kompozytor sprawdza każdy komunikat okien i nadawcę, przyjmuje
  tylko zapieczętowaną pamięć współdzieloną, egzekwuje role (pulpit, Ustawienia); pulpit i każda
  aplikacja we własnej klatce (Notatki widzą tylko swój katalog, pulpit nie widzi treści okien).
- Testy: `scripts/test-m4.sh` (klikanie, pisanie po polsku, przeciąganie, motyw, tapeta, wyłączenie,
  start z ISO przez UEFI i BIOS — zrzuty sprawdzane automatycznie); w systemie: klatki i próby dla
  pulpitu i aplikacji, złośliwe okna, restart pulpitu (88 sprawdzeń); testy jednostkowe przybornika.

### Zmienione
- Jądro: simpledrm (grafika z firmware), loop (rdzeń wbudowany w ISO), zegar sprzętowy CMOS.
- `ekran` przepisany na kompozytor okien (`src/ekran/main.c` + `drm.c`); tapeta bez dużego logo.
- `/dane_uzytkownika` z prawami `0711` (aplikacja wchodzi tylko do swojego podkatalogu).
- Rozpoczęcie: `scripts/przygotuj-srodowisko.sh` instaluje też czcionki, FreeType i GRUB.

## [0.3.0] — 2026-11-06

Etap **M3 — Trusted Core**: każda usługa systemu działa w osobnej „klatce” i wraca po awarii.

### Dodane
- Nadzorca w pełnej wersji (ADR 0007): każda usługa ma własnego użytkownika, zero uprawnień
  administratora, blokadę podnoszenia uprawnień, białą listę wywołań jądra (seccomp), dostęp do plików
  tylko do własnego programu (Landlock) i limity pamięci oraz procesów (cgroups v2).
- Urządzenia (karta graficzna, klawiatura, mysz) otwiera nadzorca i przekazuje usługom — usługi nie
  widzą żadnych plików ani urządzeń poza przekazanymi.
- Kanały między usługami przez gniazda UNIX z kontrolą nadawcy: ekran odrzuca komunikaty od kogoś
  innego niż przypisana usługa. Nowy wspólny protokół komunikatów (32 bajty, testy + fuzzing).
- Logi usług przechodzą przez nadzorcę: bez znaków sterujących, z limitem, usługa nie podszyje się
  pod inną; usługi nie mają już dostępu do konsoli.
- Restart usług po awarii (ekran wraca razem z klientami), usługa padająca w kółko zostaje wyłączona.
- Testy: sprawdzenie klatek z zewnątrz, „próby” w klatkach (22 zabronione działania), obcy nadawca,
  restart ekranu, limit restartów; testy jednostkowe protokołu i kanałów, fuzzing protokołu.
- `docs/DO_ZROBIENIA.txt` — lista tego, co zostało do zrobienia, i decyzji potrzebnych od właściciela.
- CI: workflow „Sprzątanie artefaktów” (usuwa stare obrazy Kons-OS, które zajmowały miejsce).

### Zmienione
- Jądro: gniazda UNIX (`CONFIG_UNIX`) — tylko do kanałów między usługami.
- Skill `kons-dev` opisuje architekturę K1, niezmienniki bezpieczeństwa i przepisy na typowe zmiany.
- Decyzja: na razie bez `fortify-headers` (ADR 0007).

## [0.2.0] — 2026-11-06

Etap **M2 — Ekran**: K1 pokazuje obraz i reaguje na mysz i klawiaturę.

### Dodane
- Nowa tapeta i logo K1 (ustalenia właściciela, `docs/WYGLAD.md`): ciemne, ciepłe tło z łukiem
  „horyzontu”, logo — tarcza z blankami twierdzy (bezpieczeństwo, niezależność) z monogramem K1.
  Styl nawiązuje do Claude Code (terakota, krem, ciemny brąz).
- `ekran`: jedyny proces z dostępem do karty graficznej (DRM/KMS bez libdrm), własny renderer
  programowy z wygładzaniem krawędzi, podwójne buforowanie, kursor myszy.
- `wejscie`: klawiatura i mysz (PS/2, evdev); kody klawiszy nigdy nie trafiają do logu.
- Protokół zdarzeń wejście → ekran (16 bajtów, wersja, typ, długość, sprawdzanie każdego pola).
- `nadzorca` w wersji minimalnej: uruchamia `wejscie` i `ekran` i łączy je kanałem (pełny — M3).
- Testy: `scripts/test-m2.sh` (mysz i klawiatura przez monitor QEMU, zrzuty ekranu sprawdzane
  automatycznie), `scripts/testy.sh` (wszystko), nowe testy negatywne: przejęcie ekranu przez inny
  proces, złośliwe komunikaty do ekranu, awaria procesu ekran; testy jednostkowe i fuzzing protokołu.
- Jądro: DRM + sterownik `bochs` (QEMU), evdev, i8042, klawiatura AT, mysz PS/2 — bez fbdev i fbcon.

### Zmienione
- CI: tag wersji zakładany zaraz po udanych testach; brak miejsca na artefakty GitHuba nie blokuje już wersji.
- Program testowy w systemie: `/rdzen/system/test-systemu` (uruchamia go nadzorca).

## [0.1.0] — 2026-11-06

Pierwsza wersja Kons K1 Sovereign: etapy **M0** (porządki) i **M1** (start systemu).
Na razie bez ekranu — system startuje w maszynie wirtualnej i zgłasza się na porcie szeregowym.

### Dodane
- Jądro Linux 6.18.60 (LTS) z własną, minimalną konfiguracją: bez modułów, bez swapu i hibernacji,
  bez terminali, lockdown + Landlock + Yama, wzmocnienia według KSPP, sprawdzanie sumy i podpisu źródeł.
- `k1-initramfs`: znajduje obraz systemu, montuje go tylko do odczytu jako `/rdzen` i przekazuje
  sterowanie dalej; przy każdym błędzie czytelny komunikat i bezpieczne wyłączenie maszyny.
- `/rdzen/init` (PID 1): montowania pod `/rdzen/jadro`, 13 ustawień bezpieczeństwa jądra,
  sprzątanie osieroconych procesów, kontrolowane wyłączanie. W korzeniu systemu są tylko
  `/rdzen`, `/aplikacje`, `/dane_uzytkownika`.
- Testy: 33 sprawdzenia wewnątrz systemu (w tym negatywne: nie da się uruchomić programu z danych
  ani z pamięci, nie ma terminali, usunięte niebezpieczne wywołania jądra), 6 scenariuszy błędów
  startu, testy jednostkowe z sanitizerami i fuzzing parsera.
- Skrypty: budowa (powtarzalna), uruchamianie w QEMU z logiem, zrzut ekranu, testy.
- CI: GitHub Actions buduje i testuje `main`, publikuje obrazy i logi jako artefakt, zakłada tag `k1-v0.1.0`.
- Dokumentacja: specyfikacja, ADR 0001–0005, architektura, komponenty, testy, zależności, ryzyka.

### Usunięte
- Kons-OS na bazie Debiana (live-build, XFCE) — przeniesiony do gałęzi `archiwum/debian-0.1.2`.

---

# Archiwum: Kons-OS (Debian, live-build)

Historia poprzedniego systemu; kod w gałęzi `archiwum/debian-0.1.2`, tagi `v0.1.0`–`v0.1.2`.

## [Kons-OS 0.1.2] — 2026-11-02
- Naprawiona tapeta XFCE; firmware wybierane ręcznie (ISO ~1,3 GB); lokalne budowanie ISO.

## [Kons-OS 0.1.1] — 2026-11-02
- System odchudzony do pulpitu XFCE, ustawień i Firefoksa; szablony zgłoszeń; tagi wersji.

## [Kons-OS 0.1.0] — 2026-11-02
- Pierwsza wersja: Debian 13 live, XFCE, polski język, budowanie ISO w GitHub Actions.

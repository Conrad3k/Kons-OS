# Komponent: strefa (VMM — mikro-maszyna wirtualna dla obcych aplikacji)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.10.0 krok 1: strefa próbna, tylko w obrazie testowym; krok 2: przeglądarka — sesje Ogólna, Bank, Prywatna, ADR 0043; krok 5: sesje, które pamiętają — dysk strefy w skarbcu); PRZETESTOWANE na hoście (`test_virtio`, `test_strefa_start`, `fuzz_virtq`), w systemie (`t_kvm`, `t_strefa_klatka`, próba w klatce `t_proby`) i w QEMU (`scripts/test-strefa.sh`: okno w ramce, wejście, pasek tytułu K1, zamykanie, 4 złośliwe tryby gościa, komputer bez wirtualizacji) — w emulacji AMD SVM; na prawdziwym sprzęcie — po wydaniu (krok 3) |
| **Pliki** | `src/strefa/`: `main.c` (start, pętla wątku głównego, koniec), `kvm.c` (maszyna, procesory, wyjścia), `start.{c,h}` (start jądra bez firmware: strona zerowa, e820, tablice stron, GDT, tablica MP), `virtq.{c,h}` (kolejka virtio), `mmio.{c,h}` (rejestry virtio-mmio), stróż `stroz.c`, urządzenia `blk.c`, `konsola.c`, `rng.c`, `wejscie.c`, `gpu.c`, `vsock.c`, dysk strefy `dane.c` + `dysk.{c,h}` (0.10.0 krok 5), okno `okno.{c,h}`; wspólne `strefa.h` (tabela urządzeń `K1_URZADZENIA`); gość `src/strefa/gosc/` (`main.c`, `obraz.c`, `zlosliwy.c`, `przegladarka.c`, `pamiec.c`, `dysk.c`); nadzorca `src/nadzorca/strefy.c`, `profile.c` (`KLATKA_STREFA`), sesje `strefy_ust.c` + `sesje.{c,h}`; ekran `src/ekran/strefy.c` (ramka); tabela stref `K1_STREFY` w `src/wspolne/k1_uslugi.h`; jądro strefy `jadro/strefa.config` |
| **Decyzje** | ADR 0042 (T1–T6, T10, zaufana ramka), ADR 0020 (kierunek), decyzje właściciela D1 (szkic B2, S1; krok 3: szkic A — pełny pasek tytułu), D4 (testy w emulacji) |

## Cel
Uruchomić obcą aplikację w osobnym, małym komputerze udawanym przez procesor (mikro-maszyna, KVM), z własnym jądrem Linux
(jądro strefy), i pokazać jej obraz jako okno K1 w zaufanej ramce. Jeden proces `strefa` = jedna maszyna. W 0.10.0 krok 1
w środku działa program próbny `gosc`; w kroku 2 — przeglądarka.

## Granica zaufania
**Gość jest wrogi** — wszystko, co przychodzi z maszyny (jej pamięć, kolejki virtio, dostępy do rejestrów, polecenia
grafiki, pakiety vsock, linie konsoli), sprawdzane pole po polu; coś, co robi tylko zepsuty albo wrogi sterownik, kończy
tę maszynę z powodem w logu (`K1-STREFA: strefa N: koniec — <powód>`), nigdy K1. Ekran i nadzorca są zaufane.
Konsola gościa: tylko w strefie, której wiersz `K1_STREFY` na to pozwala (strefa próbna), jej wiersze idą do logu K1
(oczyszczone: znaki sterujące i zły UTF-8 → „?”, najwyżej 3000); w innych strefach — tylko liczba bajtów (niezmiennik 8)
i, raz na rodzaj, nazwa ciężkiego błędu jądra gościa z naszej tabeli (`konsola.c`: panika, brak pamięci, zawieszony
procesor lub zadanie, BUG/Oops, padnięty program): `K1-STREFA: gość: jądro gościa zgłasza: <rodzaj>` — nigdy jego słowa.
**Stróż** (`stroz.c`, osobny wątek): gdy pętla główna VMM nie ruszy się przez 15 s (okno bez obrazów, „zamknij” od ekranu
niewidoczne), raz zapisuje, gdzie jest każdy wątek — krok pętli, kto trzyma zamek urządzeń, każdy procesor (w gościu, przy
wyjściu, czeka na zamek, ma zamek; ostatnie wyjście i urządzenie): `K1-STREFA: stróż: pętla główna stoi od N s (…)`.
Tylko patrzy — niczego nie zmienia i nie kończy maszyny.
Kody klawiszy nigdy nie trafiają do logu: gosc loguje tylko liczniki.

## Uprawnienia (klatka `KLATKA_STREFA`, z wiersza `K1_STREFY`)
| | |
|---|---|
| UID | z wiersza (strefa próbna: 300), bez grup i capabilities, `NoNewPrivs`, Landlock bez ścieżek |
| deskryptory od nadzorcy | 3: kanał do ekranu (rola `STREFA`, numer strefy), 4: `/dev/kvm`, 5: jądro strefy (tylko odczyt), 6: obraz strefy (tylko odczyt), 7: kanał do nadzorcy — połączenia przeglądarki (0.10.0 krok 2); 8, 9 (krok 5, tylko sesja, która pamięta): kanał do skarbca (właściciel = profil wiersza strefy) i pusty dysk strefy `/rdzen/strefy/dysk.img` (tylko odczyt) |
| seccomp (poza bazą) | `ioctl` tylko typu KVM (0xAE), `memfd_create`, `ftruncate`, `eventfd2`, `futex`, `getrandom`, `pread64`, `clone` tylko z `CLONE_THREAD`; od kroku 2: `shutdown`, `getsockopt` tylko `SO_ERROR` (gniazda od nadzorcy; VMM nie otwiera gniazd) |
| cgroup | pamięć gościa + 184 MiB (od kroku 5: + pamięć podręczna dysku 16 MiB i bufor prośby 4 MiB); `pids.max` = 1 + procesory + 1 (stróż) + 1 (wątek dysku, krok 5) + 1 (zadanie KVM w procesie maszyny) |
| `RLIMIT_FSIZE` | pamięć gościa (memfd jej pamięci) |
| argumenty | numer strefy, tryb gościa testowego (0; 1–4 złośliwy, 5 licznik na dysku — tylko od programu testowego) |

## Maszyna
- **Pamięć:** memfd z `MFD_NOEXEC_SEAL` od adresu 0 gościa (≤ 3 GiB, poniżej urządzeń); nigdy wykonywana przez K1.
- **Start:** 64-bitowy protokół startu Linuksa z pliku bzImage — jądro od 1 MiB, strona zerowa (nagłówek z jądra, e820,
  linia poleceń), GDT, tablice stron 1:1 (4 GiB), tablica MP (procesory, I/O APIC; bez ACPI). Linia poleceń gościa:
  urządzenia virtio-mmio, `console=hvc0 root=/dev/vda ro rootfstype=squashfs init=/init reboot=k panic=-1 k1.tryb=N`.
- **Procesory:** wątek na procesor; CPUID od KVM z identyfikatorem APIC, bitem „hiperwizor” i bez LA57 (ADR 0042 T10).
- **Przerwania:** w jądrze (`KVM_CREATE_IRQCHIP`, `KVM_CREATE_PIT2`), irqfd na urządzenie (linie 5…11).
- **Wyjścia:** MMIO urządzeń (pod zamkiem), reset przez kontroler klawiatury (koniec gościa), potrójny błąd, inne
  nieobsługiwane — koniec; dostęp do MMIO zgłoszony jako „w trakcie przerwania” — ponawiany do 8 razy (emulacja QEMU,
  ADR 0042 T10).
- **Koniec:** gość sam (reset), zły dostęp, `ZAMKNIJ` od ekranu → „zamknij” przez vsock, po 40 s maszyna wyłączana (do
  0.10.0 krok 5: 10 s — za mało na przeglądarkę po ciężkiej stronie i zapis dysku strefy, przebieg CI 132).
- **Powód końca** (krok 3, S26; `main.c` `kod_wyjscia`): kod wyjścia VMM — 0, gdy zamknął użytkownik (`ZAMKNIJ` od ekranu)
  albo program strefy zamknął się zwyczajnie; `K1_STREFA_KONIEC_PAMIEC` / `_AWARIA`, gdy program strefy skończył się sam
  i powiedział dlaczego („koniec …” na kanale sterującym); `_BLAD`, gdy maszyna stanęła bez słowa (jądro gościa, zły
  dostęp). Nadzorca (`strefy.c` `strefa_koniec`) dokłada SIGKILL = `_PAMIEC_K1` (OOM w K1 — w cgroup albo w komputerze)
  i daje powód gui (`STREFA_STAN`) — okienko „<tytuł> zamknęła się”; bez okienka, gdy zamknął użytkownik, K1 zatrzymał
  strefę sam (`bez_liczenia`) albo to złośliwy gość testu. Wcześniej okno po prostu znikało.

## Urządzenia virtio-mmio (`K1_URZADZENIA`, okno 4 KiB od 0xd0000000, linia przerwania od 5)
| Urządzenie | Kolejki | Co robi | Co kończy maszynę |
|---|---|---|---|
| obraz (blk) | 1 | odczyt z obrazu strefy (`pread`), `VIRTIO_BLK_F_RO`, `SEG_MAX` | zapis do obrazu, odczyt > 4 MiB, prośba bez nagłówka |
| konsola | 2 | wiersze gościa do logu (strefa próbna) albo licznik | — |
| losowość (rng) | 1 | `getrandom` do 4 KiB na prośbę | — |
| klawiatura, wskaźnik (input) | 2 + 2 | zdarzenia `EV_KEY`, `EV_ABS` (0…32767), `EV_SYN` z ekranu; od 0.10.0 krok 4 wskaźnik z kółkiem: `REL_WHEEL`, `REL_HWHEEL` i `*_HI_RES` (1/120 ząbka) z `PRZEWIN` — z kółka oba, z touchpada tylko `*_HI_RES`; bez wolnego bufora — zdarzenie przepada (licznik) | — |
| ekran (gpu 2D) | 2 | zasoby ≤ 4096 × 4096 (razem ≤ 96 MiB), tło z pamięci gościa (sprawdzone przy dołączeniu), przeniesienie, pokazanie, odświeżenie → okno | polecenie krótsze niż jego struktura, brak miejsca na odpowiedź |
| vsock | 3 | kanał sterujący (port 1 hosta): „gotowa”, „zamykam” od gościa, „zamknij” do gościa; od kroku 3 od gościa także „koniec pamiec\|awaria\|sam” (dlaczego program strefy skończył się sam), „pamiec strona” (strona zakończona z braku pamięci — licznik w logu), „etap okno” (pierwsze okno przeglądarki — czas od startu maszyny w logu, S30); od kroku 5 „zamykanie gotowe\|sygnal\|po czasie” (jak przeglądarka skończyła się po „zamknij” — czas w logu); każdy inny wiersz — tylko licznik; port 2 (krok 2): połączenia przeglądarki od przekaźnika — do 128 naraz, każde z kredytem 64 KiB w obie strony, bajty do łącznika (`polaczenia.c`); inne porty — RST | zły adres albo typ, przekroczony kredyt, za mały bufor gościa |

| dysk strefy (blk, krok 5) | 1 | drugi dysk gościa (`/dev/vdb`) do zapisu i odczytu, `FLUSH`, `SEG_MAX`; 2 GiB w kawałkach po 64 KiB przez pamięć podręczną `dysk.c`; strefa, która zapomina: 0 sektorów | prośba bez nagłówka, odczyt albo zapis > 4 MiB |

Wspólne dla wszystkich (`virtq.c`, `mmio.c`): deskryptor poza pamięcią, pętla deskryptorów, deskryptor pośredni, do
odczytu po do zapisu, skok indeksu, kolejka poza pamięcią albo niewyrównana, rejestr czytany nie po 4 bajty — koniec.

## Okno
Klient ekranu z rolą `STREFA`, jedno okno (≤ 1024 × 720, z rozmiaru ekranu; tyle widzi gość jako wyświetlacz). Przed
pierwszym obrazem gościa: „Uruchamiam: <tytuł>…” z ikoną strefy i paskiem w jej kolorze (D1: S1). Obraz gościa — zmienione
prostokąty kopiowane do wolnej połówki powierzchni (nieprzezroczyste). Klawisze i wskaźnik od ekranu (tylko przy fokusie)
→ urządzenia wejścia; utrata fokusu puszcza wszystko. Przewijanie (`PRZEWIN`, krok 4) przychodzi do okna pod kursorem,
także bez fokusu: VMM ustawia najpierw położenie wskaźnika (gość przewija to, co jest pod nim), potem kółko. Ramkę — pasek tytułu nad obrazem i obwódkę (krok 3, szkic A) —
rysuje ekran (`docs/komponenty/ekran.md`); wysokość obrazu jest o pasek (`K1_STREFA_TYTUL`) mniejsza.

## Przeglądarka (0.10.0 krok 2, ADR 0043)
Wiersze `K1_STREFY` „Ogólna” (szałwia, w menu Start jako „Przeglądarka”), „Bank” (łupek), „Prywatna” (wrzos): obraz
`/rdzen/strefy/przegladarka.img`, 1536 MiB, 2 procesory (w emulacji QEMU — 1), jednorazowe (krok 3: pamiętają).
- **Obraz** (`scripts/zbuduj-strefe.sh`): pakiety Debiana 13 z tabeli `strefy/przegladarka.txt` (wersje i sumy;
  odświeżenie `scripts/odswiez-strefe.py` — podpisy Debiana sprawdzane kluczami z `strefy/klucze/`), rozpakowane bez
  skryptów pakietów, bez powłoki; nasze: `/init` (`gosc`), `/usr/lib/k1/kompozytor`, `/usr/lib/k1/przekaznik`, mapa
  klawiatury (`strefy/klawiatura-pl.xkb`), ustawienia Firefoksa (`strefy/przegladarka/k1.js`, `policies.json`) i GTK
  (`strefy/przegladarka/gtk-settings.ini`: pusty układ przycisków okna — Firefox nie rysuje własnych minimalizuj /
  powiększ / zamknij, krok 3, S28).
- **`gosc`** (tryb przeglądarki, `gosc/przegladarka.c`): tmpfs na `/tmp`, `/run`, `/home`, `/dev/shm`, pętla zwrotna, nowy
  `machine-id`; przekaźnik, kompozytor, Firefox jako użytkownik 1000; koniec przeglądarki = koniec maszyny; „zamknij” →
  zamknięcie okien (krok 5): `gosc` daje kompozytorowi SIGUSR1, ten wysyła każdemu oknu `xdg_toplevel.close` — Firefox
  kończy się jak po kliknięciu krzyżyka i **zapisuje dane sesji** (ciasteczka, logowania); po SIGTERM kończył się w 0,3 s
  bez zapisu i sesja zapominała ostatnie ciasteczko (przebiegi CI 132, 136). SIGTERM dopiero po 15 s, koniec po 20 s,
  potem `sync` dysku i koniec maszyny. VMM dostaje „zamykanie gotowe\|sygnal\|po czasie” i loguje `gość: przeglądarka
  zamknęła się sama (dane sesji zapisane) po N s` (albo „zamknięta sygnałem”, „nie zamknęła się w czasie”). Bez pytania
  „Zamknąć karty?” — `browser.tabs.warnOnClose`, `browser.warnOnQuit` wyłączone w `k1.js`. Krok 3 (S26): gdy przeglądarka skończy się sama — „koniec pamiec” (zabita przez jądro gościa z braku
  pamięci: `/proc/vmstat` oom_kill), „koniec awaria” (sygnał, kod ≠ 0, padł kompozytor) albo „koniec sam” (zamknięta
  z jej menu); co sekundę „pamiec strona”, gdy jądro gościa zabiło proces strony.
- **Pamięć strefy** (krok 3, S26; `gosc/pamiec.c`): na IdeaPadzie z 4 GB okno przeglądarki znikało — ciężkie strony
  zapełniały 1536 MiB gościa (bez wymiany), a jądro gościa zabijało największy proces, często samą przeglądarkę. Teraz:
  **pamięć kompresowana** — zram w jądrze strefy (lz4) jako urządzenie wymiany w pamięci gościa, rozmiaru jego pamięci
  (nagłówek wymiany pisze `gosc` — obraz nie ma `mkswap`), `swappiness` 100; główny proces przeglądarki chroniony
  (`oom_score_adj` −900), każdy inny proces użytkownika 1000 idzie pierwszy (+300) — przy braku pamięci kończy się strona
  („Karta uległa awarii”), nie przeglądarka; Firefox bez pamięci podręcznej na „dysku” (to tmpfs, czyli pamięć),
  zamknięcie ostatniej karty nie zamyka okna. Gość dalej nie bierze od K1 więcej niż pamięć z wiersza `K1_STREFY`.
- **Czas startu** (krok 3, S30): kompozytor pisze „okno” na fd 3 (rura od `gosc`) przy pierwszym oknie przeglądarki,
  `gosc` przekazuje „etap okno”, VMM loguje `gość: okno przeglądarki po N s od startu maszyny` (i czas „gotowa”).
  Jądro strefy rozpakowuje obraz wprost do pamięci podręcznej stron i na każdym procesorze osobno.
- **Kompozytor** (`src/strefa/kompozytor/`): Wayland bez libwayland — tabela interfejsów `K1_INTERFEJSY` (`wl_compositor`,
  `wl_shm`, `wl_subcompositor`, `wl_seat`, `wl_output`, `wl_data_device_manager` bez schowka, `xdg_wm_base`), każda
  wiadomość sprawdzana według sygnatury (zła — koniec tego klienta). Okno na cały wyświetlacz, dialogi na środku,
  wyskakujące menu z pozycjonerem; zdarzenia wejścia do każdego `wl_pointer`/`wl_keyboard` klienta (Firefox ma po dwa);
  „klatka gotowa” także po zatwierdzeniu bez zmian (inaczej GTK przestaje rysować). Kółko (krok 4): jedna ramka
  zdarzeń urządzenia = jedno przewinięcie `wl_pointer.axis` (10 jednostek na ząbek, jak weston); z ząbkami —
  `axis_source` „wheel” i `axis_discrete`, same części ząbka (touchpad) — „finger”, więc Firefox przewija płynnie. Od
  kroku 5: **jeden ząbek = jedna ramka** `wl_pointer` (do 16, reszta w ostatniej), każda z późniejszym czasem — GTK robi
  z `axis_discrete` jednej ramki jeden krok bez względu na liczbę (przebieg CI 134: 5 ząbków w górę, 2 kroki), a Firefox
  pomija zdarzenie kółka z czasem poprzedniego. Ząbki skleja QEMU w teście i ekran, gdy klient nie nadąża.
- **Przekaźnik** (`src/strefa/przekaznik/`): SOCKS5 na 127.0.0.1:1080 → jedno połączenie vsock (port 2) na każde.
- **Łącznik** (moduł VMM: `lacznik.c` czyste protokoły, `polaczenia.c` stan połączeń): SOCKS5 — tylko CONNECT, IPv4 albo
  nazwa (BIND, UDP, IPv6 — odmowa kodem SOCKS); nazwy — DNS (rekord A) przez gniazdo UDP od nadzorcy do serwera z DHCP;
  połączenie — `STREFA_POLACZ` do nadzorcy (reguła D3, limit), gniazdo TCP w `STREFA_GNIAZDO`, odmowa w `STREFA_ODMOWA`.
  W logu tylko liczniki: „połączenia przeglądarki: N, nazwy N, odmowy N”.

## Dysk strefy i sesje (0.10.0 krok 5, ADR 0042 T11)
- **Co pamięta:** sesja, która pamięta (Ustawienia → Przeglądarka: „Pamięta”), dostaje drugi dysk virtio-blk (`/dev/vdb`,
  2 GiB). Gość montuje go jako katalog domowy przeglądarki (`gosc/dysk.c`, ext4 z dziennikiem: przerwane wyłączenie
  naprawia jądro gościa przy montowaniu) — logowania, zakładki, historia i ustawienia Firefoksa zostają po zamknięciu i po
  ponownym starcie K1. Sesja, która zapomina, ma ten dysk z 0 sektorów: katalog domowy w pamięci, jak w kroku 2.
- **Gdzie leży:** w skarbcu, jako obiekty właściciela = profil wiersza strefy (`K1_OB_DYSK` + numer kawałka, kawałki po
  64 KiB — `K1_STREFA_DYSK_KAWALEK`), szyfrowane jak wszystko w skarbcu (VMM nie ma własnego szyfrowania). Kawałek, którego
  sesja jeszcze nie zapisała, VMM czyta z pustego dysku strefy `/rdzen/strefy/dysk.img` — ext4 zrobiony przy budowie
  (`scripts/zbuduj-obraz.sh`: `mke2fs` z ustalonym UUID, ziarnem i czasem — powtarzalnie; w SquashFS rdzenia to kilka KiB),
  więc w strefie nie ma `mkfs`. Inny klient skarbca nie nazwie tych obiektów (`K1_MAG_ARG`), a sesje nie widzą swoich
  dysków (inny właściciel). Kawałek nigdy nie bierze rezerwy, którą pliki zostawiają systemowi (`dok_rezerwa`).
- **Pamięć podręczna** (`dysk.c`, czyste, `test_dysk_strefy`): 256 kawałków (16 MiB); zapisany kawałek czeka w VMM do
  `FLUSH` gościa (wtedy wszystkie idą do skarbca, zanim gość usłyszy „gotowe”) albo do chwili, gdy potrzeba jego miejsca.
  Błąd skarbca = błąd prośby (`VIRTIO_BLK_S_IOERR`), nigdy inne dane; kawałek, którego nie zapisano, zostaje. Na końcu
  maszyny VMM zapisuje, co zostało (najwyżej 20 s), i loguje liczby: „dysk strefy: kawałki wczytane N, zapisane N, błędy N”.
- **Wątek dysku** (`dane.c`): skarbiec odpowiada w milisekundach albo sekundach (pendrive, kompaktowanie), więc prośby
  dysku robi osobny wątek — procesor gościa tylko go budzi; łańcuchy bierze i oddaje pod zamkiem urządzeń, pracuje bez niego.
- **Sesje** (`src/nadzorca/strefy_ust.c`, format `sesje.{c,h}`): nadzorca trzyma, która sesja pamięta i jak się nazywa —
  w skarbcu (właściciel `K1_WL_SYSTEM`, obiekt `K1_OB_STREFY`, „K1Z1”) — i mówi to gui (menu Start, okienka), ekranowi
  (pasek tytułu, „zapomina po zamknięciu”) i Ustawieniom. Domyślnie z tabeli `K1_STREFY`: Ogólna i Bank pamiętają,
  Prywatna zapomina. Zmiana działa od następnego otwarcia sesji. Strefa pamięta tylko, gdy dane K1 są na dysku (K1-DANE) i
  sesje zostały już wczytane; bez K1-DANE każda zapomina. „Wyczyść dane sesji” (nie, gdy działa): nadzorca → skarbiec
  `SKARB_WYCZYSC` — wszystkie kawałki tego właściciela znikają, następne otwarcie zaczyna od pustego dysku.
- **Ustawienia → Miejsce:** część „Strefy” — ile zajmują dyski wszystkich sesji (`K1_CZESC_STREFY`).

## Komunikaty
| Komunikat | Kierunek | Pola |
|---|---|---|
| `STREFA_POLACZ` | VMM → nadzorca (fd 7) | obiekt = połączenie 1…128; TCP: a = adres IPv4 (`k1_siec_serwer_dobry`), b = port 1…65535, c = `K1_STREFA_TCP`; DNS: a = b = 0, c = `K1_STREFA_DNS`; d = e = 0 |
| `STREFA_GNIAZDO` | nadzorca → VMM (+ 1 deskryptor) | obiekt = połączenie, a = `K1_STREFA_TCP` (gniazdo TCP w trakcie łączenia) / `K1_STREFA_DNS` (gniazdo UDP do serwera DNS, port 53), reszta 0 |
| `STREFA_ODMOWA` | nadzorca → VMM | obiekt = połączenie, a = `K1_STREFA_ODM_*` (adres poza D3, brak sieci albo DNS, limit, błąd gniazda), reszta 0 |
| `URUCHOM_STREFE` | gui → nadzorca (fd 4); program testowy → nadzorca | `a` strefa 1…`K1_STREFY_MAKS`, `b` tryb gościa testowego 0…4 (≠ 0 tylko od programu testowego; gui — odrzucone), reszta 0 |
| `STREFA_STAN` | nadzorca → gui (fd 5) | `a` strefa, `b` powód: nie ruszyła — `K1_STREFA_BEZ_WIRTUALIZACJI`, `_ZA_MALO_PAMIECI`, `_BRAK_OBRAZU`; zamknęła się sama (krok 3) — `K1_STREFA_KONIEC_PAMIEC` (pamięć strefy), `_PAMIEC_K1` (pamięć komputera), `_AWARIA`, `_BLAD` (1…`K1_STREFA_STAN_MAKS`); reszta 0 |
| `KLIENT` (rola `STREFA`) | nadzorca → ekran | `b` = numer strefy (1…`K1_STREFY_MAKS`) |
| `STREFY_STAN` | nadzorca → gui (fd 5), Ustawienia (fd 4), ekran (kanał sterujący) | krok 5: `a` bity stref, które pamiętają (bit nr−1), `b` = 1 komputer ma wirtualizację, `c` = 1 dane K1 na dysku (bez tego `a` = 0), `d` bity stref, które działają, `e` = 0 |
| `STREFA_NAZWA` | nadzorca → gui, Ustawienia, ekran; Ustawienia → nadzorca (nowa nazwa) | krok 5: obiekt = strefa, `a`…`e` = nazwa sesji, 20 bajtów UTF-8 dopełnione zerami (`k1_strefa_nazwa_dobra`: bez znaków sterujących, niewidocznych i zmieniających kierunek, bez spacji na brzegach); nigdy w logu |
| `STREFA_TRWALOSC` | Ustawienia → nadzorca | krok 5: obiekt = strefa, `a` = `K1_STALA` / `K1_JEDNORAZOWA` (od następnego otwarcia), reszta 0 |
| `STREFA_WYCZYSC` | Ustawienia → nadzorca; program testowy → nadzorca | krok 5: obiekt = strefa, reszta 0 — „Wyczyść dane sesji” |
| `STREFA_WYCZYSZCZONO` | nadzorca → Ustawienia | krok 5: obiekt = strefa, `a` = `K1_STREFA_WYCZ_OK` / `_DZIALA` (otwarta — najpierw zamknij) / `_BLAD`, `b` = usunięte kawałki, reszta 0 |
| `SKARB_WYCZYSC` | nadzorca → skarbiec (kanał sterujący) | krok 5: `a` = właściciel — profil strefy (inny — odrzucony), reszta 0 |
| `SKARB_WYCZYSZCZONO` | skarbiec → nadzorca | krok 5: `a` = ten właściciel, `b` = 0 albo `K1_MAG_*`, `c` = usunięte kawałki, reszta 0 |
| `SKARB_CZYTAJ`, `SKARB_ZAPISZ`, `SKARB_USUN`, `SKARB_DANE`, `SKARB_WYNIK` (dysk strefy) | VMM ↔ skarbiec (fd 8) | krok 5: obiekt = `K1_OB_DYSK` + kawałek (0…32 767), zapis i dane — najwyżej 64 KiB; brak kawałka: `WYNIK` b = `K1_SKARB_BRAK` (VMM czyta wtedy pusty dysk) |
| `OKNO` | ekran → gui | okno strefy: `a` = 0, `c` = numer strefy |

## Gość próbny `gosc` (`src/strefa/gosc/`, `/init` obrazu `/rdzen/strefy/probna.img` — tylko obraz testowy)
Montuje `/proc`, `/sys`; rysuje wzór przez KMS (bufor „dumb”): granatowe tło, żółte rogi, pas — zielony po parzystej liczbie
klawiszy, pomarańczowy po nieparzystej — i biały kwadrat w miejscu ostatniego kliknięcia; czyta wejście (`/dev/input`),
loguje liczniki; „gotowa” i „zamykam” przez vsock; kończy przez `reboot`. Od kroku 5 strefa próbna pamięta: gość montuje
dysk strefy w `/dane` i liczy na nim swoje uruchomienia („K1-GOSC: dysk: uruchomienie N”); tryb 5 — tylko to i koniec. Tryby złośliwe (`k1.tryb=1…4`): sam steruje
urządzeniem w gnieździe LOSOWOSC przez `/dev/mem` (VMM nie podaje go jądru gościa i rezerwuje 1 MiB pamięci) — deskryptor
poza pamięcią, pętla deskryptorów, odczyt 8 MiB, zapis do obrazu (w trybach 3–4 VMM wkłada tam virtio-blk).

## Testy
| Test | Rodzaj |
|---|---|
| `test_virtio`: łańcuchy, zapis i odczyt w łańcuchu, zawijanie pierścienia; wrogie łańcuchy (poza pamięcią, pętle, pośrednie, kolejność, skok indeksu); rejestry, cechy, gotowość kolejki, przerwania, reset | funkcjonalny, negatywny |
| `test_strefa_start`: strona zerowa, e820, tablice stron, GDT, tablica MP (sumy); złe jądra i pamięci | funkcjonalny, negatywny |
| `fuzz_virtq`: dowolna pamięć gościa i zapisy rejestrów | fuzzing |
| `test_dysk_strefy` (krok 5): losowe odczyty i zapisy na 64 MiB z pamięcią 16 MiB, `FLUSH` zapisuje wszystko, cały kawałek bez odczytu, granice dysku, źródło z błędem (błąd, nigdy inne dane; kawałek zostaje), kolejność zapisu, pełna pamięć podręczna bez możliwości zapisu | funkcjonalny, negatywny |
| `test_sesje` (krok 5): format sesji w skarbcu — domyślne, zapis i odczyt, złe pole zostawia domyślne, nieznana strefa, uszkodzony obiekt | funkcjonalny, negatywny |
| `test_komunikat` (krok 5): `STREFY_STAN`, `STREFA_*`, `SKARB_WYCZYSC`/`_WYCZYSZCZONO`, nazwy sesji (znaki sterujące, niewidoczne, kierunek, zły UTF-8), zakres obiektów dysku | negatywny |
| `t_skarb_strefy` (krok 5, w systemie): sesja zapisuje i czyta swój kawałek; NEGATYWNE: inna sesja go nie widzi, aplikacja nie nazwie obiektu dysku; „Wyczyść dane sesji” | funkcjonalny, negatywny |
| `t_kvm`: `/dev/kvm` tylko root, maszyna z pamięcią z memfd bez prawa wykonania wykonuje kod gościa | funkcjonalny |
| `t_strefa_klatka`: klatka VMM (UID, capabilities, seccomp, cgroup z wątkami, tylko przekazane deskryptory); zabity VMM — koniec tylko strefy | funkcjonalny, negatywny |
| `t_proby` (profil strefy): bez `open` (także `/dev/kvm`), `socket`, procesów, pamięci wykonywalnej | negatywny |
| `test_lacznik`: SOCKS5 (powitanie, prośby, odmowy: BIND, UDP, IPv6, port 0, zła nazwa, 0.0.0.0, pętla zwrotna), DNS (pytanie; odpowiedzi: inny identyfikator, inne pytanie, ucięte, NXDOMAIN, CNAME, adres prywatny, wskaźniki), reguła D3, komunikaty `STREFA_*` | funkcjonalny, negatywny |
| `fuzz_lacznik`: dowolna prośba SOCKS5 i odpowiedź DNS | fuzzing |
| `scripts/test-przegladarka.sh` | QEMU: przeglądarka, strona przez łącznik, formularz, odmowa D3, strona zajmująca 1400 MiB (strefa żyje), bez przycisków okna Firefoksa, logi bez adresów (opis w `docs/TESTY.md`) |
| `scripts/test-strefa.sh` | QEMU, funkcjonalny i negatywny (opis w `docs/TESTY.md`) |

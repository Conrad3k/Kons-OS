# Komponent: strefa (VMM — mikro-maszyna wirtualna dla obcych aplikacji)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.10.0 krok 1: strefa próbna, tylko w obrazie testowym; krok 2: przeglądarka — sesje Ogólna, Bank, Prywatna, jednorazowe, ADR 0043); PRZETESTOWANE na hoście (`test_virtio`, `test_strefa_start`, `fuzz_virtq`), w systemie (`t_kvm`, `t_strefa_klatka`, próba w klatce `t_proby`) i w QEMU (`scripts/test-strefa.sh`: okno w ramce, wejście, plakietka, zamykanie, 4 złośliwe tryby gościa, komputer bez wirtualizacji) — w emulacji AMD SVM; na prawdziwym sprzęcie — po wydaniu (krok 3) |
| **Pliki** | `src/strefa/`: `main.c` (start, pętla wątku głównego, koniec), `kvm.c` (maszyna, procesory, wyjścia), `start.{c,h}` (start jądra bez firmware: strona zerowa, e820, tablice stron, GDT, tablica MP), `virtq.{c,h}` (kolejka virtio), `mmio.{c,h}` (rejestry virtio-mmio), stróż `stroz.c`, urządzenia `blk.c`, `konsola.c`, `rng.c`, `wejscie.c`, `gpu.c`, `vsock.c`, okno `okno.{c,h}`; wspólne `strefa.h` (tabela urządzeń `K1_URZADZENIA`); gość próbny `src/strefa/gosc/` (`main.c`, `obraz.c`, `zlosliwy.c`); nadzorca `src/nadzorca/strefy.c`, `profile.c` (`KLATKA_STREFA`); ekran `src/ekran/strefy.c` (ramka); tabela stref `K1_STREFY` w `src/wspolne/k1_uslugi.h`; jądro strefy `jadro/strefa.config` |
| **Decyzje** | ADR 0042 (T1–T6, T10, zaufana ramka), ADR 0020 (kierunek), decyzje właściciela D1 (szkic B2, S1), D4 (testy w emulacji) |

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
| deskryptory od nadzorcy | 3: kanał do ekranu (rola `STREFA`, numer strefy), 4: `/dev/kvm`, 5: jądro strefy (tylko odczyt), 6: obraz strefy (tylko odczyt), 7: kanał do nadzorcy — połączenia przeglądarki (0.10.0 krok 2) |
| seccomp (poza bazą) | `ioctl` tylko typu KVM (0xAE), `memfd_create`, `ftruncate`, `eventfd2`, `futex`, `getrandom`, `pread64`, `clone` tylko z `CLONE_THREAD`; od kroku 2: `shutdown`, `getsockopt` tylko `SO_ERROR` (gniazda od nadzorcy; VMM nie otwiera gniazd) |
| cgroup | pamięć gościa + 160 MiB; `pids.max` = 1 + procesory + 1 (stróż) + 1 (zadanie KVM w procesie maszyny) |
| `RLIMIT_FSIZE` | pamięć gościa (memfd jej pamięci) |
| argumenty | numer strefy, tryb gościa testowego (0; 1–4 tylko od programu testowego) |

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
- **Koniec:** gość sam (reset), zły dostęp, `ZAMKNIJ` od ekranu → „zamknij” przez vsock, po 10 s maszyna wyłączana.

## Urządzenia virtio-mmio (`K1_URZADZENIA`, okno 4 KiB od 0xd0000000, linia przerwania od 5)
| Urządzenie | Kolejki | Co robi | Co kończy maszynę |
|---|---|---|---|
| obraz (blk) | 1 | odczyt z obrazu strefy (`pread`), `VIRTIO_BLK_F_RO`, `SEG_MAX` | zapis do obrazu, odczyt > 4 MiB, prośba bez nagłówka |
| konsola | 2 | wiersze gościa do logu (strefa próbna) albo licznik | — |
| losowość (rng) | 1 | `getrandom` do 4 KiB na prośbę | — |
| klawiatura, wskaźnik (input) | 2 + 2 | zdarzenia `EV_KEY`, `EV_ABS` (0…32767), `EV_SYN` z ekranu; bez wolnego bufora — zdarzenie przepada (licznik) | — |
| ekran (gpu 2D) | 2 | zasoby ≤ 4096 × 4096 (razem ≤ 96 MiB), tło z pamięci gościa (sprawdzone przy dołączeniu), przeniesienie, pokazanie, odświeżenie → okno | polecenie krótsze niż jego struktura, brak miejsca na odpowiedź |
| vsock | 3 | kanał sterujący (port 1 hosta): „gotowa”, „zamykam” od gościa (przeglądarka, gdy się zmieni — „wejscie <n>”: bity `znaki_wejscia.h`, co kompozytor zrobił z wejściem; w logu tylko tak/nie), „zamknij” do gościa; port 2 (krok 2): połączenia przeglądarki od przekaźnika — do 128 naraz, każde z kredytem 64 KiB w obie strony, bajty do łącznika (`polaczenia.c`); inne porty — RST | zły adres albo typ, przekroczony kredyt, za mały bufor gościa |

Wspólne dla wszystkich (`virtq.c`, `mmio.c`): deskryptor poza pamięcią, pętla deskryptorów, deskryptor pośredni, do
odczytu po do zapisu, skok indeksu, kolejka poza pamięcią albo niewyrównana, rejestr czytany nie po 4 bajty — koniec.

## Okno
Klient ekranu z rolą `STREFA`, jedno okno (≤ 1024 × 720, z rozmiaru ekranu; tyle widzi gość jako wyświetlacz). Przed
pierwszym obrazem gościa: „Uruchamiam: <tytuł>…” z ikoną strefy i paskiem w jej kolorze (D1: S1). Obraz gościa — zmienione
prostokąty kopiowane do wolnej połówki powierzchni (nieprzezroczyste). Klawisze i wskaźnik od ekranu (tylko przy fokusie)
→ urządzenia wejścia; utrata fokusu puszcza wszystko. Ramkę, znaczek i plakietkę rysuje ekran (`docs/komponenty/ekran.md`).

## Przeglądarka (0.10.0 krok 2, ADR 0043)
Wiersze `K1_STREFY` „Ogólna” (szałwia, w menu Start jako „Przeglądarka”), „Bank” (łupek), „Prywatna” (wrzos): obraz
`/rdzen/strefy/przegladarka.img`, 1536 MiB, 2 procesory (w emulacji QEMU — 1), jednorazowe (krok 3: pamiętają).
- **Obraz** (`scripts/zbuduj-strefe.sh`): pakiety Debiana 13 z tabeli `strefy/przegladarka.txt` (wersje i sumy;
  odświeżenie `scripts/odswiez-strefe.py` — podpisy Debiana sprawdzane kluczami z `strefy/klucze/`), rozpakowane bez
  skryptów pakietów, bez powłoki; nasze: `/init` (`gosc`), `/usr/lib/k1/kompozytor`, `/usr/lib/k1/przekaznik`, mapa
  klawiatury (`strefy/klawiatura-pl.xkb`), ustawienia Firefoksa (`strefy/przegladarka/k1.js`, `policies.json`).
- **`gosc`** (tryb przeglądarki, `gosc/przegladarka.c`): tmpfs na `/tmp`, `/run`, `/home`, `/dev/shm`, pętla zwrotna, nowy
  `machine-id`; przekaźnik, kompozytor, Firefox jako użytkownik 1000; koniec przeglądarki = koniec maszyny; „zamknij” →
  SIGTERM i 5 s.
- **Kompozytor** (`src/strefa/kompozytor/`): Wayland bez libwayland — tabela interfejsów `K1_INTERFEJSY` (`wl_compositor`,
  `wl_shm`, `wl_subcompositor`, `wl_seat`, `wl_output`, `wl_data_device_manager` bez schowka, `xdg_wm_base`), każda
  wiadomość sprawdzana według sygnatury (zła — koniec tego klienta). Okno na cały wyświetlacz, dialogi na środku,
  wyskakujące menu z pozycjonerem; zdarzenia wejścia do każdego `wl_pointer`/`wl_keyboard` klienta (Firefox ma po dwa);
  „klatka gotowa” także po zatwierdzeniu bez zmian (inaczej GTK przestaje rysować).
- **Przekaźnik** (`src/strefa/przekaznik/`): SOCKS5 na 127.0.0.1:1080 → jedno połączenie vsock (port 2) na każde.
- **Łącznik** (moduł VMM: `lacznik.c` czyste protokoły, `polaczenia.c` stan połączeń): SOCKS5 — tylko CONNECT, IPv4 albo
  nazwa (BIND, UDP, IPv6 — odmowa kodem SOCKS); nazwy — DNS (rekord A) przez gniazdo UDP od nadzorcy do serwera z DHCP;
  połączenie — `STREFA_POLACZ` do nadzorcy (reguła D3, limit), gniazdo TCP w `STREFA_GNIAZDO`, odmowa w `STREFA_ODMOWA`.
  W logu tylko liczniki: „połączenia przeglądarki: N, nazwy N, odmowy N”.

## Komunikaty
| Komunikat | Kierunek | Pola |
|---|---|---|
| `STREFA_POLACZ` | VMM → nadzorca (fd 7) | obiekt = połączenie 1…128; TCP: a = adres IPv4 (`k1_siec_serwer_dobry`), b = port 1…65535, c = `K1_STREFA_TCP`; DNS: a = b = 0, c = `K1_STREFA_DNS`; d = e = 0 |
| `STREFA_GNIAZDO` | nadzorca → VMM (+ 1 deskryptor) | obiekt = połączenie, a = `K1_STREFA_TCP` (gniazdo TCP w trakcie łączenia) / `K1_STREFA_DNS` (gniazdo UDP do serwera DNS, port 53), reszta 0 |
| `STREFA_ODMOWA` | nadzorca → VMM | obiekt = połączenie, a = `K1_STREFA_ODM_*` (adres poza D3, brak sieci albo DNS, limit, błąd gniazda), reszta 0 |
| `URUCHOM_STREFE` | gui → nadzorca (fd 4); program testowy → nadzorca | `a` strefa 1…`K1_STREFY_MAKS`, `b` tryb gościa testowego 0…4 (≠ 0 tylko od programu testowego; gui — odrzucone), reszta 0 |
| `STREFA_STAN` | nadzorca → gui (fd 5) | `a` strefa, `b` powód: `K1_STREFA_BEZ_WIRTUALIZACJI`, `_ZA_MALO_PAMIECI`, `_BRAK_OBRAZU`; reszta 0 |
| `KLIENT` (rola `STREFA`) | nadzorca → ekran | `b` = numer strefy (1…`K1_STREFY_MAKS`) |
| `OKNO` | ekran → gui | okno strefy: `a` = 0, `c` = numer strefy |

## Gość próbny `gosc` (`src/strefa/gosc/`, `/init` obrazu `/rdzen/strefy/probna.img` — tylko obraz testowy)
Montuje `/proc`, `/sys`; rysuje wzór przez KMS (bufor „dumb”): granatowe tło, żółte rogi, pas — zielony po parzystej liczbie
klawiszy, pomarańczowy po nieparzystej — i biały kwadrat w miejscu ostatniego kliknięcia; czyta wejście (`/dev/input`),
loguje liczniki; „gotowa” i „zamykam” przez vsock; kończy przez `reboot`. Tryby złośliwe (`k1.tryb=1…4`): sam steruje
urządzeniem w gnieździe LOSOWOSC przez `/dev/mem` (VMM nie podaje go jądru gościa i rezerwuje 1 MiB pamięci) — deskryptor
poza pamięcią, pętla deskryptorów, odczyt 8 MiB, zapis do obrazu (w trybach 3–4 VMM wkłada tam virtio-blk).

## Testy
| Test | Rodzaj |
|---|---|
| `test_virtio`: łańcuchy, zapis i odczyt w łańcuchu, zawijanie pierścienia; wrogie łańcuchy (poza pamięcią, pętle, pośrednie, kolejność, skok indeksu); rejestry, cechy, gotowość kolejki, przerwania, reset | funkcjonalny, negatywny |
| `test_strefa_start`: strona zerowa, e820, tablice stron, GDT, tablica MP (sumy); złe jądra i pamięci | funkcjonalny, negatywny |
| `fuzz_virtq`: dowolna pamięć gościa i zapisy rejestrów | fuzzing |
| `t_kvm`: `/dev/kvm` tylko root, maszyna z pamięcią z memfd bez prawa wykonania wykonuje kod gościa | funkcjonalny |
| `t_strefa_klatka`: klatka VMM (UID, capabilities, seccomp, cgroup z wątkami, tylko przekazane deskryptory); zabity VMM — koniec tylko strefy | funkcjonalny, negatywny |
| `t_proby` (profil strefy): bez `open` (także `/dev/kvm`), `socket`, procesów, pamięci wykonywalnej | negatywny |
| `test_lacznik`: SOCKS5 (powitanie, prośby, odmowy: BIND, UDP, IPv6, port 0, zła nazwa, 0.0.0.0, pętla zwrotna), DNS (pytanie; odpowiedzi: inny identyfikator, inne pytanie, ucięte, NXDOMAIN, CNAME, adres prywatny, wskaźniki), reguła D3, komunikaty `STREFA_*` | funkcjonalny, negatywny |
| `fuzz_lacznik`: dowolna prośba SOCKS5 i odpowiedź DNS | fuzzing |
| `scripts/test-przegladarka.sh` | QEMU: przeglądarka, strona przez łącznik, formularz, odmowa D3, logi bez adresów (opis w `docs/TESTY.md`) |
| `scripts/test-strefa.sh` | QEMU, funkcjonalny i negatywny (opis w `docs/TESTY.md`) |

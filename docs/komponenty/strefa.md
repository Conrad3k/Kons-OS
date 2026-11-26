# Komponent: strefa (VMM — mikro-maszyna wirtualna dla obcych aplikacji)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.10.0 krok 1: strefa próbna, tylko w obrazie testowym; krok 2: przeglądarka — sesje Ogólna, Bank, Prywatna, ADR 0043; krok 5: sesje, które pamiętają — dysk strefy w skarbcu); PRZETESTOWANE na hoście (`test_virtio`, `test_strefa_start`, `fuzz_virtq`), w systemie (`t_kvm`, `t_strefa_klatka`, próba w klatce `t_proby`) i w QEMU (`scripts/test-strefa.sh`: okno w ramce, wejście, pasek tytułu K1, zamykanie, 4 złośliwe tryby gościa, komputer bez wirtualizacji) — w emulacji AMD SVM; na prawdziwym sprzęcie — po wydaniu (krok 3) |
| **Pliki** | `src/strefa/`: `main.c` (start, pętla wątku głównego, koniec), `kvm.c` (maszyna, procesory, wyjścia), `start.{c,h}` (start jądra bez firmware: strona zerowa, e820, tablice stron, GDT, tablica MP), `virtq.{c,h}` (kolejka virtio), `mmio.{c,h}` (rejestry virtio-mmio), stróż `stroz.c`, powiadomienia kolejek `powiadomienia.c` (0.11.2), urządzenia `blk.c`, `konsola.c`, `rng.c`, `wejscie.c`, `gpu.c` (obraz gościa z pamięci przez czyste `tlo.{c,h}` — każdy adres przez `gp_wsk`, także ponad 4 GiB; do 0.10.5 kopia z górnej części pamięci szła spod złego adresu: czarne okno przeglądarki na X230 z 8 GB, test `test_tlo`), `vsock.c`, dysk strefy `dane.c` + `dysk.{c,h}` (0.10.0 krok 5), okno `okno.{c,h}`; wspólne `strefa.h` (tabela urządzeń `K1_URZADZENIA`); gość `src/strefa/gosc/` (`main.c`, `obraz.c`, `zlosliwy.c`, `przegladarka.c`, `pamiec.c`, `dysk.c`; 0.10.5 krok 5: test Raptor Bridge `most.{c,h}`, `most_fazy.c`); nadzorca `src/nadzorca/strefy.c`, `profile.c` (`KLATKA_STREFA`), sesje `strefy_ust.c` + `sesje.{c,h}`; ekran `src/ekran/strefy.c` (ramka); tabela stref `K1_STREFY` w `src/wspolne/k1_uslugi.h`; jądro strefy `jadro/strefa.config` |
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
| seccomp (poza bazą) | `ioctl` tylko typu KVM (0xAE; od 0.11.2 też `KVM_IOEVENTFD` — powiadomienia kolejek, `KVM_CHECK_EXTENSION` — ile procesorów ma komputer), `memfd_create`, `ftruncate`, `eventfd2`, `futex`, `getrandom`, `pread64`, `clone` tylko z `CLONE_THREAD`; od kroku 2: `shutdown`, `getsockopt` tylko `SO_ERROR` (gniazda od nadzorcy; VMM nie otwiera gniazd); od 0.10.1 krok 2: `fallocate` tylko z `PUNCH_HOLE \| KEEP_SIZE` (balon oddaje strony K1, ADR 0044 T2) |
| cgroup | pamięć gościa (od 0.10.1 krok 2: jej górna granica — ADR 0044 T4, stała przez całe życie strefy) + 184 MiB (od kroku 5: + pamięć podręczna dysku 16 MiB i bufor prośby 4 MiB); `pids.max` = 1 + procesory + 1 (stróż) + 1 (wątek dysku, krok 5) + 1 (wątek mostu szybkiej grafiki, 0.10.9 krok 6) + 1 (wątek powiadomień, 0.11.2) + 1 (zadanie KVM w procesie maszyny); deskryptorów o 36 więcej (eventfd na kolejkę każdego urządzenia, 0.11.2); z pośrednikiem grafiki pamięć + `K1_POSR_PAMIEC` (bufory karty zmapowane w VMM-ie) |
| `RLIMIT_FSIZE` | pamięć gościa (memfd jej pamięci; od 0.10.1 krok 2 — górna granica) |
| argumenty | numer strefy, tryb gościa testowego (0; 1–4 złośliwy, 5 licznik na dysku; od 0.10.1 krok 2: 6–8 złośliwy dla balonu, 9 pamięć; od 0.10.5 krok 5: 13 test Raptor Bridge, 14–15 złośliwy dla vsock — gość sam steruje urządzeniem vsock, którego jego jądro nie dostaje; tylko od programu testowego), od 0.10.1 krok 2 pamięć gościa w MiB (górna granica od nadzorcy: `pamiec_stref.c`) |

## Maszyna
- **Pamięć:** memfd z `MFD_NOEXEC_SEAL`; nigdy wykonywana przez K1. Od adresu 0 gościa do 3 GiB (poniżej urządzeń), od
  0.10.1 krok 2 (ADR 0044 T5) reszta większego gościa od 4 GiB — drugi wpis RAM w e820 i drugie miejsce pamięci KVM z tego
  samego memfd (`gosc_pamiec.dol`; `gp_wsk` nie przepuszcza zakresu przez dziurę, kolejki virtio czytane tylko przez nie).
  Od 0.11.3 do 256 GiB (`K1_STREFA_PAMIEC_MAKS`; było 64 GiB); okno szybkiej grafiki (1 GiB) na 32 GiB albo zaraz nad
  pamięcią gościa (`most_okno_baza`), a całość pod granicą adresów gościa z CPUID 0x80000008 (`kvm_pamiec_w_granicach`).
  Od 0.11.2 **duże strony** (2 MiB): VMM prosi o nie dla pamięci gościa (`madvise(MADV_HUGEPAGE)`, jak QEMU), jądro K1
  daje je memfd tylko na prośbę (`TRANSPARENT_HUGEPAGE_SHMEM_HUGE_ADVISE`, `jadro/k1.config`) — jeden wpis w TLB procesora
  i w tablicach EPT/NPT KVM obejmuje 512 stron, krótsze przejścia tablic stron przy każdym chybieniu w TLB. Gdy jądro
  nie złoży dużej strony (pamięć pocięta), zostają małe — jak wcześniej. Balon wycina strony jak dotąd (duża strona się
  dzieli; zgłoszenia wolnych stron i tak idą kawałkami po 2 MiB).
- **Wielkość i balon** (0.10.1 krok 2, ADR 0044): gość dostaje swoją **górną granicę** — pamięć komputera minus rezerwa
  K1 (D2: 768 MiB do 6 GiB pamięci komputera, 1 GiB od 6 GiB; nigdy mniej niż pamięć na start z wiersza `K1_STREFY`;
  `src/nadzorca/pamiec_stref.c`) — ale K1 trzyma tylko strony, których gość dotknął; zwolnione wracają do K1 (balon,
  niżej). Raz na sekundę VMM mówi nadzorcy `STREFA_PAMIEC`. Od 0.10.1 krok 3: gdy sterownik ustawia `DRIVER_OK`,
  urządzenie zagląda do każdej gotowej kolejki (`mmio.c`) — powiadomienie sprzed `DRIVER_OK` nie przepada (sterownik
  balonu w Linuksie wkłada pierwszy bufor statystyk i powiadamia, zanim urządzenie jest gotowe; do 0.10.1 krok 3
  statystyki nie dochodziły nigdy — „wolne w gościu” było nieznane).
- **Start:** 64-bitowy protokół startu Linuksa z pliku bzImage — jądro od 1 MiB, strona zerowa (nagłówek z jądra, e820,
  linia poleceń), GDT, tablice stron 1:1 (4 GiB), tablica MP (procesory, I/O APIC; bez ACPI). Linia poleceń gościa:
  urządzenia virtio-mmio, `console=hvc0 root=/dev/vda ro rootfstype=squashfs init=/init reboot=k panic=-1 k1.tryb=N`.
- **Procesory:** wątek na procesor; CPUID od KVM z identyfikatorem APIC, bitem „hiperwizor” i bez LA57 (ADR 0042 T10).
  Liczba z wiersza `K1_STREFY` to najwięcej, ile strefa dostaje — od 0.11.2 nigdy więcej, niż ma komputer
  (`KVM_CAP_NR_VCPUS`: procesory włączone w K1); sesje przeglądarki mają w wierszu od 0.11.3 `K1_STREFA_PROCESORY_MAKS`
  (254 — wszystkie procesory komputera; 0.11.2: 4, do 0.11.1: 2). 254, bo tablica MP daje procesorowi 8-bitowy
  identyfikator APIC (255 = wszystkie, jeden bierze I/O APIC); tablica od 0.11.3 pod `ST_MP_TABELA` (0x9e000), wskaźnik
  `_MP_` dalej w ostatnim KiB pamięci podstawowej. CPUID od 0.11.3 z bitem TSC-deadline (`KVM_CAP_TSC_DEADLINE_TIMER`,
  poza emulacją QEMU).
- **Przerwania:** w jądrze (`KVM_CREATE_IRQCHIP`, `KVM_CREATE_PIT2`), irqfd na urządzenie (linie 5…11).
- **Powiadomienia kolejek** (0.11.2, `powiadomienia.c`): zapis gościa „kolejka N” do `QueueNotify` nie wychodzi z KVM —
  KVM sam sygnalizuje eventfd tej kolejki (`KVM_IOEVENTFD` z `datamatch`), procesor wraca od razu do gościa, a wątek
  powiadomień oddaje powiadomienie urządzeniu pod zamkiem (`mmio_powiadomienie`, to samo co zapis rejestru). Wcześniej
  każde powiadomienie (każda klatka obrazu, pakiet przeglądarki, polecenie karty, odczyt obrazu) zatrzymywało procesor
  gościa: wyjście do VMM z ochronami procesora w drodze powrotnej, czekanie na zamek i praca urządzenia na jego wątku.
  Zapis innej wartości wychodzi do VMM jak dotąd (nieistniejąca kolejka — pomijana).
- **Wyjścia:** MMIO urządzeń (pod zamkiem), reset przez kontroler klawiatury (koniec gościa), potrójny błąd, inne
  nieobsługiwane — koniec; dostęp do MMIO zgłoszony jako „w trakcie przerwania” — ponawiany do 8 razy (emulacja QEMU,
  ADR 0042 T10).
- **Koniec:** gość sam (reset), zły dostęp, `ZAMKNIJ` od ekranu → „zamknij” przez vsock, po 40 s maszyna wyłączana (do
  0.10.0 krok 5: 10 s — za mało na przeglądarkę po ciężkiej stronie i zapis dysku strefy, przebieg CI 132).
- **Zamykanie w tle** (0.10.2 krok 1, S3, T2): po `ZAMKNIJ` od ekranu albo `STREFA_ZAMKNIJ` od nadzorcy (K1 się wyłącza)
  VMM od razu usuwa swoje okno u ekranu (`USUN` — „okno strefy zamknięte — maszyna kończy się w tle”), mówi nadzorcy
  `STREFA_ZAMYKA` i prosi gościa o „zamknij”; reszta jak wyżej — maszyna kończy się w tle (przeglądarka zapisuje sesję).
  Wcześniej okno wisiało, dopóki maszyna się nie skończyła (do 20–40 s).
- **Powód końca** (krok 3, S26; `main.c` `kod_wyjscia`): kod wyjścia VMM — 0, gdy zamknął użytkownik (`ZAMKNIJ` od ekranu)
  albo program strefy zamknął się zwyczajnie; `K1_STREFA_KONIEC_PAMIEC` / `_AWARIA`, gdy program strefy skończył się sam
  i powiedział dlaczego („koniec …” na kanale sterującym); `_BLAD`, gdy maszyna stanęła bez słowa (jądro gościa, zły
  dostęp). Nadzorca (`strefy.c` `strefa_koniec`, czysta część `strefy_powod.c`) dokłada SIGKILL — od 0.10.1 krok 1 (S3)
  rozróżnia po zdarzeniach klatki VMM (`memory.events`, różnica od startu strefy): `oom` wzrosło → `_LIMIT` (strefa
  zużyła całą pamięć, którą K1 jej daje — limit własnej klatki), tylko `oom_kill` → `_PAMIEC_K1` (pamięć komputera),
  ani jedno → `_BLAD`; nieczytelne → `_PAMIEC_K1` jak dawniej. (`oom_kill` liczy zabicia przez każdy OOM, także całego
  komputera — Documentation/admin-guide/cgroup-v2.rst; o limicie klatki mówi tylko `oom`.) W logu: `strefa N: pamięć
  klatki — szczyt X MiB (od startu K1), limit osiągnięty A, zabite B`. Powód idzie do gui (`STREFA_STAN`) — okienko
  „<tytuł> zamknęła się”; bez okienka, gdy zamknął użytkownik, K1 zatrzymał strefę sam (`bez_liczenia`) albo to
  złośliwy gość testu. Wcześniej okno po prostu znikało.
- **Liczniki** (0.10.1 krok 1, S13): `gosc` co 5 s wysyła „liczniki A Z K” (pamięć dostępna w gościu w MiB, zajęty zram
  w MiB, procesy zabite przez OOM gościa); VMM dokłada swoje i loguje jedną linię (najwyżej co 4 s, niezależnie od tego,
  ile przyśle gość): `liczniki: gość — dostępna A MiB, zram Z MiB, zabite K; pamięć w K1 W MiB, oddane O MiB, balon B
  MiB; grafika: zasoby N, M MiB; połączenia P; vsock czeka Q, odrzucone R` (pamięć w K1, oddane, balon — od 0.10.1 krok
  2); przy końcu maszyny jeszcze raz (`liczniki przy końcu`). Same liczby (niezmiennik 8).
- **Pomiar drogi obrazu** (0.10.11 krok 1, plan T1): kompozytor strefy mierzy swoje ogniwa (czekanie na kartę —
  `VIRTGPU_WAIT`, kopia bufora `wl_shm`, składanie sceny, wysłanie do jądra strefy — `DIRTYFB`) i liczy klatki oraz bufory
  karty i pamięci (czy przeglądarka rysuje kartą); co 10 s wiersz „pomiar” z 11 liczbami (µs, liczby) przez `gosc` do VMM-a
  (`linia.c`). VMM (`pomiar.c`) dokłada swoje: przeniesienie z pamięci gościa (`TRANSFER_TO_HOST_2D`), kopia do okna, czekanie
  na ekran (od zatwierdzenia do `RAMKA`), prośby mostu do pośrednika (tam i z powrotem), od klawisza / ruchu / kółka do
  następnego obrazu gościa (do 1 s), rodzaje mapowań buforów karty (WB / UC / WC — plan P3). Co 10 s, gdy są obrazy, dwa
  wiersze (od kroku 2 w wierszu gościa 12. liczba — klatki prosto z bufora przeglądarki, „bez sceny”): `pomiar strefy (10 s,
  średnio/95%): klatki … (bez sceny …), bufory karty …, pamięci …; czekanie na kartę A/B ms, kopia …, scena
  …, wysłanie …` i `pomiar VMM (10 s, średnio/95%): obrazy gościa …, przeniesienie …; okno … klatek, kopia …, ekran …; most N
  próśb, …; wejście→obraz …; mapy karty WB …, UC …, WC …`. Same liczby (niezmiennik 8); jak mierzyć na laptopie — `docs/TESTY.md`.
- **Kto się skończył** (0.10.1 krok 1, S15): gdy przeglądarka albo kompozytor skończy się sam, `gosc` wysyła przed
  „koniec …” linię „kto P K R” — P 1 przeglądarka / 2 kompozytor, K kod wyjścia albo 128 + sygnał, R numer reguły
  kompozytora, którą przeglądarka złamała ostatnio (0: żadnej). VMM loguje `gość: przeglądarka zakończony (kod 1) —
  kompozytor odrzucił przeglądarkę (reguła 28)`. Linie z liczbami sprawdza `linia.c` (czyste, `test_virtio`): słowo,
  potem dokładnie tyle liczb dziesiętnych, każda w limicie — nic więcej; tekstu gościa nie ma w logu.

## Urządzenia virtio-mmio (`K1_URZADZENIA`, okno 4 KiB od 0xd0000000, linia przerwania od 5)
| Urządzenie | Kolejki | Co robi | Co kończy maszynę |
|---|---|---|---|
| obraz (blk) | 1 | odczyt z obrazu strefy (`pread`), `VIRTIO_BLK_F_RO`, `SEG_MAX` | zapis do obrazu, odczyt > 4 MiB, prośba bez nagłówka |
| konsola | 2 | wiersze gościa do logu (strefa próbna) albo licznik | — |
| losowość (rng) | 1 | `getrandom` do 4 KiB na prośbę | — |
| klawiatura, wskaźnik (input) | 2 + 2 | zdarzenia `EV_KEY`, `EV_ABS` (0…32767), `EV_SYN` z ekranu; od 0.10.0 krok 4 wskaźnik z kółkiem: `REL_WHEEL`, `REL_HWHEEL` i `*_HI_RES` (1/120 ząbka) z `PRZEWIN` — z kółka oba, z touchpada tylko `*_HI_RES`; bez wolnego bufora — zdarzenie przepada (licznik) | — |
| ekran (gpu 2D) | 2 | zasoby ≤ 4096 × 4096 (razem ≤ 96 MiB), tło z pamięci gościa (sprawdzone przy dołączeniu), przeniesienie, pokazanie, odświeżenie → okno; od 0.10.9 krok 6 z pośrednikiem grafiki także 3D — niżej „Szybka grafika” | polecenie krótsze niż jego struktura, brak miejsca na odpowiedź; z pośrednikiem: polecenie dłuższe niż 1 MiB + 32 B, strumień `SUBMIT_3D` dłuższy niż polecenie, więcej czekających na kartę niż mieści kolejka |
| vsock | 3 | kanał sterujący (port 1 hosta; od 0.10.11 wiersz do 160 znaków): „gotowa”, „zamykam” od gościa, „zamknij” do gościa; od kroku 3 od gościa także „koniec pamiec\|awaria\|sam” (dlaczego program strefy skończył się sam), „pamiec strona” (strona zakończona z braku pamięci — licznik w logu), „etap okno” (pierwsze okno przeglądarki — czas od startu maszyny w logu, S30; od 0.10.2 koniec ekranu startu), od 0.10.2 krok 1 „etap start” (program strefy pokaże okno — ekran startu zostaje do „etap okno”); od 0.10.1 krok 1 „liczniki A Z K” i „kto P K R” (same liczby, `linia.c`); od 0.10.11 krok 1 „pomiar” i 11 liczb (pomiar kompozytora); od 0.11.0 krok 1 „pelny 1” / „pelny 0” od gościa (okno prosi o pełny ekran / wyszło z niego — do ekranu, najwyżej 4 zmiany na sekundę) i „pelny 0” do gościa (K1 zakończył pełny ekran); od kroku 5 „zamykanie gotowe\|sygnal\|po czasie” (jak przeglądarka skończyła się po „zamknij” — czas w logu); każdy inny wiersz — tylko licznik; port 2 (krok 2): połączenia przeglądarki od przekaźnika — do 128 naraz, każde z kredytem 64 KiB w obie strony, bajty do łącznika (`polaczenia.c`); inne porty — RST | zły adres albo typ, przekroczony kredyt, za mały bufor gościa |

| balon (0.10.1 krok 2, ADR 0044) | 4 | `balon.c`: cechy `STATS_VQ`, `DEFLATE_ON_OOM`, `REPORTING`; kolejki 0 pompowanie, 1 spuszczanie (listy le32 numerów stron 4 KiB, najwyżej 256), 2 statystyki (VMM trzyma bufor i co 5 s oddaje go po świeże — `AVAIL` do `STREFA_PAMIEC`), 3 zgłaszanie wolnych bloków (bufory do zapisu, każdy blok z całych stron); strony z balonu i zgłoszone bloki → `fallocate(PUNCH_HOLE \| KEEP_SIZE)` na memfd — wracają do K1; konfiguracja: `num_pages` (cel od nadzorcy, `STREFA_BALON` → zmiana konfiguracji), `actual` (pisze gość; log, gdy dojdzie do celu); balon domyślnie pusty. Czyste części: `strony.c` | strona poza RAM gościa albo w dziurze, lista nie z całych numerów albo dłuższa niż 256, blok nie z całych stron, statystyki złej długości albo o nieznanym/powtórzonym znaczniku, zapis konfiguracji poza `actual` albo `actual` ponad pamięć gościa, zgłoszenie z częścią do odczytu |
| dysk strefy (blk, krok 5) | 1 | drugi dysk gościa (`/dev/vdb`) do zapisu i odczytu, `FLUSH`, `SEG_MAX`; 2 GiB w kawałkach po 64 KiB przez pamięć podręczną `dysk.c`; strefa, która zapomina: 0 sektorów | prośba bez nagłówka, odczyt albo zapis > 4 MiB |
| dźwięk (virtio-snd, 0.12.0 krok 3, ADR 0054 T5) | 4 | `dzwiek.c` + czyste `dzwiek_prosby.c`: jeden strumień wyjścia, 48 kHz / stereo w formacie gościa — od 0.12.3 (ADR 0055) S16, S32 albo FLOAT (tabela `DZW_FORMATY`; VMM przelicza je bez straty do pierścienia F32 usługi dźwięku: S16 dokładnie, S32 do 24 bitów, FLOAT kopiuje — liczby sprawdza usługa; log „gość gra w formacie …” raz na format), bez gniazd, map kanałów, kontrolek i wejścia (D1 = A); kolejki 0 sterowanie (`PCM_INFO`, `SET_PARAMS`, `PREPARE`, `START`, `STOP`, `RELEASE` — w stanach z virtio 5.14.6.6.1, inne `BAD_MSG`, inny format `NOT_SUPP`), 1 zdarzenia (bufory czekają — nie ma zdarzeń), 2 transfery (nagłówek: strumień 0, całe ramki formatu strumienia, ≤ 64 KiB próbek → kopia do pierścienia klienta `STREFA` usługi dźwięku; transfer wraca do gościa, gdy jego ostatnia ramka została zagrana — usługa zgłasza postęp co 1024 ramki; w pierścieniu najwyżej 8192 ramek niezagranych), 3 wejście (nieużywane); `RELEASE` oddaje gościowi wszystkie transfery. Kanał do usługi od nadzorcy na fd 7 (`DZWIEK_KANAL`); bez usługi albo karty — własny zegar urządzenia (próbki przepadają, film idzie w swoim tempie) | prośba sterująca bez miejsca na odpowiedź (4 B), transfer bez miejsca na stan (8 B) |

Wspólne dla wszystkich (`virtq.c`, `mmio.c`): deskryptor poza pamięcią, pętla deskryptorów, deskryptor pośredni, do
odczytu po do zapisu, skok indeksu, kolejka poza pamięcią albo niewyrównana, rejestr czytany nie po 4 bajty — koniec.

## Szybka grafika (0.10.9 krok 6, ADR 0049 pkt 4, 7; plan T12, S40–S47)
Sesja z włączonym przełącznikiem „Szybka grafika” (Ustawienia → Przeglądarka; domyślnie tylko Ogólna — D5),
na komputerze, którego karta oddziela pracę stref (pośrednik grafiki: „umie” — IdeaPad; X230 nie), dostaje od nadzorcy
na **fd 10** kanał do swojego **pośrednika grafiki** (`docs/komponenty/posrednik.md`), a VMM pamięć większą
o `K1_POSR_PAMIEC` (bufory karty zmapowane w VMM-ie liczą się do jego klatki). Bez tego virtio-gpu zostaje 2D, a
przeglądarka rysuje procesorem.
- **Start** (`gpu_lacze.c`): pierwsze słowo pośrednika (`POSR_UMIE`: numer karty PCI, rewizja, wersja sterownika),
  okno strumienia (memfd 1 MiB z pieczęciami) → `POSR_OKNO`; dopiero wtedy virtio-gpu ma cechy `VIRGL`, `RESOURCE_BLOB`,
  `CONTEXT_INIT`, jeden zbiór możliwości (capset `DRM`, 160 bajtów jak `virgl_renderer_capset_drm` Mesy: kontekst i915,
  numer karty), obszar pamięci widocznej dla gościa (rejestry pamięci dzielonej virtio-mmio: 1 GiB od 32 GiB adresów
  gościa) i linia poleceń gościa `k1.grafika=1`. Pośrednik mówi „nie” albo milczy 10 s → strefa rysuje procesorem.
- **Polecenia 3D** (`gpu_most.c`, czyste — `test_gpu_most`, `fuzz_gpu_most`): kontekst z capsetem `DRM` (każdy proces
  strefy z otwartą kartą: najwyżej `K1_POSR_GOSCIE` = 8; kontekst bez capsetu — jądro gościa robi go każdemu, kto otworzy
  kartę — nic nie znaczy), zasoby blob tylko z pamięci karty (`HOST3D`, bez stron gościa, całe strony, ≤ 512 MiB; blob 0 =
  pamięć wspólna mostu tego kontekstu, 4 KiB…1 MiB — memfd VMM-a → `POSR_WSPOLNA`; inne → `POSR_BLOB`), mapowanie
  (`MAP_BLOB`: tylko zasób oznaczony do mapowania, całe strony, w obszarze, nigdy na innym mapowaniu; bufor karty jako
  dma-buf od pośrednika — `POSR_BUFOR` — zmapowany do odczytu i zapisu, **nigdy wykonywalnie**, jako miejsce pamięci KVM;
  odpowiedź mówi, jak gość ma go mapować: z pamięcią podręczną albo write-combining), `SUBMIT_3D` (strumień ≤ 1 MiB,
  wielokrotność 4 → okno → `POSR_PROSBY` z numerem kontekstu gościa; z płotem odpowiedź czeka na `POSR_PLOT_GOTOWY` —
  virtio pozwala odpowiadać nie po kolei), `UNMAP`, `UNREF` (`POSR_ZWOLNIJ`), koniec kontekstu (`POSR_GOSC_KONIEC`).
  Polecenia 3D virgl, EDID — błąd. **`SET_SCANOUT_BLOB`** (0.10.11 krok 2, ADR 0051): bufor karty jako wyświetlacz gościa —
  sprawdzony pole po polu (bufor karty tej strefy, XRGB8888/ARGB8888, jedna płaszczyzna, cały obraz, wiersz i początek
  w buforze, nie większy niż okno), jego dma-buf wzięty raz od pośrednika; okno (`okno_bufor.c`) nic nie kopiuje — daje go
  ekranowi (`STREFA_BUFOR` raz, `STREFA_OBRAZ` co klatkę, numery 1…4 dla ostatnio pokazanych buforów; `RESOURCE_FLUSH`
  pokazanego bufora = nowa klatka). `SET_SCANOUT` zasobu 2D wraca do kopii (droga 2D).
- **Wątek mostu**: kolejkę sterującą virtio-gpu obsługuje wtedy osobny wątek (pośrednik odpowiada w milisekundach,
  płot czeka na kartę) — wątek procesora tylko go budzi; polecenia 2D wykonuje pod zamkiem VMM-a, 3D bez niego.
- **Koniec pośrednika** (awaria, zawieszenie karty — T15): każde polecenie 3D dostaje błąd, czekające płoty od razu
  „gotowe”, mapowania zostają (trzymają swoje bufory) — Mesa w strefie dostaje błędy, Firefox rysuje dalej procesorem,
  strefa i K1 żyją. Licznik w logu na końcu: „szybka grafika: polecenia 3D …, płoty …, mapowania …”.
- **W strefie**: nasza Mesa (`scripts/zbuduj-mese.sh`, ADR 0049 pkt 7 — iris z `intel-virtio-experimental`, bez LLVM),
  kompozytor przyjmuje bufory karty (`zwp_linux_dmabuf_v1` w wersji 4, tylko z `K1_GRAFIKA=1`: liniowe XRGB8888 i
  ARGB8888, urządzenie z „feedbacku” — `/dev/dri/renderD128`; czeka, aż karta skończy — `VIRTGPU_WAIT`; od 0.10.11
  krok 1 bufora karty nie kopiuje: scena czyta go tam, gdzie jest, a kompozytor oddaje go przeglądarce dopiero, gdy
  przyjdzie następny — `zawartosc.c`; jedna kopia każdej klatki mniej; od kroku 2 trzyma dwa ostatnie — poprzedni oddaje
  klatkę później, bo ekran może go jeszcze pokazywać); od 0.10.11 krok 2, gdy na wierzchu jest bufor karty na cały
  wyświetlacz (okno przeglądarki bez menu i podpowiedzi nad nim), nie rysuje sceny — ten bufor jest wyświetlaczem gościa
  (`bezposredni.c`, `gosc/kms.c`: framebuffer z dma-bufa, `SETCRTC`; jądro strefy wysyła `SET_SCANOUT_BLOB`),
  a `gosc` pisze w profilu `user.js` z rysowaniem kartą (bez `gfx.webrender.software`) — przy każdym starcie, w obie
  strony.
- **Ramka**: ekran rysuje mały znak — układ scalony — tuż za nazwą sesji, póki strefa działa ze swoim pośrednikiem
  (`STREFY_GRAFIKA` b; D7 właściciela). Gdy pośrednik się skończy, znak znika.

## Okno
Klient ekranu z rolą `STREFA`, jedno okno (≤ 1024 × 720, z rozmiaru ekranu; tyle widzi gość jako wyświetlacz). Na
początku **ekran startu**: „Uruchamiam: <tytuł> · <sesja>…” z ikoną strefy i paskiem, po którym przesuwa się odcinek w
kolorze strefy (0.10.2 krok 1, D1 właściciela: szkic A; VMM rysuje go co 100 ms, nazwę sesji dostaje od nadzorcy jako
`argv[4]` i sprawdza jak każdą nazwę — `k1_strefa_nazwa_dobra`). Zostaje do „etap okno” od gościa, który powiedział „etap
start” (przeglądarka: jej pierwsze okno — kompozytor pokazuje swoje tło kilkadziesiąt sekund wcześniej, S1, T1), a gość
bez „etap start” (strefa próbna) — do swojego pierwszego obrazu; najwyżej 5 min, potem obraz gościa. Obraz gościa — zmienione
prostokąty kopiowane do wolnej połówki powierzchni (nieprzezroczyste). Klawisze i wskaźnik od ekranu (tylko przy fokusie)
→ urządzenia wejścia; utrata fokusu puszcza wszystko. Przewijanie (`PRZEWIN`, krok 4) przychodzi do okna pod kursorem,
także bez fokusu: VMM ustawia najpierw położenie wskaźnika (gość przewija to, co jest pod nim), potem kółko. Ramkę — pasek tytułu nad obrazem i obwódkę (krok 3, szkic A; od 0.10.7 w kolorach okna K1, kolor strefy w kropce i linii pod paskiem — ADR 0048) —
rysuje ekran (`docs/komponenty/ekran.md`); wysokość obrazu jest o pasek (`K1_STREFA_TYTUL`) mniejsza.

## Przeglądarka (0.10.0 krok 2, ADR 0043)
Wiersze `K1_STREFY` „Ogólna” (szałwia, w menu Start jako „Przeglądarka”), „Bank” (łupek), „Prywatna” (wrzos): obraz
przeglądarki — od 0.10.9 krok 4 poza rdzeniem (ADR 0050): w miejscu A/B zaraz za k1.efi, z drzewem dm-verity; w rdzeniu
tylko jego skrót `/rdzen/strefy/przegladarka.skrot`; nadzorca (`obraz_strefy.c`) zakłada dm-verity i daje VMM-owi
urządzenie jako fd 6 (VMM bierze rozmiar przez `lseek`); w testach QEMU z rdzeniem na `/dev/vda` obraz to drugi dysk
virtio (`scripts/uruchom-qemu.sh`, `--obraz-strefy`); 1536 MiB na start (od 0.10.1 górna granica: pamięć komputera minus rezerwa K1, balon —
ADR 0044), wszystkie procesory komputera do `K1_STREFA_PROCESORY_MAKS` = 254 (0.11.3; 0.11.2: 4, do 0.11.1: 2; w emulacji QEMU — 1, i od 0.10.5 krok 7 gość z `highres=off`: jądro strefy 7.x odracza przestawianie zegara (`HRTIMER_REARM_DEFERRED`) i pod emulowanym SVM bezczynny gość widział klawisz po minucie; z KVM na prawdziwym procesorze bez zmian); Ogólna i Bank pamiętają (krok 5), Prywatna zapomina.
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
  wiadomość sprawdzana według sygnatury (zła — koniec tego klienta). Okno na cały wyświetlacz, dialogi na środku (od 0.11.4
  nigdy ponad górną krawędzią, a większe niż wyświetlacz dostają `configure` z jego rozmiarem — GTK zmniejsza się do
  swojego minimum; okno „Zapisz obraz jako…” miało 1124×859 przy oknie sesji 640 px wysokości, góra wychodziła poza ekran),
  wyskakujące menu z pozycjonerem; zdarzenia wejścia do każdego `wl_pointer`/`wl_keyboard` klienta (Firefox ma po dwa);
  „klatka gotowa” także po zatwierdzeniu bez zmian (inaczej GTK przestaje rysować). Kółko (krok 4): jedna ramka
  zdarzeń urządzenia = jedno przewinięcie `wl_pointer.axis` (10 jednostek na ząbek, jak weston); z ząbkami —
  `axis_source` „wheel” i `axis_discrete`, palce na touchpadzie — „finger”, więc Firefox przewija płynnie. **Palce**
  (0.10.1 krok 5, D6): ramkę z touchpada VMM oznacza `BTN_TOOL_FINGER` (wciśnięty i puszczony w tej samej ramce;
  urządzenie wskazujące ma ten bit) — wcześniej „same części ząbka” znaczyły palce, a tak wygląda też precyzyjne kółko.
  Gest palców kończy się ramką z `axis_source` „finger” i `axis_stop` (`palce.c`, czyste — `test_palce`): po 200 ms
  bez ramki palców (K1 nie wysyła chwili podniesienia palców) albo od razu przed kółkiem, wciśnięciem przycisku
  i zmianą powierzchni pod kursorem. Bez `axis_stop` gest w Firefoksie (APZ: blok gestu przesuwania) nie kończył się
  nigdy i każde dalsze przewijanie szło do strony karty, w której palce przewinęły pierwszy raz — po nowej karcie nic
  się nie przewijało, w żadnej karcie, aż do zamknięcia przeglądarki. Od
  kroku 5: **jeden ząbek = jedna ramka** `wl_pointer` (do 16, reszta w ostatniej), każda z późniejszym czasem — GTK robi
  z `axis_discrete` jednej ramki jeden krok bez względu na liczbę (przebieg CI 134: 5 ząbków w górę, 2 kroki), a Firefox
  pomija zdarzenie kółka z czasem poprzedniego. Ząbki skleja QEMU w teście i ekran, gdy klient nie nadąża.
  **Reguły** (0.10.1 krok 1, S15): każdy powód odrzucenia klienta to wiersz tabeli `K1_REGULY` (`kompozytor.h`) z numerem
  (od 1, numery się nie zmieniają); przy błędzie protokołu kompozytor wysyła `gosc` „regula N” na fd 3 (ta sama rura co
  „okno”), a ten — numer w „kto”. **Ta sama rola drugi raz** (0.10.1 krok 1, D1 f): rola powierzchni jest na stałe, ale
  wolno ją nadać ponownie (wayland.xml: „must not already have another role, and it must not have an existing
  wl_subsurface object”) — Firefox niszczy podpowierzchnię podglądu karty, gdy go chowa, i tworzy ją znów na tej samej
  powierzchni. Kompozytor odrzucał to (reguła 28) — drugi podgląd karty kończył przeglądarkę: awaria przy przełączaniu
  kart, po minimalizacji (kursor wychodzi z okna górną krawędzią, gość „widzi” go dalej nad kartami) i „później, bez
  minimalizacji”. Teraz: podpowierzchnia — ponownie, gdy nie ma już `wl_subsurface` (pozycja od 0,0, stan czekający
  starej znika, jej wywołania klatki „gotowe”); `xdg_surface` — ponownie, gdy poprzednia zniszczona (to, co pokazywała,
  znika razem z nią). Za dużo podpowierzchni ma osobną regułę (29).
- **Przekaźnik** i **łącznik** — części **Raptor Bridge**, mostu strefy do świata przez K1: droga bajtu, granice zaufania,
  limity, liczniki, pomiary, pułapki i przepis na nową strefę — `docs/komponenty/raptor-bridge.md` (0.10.5 krok 5).

## Rozmiar okna (0.10.5 krok 4, T12–T14)
Rozmiar okna strefy zmienia tylko ekran (ramka K1, `ekran.md`); VMM dostaje `ROZMIAR` (`okno.c`): nowa powierzchnia
dokładnie tego rozmiaru (`k1_pow_zamien`), obie jej połowy najpierw czarne (nigdy przezroczyste), potem obraz gościa;
virtio-gpu zgłasza gościowi zmianę wyświetlacza (`gpu_zmiana_ekranu`: `events_read` = `VIRTIO_GPU_EVENT_DISPLAY`
i przerwanie konfiguracji; gość czyści je przez `events_clear`) — jądro strefy pyta o wyświetlacz znowu i ogłasza
„hotplug” (uevent). Programy gościa (`gosc/kms.c`, wspólny dla `gosc` i kompozytora) słuchają uevent (gniazdo
`NETLINK_KOBJECT_UEVENT`), czytają nowy tryb złącza i przełączają się na nowy bufor „dumb” (stary zwolniony); log
`ekran gościa: nowy rozmiar W×H`. Kompozytor wysyła wtedy oknu głównemu (Firefox) nową konfigurację `xdg_toplevel`
(rozmiar wyświetlacza, zmaksymalizowane, aktywne). **EDID (0.11.0 krok 1, T9, `edid.c`):** virtio-gpu oferuje
`VIRTIO_GPU_F_EDID` i na `GET_EDID` daje EDID 1.4 z jednym trybem dokładnie w rozmiarze okna — bez niego jądro strefy robi tryb
z `drm_cvt_mode`, który zaokrągla szerokość w dół do 8 (1366 → 1360: szpary po bokach zmaksymalizowanej przeglądarki);
rozmiar poza 64…4095 — błąd i dawny tryb z informacji o wyświetlaczu. **Pełny ekran (0.11.0 krok 1, D1):** Firefox prosi
`set_fullscreen` → kompozytor (`xdg.c`) zgłasza `gosc` „pelny 1” (okno główne w pełnym ekranie) / „pelny 0” (żadne), `gosc`
przekazuje VMM-owi, a VMM (`okno.c`) ekranowi `PELNY_EKRAN` — tylko zmiany, najwyżej 4 na sekundę (więcej — liczone,
pominięte). Decyduje ekran (`ekran.md`): gdy się zgodzi, `ROZMIAR` z `c` = `K1_ROZMIAR_PELNY` daje cały ekran; gdy K1
zakończy pełny ekran (`ROZMIAR` z innym `c`, póki gość w nim jest), VMM mówi gościowi „pelny 0”, `gosc` daje kompozytorowi
SIGUSR2, a ten zdejmuje oknom stan pełnego ekranu. Strefa nie ma komunikatu, którym zmieniłaby swój rozmiar albo
położenie. Strefa próbna: F11 prosi o pełny ekran / kończy go, a po „pelny 0” od K1 od razu prosi znowu sama (jak
złośliwa strona — ekran musi odmówić, test-strefa).

## Strefa pobierania (0.10.5 krok 2, ADR 0046)
Wiersz `POBIERANIE` w `K1_STREFY` — jedyny bez okna (kolumna „okno” = 0): VMM nie dostaje kanału do ekranu (fd 3 =
`/dev/null`, „strefa bez okna”), gość nie widzi urządzeń ekranu, klawiatury ani wskaźnika (`kvm_linia_urzadzen`).
Obraz przeglądarki z curl i zaufanymi certyfikatami (`strefy/przegladarka.txt`; plik certyfikatów robi
`scripts/zbuduj-strefe.sh`); argv[4] VMM = co pobrać (`lacznik_cel_dobry`: a–z, 0–9, `.`, `-`, `/`, bez `..`) → `k1.pobierz=`
w linii jądra → `gosc/pobieranie.c`: przekaźnik, połączenie SOCKS5 do `pobrane.k1:1` (VMM prosi nadzorcę o gniazdo
`K1_STREFA_POBRANE` — koniec pary do programu zapisującego), curl z `--socks5-hostname` pisze plik w to połączenie; od
0.13.0 (ADR 0056) curl zapisuje też nagłówek `Date` serwera (`--write-out %output{/tmp/k1-data}%header{date}`), a po całym
pliku (kod 0) gość dokleja **stopkę** z tym czasem (`k1_stopka.h`, 16 B). Łącznik tej
strefy: tylko nazwy z `K1_HOSTY_POBIERANIA` na porcie 443 (`lacznik_cel`); adres liczbami — tylko w teście aktualizacji
(tryb `K1_STREFA_TRYB_AKTUALIZACJA`, serwer testowy, curl `-k`). Odmowy liczy podsumowanie połączeń („poza listą hostów”).
Popupów gui o tej strefie nie ma (nadzorca `zglos` ją pomija). Cała droga: `docs/komponenty/aktualizacja.md`.

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

## Droga plików: plik ze strefy do Plików (0.11.0 krok 2, ADR 0052)
- **Port plików** — vsock port 3 (`K1_PS_PORT`), jedno połączenie naraz, kredyt 64 KiB; ramki `src/wspolne/k1_pliki_strefy.h`
  (`ZGLOS`, `DANE`, `BRAK` od strefy; `WYSLIJ`, `WYNIK` od K1; czyste: `test_pliki_strefy`, `fuzz_pliki_strefy`). VMM
  (`pliki.c`) sprawdza ramki i kolejność, zamienia je na `STREFA_PLIK` (nazwa naprawiona zasadami nazw K1 w memfd),
  `STREFA_PLIK_KAWALEK` (1 MiB w memfd), `STREFA_PLIK_KONIEC` na fd 7; odpowiedzi `STREFA_PLIK_ODP`. Najwyżej 4 pliki czekają
  naraz (reszta — „odmowa”), bajty tylko po `WYSLIJ` i nie ponad rozmiar, jeden kawałek czeka na skarbiec (kredyt strefy
  wstrzymany). Złamanie zasad zamyka **tylko połączenie plików** (RST), maszyna żyje; w logu liczby, nigdy nazwa.
- **Agent** (`src/strefa/agent/`, `/usr/lib/k1/agent` w obrazie przeglądarki, uruchamia go `gosc` jako użytkownik
  przeglądarki): inotify na folderze pobranych `/home/k1/Pobrane` (zablokowany w `k1.js`; Firefox nie pyta, gdzie zapisać,
  ani „co zrobić z plikiem”) — zgłasza plik po zmianie nazwy `.part` (`IN_MOVED_TO`) albo po zapisie niepustego pliku,
  który nie jest `.part`; bajty dopiero na `WYSLIJ`, ten sam plik (i-węzeł) tego samego rozmiaru, inaczej `BRAK`. Pliki,
  które były w folderze przed startem, nie są zgłaszane. Sprawdzone na stanowisku (`testy/stanowisko/pobrane.py`).
  Od 0.11.4 (`agent/foldery.c`, zgłoszenie właściciela: „czasem nie pokazuje komunikatu”): okno „Zapisz obraz jako…” GTK
  pozwala zapisać gdziekolwiek (i otwiera się tam, gdzie poszedł ostatni zapis) — agent patrzy też na katalog domowy
  przeglądarki i jego widoczne foldery do 3 poziomów (bez ukrytych, profilu `profil` i „Z K1”), także założone później;
  plik zapisany w miejscu (`IN_CLOSE_WRITE`) zgłasza dopiero po 0,7 s bez kolejnego zapisu — raz, w całym rozmiarze; po
  utraconych zdarzeniach (`IN_Q_OVERFLOW`) przegląda pliki zmienione w ostatnich 2 min; ten sam niezmieniony plik nie
  wraca drugi raz (i-węzeł, rozmiar, czas zmiany); plik, któremu K1 odmówił (`ODMOWA`: 4 czekają na odpowiedź), zgłasza
  znowu, gdy inny się skończy, albo po 5 s — do 0.11.3 przepadał. Test na hoście `test_agent_foldery`, stanowisko
  `testy/stanowisko/zapis.py`.
- **Zgoda i zapis:** nadzorca (`strefy_pliki.c`) → gui (okienko, `POBRANIE`) → „Zapisz w Pobranych” → nadzorca zapisuje
  plik w skarbcu swoim kanałem `K1_WL_POBRANIA` (tylko nowy plik w Pobranych z pochodzeniem strefy) jako zapis w toku:
  przerwanie nie zostawia nic. Pochodzenie: `K1_POCH_STREFA_OD` + numer strefy, tekst z kolumny „skąd” `K1_STREFY`
  („z przeglądarki · Ogólna”); twórca pliku — profil strefy.
- **Z Plików do strefy (0.11.0 krok 3, D4: A):** „Wyślij do” / sesja w „Otwórz za pomocą” → skarbiec daje kanałowi nadzorcy
  `K1_WL_DO_STREF` ten plik → nadzorca otwiera zamkniętą sesję (`strefy_do.c`) → VMM (`pliki_do.c`) ramki `PLIK` i `BAJTY`
  (kawałki na prośbę `STREFA_DO_ODP`, plik czeka na agenta do 120 s) → agent (`agent/odbior.c`) zapisuje kopię w folderze
  sesji **„Z K1”** i odpowiada `ODEBRANY`; „Otwórz w…”: linia z adresem do kolejki FIFO kompozytora (`/run/k1/otworz`), który
  wpisuje adres w nową kartę (`kompozytor/otworz.c`: Ctrl+L, adres, Alt+Enter — Firefox bez D-Bus i z `--no-remote` nie ma
  innej drogi; tylko folder „Z K1”, zwykłe znaki, raz na sekundę). Zmieniona wersja (pobranie o tej samej nazwie, „nazwa(1)”
  Firefoksa, ponowny zapis w „Z K1”) wraca jako `ZGLOS` z polem dotyczy — tylko pliku, który K1 dał tej sesji (ADR 0052 p. 10).
- **Upuszczone na okno (0.11.0 krok 4):** ekran daje VMM-owi `UPUSC_STREFA` (punkt w obrazie, liczba plików — dopiero po
  puszczeniu; póki kursor tylko jest nad oknem, strefa nic nie dostaje) → VMM ramka `UPUSC` dla agenta przed plikami
  (`pliki_do.c`) → pliki jak „Wyślij do” z trybem upuszczenia (`PLIK` tryb 2) → agent, gdy ma wszystkie w „Z K1”, pisze
  kompozytorowi „upusc X Y <adresy>” → kompozytor (`kompozytor/dane.c`) odgrywa upuszczenie w tym punkcie: `wl_data_offer`
  (numer od `0xff000000` — obiekt kompozytora), `text/uri-list`, `enter`, `motion`, akcja „kopia”, `drop`, adresy na
  `receive`. Tylko adresy „Z K1”, najwyżej 8, punkt na ekranie strefy, raz na sekundę. Firefox bierze je jak pliki
  (strona: `dataTransfer.files`; pole „Prześlij plik” się wypełnia — stanowisko `upusc.py`). Przeciąganie zaczęte w strefie
  dalej anulowane od razu.
- **Schowek (0.11.0 krok 5, ADR 0052 T8, D5: A):** Ctrl+V w oknie — ekran daje VMM-owi tekst (`SCHOWEK`) przed klawiszem;
  VMM (`schowek.c`) wysyła agentowi `WKLEJ` + `WKLEJ_TEKST` i wstrzymuje klawisze (najwyżej 0,5 s), aż agent powie
  `WKLEJONE` — kompozytor (`kompozytor/schowek.c`) zrobił z tekstu zaznaczenie okna (`wl_data_offer` tekstu,
  `wl_data_device.selection`) i anulował własne źródło przeglądarki. Kopia w strefie: przeglądarka ustawia zaznaczenie →
  kompozytor czyta tekst (`wl_data_source.send`, do 1 MiB, sprawdzony) → kolejka FIFO `/run/k1/schowek` → agent `KOPIA` +
  `KOPIA_TEKST` → VMM (cały tekst, jak zapowiedziany, `k1_schowek_dobry`, najwyżej jedna kopia na 0,2 s) → ekran
  `SCHOWEK_USTAW`. Zaznaczenie ustawia tylko klient z klawiaturą. W strefie nie ma własnego schowka do odczytu przez stronę.
- Gość próbny: tryby 18 (uczciwe pobranie), 19 (złośliwe) i 20 (odbiór z K1 i zmiany z powrotem, upuszczenie pliku
  z pulpitu — 0.11.0 krok 4, schowek — krok 5; `gosc/odbior.c`) — `scripts/test-pobrane.sh`.

## Komunikaty
| Komunikat | Kierunek | Pola |
|---|---|---|
| `ROZMIAR` (0.10.5 krok 4) | ekran → VMM (fd 3) | obiekt = okno, a, b = nowy rozmiar obrazu (od 640 × 480; do 0.11.0 szerokość co 8), c = `K1_ROZMIAR_*` (0 zwykłe, 1 zmaksymalizowane, 2 pełny ekran — 0.11.0); odpowiedź: `POWIERZCHNIA` e = 1 tego rozmiaru (`ekran.md`) |
| `PELNY_EKRAN` (0.11.0 krok 1) | VMM → ekran (fd 3) | obiekt = okno, a = 1 gość prosi o pełny ekran / 0 wyszedł z niego; tylko zmiany, najwyżej 4 na sekundę (`ekran.md`) |
| `STREFA_POLACZ` | VMM → nadzorca (fd 7) | obiekt = połączenie 1…128; TCP: a = adres IPv4 (`k1_siec_serwer_dobry`), b = port 1…65535, c = `K1_STREFA_TCP`; DNS: a = b = 0, c = `K1_STREFA_DNS`; d = e = 0 |
| `STREFA_GNIAZDO` | nadzorca → VMM (+ 1 deskryptor) | obiekt = połączenie, a = `K1_STREFA_TCP` (gniazdo TCP w trakcie łączenia) / `K1_STREFA_DNS` (gniazdo UDP do serwera DNS, port 53), reszta 0 |
| `STREFA_PAMIEC` (0.10.1 krok 2, ADR 0044) | VMM → nadzorca, najwyżej raz na sekundę | obiekt 0, a = pamięć gościa trzymana w K1 (MiB — strony memfd), b = dostępna w gościu wg jego statystyk (−1: jeszcze brak; podpowiedź), c = w balonie, d = cel balonu, e = oddane K1 od startu (a–d: 0…`K1_STREFA_PAMIEC_MAKS`, e: 0…`INT32_MAX`) |
| `STREFA_BALON` (0.10.1 krok 2) | nadzorca → VMM; program testowy → nadzorca | obiekt = strefa, a = cel balonu 0…`K1_STREFA_PAMIEC_MAKS` MiB, reszta 0; od kroku 3 cel ustawia rozdzielacz nadzorcy (`nadzorca.md`); strefa programu testowego (tryby 1…9) — tylko jego prośby |
| `STREFA_FOKUS` (0.10.1 krok 3) | gui → nadzorca (fd 4) | obiekt 0, a = strefa, której okno ma fokus 0…`K1_STREFY_MAKS` (0: żadna), reszta 0; tylko od gui (UID), podpowiedź dla rozdzielacza i `oom_score_adj` |
| `STREFA_PRZYDZIAL` (0.10.1 krok 3) | nadzorca → Zasoby (fd 4, co sekundę), gui (fd 5, tuż przed `MALO_PAMIECI`) | obiekt = działająca strefa, a = ile używa (pamięć gościa w K1), b = ile może dostać (górna granica minus cel balonu), MiB 0…`K1_STREFA_PAMIEC_MAKS`; c = 1 jej okno ma fokus; d = e = 0 |
| `MALO_PAMIECI` (0.10.1 krok 3) | nadzorca → gui (fd 5) | obiekt 0, a = ile K1 ma wolnej pamięci (MiB, 0…`K1_PAMIEC_MAKS_MIB`), b = bit nr−1: strefa nr w okienku (tylko bity stref, nie 0), reszta 0 |
| `STREFA_ODMOWA` | nadzorca → VMM | obiekt = połączenie, a = `K1_STREFA_ODM_*` (adres poza D3, brak sieci albo DNS, limit, błąd gniazda), reszta 0 |
| `URUCHOM_STREFE` | gui → nadzorca (fd 4); program testowy → nadzorca | `a` strefa 1…`K1_STREFY_MAKS`, `b` tryb gościa testowego 0…`K1_STREFA_TRYB_MAKS` (≠ 0 tylko od programu testowego; gui — odrzucone; 10 od 0.10.1 krok 3: zwykły gość, którego balonem rządzi rozdzielacz; 11 od 0.10.2 krok 1: gość z „oknem” jak przeglądarka — „etap start”, pierwszy obraz od razu, „etap okno” 8 s później, zamyka się 6 s), reszta 0; prośba o strefę, która **zamyka się** w tle (0.10.2, D2): nadzorca otwiera ją sam, gdy stara maszyna skończy |
| `STREFA_STAN` | nadzorca → gui (fd 5) | `a` strefa, `b` powód: nie ruszyła — `K1_STREFA_BEZ_WIRTUALIZACJI`, `_ZA_MALO_PAMIECI`, `_BRAK_OBRAZU`; zamknęła się sama (krok 3) — `K1_STREFA_KONIEC_PAMIEC` (pamięć strefy), `_PAMIEC_K1` (pamięć komputera), `_AWARIA`, `_BLAD`; od 0.10.1 krok 1 `_LIMIT` (8: strefa zużyła całą pamięć, którą K1 jej daje — limit klatki) (1…`K1_STREFA_STAN_MAKS`); reszta 0 |
| `KLIENT` (rola `STREFA`) | nadzorca → ekran | `b` = numer strefy (1…`K1_STREFY_MAKS`) |
| `STREFY_STAN` | nadzorca → gui (fd 5), Ustawienia (fd 4), ekran (kanał sterujący) | krok 5: `a` bity stref, które pamiętają (bit nr−1), `b` = 1 komputer ma wirtualizację, `c` = 1 dane K1 na dysku (bez tego `a` = 0), `d` bity stref, które działają, `e` (0.10.2 krok 1) bity stref, które **zamykają się** w tle (tylko działające: `e` ⊆ `d`) |
| `DZWIEK_KANAL` (0.12.0 krok 3) | nadzorca → VMM (fd 7, + 1 deskryptor) | wszystko 0 — kanał do usługi dźwięku (klient `STREFA` z numerem strefy; od nowa po restarcie usługi); tylko strefy z oknem (`dzwiek.md`) |
| `STREFA_ZAMYKA` (0.10.2 krok 1) | VMM → nadzorca (fd 7) | obiekt 0, reszta 0 — użytkownik zamknął strefę: okna już nie ma, maszyna kończy się w tle |
| `STREFA_ZAMKNIJ` (0.10.2 krok 1) | nadzorca → VMM (fd 7) | obiekt = strefa, reszta 0 — zamknij się jak po „zamknij” na pasku (K1 się wyłącza albo uruchamia ponownie: nadzorca czeka na strefy) |
| `STREFA_NAZWA` | nadzorca → gui, Ustawienia, ekran; Ustawienia → nadzorca (nowa nazwa) | krok 5: obiekt = strefa, `a`…`e` = nazwa sesji, 20 bajtów UTF-8 dopełnione zerami (`k1_strefa_nazwa_dobra`: bez znaków sterujących, niewidocznych i zmieniających kierunek, bez spacji na brzegach); nigdy w logu |
| `STREFA_TRWALOSC` | Ustawienia → nadzorca | krok 5: obiekt = strefa, `a` = `K1_STALA` / `K1_JEDNORAZOWA` (od następnego otwarcia), reszta 0 |
| `STREFY_GRAFIKA` (0.10.9 krok 6) | nadzorca → gui (fd 5), Ustawienia (fd 4), Zasoby, ekran (kanał sterujący), po `STREFY_STAN` | `a` bity sesji z włączonym przełącznikiem „Szybka grafika”, `b` bity stref, które działają teraz ze swoim pośrednikiem grafiki (znak na ramce), reszta 0 |
| `STREFA_GRAFIKA` (0.10.9 krok 6) | Ustawienia → nadzorca | obiekt = strefa, `a` = 1 włącz / 0 wyłącz (od następnego otwarcia), reszta 0 |
| `STREFA_WYCZYSC` | Ustawienia → nadzorca; program testowy → nadzorca | krok 5: obiekt = strefa, reszta 0 — „Wyczyść dane sesji” |
| `STREFA_WYCZYSZCZONO` | nadzorca → Ustawienia | krok 5: obiekt = strefa, `a` = `K1_STREFA_WYCZ_OK` / `_DZIALA` (otwarta — najpierw zamknij) / `_BLAD`, `b` = usunięte kawałki, reszta 0 |
| `STREFA_PLIK` (0.11.0 krok 2, ADR 0052) | VMM → nadzorca (fd 7, + memfd) | obiekt = numer pliku u VMM 1…`K1_PS_NR_MAKS`, a, b = rozmiar (niskie, wysokie; 0…`K1_PS_ROZMIAR_MAKS`), c = długość nazwy 1…127, d = e = 0; memfd zapieczętowany, dokładnie c bajtów — nazwa K1 (VMM naprawił ją `k1_dok_nazwa_popraw`, nadzorca sprawdza `k1_dok_nazwa_poprawna`) |
| `STREFA_PLIK_KAWALEK` (0.11.0 krok 2) | VMM → nadzorca (fd 7, + memfd) | obiekt = plik, a = bajty 1…`K1_DOK_KAWALEK` (każdy kawałek prócz ostatniego: cały), b = numer kawałka 0…4095, c = 1 ostatni / 0, d = e = 0; tylko po `K1_PLIK_WYSLIJ`, następny dopiero po `K1_PLIK_DALEJ` |
| `STREFA_PLIK_KONIEC` (0.11.0 krok 2) | VMM → nadzorca (fd 7) | obiekt = plik, a = `K1_PS_W_BLAD` (strefa złamała zasady portu plików — połączenie zamknięte) albo `K1_PS_W_ZOSTAWIONY` (strefa go wycofała: zniknął, zmienił się, agent odszedł), reszta 0 |
| `STREFA_PLIK_ODP` (0.11.0 krok 2) | nadzorca → VMM (fd 7) | obiekt = plik, a = `K1_PLIK_WYSLIJ` (użytkownik powiedział „Zapisz”) / `K1_PLIK_DALEJ` (skarbiec ma kawałek) / `K1_PLIK_GOTOWE` (koniec: b = `K1_PS_W_*` — zapisany, zostaje, za mało miejsca, za duży, błąd, odmowa), b poza `GOTOWE` 0, reszta 0 |
| `POBRANIE` (0.11.0 krok 2, D2: A) | nadzorca → gui (fd 5, + memfd) | obiekt = strefa, a = plik, b, c = rozmiar, d = długość nazwy 1…127, e = 0; memfd jak w `STREFA_PLIK` (gui czyta i sprawdza nazwę jeszcze raz) — okienko „<sesja> pobrała plik” |
| `POBRANIE_DECYZJA` (0.11.0 krok 2) | gui → nadzorca (fd 4); program testowy → nadzorca | obiekt = strefa, a = plik, b = 1 „Zapisz w Pobranych” / 0 „Zostaw w przeglądarce” (albo „OK” pliku za dużego), reszta 0 |
| `POBRANIE_WYNIK` (0.11.0 krok 2) | nadzorca → gui (fd 5) | obiekt = strefa, a = plik, b = `K1_PS_W_*`, reszta 0 — jego okienko znika; po „Zapisz” bez zapisu gui mówi dlaczego |
| `SCHOWEK` (0.11.0 krok 5) | ekran → VMM (fd 3, + memfd) | obiekt = okno, a = bajty tekstu, b = skąd (0 K1, numer strefy) — tuż przed Ctrl+V do okna (`ekran.md`) |
| `SCHOWEK_USTAW` (0.11.0 krok 5) | VMM → ekran (fd 3, + memfd) | obiekt = okno, a = bajty — tekst skopiowany w strefie (ekran bierze go tylko po geście w oknie) |
| `UPUSC_STREFA` (0.11.0 krok 4) | ekran → VMM (fd 3) | obiekt = okno, a, b = punkt w obrazie, c = liczba plików 1…8, reszta 0 — upuszczono pliki na okno; VMM: ramka `UPUSC` dla agenta (`ekran.md`) |
| `DOK_DO_STREFY` (0.11.0 krok 3, ADR 0052) | Pliki, pulpit → skarbiec | obiekt = widoczny plik, a = strefa z oknem, b = tryb `K1_PS_TRYB_*`: 1 „Otwórz za pomocą” sesja (nowa karta) / 0 „Wyślij do” / 2 upuszczony na okno (krok 4), reszta 0; tylko z prawem `P_STREFA` (Pliki, pulpit — pliki Pulpitu) — odpowiedź `DOK_WYNIK` |
| `SKARB_DO_STREFY` (0.11.0 krok 3) | skarbiec → nadzorca (kanał sterujący, + memfd) | obiekt = plik, a = strefa, b = tryb (0…2), reszta 0; memfd: wpis pliku (`K1_DOK_WPIS_BAJTY`) — kanał nadzorcy `K1_WL_DO_STREF` może go teraz czytać |
| `STREFA_DO_PLIK` (0.11.0 krok 3) | nadzorca → VMM (fd 7, + memfd) | obiekt = numer pliku u K1 1…`K1_PS_NR_MAKS`, a, b = rozmiar (≤ `K1_DOK_MAKS_BAJTY`), c = długość nazwy 1…127, d = tryb 0…2 (wyślij, otwórz, upuść), e = 0; memfd z nazwą |
| `STREFA_DO_KAWALEK` (0.11.0 krok 3) | nadzorca → VMM (fd 7, + memfd) | obiekt = plik, a = bajty 1…`K1_DOK_KAWALEK`, b = numer kawałka, reszta 0 — tylko po `STREFA_DO_ODP` DALEJ |
| `STREFA_DO_ODP` (0.11.0 krok 3) | VMM → nadzorca (fd 7) | obiekt = plik, a = `K1_PLIK_DALEJ` (następny kawałek) / `K1_PLIK_GOTOWE` (b = 0 w „Z K1” sesji, 1 nie), reszta 0 |
| `SKARB_WYCZYSC` | nadzorca → skarbiec (kanał sterujący) | krok 5: `a` = właściciel — profil strefy (inny — odrzucony), reszta 0 |
| `SKARB_WYCZYSZCZONO` | skarbiec → nadzorca | krok 5: `a` = ten właściciel, `b` = 0 albo `K1_MAG_*`, `c` = usunięte kawałki, reszta 0 |
| `SKARB_CZYTAJ`, `SKARB_ZAPISZ`, `SKARB_USUN`, `SKARB_DANE`, `SKARB_WYNIK` (dysk strefy) | VMM ↔ skarbiec (fd 8) | krok 5: obiekt = `K1_OB_DYSK` + kawałek (0…32 767), zapis i dane — najwyżej 64 KiB; brak kawałka: `WYNIK` b = `K1_SKARB_BRAK` (VMM czyta wtedy pusty dysk) |
| `OKNO` | ekran → gui | okno strefy: `a` = 0, `c` = numer strefy |

## Gość próbny `gosc` (`src/strefa/gosc/`, `/init` obrazu `/rdzen/strefy/probna.img` — tylko obraz testowy)
Montuje `/proc`, `/sys`; rysuje wzór przez KMS (bufor „dumb”): granatowe tło, żółte rogi, pas — zielony po parzystej liczbie
klawiszy, pomarańczowy po nieparzystej — i biały kwadrat w miejscu ostatniego kliknięcia; czyta wejście (`/dev/input`),
loguje liczniki; „gotowa” i „zamykam” przez vsock; kończy przez `reboot`. Od kroku 5 strefa próbna pamięta: gość montuje
dysk strefy w `/dane` i liczy na nim swoje uruchomienia („K1-GOSC: dysk: uruchomienie N”); tryb 5 — tylko to i koniec.
Tryb 9 (0.10.1 krok 2, `pamiec.c`): zajmuje i zwalnia 64 MiB (zgłoszone wolne bloki wracają do K1), czeka, aż program
testowy poprosi o balon 32 MiB i 0, po 55 s bierze naraz prawie całą swoją pamięć („skok … żyję”) i kończy się. Tryby złośliwe (`k1.tryb=1…4`): sam steruje
urządzeniem w gnieździe LOSOWOSC przez `/dev/mem` (VMM nie podaje go jądru gościa i rezerwuje 1 MiB pamięci) — deskryptor
poza pamięcią, pętla deskryptorów, odczyt 8 MiB, zapis do obrazu (w trybach 3–4 VMM wkłada tam virtio-blk); od 0.10.1
krok 2 tryby 6–8 — VMM wkłada tam balon: strona poza pamięcią (pompowanie), blok pół strony (zgłaszanie), lista 300 numerów.

## Testy
| Test | Rodzaj |
|---|---|
| `test_virtio`: łańcuchy, zapis i odczyt w łańcuchu, zawijanie pierścienia; wrogie łańcuchy (poza pamięcią, pętle, pośrednie, kolejność, skok indeksu); rejestry, cechy, gotowość kolejki, przerwania, reset | funkcjonalny, negatywny |
| `test_strefa_start`: strona zerowa, e820, tablice stron, GDT, tablica MP (sumy); złe jądra i pamięci | funkcjonalny, negatywny |
| `fuzz_virtq`: dowolna pamięć gościa i zapisy rejestrów | fuzzing |
| `test_balon` (0.10.1 krok 2): listy stron (łączenie sąsiednich, pamięć ponad 4 GiB, dziura, koniec, nie z całych numerów, ponad 256), bloki zgłaszania, statystyki (długość, znacznik nieznany i powtórzony); `test_strefa_start` — e820 z drugą częścią, `gp_wsk` przez dziurę; `test_pamiec_stref` — górna granica (D2) | funkcjonalny, negatywny |
| `fuzz_balon`: dowolna lista, blok, statystyki od gościa | fuzzing |
| `test_dysk_strefy` (krok 5): losowe odczyty i zapisy na 64 MiB z pamięcią 16 MiB, `FLUSH` zapisuje wszystko, cały kawałek bez odczytu, granice dysku, źródło z błędem (błąd, nigdy inne dane; kawałek zostaje), kolejność zapisu, pełna pamięć podręczna bez możliwości zapisu | funkcjonalny, negatywny |
| `test_sesje` (krok 5): format sesji w skarbcu — domyślne, zapis i odczyt, złe pole zostawia domyślne, nieznana strefa, uszkodzony obiekt; 0.10.9 krok 6: przełącznik szybkiej grafiki (domyślnie Ogólna), format `K1Z2`, stary `K1Z1` czytany, nieznane cechy zostawiają domyślne | funkcjonalny, negatywny |
| `test_gpu_most` (0.10.9 krok 6): capset, konteksty (DRM, bez capsetu, za dużo), zasoby blob (pamięć wspólna, bufor, strony gościa, rozmiary, cechy), mapowanie (w obszarze, na innym mapowaniu, bez prawa mapowania), `SUBMIT_3D` (okno, płot, oś, długości), koniec zasobu i kontekstu, pośrednik zakończony, polecenie krótsze niż struktura — koniec maszyny | funkcjonalny, negatywny |
| `fuzz_gpu_most`: dowolny ciąg poleceń 3D z każdą odpowiedzią pośrednika; mapowanie zawsze w obszarze | fuzzing |
| `test_pliki_strefy` (0.11.0 krok 2): ramki portu plików — zapis i odczyt w kawałkach dowolnej wielkości, każde pole poza zasadami (typ drugiego kierunku, zarezerwowane, długości, numery, NUL w nazwie, wynik); `fuzz_pliki_strefy`: dowolny strumień od strefy i od K1; `fuzz_vsock` — także port 3 z odpowiedziami nadzorcy | funkcjonalny, negatywny, fuzzing |
| `test-pobrane.sh` (0.11.0 krok 2): uczciwe pobranie 4 plików z kliknięciem okienek, zawartość bajt w bajt, nazwa naprawiona, PDF z treści, pusty; złośliwa strefa — bajty bez zgody, ponad rozmiar, zła ramka, zalew, 5 GB, `BRAK` i koniec strefy w trakcie | funkcjonalny, negatywny |
| `test_komunikat` (krok 5): `STREFY_STAN`, `STREFA_*`, `SKARB_WYCZYSC`/`_WYCZYSZCZONO`, nazwy sesji (znaki sterujące, niewidoczne, kierunek, zły UTF-8), zakres obiektów dysku | negatywny |
| `t_skarb_strefy` (krok 5, w systemie): sesja zapisuje i czyta swój kawałek; NEGATYWNE: inna sesja go nie widzi, aplikacja nie nazwie obiektu dysku; „Wyczyść dane sesji” | funkcjonalny, negatywny |
| `t_kvm`: `/dev/kvm` tylko root, maszyna z pamięcią z memfd bez prawa wykonania wykonuje kod gościa | funkcjonalny |
| `test_obraz_strefy` (0.10.9 krok 4): linia skrótu obrazu strefy (kształt, liczby, rozmiary drzewa) i miejsce obrazu w miejscu A/B (koniec obrazu miejsca, wyrównanie, FAT przed nim, manifesty) | funkcjonalny, negatywny |
| `test-strefa.sh` (0.10.9 krok 4): obraz przeglądarki znaleziony i sprawdzony (dm-verity); NEGATYWNY — obraz ze zmienionym bajtem: nadzorca go nie bierze, sesja Ogólna „nie może ruszyć”, K1 żyje; `test-m5`, `test-m5f`: obraz z miejsca A pendrive'a i dysku, po aktualizacji z miejsca B; `t_sprzet`: nadzorca trzyma tylko obraz strefy pod dm-verity, do odczytu | funkcjonalny, negatywny |
| `t_strefa_klatka`: klatka VMM (UID, capabilities, seccomp, cgroup z wątkami, tylko przekazane deskryptory); zabity VMM — koniec tylko strefy | funkcjonalny, negatywny |
| `t_proby` (profil strefy): bez `open` (także `/dev/kvm`), `socket`, procesów, pamięci wykonywalnej | negatywny |
| `test_lacznik`: SOCKS5 (powitanie, prośby, odmowy: BIND, UDP, IPv6, port 0, zła nazwa, 0.0.0.0, pętla zwrotna), DNS (pytanie; odpowiedzi: inny identyfikator, inne pytanie, ucięte, NXDOMAIN, CNAME, adres prywatny, wskaźniki), reguła D3, komunikaty `STREFA_*` | funkcjonalny, negatywny |
| `fuzz_lacznik`: dowolna prośba SOCKS5 i odpowiedź DNS | fuzzing |
| `scripts/test-strefa.sh` | QEMU, funkcjonalny i negatywny (opis w `docs/TESTY.md`); od 0.10.9 krok 6: tryby 16 i 17 gościa próbnego (`gosc/grafika.c`) — most szybkiej grafiki z silnikiem testowym pośrednika („pamięć karty” od nadzorcy): capset, kontekst, pamięć wspólna, prośba z płotem, bufor zmapowany w pamięci gościa (zapis i odczyt), paczka pracy; złośliwy gość: nieznana prośba, relokacje, zalew, bufory ponad limit, „zawieszenie karty” — koniec pośrednika, strefa żyje |

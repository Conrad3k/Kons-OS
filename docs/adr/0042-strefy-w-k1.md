# 0042. Strefy w K1: KVM w jądrze, własny VMM „strefa” w klatce, jądro i obraz strefy w rdzeniu, zaufana ramka

- Status: Przyjęty; wdrożony w 0.10.0 — krok 1 (strefa próbna — tylko w obrazie testowym), krok 2 przeglądarka (ADR 0043),
  krok 5 dysk strefy (uzupełnienie T11 niżej)
- Data: 2026-11-14
- Dotyczy: ADR 0020 p. 1 i 3 (szczegóły techniczne stref), ADR 0007 (klatki), ADR 0008 (okna), ADR 0004 (rdzeń),
  SPEC §4, §8 (dopiski [korekta: ADR 0042]), decyzje T1–T6, T10 i D1, D4 planu `docs/archiwum/PLAN-0.10.0.txt`; RYZYKA R77–R80

## Kontekst
ADR 0020 ustalił kierunek: każda obca aplikacja w osobnej mikro-maszynie wirtualnej (strefie), okno strefy w ramce
rysowanej przez K1. Brakowało wszystkiego technicznego: KVM w jądrze K1, programu, który prowadzi maszynę (VMM),
jądra dla wnętrza maszyny, miejsca na obrazy stref, sposobu pokazania obrazu maszyny w oknie K1 i testów — w kontenerze
Claude i w CI nie ma sprzętowej wirtualizacji dla K1 w QEMU (kontener: bez KVM w ogóle).

## Decyzja

**Jądro K1** dostaje KVM (`KVM`, `KVM_INTEL`, `KVM_AMD`, `KVM_IOAPIC` — „default y”, które allnoconfig wyłącza, bez niego
nie ma przerwań w jądrze) i wraca `MITIGATION_VMSCAPE`. Bez emulacji SMM, Hyper-V, Xen, SGX dla gości, SEV, TDX (gość K1
startuje bez firmware). `/dev/kvm` istnieje tylko przy procesorze z wirtualizacją włączoną w BIOS-ie (sterownik nie
rejestruje urządzenia bez VT-x/AMD-V), ma prawa 0600 root.

**T1 — VMM: własny, mały, w C** (`src/strefa/`, program `/rdzen/system/strefa`, log `K1-STREFA`). Jeden proces = jedna
maszyna. Pasuje do wzoru K1: urządzenie (`/dev/kvm`) otwiera nadzorca i daje deskryptor, VMM nie dostaje żadnej ścieżki,
klatka z białą listą; robi tylko to, czego trzeba. Firecracker / Cloud Hypervisor (Rust, bez wyświetlacza i wejścia),
crosvm (wszystko, ale ~300 tys. wierszy z własną piaskownicą i ścieżkami), QEMU i kvmtool (ogromne) — odrzucone (plan T1).
Maszynę tworzy sam VMM (`KVM_CREATE_VM`: maszyna należy do procesu, który ją stworzył — `virt/kvm/kvm_main.c`).

**T2 — start i urządzenia.** Jądro strefy startuje wprost: 64-bitowy protokół startu Linuksa (`Documentation/arch/x86/
boot.rst`) — VMM kładzie do pamięci gościa jądro z pliku bzImage (od 1 MiB), stronę zerową (nagłówek z samego jądra +
pola ładowacza, mapa pamięci e820, linia poleceń), GDT z segmentami 0x10/0x18, tablice stron 1:1 dla 4 GiB i **tablicę
MP** (procesory, I/O APIC, przerwania 1:1 — bez ACPI; S10); procesor 0 zaczyna w trybie 64-bitowym na `startup_64`.
Urządzenia — virtio-mmio (wersja 2) opisane w linii poleceń gościa (`virtio_mmio.device=4K@0xd0000000:5` …), jedna
tabela `K1_URZADZENIA` w `src/strefa/strefa.h`: obraz (blk, tylko do odczytu), konsola (hvc0), losowość (rng), klawiatura
i wskaźnik bezwzględny (input ×2), ekran (gpu 2D), vsock (kanał sterujący „gotowa”/„zamykam”/„zamknij”). Przerwania —
w jądrze (`KVM_CREATE_IRQCHIP`, `KVM_CREATE_PIT2`), po jednym irqfd na urządzenie (zbocze). Koniec gościa: `reboot=k` —
reset przez kontroler klawiatury (port 0x64, wartość 0xFE), który VMM łapie. Bez PCI, karty sieciowej, dźwięku, USB.
Każda kolejka virtio sprawdzana pole po polu (niezmiennik 6): adres i długość każdego deskryptora w pamięci gościa,
pętla deskryptorów (łańcuch dłuższy niż kolejka), deskryptory pośrednie (nieoferowane), do odczytu po do zapisu,
skok indeksu dalej niż rozmiar kolejki, kolejka poza pamięcią albo niewyrównana; virtio-blk: zapis do obrazu, odczyt
ponad 4 MiB; virtio-gpu: polecenie krótsze niż struktura, brak miejsca na odpowiedź; vsock: zły adres, przekroczony
kredyt. **Zły deskryptor = koniec tej maszyny z powodem w logu, nigdy K1.** Części czyste (kolejki, rejestry, start)
testowane na hoście (`test_virtio`, `test_strefa_start`) i fuzzowane (`fuzz_virtq`).

**T3 — kto co robi.** **Nadzorca** (`src/nadzorca/strefy.c`; jedna tabela stref `K1_STREFY` w `k1_uslugi.h`: numer,
profil, UID od 300, obraz, tytuł, sesja, pamięć, procesory, kolor ramki, trwałość, ikona, log gościa): na prośbę gui
(`URUCHOM_STREFE`; program testowy także z trybem złośliwego gościa) otwiera `/dev/kvm`, jądro i obraz strefy (tylko do
odczytu), daje kanał do ekranu (rola `STREFA`), uruchamia VMM w klatce `KLATKA_STREFA`; brak `/dev/kvm`, obrazu albo
wolnej pamięci → `STREFA_STAN` do gui, nic nie startuje. **VMM** w klatce (własny UID z wiersza, 0 capabilities, Landlock
bez ścieżek, seccomp: `ioctl` tylko typu KVM 0xAE — nadzorca nie daje mu innych urządzeń, więc typ wystarcza — memfd,
eventfd, wątki: `clone` tylko z `CLONE_THREAD`, futex; `pids.max` = 1 + procesory + 1 zadanie KVM (wątek odzyskiwania
dużych stron to `vhost_task` w procesie maszyny, `arch/x86/kvm/mmu/mmu.c`), cgroup = pamięć gościa + 160 MiB) — wszystko,
co przychodzi od gościa. **Ekran** — okno, ramka, wejście. Klawisze i wskaźnik dostaje VMM od ekranu jak każda aplikacja:
tylko przy fokusie, kody Linuksa (układ PL tłumaczy gość — S5); przy utracie fokusu VMM puszcza w gościu wszystkie
wciśnięte klawisze. Kody klawiszy nigdy nie trafiają do logu (niezmiennik 8).

**T4 — pamięć gościa i niezmienniki.** Pamięć gościa to memfd z `MFD_NOEXEC_SEAL` (`vm.memfd_noexec=2` zostaje),
zmapowana w VMM do odczytu i zapisu, nigdy wykonywana przez K1 — wykonuje ją tylko procesor w trybie gościa (EPT/NPT);
`KVM_SET_USER_MEMORY_REGION` przyjmuje ją (S3 — POTWIERDZONE próbą `t_kvm`). Niezmienniki 1–4 dotyczą świata natywnego K1;
we wnętrzu strefy (inne jądro, granica sprzętowa) wolno JIT (przeglądarka, krok 2). Jądro strefy ma własny fragment
`jadro/strefa.config` na allnoconfig tych samych źródeł (jedna wersja jądra), z tymi samymi zasadami opisu (każda linia
z komentarzem). Nasze programy w strefie (gosc) — z flagami SPEC §11; obce (krok 2) — nie podlegają im.

**T5 — jądro i obrazy stref w rdzeniu** (`/rdzen/strefy/jadro`, `/rdzen/strefy/<obraz>.img`, SquashFS): dm-verity rdzenia
obejmuje je bez nowego kodu, działa tak samo z ISO, pendrive'a i dysku, A/B i podpis bez zmian. Jądro strefy (2,2 MB) jest
w obu obrazach; obraz strefy próbnej (20 KiB) — tylko w obrazie testowym (`scripts/zbuduj-obraz.sh`), produkcyjny nie ma go
(test `test-strefa.sh` to sprawdza); nadzorca bez pliku obrazu mówi „brak obrazu”.

**T6 — grafika w pełnej izolacji: virtio-gpu 2D.** Gość rysuje procesorem do swojej pamięci; `TRANSFER_TO_HOST_2D` kopiuje
prostokąt do kopii zasobu w VMM (zasoby ≤ 4096 × 4096, razem ≤ 96 MiB), `RESOURCE_FLUSH` pokazywanego zasobu mówi oknu,
co się zmieniło; wątek główny VMM kopiuje zmienione prostokąty do połówki powierzchni okna (memfd z pieczęciami, jak
u aplikacji — ekran mapuje tylko do odczytu) i zatwierdza, gdy ekran oddał połówkę (`RAMKA`). Ekran składa tylko przy
zmianie. Rozmiar wyświetlacza gościa = rozmiar okna (≤ 1024 × 720, z rozmiaru ekranu); zmiana rozmiaru okna — później.

**Zaufana ramka (ADR 0020 p. 3; decyzja właściciela D1: szkic B2).** Ekran rysuje sam (`src/ekran/strefy.c`): obwódkę
3 px w kolorze strefy **na zewnątrz** obrazu strefy (rysowaną przed nim — strefa ma tylko swój prostokąt, więc nie może jej
zasłonić ani podrobić) i mały znaczek z ikoną strefy na lewym górnym rogu, nad wszystkim w swoim oknie. Kursor na znaczku
rozwija plakietkę: nazwa strefy · „osobna maszyna” (jednorazowa: „· zapomina po zamknięciu”), minimalizuj, zamknij;
chwycenie znaczka przesuwa okno. Wskaźnik na ramce, znaczku i plakietce nigdy nie trafia do strefy. „Zamknij” → VMM
mówi gościowi „zamknij” (vsock), gość się zamyka; po 10 s VMM wyłącza maszynę sam. Strefa jednorazowa — jasny znaczek
z obwódką; okno bez fokusu — kolor ramki przygaszony. Przed pierwszym obrazem gościa w oknie „Uruchamiam: …” (D1: S1).

**T10 — testy w emulacji (decyzja właściciela D4).** QEMU TCG z `-cpu max` udaje procesor AMD z SVM i zagnieżdżonym
stronicowaniem (NPT) — jądro K1 z `KVM_AMD` uruchamia w nim strefę. Ustalenia (POTWIERDZONE próbami 2026-11-14):
- **czas:** jądro strefy startuje w ok. 0,6 s czasu gościa, cała maszyna do „gotowa” w 1,5–2 s nawet w emulacji (S6);
- **QEMU 8.2: emulowane NPT nie działa dla gościa bez stronicowania** (tryb rzeczywisty, chwilowe wyłączenie stronicowania):
  gość widzi złą pamięć. Dlatego: start prosto w trybie 64-bitowym (i tak potrzebny), CPUID gościa bez LA57 (bez tego
  dekompresor jądra przełącza się na 5 poziomów stron przez tryb bez stronicowania; strefie 5 poziomów są zbędne), strefa
  testowa z 1 procesorem (kolejne procesory gościa startują w trybie rzeczywistym — na prawdziwym sprzęcie bez znaczenia);
- **QEMU 8.2 zostawia typ dostarczonego zdarzenia w `exit_int_info`**, a KVM 6.18 (`svm_check_emulate_instruction`) patrzy
  na bity typu, nie na bit VALID — dostęp do MMIO tuż po przerwaniu kończy się `KVM_INTERNAL_ERROR_DELIVERY_EV`. Jądro strefy
  ma x2APIC (koniec przerwania przez MSR, nie MMIO — także szybciej), a VMM ponawia taki dostęp do swoich urządzeń i APIC
  najwyżej 8 razy z rzędu (KVM niczego jeszcze nie wykonał; na prawdziwym procesorze pole jest zerowe, nic się nie ponawia);
- **CI:** runner GitHuba ma `/dev/kvm`; K1 dostaje wirtualizację tylko przy zagnieżdżonej (`nested=1` w module kvm runnera) —
  inaczej `test-strefa.sh` sam przechodzi na emulację (`uruchom-qemu.sh --emulacja`; S7 sprawdzi pierwszy przebieg CI).

### Uzupełnienie (0.10.0 krok 5): dysk strefy (T11)
- **Sesja, która pamięta**, dostaje drugie urządzenie virtio-blk do zapisu (`/dev/vdb`, 2 GiB) — w ostatnim wierszu tabeli
  `K1_URZADZENIA`; sesja, która zapomina, ma je z 0 sektorów (gość trzyma dane w pamięci, jak w kroku 2).
- **Dane w skarbcu**, nie w pliku: kawałki po 64 KiB to obiekty właściciela = profil wiersza strefy (`K1_OB_DYSK` + numer);
  szyfruje skarbiec, VMM nie ma własnego szyfrowania; nadzorca daje VMM kanał do skarbca z tym właścicielem (fd 8). Kawałki
  64 KiB, nie 1 MiB jak pliki: zapis 4 KiB gościa przepisuje cały kawałek, a baza danych przeglądarki synchronizuje się
  często (S21 — mniejsza cena zapisu ważniejsza niż ok. 3 % na nagłówki rekordów). Inni klienci skarbca nie nazwą tych
  obiektów; kawałek nie bierze rezerwy, którą pliki zostawiają systemowi.
- **Pusty dysk strefy w rdzeniu** (`/rdzen/strefy/dysk.img`, fd 9): ext4 zrobiony przy budowie (`mke2fs` z ustalonym
  UUID, ziarnem i czasem — powtarzalnie; w SquashFS prawie same zera, kilka KiB). Kawałek, którego sesja nie zapisała,
  VMM czyta stamtąd — w strefie nie ma `mkfs`, „Wyczyść dane sesji” to po prostu usunięcie kawałków.
- **System plików z dziennikiem w gościu** (ext4, `CONFIG_EXT4_FS` w `jadro/strefa.config`): przerwane wyłączenie naprawia
  jądro gościa przy montowaniu. VMM honoruje `FLUSH`: wszystko, co gość zapisał przed nim, jest w skarbcu, zanim usłyszy
  „gotowe”; między `FLUSH`-ami VMM trzyma kawałki w pamięci podręcznej (256 × 64 KiB) i może zapisać je wcześniej.
- **Wątek dysku w VMM:** skarbiec odpowiada wolno (pendrive, kompaktowanie), więc prośby dysku idą w osobnym wątku, bez
  zamka urządzeń — okno i wejście strefy nie stoją; `pids.max` klatki +1, pamięć +24 MiB.
- **Kto decyduje:** nadzorca (ustawienia sesji w skarbcu — właściciel `K1_WL_SYSTEM`); strefa pamięta tylko, gdy dane K1
  są na dysku (K1-DANE). Bez TRIM (`discard`): usunięte pliki gościa nie zwalniają miejsca w skarbcu — dysk sesji nie
  przekroczy 2 GiB, a „Wyczyść dane sesji” zwalnia wszystko (RYZYKA R87).

## Konsekwencje
- Nowa powierzchnia ataku: VMM w C rozbiera wszystko od gościa (klatka, testy i fuzzing; RYZYKA R77), błędy KVM w jądrze K1
  (R78), ataki procesora na VMM (VMSCAPE i podobne; R79). Okno strefy nie może udawać K1 (R26 domknięte dla stref, R80).
- Komputer bez wirtualizacji (albo z wyłączoną w BIOS-ie) nie uruchomi strefy — okienko mówi, co włączyć (D5).
- Strefa próbna jest tylko w obrazie testowym; użytkownik zobaczy strefy w kroku 2 (Przeglądarka).
- Później: zmiana rozmiaru okna strefy (0.10.5), szybka grafika (virgl/Venus, ADR 0020 p. 2), szybki start i oddawanie
  pamięci (0.10.1 „Balon”). Kółko myszy — 0.10.0 krok 4; dysk strefy — krok 5 (wyżej).

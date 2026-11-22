# Komponent: jądro Linux (konfiguracja K1)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1) — start w QEMU, testy `scripts/test-m1.sh` |
| **Wersja** | 7.3.5 — najnowsza linia stabilna, nie długoterminowa (`jadro/wersja.env`; od 0.10.5 krok 7, D14, T20 — wcześniej 6.18 LTS), decyzje: ADR 0003 |
| **Pliki** | `jadro/k1.config` (fragment Kconfig), `jadro/cmdline`, `jadro/klucze/`, `scripts/zbuduj-jadro.sh`; zamknięte jądro: `jadro/k1-zamkniete.config`, `scripts/zbuduj-k1efi.sh`, klucz Secure Boot: `narzedzia/klucz_sb.py` (ADR 0022) |

## Cel
Jedyny kod działający z pełnymi uprawnieniami sprzętowymi. Wspólna granica zaufania całego świata
natywnego (SPEC §3) — dlatego ma tylko to, czego K1 potrzebuje.

## Granica zaufania
Wszystko w przestrzeni użytkownika (także root) jest „po drugiej stronie”. Lockdown w trybie
confidentiality odcina roota od pamięci jądra (`/dev/mem`, kcore, modyfikacje jądra w locie).
Luka w jądrze = przejęcie całego świata natywnego; stąd minimalna konfiguracja i w przyszłości
osobne jądro dla przeglądarki (0.10, strefy).

## Wejścia i wyjścia
- Wejście: linia poleceń (`jadro/cmdline`: `console=ttyS0 panic=-1 oops=panic k1.rdzen=/dev/vda`),
  initramfs, urządzenie blokowe z obrazem rdzenia, wywołania systemowe procesów.
- Wyjście: konsola na porcie szeregowym `ttyS0` (tylko w developmencie).
- `panic=-1` — po panic natychmiastowy restart (w QEMU z `-no-reboot` = koniec maszyny);
  `oops=panic` — każdy „oops” jądra kończy się panic (stan jądra po oopsie jest niepewny).

## Zabezpieczenia i odstępstwa od KSPP
Lista KSPP: `scripts/sprawdz-kspp.sh` (dane z kernel-hardening-checker v0.6.17.1). Wynik dla
7.3.5 (0.10.5 krok 7 — ten sam co dla 6.18.60 poza jedną nową nazwą, niżej) i powód każdej różnicy:

| Opcja wg KSPP | Dlaczego u nas inaczej |
|---|---|
| `STRICT_DEVMEM`, `IO_STRICT_DEVMEM` | nie dotyczy — `/dev/mem` w ogóle nie istnieje (`DEVMEM=n`) |
| `SYN_COOKIES` | brak TCP/IP do 0.9 |
| `DEFAULT_MMAP_MIN_ADDR=32768` | mamy 65536 (silniej) |
| `MODULE_SIG*` | nie dotyczy — brak modułów (`MODULES=n`) |
| `CFI_CLANG`, `UBSAN_LOCAL_BOUNDS` | wymagają Clanga do budowy jądra (budujemy GCC; ADR 0002/0003) |
| `RANDSTRUCT_FULL`, `GCC_PLUGIN_RANDSTRUCT` | losowe ziarno przy każdym buildzie kłóci się z powtarzalnym buildem; decyzja przy 0.12 |
| `HW_RANDOM_TPM` | TPM — 0.12 |
| `INTEL_IOMMU_SVM` | wymaga dodatkowych podsystemów (SVA/PASID), niepotrzebnych w M1 |
| `AMD_IOMMU_V2`, `DEBUG_CREDENTIALS`, `GCC_PLUGIN_STRUCTLEAK*`, `PAGE_POISONING_ZERO`, `PAGE_TABLE_ISOLATION`, `SLS`, `UBSAN_SANITIZE_ALL` | nazwy ze starszych wersji jądra; w 6.18 zastąpione (`MITIGATION_PAGE_TABLE_ISOLATION`, `MITIGATION_SLS`, `INIT_STACK_ALL_ZERO`, … — wszystkie włączone) albo usunięte |
| `ARM64_*`, `ARM_*`, `SHADOW_CALL_STACK`, `KASAN_HW_TAGS`, `HIGHMEM*`, `X86_PAE` | inne architektury / 32-bit |
| `RANDOM_KMALLOC_CACHES` | nazwa z 6.18; w 7.x ta sama ochrona to `KMALLOC_PARTITION_CACHES` z trybem `KMALLOC_PARTITION_RANDOM` — włączone (lista KSPP zna jeszcze starą nazwę) |

**Linia 7.3 (0.10.5 krok 7):** w 7.x na x86 nie ma już trybu bez wywłaszczania (`PREEMPT_NONE`) — jądro bierze tryb
„leniwy” (`PREEMPT_LAZY`), nowy domyślny; `PROC_SYSCTL` nazywa się `SYSCTL`; wybór TSX w allnoconfig to teraz „auto”,
więc fragment mówi wprost „off” (`X86_INTEL_TSX_MODE_OFF`). Nowe opcje „domyślnie tak”, których K1 nie chce, z powodem —
`jadro/k1-odrzucone.txt` i `jadro/strefa-odrzucone.txt` (koniec plików). Rozmiar: jądro K1 12 972 KiB (6.18: 12 625),
jądro stref 2 976 KiB (2 852), `k1.efi` 125 600 KiB (125 110) — w miejscu A/B 128 MiB zostaje ok. 5 MiB. (Od 0.10.9 krok 4 obraz przeglądarki jest poza
rdzeniem: `k1.efi` 18,2 MiB; od kroku 4b miejsce A/B ma 768 MiB, a jądro strefy nie ma XZ — obraz jest w zstd, ADR 0050.)

Pozostałe zalecenia KSPP dla x86_64 są spełnione (np. KASLR, `STACKPROTECTOR_STRONG`,
`HARDENED_USERCOPY`, `INIT_ON_ALLOC/FREE`, `LIST_HARDENED`, `KSTACK_ERASE`, `IOMMU_DEFAULT_DMA_STRICT`,
lockdown, Yama, brak `IA32_EMULATION`, `LEGACY_TIOCSTI`, `LDISC_AUTOLOAD`, `BINFMT_MISC`, `PROC_KCORE`…).

Wymuszone przez architekturę mimo naszego wyboru: `PERF_EVENTS` (x86 `select PERF_EVENTS`) —
`perf_event_paranoid=2`, a od M3 seccomp usług.

## Uprawnienia, urządzenia
- Sterowniki: PCI, ACPI (wyłączanie zasilania), virtio-blk, 8250 (port szeregowy), IOMMU, EFI stub;
  od M2: DRM + `bochs` (karta QEMU stdvga, tylko KMS — bez fbdev/fbcon), evdev, i8042 + atkbd + psmouse
  (bez mousedev; od 0.10.0 krok 4 z protokołami touchpadów Synaptics — X230 — Elantech i ALPS: dopiero w nich jądro
  podaje palce, więc działa przewijanie dwoma palcami i stuknięcia, `src/wejscie/touchpad.c`; bez wersji przez SMBus); od M4: `simpledrm` (obraz z firmware), loop, CMOS RTC (od 0.9.0
  krok 8 z `/dev/rtc0` — tylko nadzorca zapisuje tam czas z internetu);
  od M4.1: `vmwgfx` (VirtualBox VMSVGA, VMware) z `HYPERVISOR_GUEST` — bez PARAVIRT, KVM_GUEST, Xen
  (ADR 0012); od M5a (ADR 0013): dyski SATA (AHCI) i NVMe, dyski SCSI (`sd`: SATA i pamięci USB),
  tablice partycji tylko GPT, USB (xHCI, EHCI, OHCI), HID generic (klawiatury, myszy USB), mass
  storage — bez napędów CD, `sg`/bsg, `hidraw`/`hiddev`, UAS i sterowników producentów.
  Od 0.5.3 (ADR 0014): ACPI Embedded Controller, bateria i zasilacz (`power_supply`).
  Od 0.5.4: `MTRR` + `X86_PAT` — bez nich `ioremap_wc` (pamięć obrazu simpledrm) daje mapowanie bez
  buforowania zapisu i pulpit na X230 się zacinał. Skutek uboczny: `/proc/mtrr` (tylko root; usługi
  nie widzą `/proc` poza własnymi procesami i nie mają do niego dostępu w klatce).
  Od 0.6.3 (M6b, ADR 0021): device-mapper z `DM_VERITY` (SHA-256) — obraz rdzenia sprawdzany przy starcie; od 0.7.0
  (ADR 0022): `EFIVAR_FS` (zmienne UEFI dla nadzorcy).
  Od 0.8.6 (audyt przed 0.9): **zarządzanie energią procesora** — `NO_HZ_IDLE` (bezczynny procesor bez przerwań
  zegara; wcześniej 250 na sekundę zawsze), `HIGH_RES_TIMERS` (`poll`/`nanosleep` dokładniejsze niż takt 4 ms:
  ekran bez page flip składa 60 klatek na sekundę zamiast ok. 50), `CPU_IDLE` + `INTEL_IDLE` (głębokie stany
  uśpienia; governor `menu`), `CPU_FREQ` + `X86_INTEL_PSTATE` (częstotliwość i turbo; governor `schedutil`),
  `ACPI_PROCESSOR` (stany uśpienia z tablic ACPI dla innych procesorów), `ACPI_THERMAL` +
  `THERMAL_EMERGENCY_POWEROFF_DELAY_MS=1000` (temperatura krytyczna → jądro wyłącza komputer; bez tej drugiej
  opcji próba kończyłaby się na nieistniejącym programie pomocniczym). Do 0.8.5 nie było żadnej z nich (skutek
  `allnoconfig`): procesor spał tylko w `HLT` i nie zmieniał częstotliwości. Sterowniki nie mają interfejsu dla
  usług (sysfs tylko do odczytu, klatki bez ścieżek); w QEMU (TCG) ich nie widać — efekt sprawdza się na sprzęcie
  (X230: Zasoby → pobór mocy na baterii). Bez `CONFIG_PM` (usypianie urządzeń, np. autosuspend USB) i bez
  usypiania komputera — RYZYKA R63.
  Od 0.9.0 (ADR 0039): **sieć** — `INET` (tylko IPv4; bez IPv6, zapory netfilter, multicastu, routingu
  zaawansowanego, IPsec, `IP_PNP`, `INET_DIAG`), `SYN_COOKIES`, `PACKET` (gniazdo pakietowe dla DHCP — otwiera je tylko
  nadzorca), sterowniki kart przewodowych szeroko (decyzje D0, T6): Intel (`E1000E`, `E1000`, `IGB`, `IGC`, `E100`),
  Realtek (`R8169`, `8139CP`, `8139TOO`), Broadcom (`TIGON3`), Qualcomm Atheros (`ALX`, `ATL1C`, `ATL1E`, `ATL1`),
  Marvell (`SKY2`, `SKGE`), NVIDIA (`FORCEDETH`), JMicron (`JME`), VIA (`VIA_RHINE`), maszyny wirtualne (`VIRTIO_NET`,
  `VMXNET3`), przejściówki USB (`USB_RTL8152`, `USB_NET_AX8817X`, `USB_NET_AX88179_178A`, `USB_NET_CDCETHER`,
  `USB_NET_CDC_NCM`). `IGC` wymaga `ETHTOOL_NETLINK`; `IGB` wybiera `I2C`; `R8169` wybiera `FW_LOADER` (K1 nie ma
  plików od producenta — karta działa bez nich). Nie dają się wyłączyć (`def_bool`): `NET_DEVMEM`, `NET_SELFTESTS`.
  Jądro: bzImage 5,0 → 6,3 MiB, `k1.efi` 7,1 → 8,4 MiB. ICMP i ARP ograniczają sysctl w init (`init.md`).
  Brak VT, IPv6, I2C-HID.

### Karta graficzna Intela (0.10.9 krok 2, ADR 0049 pkt 1)
`DRM_I915` (X230: HD 4000, Ivy Bridge; IdeaPad: HD 520, Skylake): przejmuje obraz od simpledrm (zawsze —
`aperture_remove_conflicting_pci_devices` w `i915_driver.c`), węzeł card dalej tylko dla ekranu, węzeł render tylko dla
dekodera filmów (`filmy.md`). Wybiera sam m.in. `SHMEM`/`TMPFS` (bufory karty), `I2C` (DDC monitora), `ACPI_VIDEO` z
`BACKLIGHT_CLASS_DEVICE` i `ACPI_WMI`, `RELAY`, `DRM_TTM`, `DRM_BUDDY`. Wyłączone wprost w `k1.config` (opcje „domyślnie
tak”, żeby porównanie jądra o nie nie pytało): `DRM_I915_CAPTURE_ERROR` i `DRM_I915_COMPRESS_ERROR` (zrzut stanu karty
po zawieszeniu — K1 nie ma debugfs), `DRM_I915_USERPTR` (bufory z pamięci procesu — i965 ich nie używa, mniej kodu
w zasięgu strefy), `WMI_BMOF`. Plik DMC dla Skylake (`i915/skl_dmc_ver1_27.bin`, głębokie stany oszczędzania obrazu, S10)
w `jadro/oprogramowanie-kart.txt`; Ivy Bridge go nie ma. bzImage: +1,3 MiB.

**Nowsze karty zintegrowane Intela (0.10.11 krok 3, ADR 0051 — uzupełnienie; decyzje właściciela D5, D6 „A”).** Ten sam
sterownik i915 — bez nowych opcji jądra, tylko pliki od producenta (`jadro/oprogramowanie-kart.txt`; k1.efi +1,3 MiB).
Zakres: karty zintegrowane, które i915 jądra 7.3 obsługuje bez `force_probe` (`i915_pci.c`); karty dyskretne (Arc, DG1,
DG2) — poza zakresem. **Niesprawdzone na sprzęcie** poza Ivy Bridge (X230) i Skylake (IdeaPad) — dowodem są źródła i915
i Mesy 26.2.6 oraz testy na hoście i w CI.

| Generacja (lata) | Przykłady | DMC (obraz, bateria) | GuC | Szybka grafika |
|---|---|---|---|---|
| Ivy Bridge, Haswell, Broadwell (2012–2015) | X230 | brak / niepotrzebny | nie | nie — bez pełnego PPGTT (Ivy Bridge, Haswell); Broadwell: wg sprawdzenia pośrednika |
| Skylake (2015) | IdeaPad 110 | `skl_dmc_ver1_27` | nie | tak (sprawdzone) |
| Apollo Lake, Gemini Lake (2016–2017) | tanie laptopy z Celeronem / Pentium | `bxt_dmc_ver1_07`, `glk_dmc_ver1_04` | nie | wg sprawdzenia pośrednika |
| Kaby / Coffee / Whiskey / Amber / Comet Lake (2016–2020) | 7.–10. gen. Core | `kbl_dmc_ver1_04` | nie | tak — jak Skylake (Gen9) |
| Ice Lake, Elkhart / Jasper Lake (2019–2021) | 10. gen. Core „G” | `icl_dmc_ver1_09` | nie | tak (Gen11) |
| Tiger Lake, Rocket Lake (2020–2021) | 11. gen. Core | `tgl_dmc_ver2_12`, `rkl_dmc_ver2_03` | nie (i915: domyślnie wyłączony) | tak (Gen12) |
| Alder Lake S / HX (2021–2022) | 12. gen. Core, komputery stacjonarne i HX | `adls_dmc_ver2_01` | tylko HuC (nie potrzebny) | tak (Gen12) |
| Alder Lake P / N, Raptor Lake P / U (2022–2023) | 12.–13. gen. Core laptopów | `adlp_dmc` | **tak** — `adlp_guc_70` | tak (Gen12) |
| Raptor Lake S / HX (2022–2023) | 13.–14. gen. Core | `adls_dmc_ver2_01` | **tak** — `tgl_guc_70` | tak (Gen12) |
| Meteor / Arrow Lake (2023–2024) | Core Ultra 1. i 2. serii „H/U” | `mtl_dmc` | **tak** — `mtl_guc_70` | tak (Gen12.7; pośrednik przyjmuje „PAT”) |
| Lunar Lake i nowsze (2024–) | Core Ultra 2. serii „V” | — | — | nie: sterownik xe (D6 „A”: nie w tej wersji); pulpit przez obraz z firmware (simpledrm) |

- **GuC** (`gt/uc/intel_uc.c`: `uc_expand_default_options`): na Alder Lake P, Raptor Lake i Meteor / Arrow Lake i915
  zleca pracę karcie tylko przez GuC; bez pliku uznaje kartę za zawieszoną — obraz działa, rysowanie nie (szybka grafika,
  dekoder filmów). Raptor Lake S / HX biorą plik Tiger Lake (`fw_def(ALDERLAKE_S, …, tgl)`). HuC i GSC (tylko filmy kartą)
  — nie dodane: ich brak i915 zgłasza tylko w logu.
- **DMC** (`display/intel_dmc.c`, `dmc_firmware_default`): bez niego obraz działa, ale bez głębokich stanów oszczędzania
  obrazu (bateria).
- **Zasoby → „Karta graficzna”**: Meteor / Arrow Lake mają dwie części karty (`gt0` — grafika, `gt1` — media); pliki samej
  karty łączą obie (najmniejszy czas snu, najwyższe taktowanie — `gt/intel_gt_sysfs_pm.c`), więc nadzorca czyta najpierw
  `gt/gt0/` (`k1_karta_pomiar.c`, tabela `K1_KARTA_PLIKI`). RAPL „uncore” (pobór mocy karty) — NIEPEWNE na nowszych
  procesorach; bez niego Zasoby pokazują tylko pobór laptopa.
- **Dekoder filmów** (i965, uśpiony) obsługuje karty do Gen9.5 — na nowszych pytanie przy starcie kończy się „nie obsługuje”
  (`vaInitialize` odmawia), bez awarii.
- **Droga obrazu bez kopii** (ADR 0051): warstwa karty wybiera płaszczyznę po typie i formacie (XRGB8888, liniowy), nowy
  bufor najpierw `TEST_ONLY` — nic nie zakłada Skylake; na nowszych kartach kompresji i kafelków (CCS) nie używa.
0.10.10 krok 2: `POWERCAP` i `INTEL_RAPL` — licznik energii procesora (domena „uncore”: karta graficzna) dla Zasobów
(`aplikacje.md`); czyta tylko nadzorca, K1 nie ustawia ograniczeń mocy. `IOSF_MBI` był już włączony.

### Strefy (0.10.0, ADR 0042)
`VIRTUALIZATION`, `KVM` (wbudowany), `KVM_INTEL`, `KVM_AMD`, `KVM_IOAPIC` (kontroler przerwań i PIT w jądrze — „default y”,
który allnoconfig wyłącza) i z powrotem `MITIGATION_VMSCAPE`. Bez `KVM_SMM`, `KVM_HYPERV`, `KVM_XEN`, `X86_SGX_KVM`,
`KVM_AMD_SEV`, `KVM_INTEL_TDX`, `KVM_SW_PROTECTED_VM`. KVM wybiera sam `VHOST_TASK`, `KVM_VFIO`, `KVM_GUEST_MEMFD`,
`KVM_MMIO` (nic z tego nie jest używane poza tym, co trzeba maszynom stref). `/dev/kvm` tylko przy wirtualizacji
włączonej w BIOS-ie, prawa 0600 root — otwiera go tylko nadzorca dla VMM strefy. bzImage 12 530 688 B → 12 903 424 B.

**Jądro strefy** (`jadro/strefa.config`, ten sam allnoconfig tych samych źródeł, `scripts/zbuduj-jadro.sh` buduje oba
i sprawdza oba fragmenty; obiekty w `build/jadro-strefy-obj`, wynik `build/wynik/strefa-jadro`, w rdzeniu
`/rdzen/strefy/jadro`): 64-bit, SMP (8 procesorów), zstd, gość KVM (kvmclock, x2APIC), tylko virtio-mmio z urządzeniami
z linii poleceń (blk, konsola z TTY bez VT i pty, rng, input + evdev bez PS/2, DRM virtio-gpu z KMS bez fbdev, vsock
z NET bez INET), proc, sysfs, devtmpfs (montowany sam), tmpfs, SquashFS (zstd), `DEVMEM` bez `STRICT_DEVMEM` (root gościa
i tak ma całą swoją maszynę; testy złośliwego gościa). Bez PCI, ACPI, modułów. 2,2 MB.
0.10.0 krok 3: **`SWAP` + `ZRAM`** (lz4) — pamięć kompresowana w pamięci gościa, nigdy na dysku (S26: ciężkie strony
zabijały przeglądarkę; jądro K1 dalej bez swapu — niezmiennik 3 dotyczy świata natywnego, ADR 0042 T4); SquashFS
`FILE_DIRECT` i `COMPILE_DECOMP_MULTI_PERCPU` — bloki obrazu rozpakowywane wprost do pamięci podręcznej stron i na każdym
procesorze osobno (S30: szybszy start przeglądarki).
0.10.0 krok 5: **`EXT4_FS`** — dysk sesji, która pamięta (`/dev/vdb`): ext4 z dziennikiem (przerwane wyłączenie naprawia
jądro gościa przy montowaniu; `JBD2`, `CRC32` jako biblioteka, bez `CRYPTO`); pusty system plików robi budowa
(`mke2fs`), w strefie nie ma `mkfs` (ADR 0042 T11).
0.10.1 krok 2: **`VIRTIO_BALLOON`** (wybiera `MEMORY_BALLOON` i `PAGE_REPORTING`) — balon: gość zgłasza wolne bloki
(2 MiB, ok. 2 s po zwolnieniu), VMM oddaje je K1; na prośbę K1 gość oddaje strony do balonu (ADR 0044). Bez
`BALLOON_COMPACTION` (wymaga `COMPACTION`, której jądro strefy nie ma).
0.11.2 (szybsza przeglądarka): jądro K1 — **`TRANSPARENT_HUGEPAGE`** z `COMPACTION` i `MIGRATION` (zdjęte z listy
`k1-odrzucone.txt`), domyślnie tylko na prośbę: zwykła pamięć `TRANSPARENT_HUGEPAGE_MADVISE`, memfd
`TRANSPARENT_HUGEPAGE_SHMEM_HUGE_ADVISE` — duże strony (2 MiB) dostaje tylko pamięć gościa strefy (VMM:
`madvise(MADV_HUGEPAGE)`, jak QEMU); usługi K1 nic nie proszą, więc dla nich bez zmian. Na procesorach z błędem iTLB
multihit (X230, IdeaPad) KVM i tak dzieli wykonywalne strony gościa na małe (`nx_huge_pages`, ustawienie domyślne) — duże
zostają dla danych. Jądro strefy — **`PARAVIRT_SPINLOCKS`**: procesor strefy czekający na blokadę, którą trzyma
wstrzymany procesor tej samej strefy, śpi do „kopnięcia” od KVM zamiast kręcić się w miejscu (przeglądarka ma do 4
procesorów na komputerze, na którym pracuje też K1).
0.11.3: jądro strefy — `NR_CPUS=256` (procesory komputera, do `K1_STREFA_PROCESORY_MAKS` = 254); **`TRANSPARENT_HUGEPAGE`**
(`ALWAYS`, z `COMPACTION` i `MIGRATION` zdjętymi z `strefa-odrzucone.txt`) — duże strony w gościu na dużych stronach K1;
**`CPU_IDLE` + `HALTPOLL_CPUIDLE`** (z listy odrzuconych zdjęte też `CPU_IDLE_GOV_HALTPOLL`) — bezczynny procesor strefy
czeka chwilę czynnie przed HLT; włącza go `cpuidle_haltpoll.force=1` od VMM-a (poza emulacją QEMU).

### Wi-Fi (0.9.1, ADR 0041)
`WIRELESS`, `CFG80211` (bez `CFG80211_WEXT` i CRDA), `MAC80211` z minstrelem (bez mesh), `RFKILL` (bez `RFKILL_INPUT`),
sterowniki kart PCIe: Intel iwlwifi (DVM, MVM), Qualcomm Atheros ath9k (z `ATH9K_PCOEM`) i ath10k, Realtek rtlwifi
(8723BE, 8821AE), rtw88 (8822CE, 8822BE, 8821CE, 8723DE), rtw89 (8852BE), MediaTek MT7921E; `MAC80211_HWSIM` do testów
(`mac80211_hwsim.radios=0` w linii poleceń wydania). **Pliki od producentów** kart są wbudowane (`EXTRA_FIRMWARE`, tabela
`jadro/oprogramowanie-kart.txt`, 32 pliki z linux-firmware 20261019, 27 MB) — `scripts/zbuduj-jadro.sh` dokłada
`CONFIG_EXTRA_FIRMWARE` i `CONFIG_EXTRA_FIRMWARE_DIR` do fragmentu (`build/jadro-fragment.config`), więc sprawdzenie
konfiguracji obejmuje i je. **Baza przepisów radiowych** (`regulatory.db` z wireless-regdb 2026.09.03) wbudowana
i sprawdzana podpisem (`CFG80211_REQUIRE_SIGNED_REGDB`, `CFG80211_USE_KERNEL_REGDB_KEYS` — „default y” za pytaniem
ukrytym bez `CFG80211_CERTIFICATION_ONUS`). **Odstępstwo:** to wybiera `SYSTEM_DATA_VERIFICATION` — w jądrze jest kod
podpisów (RSA, X.509, PKCS#7, `KEYS`, `SYSTEM_TRUSTED_KEYRING`) — tylko do sprawdzania bazy; wywołania `add_key`/`keyctl`
są w każdej klatce zablokowane (seccomp), używa ich tylko jądro. **Kompresja jądra: zstd** (od 0.9.1; bzImage
6 595 584 B → 12 530 688 B, `k1.efi` 8 923 792 B → 14 899 856 B; z gzip byłoby 20 MB).

## Wywołania systemowe usunięte z jądra (test: `t_syscalls`)
`init_module`, `finit_module`, `kexec_load`, `kexec_file_load`, `io_uring_setup`, `bpf`,
`userfaultfd`, `modify_ldt`, `iopl`, `ioperm` → `ENOSYS`. Gniazda `AF_INET6` → `EAFNOSUPPORT`; `AF_INET` (od 0.9.0) i UNIX
są (UNIX od M3 — kanały między usługami, ADR 0007), a `socket` i `socketpair` blokuje seccomp każdej klatki (`t_proby`:
`AF_INET`, `AF_PACKET`, `AF_NETLINK`) — gniazdo sieciowe otwiera tylko nadzorca (ADR 0039).

## Obsługa błędów
`panic=-1 oops=panic` — błąd jądra kończy się restartem, nie pracą w niepewnym stanie.

## Powierzchnia ataku (najważniejsze)
Parser SquashFS (od 0.6.3 obraz sprawdzany przez dm-verity), wywołania systemowe dostępne dla procesów
(zawężane przez seccomp od M3), sterowniki virtio, ACPI (dane z firmware), `perf_event_open`;
od M5a: urządzenia USB podłączane z zewnątrz (deskryptory USB, raporty HID, komendy mass storage)
i parser GPT obcych nośników (RYZYKA R33–R34); od 0.9.1: stos 802.11 (cfg80211, mac80211), sterowniki kart Wi-Fi
i pliki producentów działające w karcie (RYZYKA R70–R72); od 0.9.0: stos IPv4 i sterowniki kart sieciowych — pakiety od każdego
w tej samej sieci, przejściówki USB–Ethernet (RYZYKA R66, R67; test z zewnątrz `scripts/test-siec.sh`).

## Testy
`scripts/sprawdz-konfiguracje-jadra.sh` (każdy build), `scripts/sprawdz-kspp.sh` (przy zmianie
wersji), w systemie: `testy/system/t_system.c` (LSM, lockdown, Landlock, usunięte syscalle, sysctl).

## Zamknięte jądro `k1.efi` (0.7.0, ADR 0022)
Wydania (pendrive, dysk, ISO) uruchamiają `k1.efi`: to samo jądro z `k1.config` plus `jadro/k1-zamkniete.config`
— `CMDLINE_BOOL` + `CMDLINE_OVERRIDE` (linia z `jadro/cmdline`, `k1.rdzen=wbudowany`, `k1.rdzen_skrot`, `noinitrd`,
`quiet loglevel=7`) i `INITRAMFS_SOURCE` (initramfs + `rdzen.squashfs`, bez osobnej kompresji). Podpisane do Secure
Boot (`sbsign`, RSA-2048 z `narzedzia/klucz_sb.py`, zegar zamrożony `faketime` — powtarzalne). Bez protokołu „EFI
handover” (wyłączony) i z `noinitrd` program ładujący nie ma jak podać initrd — sprawdzone w QEMU z GRUB-em
Ubuntu. Budowane w tym samym katalogu co jądro deweloperskie (`bzImage`, które zostaje dla testów `test-m1`).
`CONFIG_EFIVAR_FS=y` (0.7.0): zmienne UEFI dla nadzorcy (stan Secure Boot, wpisanie klucza K1).


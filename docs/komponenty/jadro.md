# Komponent: jądro Linux (konfiguracja K1)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1) — start w QEMU, testy `scripts/test-m1.sh` |
| **Wersja** | 6.18.60 LTS (`jadro/wersja.env`), decyzje: ADR 0003 |
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
6.18.60 i powód każdej różnicy:

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
  Brak VT, IPv6, I2C-HID, `i915`.

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


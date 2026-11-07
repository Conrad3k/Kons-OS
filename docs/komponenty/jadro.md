# Komponent: jądro Linux (konfiguracja K1)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1) — start w QEMU, testy `scripts/test-m1.sh` |
| **Wersja** | 6.18.60 LTS (`jadro/wersja.env`), decyzje: ADR 0003 |
| **Pliki** | `jadro/k1.config` (fragment Kconfig), `jadro/cmdline`, `jadro/klucze/`, `scripts/zbuduj-jadro.sh` |

## Cel
Jedyny kod działający z pełnymi uprawnieniami sprzętowymi. Wspólna granica zaufania całego świata
natywnego (SPEC §3) — dlatego ma tylko to, czego K1 potrzebuje.

## Granica zaufania
Wszystko w przestrzeni użytkownika (także root) jest „po drugiej stronie”. Lockdown w trybie
confidentiality odcina roota od pamięci jądra (`/dev/mem`, kcore, modyfikacje jądra w locie).
Luka w jądrze = przejęcie całego świata natywnego; stąd minimalna konfiguracja i w przyszłości
osobne jądro dla przeglądarki (M11).

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
| `SYN_COOKIES` | brak TCP/IP do M7 |
| `DEFAULT_MMAP_MIN_ADDR=32768` | mamy 65536 (silniej) |
| `MODULE_SIG*` | nie dotyczy — brak modułów (`MODULES=n`) |
| `CFI_CLANG`, `UBSAN_LOCAL_BOUNDS` | wymagają Clanga do budowy jądra (budujemy GCC; ADR 0002/0003) |
| `RANDSTRUCT_FULL`, `GCC_PLUGIN_RANDSTRUCT` | losowe ziarno przy każdym buildzie kłóci się z powtarzalnym buildem; decyzja przy M10 |
| `HW_RANDOM_TPM` | TPM — M10 |
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
  (bez rozszerzeń producentów i bez mousedev); od M4: `simpledrm` (obraz z firmware), loop, CMOS RTC;
  od M4.1: `vmwgfx` (VirtualBox VMSVGA, VMware) z `HYPERVISOR_GUEST` — bez PARAVIRT, KVM_GUEST, Xen
  (ADR 0012); od M5a (ADR 0013): dyski SATA (AHCI) i NVMe, dyski SCSI (`sd`: SATA i pamięci USB),
  tablice partycji tylko GPT, USB (xHCI, EHCI, OHCI), HID generic (klawiatury, myszy USB), mass
  storage — bez napędów CD, `sg`/bsg, `hidraw`/`hiddev`, UAS i sterowników producentów.
  Brak VT, sieci (protokołów), I2C-HID, `i915`.

## Wywołania systemowe usunięte z jądra (test: `t_syscalls`)
`init_module`, `finit_module`, `kexec_load`, `kexec_file_load`, `io_uring_setup`, `bpf`,
`userfaultfd`, `modify_ldt`, `iopl`, `ioperm` → `ENOSYS`. Gniazda `AF_INET` i `AF_UNIX` →
`EAFNOSUPPORT` (M1).

## Obsługa błędów
`panic=-1 oops=panic` — błąd jądra kończy się restartem, nie pracą w niepewnym stanie.

## Powierzchnia ataku (najważniejsze)
Parser SquashFS (do M10 obraz niezweryfikowany), wywołania systemowe dostępne dla procesów
(zawężane przez seccomp od M3), sterowniki virtio, ACPI (dane z firmware), `perf_event_open`;
od M5a: urządzenia USB podłączane z zewnątrz (deskryptory USB, raporty HID, komendy mass storage)
i parser GPT obcych nośników (RYZYKA R33–R34).

## Testy
`scripts/sprawdz-konfiguracje-jadra.sh` (każdy build), `scripts/sprawdz-kspp.sh` (przy zmianie
wersji), w systemie: `testy/m1/k1_test_m1.c` (LSM, lockdown, Landlock, usunięte syscalle, sysctl).

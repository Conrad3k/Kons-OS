# 0003. Jądro: wersja, źródło, konfiguracja, wzmocnienia

- Status: Przyjęty; uzupełniony przez ADR 0047 (0.10.5): linię i wersję jądra prowadzi robot — najnowsza długoterminowa, ta sama linia sam, nowsza po porównaniu konfiguracji
- Data: 2026-11-06
- Dotyczy: SPEC §6 (swap, hibernacja), §7 (LSM, seccomp, brak TTY), §10 (jądro), M1

## Kontekst
- Lista wydań kernel.org (`releases.json`, 2026-11-06): najnowsza linia *longterm* to **6.18**
  (6.18.60, wydane 2026-10-30). Linie 6.12/6.6/6.1/5.15/5.10 są starsze.
- cdn.kernel.org był zablokowany w sieci kontenera; po zgodzie właściciela dodano do środowiska
  hosty cdn.kernel.org, www.kernel.org i git.kernel.org.
- Kconfig po cichu pomija opcje z niespełnionymi zależnościami. Weryfikacja fragmentu wykazała
  rozbieżności między SPEC a rzeczywistością (źródła 6.18.60):
  - `SECCOMP_FILTER` ma `depends on HAVE_ARCH_SECCOMP_FILTER && SECCOMP && NET` (`arch/Kconfig`) —
    filtr seccomp używa kodu BPF z `net/`. Bez `CONFIG_NET` nie ma seccomp-BPF (wymaganego w §7).
  - `PERF_EVENTS` jest wymuszone na x86 (`arch/x86/Kconfig`: `select PERF_EVENTS`).
  - `vm.memfd_noexec` jest rejestrowany tylko z `CONFIG_PID_NS` (`kernel/pid_namespace.c`,
    `kernel/pid_sysctl.h`).
  - `UBSAN_LOCAL_BOUNDS` działa tylko z Clangiem; `MITIGATION_VMSCAPE` zależy od `KVM`.
- Zalecenia KSPP: strona kspp.github.io jest zablokowana; listę wzięto z pliku danych projektu
  kernel-hardening-checker v0.6.17.1 (GitHub), który koduje zalecenia KSPP (odczyt jako tekst,
  bez uruchamiania kodu; `scripts/sprawdz-kspp.sh`).

## Decyzja
- **Wersja:** Linux **6.18.60** (LTS), tarball z cdn.kernel.org. Suma SHA-256 przypięta w
  `jadro/wersja.env`; podpis PGP wydania (Greg Kroah-Hartman, `647F2865…6092693E`) sprawdzany
  kluczem z `jadro/klucze/` (klucze pobrane z repozytorium pgpkeys na git.kernel.org; podpis
  `sha256sums.asc` przez autosigner kernel.org `B8868C80…589DA6B1` sprawdzony przy przypinaniu).
- **Konfiguracja:** `make allnoconfig` + fragment `jadro/k1.config` (`KCONFIG_ALLCONFIG`).
  Wszystko, czego nie ma we fragmencie, jest wyłączone. `scripts/sprawdz-konfiguracje-jadra.sh`
  sprawdza po każdej konfiguracji, że każda linia fragmentu ma w `.config` żądaną wartość — inaczej
  build się przerywa.
- **Bez modułów:** `CONFIG_MODULES=n` (wszystko wkompilowane; brak ładowania kodu do jądra w trakcie
  pracy). **Bez swapu i hibernacji:** `CONFIG_SWAP=n`, `CONFIG_HIBERNATION=n`.
- **LSM:** `CONFIG_LSM="landlock,lockdown,yama"`, lockdown wymuszony w trybie
  **confidentiality** (`LOCK_DOWN_KERNEL_FORCE_CONFIDENTIALITY`), od wczesnego startu.
- **Korekty SPEC:** `CONFIG_NET=y` bez protokołów (`INET=n`, `UNIX=n` w M1) — tylko po to, by
  działał seccomp-BPF; `CONFIG_PID_NS=y` (memfd_noexec; potrzebne i tak w M3/M7);
  `PERF_EVENTS` zostaje (wymuszone) — ograniczone przez `kernel.perf_event_paranoid=2`, a od M3
  przez seccomp usług.
- **Usunięta powierzchnia ataku:** brak VT i pseudoterminali (`VT`, `UNIX98_PTYS`, `LEGACY_PTYS`),
  skryptów `#!` (`BINFMT_SCRIPT=n`), `binfmt_misc`, core dumpów, io_uring, AIO, BPF syscall,
  userfaultfd, kexec, kprobes, ftrace, debugfs, SysRq, `/dev/mem`, `/dev/port`, `/proc/kcore`,
  emulacji 32-bit (`IA32_EMULATION`, `X86_X32_ABI`), vsyscall, `modify_ldt`, `iopl/ioperm`,
  `fhandle`, przestrzeni nazw użytkownika (`USER_NS`, decyzja w M7).
  Jądro nie uruchamia żadnych programów pomocniczych: `STATIC_USERMODEHELPER=y` z pustą ścieżką.
- **Wzmocnienia (KSPP):** KASLR (`RANDOMIZE_BASE`, `RANDOMIZE_MEMORY`), losowy offset stosu,
  `STACKPROTECTOR_STRONG`, `VMAP_STACK`, `STRICT_KERNEL_RWX`, `HARDENED_USERCOPY`, `FORTIFY_SOURCE`,
  zerowanie stosu/alokacji/zwalnianej pamięci, `ZERO_CALL_USED_REGS`, utwardzony alokator SLUB
  (`SLAB_FREELIST_*`, `SLAB_BUCKETS`, `RANDOM_KMALLOC_CACHES`), `LIST_HARDENED`,
  `BUG_ON_DATA_CORRUPTION`, `PAGE_TABLE_CHECK_ENFORCED`, `UBSAN_BOUNDS`+`UBSAN_TRAP`, `KFENCE`,
  IBT, shadow stack dla przestrzeni użytkownika, wtyczki GCC `KSTACK_ERASE` i `LATENT_ENTROPY`,
  IOMMU (Intel/AMD, tryb ścisły), `EFI_DISABLE_PCI_DMA`, `RESET_ATTACK_MITIGATION`,
  wszystkie `MITIGATION_*` dla x86 (allnoconfig domyślnie je **wyłącza** — włączamy jawnie),
  `SECURITY_DMESG_RESTRICT`, `PROC_MEM_NO_FORCE`, `DEFAULT_MMAP_MIN_ADDR=65536`.
  Pełne zestawienie z listą KSPP i powody odstępstw: `docs/komponenty/jadro.md`.
- **Świadomie bez:** `RANDSTRUCT` (losowe ziarno przy każdym buildzie kłóci się z powtarzalnym
  buildem — do decyzji przy M10), `CFI_CLANG` (wymaga Clanga do jądra), `INTEL_IOMMU_SVM`,
  `HW_RANDOM_TPM` (M10), `SYN_COOKIES` (M7).
- **Powtarzalność:** stały `KBUILD_BUILD_TIMESTAMP/USER/HOST/VERSION` i `-frandom-seed` dla wtyczki
  latent_entropy (bez niego wtyczka losuje stałe przy każdym buildzie).
- **Sprzęt w M1:** tylko to, czego potrzebuje maszyna QEMU q35 (PCI, ACPI, virtio-blk, port
  szeregowy 8250, EFI stub na przyszłość). Sterowniki prawdziwego sprzętu — od M2.

- **[korekta: plan 0.10.5, krok 7 — D14, T20; 2026-11-18]** Od 0.10.5 K1 jest na linii **7.3** (7.3.5) — najnowszej
  stabilnej w chwili zmiany; długoterminowej linii 7.x jeszcze nie było. Zasada wyboru linii (T20): najnowsza długoterminowa,
  a gdy nowszej od obecnej nie ma — najnowsza stabilna (nigdy rc); poprawki tej linii i przejście na nową długoterminową
  robi robot (ADR 0047), koniec krótkiej linii bez nowszej długoterminowej — Issue dla człowieka. Konfiguracja bez zmian
  w zasadzie (allnoconfig + fragment); przy zmianie linii porównanie `scripts/porownaj-jadro.sh` (T19).

## Konsekwencje
- Jądro ma ~3,8 MiB i buduje się w ~2–3 min na 4 rdzeniach.
- Każda aktualizacja jądra: nowa suma w `jadro/wersja.env`, sprawdzenie podpisu, ponowne
  `scripts/sprawdz-kspp.sh` i testy M1 (procedura w `jadro/README.md`).
- `NET=y` dodaje do jądra podstawowy kod sieci (bez protokołów) — większa TCB niż zakładała SPEC,
  ale bez tego nie ma seccomp-BPF.
- `PERF_EVENTS` (wymuszone przez architekturę) to znana powierzchnia ataku — `docs/RYZYKA.md`.
- `kernel.yama.ptrace_scope=3` (ustawiane przez init) jest nieodwracalne do restartu: debugowanie
  procesów jest niemożliwe także dla roota.

## Rozważane alternatywy
- Linia 6.12 (jak w Debianie 13) — starsza; 6.18 ma dłuższe wsparcie.
- Źródła z pakietu Debiana — możliwe, ale wersje opóźnione względem kernel.org i zmodyfikowany
  tarball; SPEC preferuje szybkie poprawki bezpieczeństwa.
- `tinyconfig` zamiast `allnoconfig` — włącza optymalizację rozmiaru (`-Os`) i wyłącza rzeczy,
  które chcemy; `allnoconfig` + jawny fragment jest czytelniejszy w audycie.
- Moduły podpisane — więcej mechanizmów (klucze, podpisy w buildzie) bez korzyści: K1 zna swój
  sprzęt w momencie budowy.

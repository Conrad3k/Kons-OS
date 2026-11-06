# Znane ograniczenia i ryzyka K1

Zasada (SPEC §2.5): mechanizm bezpieczeństwa to nie gwarancja. Tu zapisujemy, czego system
**jeszcze nie chroni** albo **nie może chronić**, i kiedy planujemy to zmienić.
Stan na wersję 0.1.0 (koniec M1). Klasyfikacja: **W** — wysokie, **Ś** — średnie, **N** — niskie.

| Nr | Ryzyko / ograniczenie | Waga | Co je ogranicza teraz | Plan |
|---|---|---|---|---|
| R1 | **Brak weryfikacji integralności obrazu rdzenia.** Jądro parsuje niezweryfikowany SquashFS; podmieniony lub uszkodzony obraz może uruchomić dowolny kod jako PID 1 albo wywołać kernel panic. | W | nic poza fizyczną kontrolą nośnika | M10: dm-verity z root hash w podpisanej linii poleceń |
| R2 | **Brak Secure Boot i podpisanego UKI.** Linię poleceń jądra (`k1.rdzen`, `lsm=`, `lockdown=`) może zmienić każdy, kto kontroluje bootloader/QEMU. | W | — | M10 |
| R3 | **W M1 wszystko działa jako root** z pełnymi capabilities (brak nadzorcy, UID usług, seccomp, Landlock dla procesów). | W | w obrazie nie ma żadnych programów poza init (i testem w obrazie testowym) | M3 |
| R4 | **Jądro to wspólna granica zaufania.** Luka zero-day w jądrze daje przejęcie całego świata natywnego. | W | minimalna konfiguracja (brak modułów, io_uring, BPF, userfaultfd, 32-bit, VT…), wzmocnienia KSPP, lockdown | szybkie aktualizacje LTS (`jadro/README.md`); M11: osobne jądro dla przeglądarki |
| R5 | **Awaria PID 1 = kernel panic** (cecha Linuksa). Błąd pamięci w init zatrzyma system. | Ś | init ≈240 linii, bez parsowania danych zewnętrznych; flagi i testy | audyt w M12 |
| R6 | **`/dane_uzytkownika` to tmpfs w RAM**: bez szyfrowania, dane znikają po wyłączeniu. | Ś (M1 nie przechowuje danych) | `noexec,nosuid,nodev`, limit 16 MiB, `mode=0700` | M5: Blob Vault + szyfrowanie |
| R7 | **`_FORTIFY_SOURCE` nie działa z musl** (brak kontroli przepełnień w funkcjach libc w czasie działania). | Ś | jawne limity długości, `-Wformat=2`, testy ASan/UBSan i fuzzing na hoście | decyzja o `fortify-headers` w M3 (ADR 0002) |
| R8 | **musl 1.2.4-2 z Ubuntu bez poprawki CVE-2025-26519** (iconv). | N | nasze programy nie linkują `iconv` (sprawdzone `nm`) | własny build musl 1.2.5 z poprawkami przed M3 |
| R9 | **`PERF_EVENTS` wymuszone na x86** — duża, historycznie podatna część jądra dostępna przez `perf_event_open`. | Ś | `kernel.perf_event_paranoid=2`, lockdown confidentiality | M3: seccomp usług blokuje `perf_event_open` |
| R10 | **Port szeregowy w developmencie:** konsola jądra na `ttyS0` i węzeł `/rdzen/jadro/dev/ttyS0`. „Jednokierunkowość” wynika z tego, że nic nie czyta wejścia — nie jest wymuszona sprzętowo. | Ś | brak getty/powłoki, SysRq wyłączone, w M3 usługi bez dostępu do `/dev` | M10: obraz produkcyjny bez `console=ttyS0` (decyzja w ADR) |
| R11 | **Brak kCFI i RANDSTRUCT** w jądrze (budujemy GCC; RANDSTRUCT kłóci się z powtarzalnym buildem). | Ś | IBT (sprzętowa ochrona skoków pośrednich na nowszych CPU), pozostałe wzmocnienia | ocena przy M10/M12 |
| R12 | **Brak ochrony CET w przestrzeni użytkownika** (musl nie obsługuje shadow stack/IBT). | N | PIE, RELRO, NX, kanarki stosu | — |
| R13 | **Pseudo-losowość przy starcie:** brak sprzętowego źródła (TPM) w konfiguracji. | N | jądro korzysta z RDRAND/RDSEED (gdy CPU je ma) i jittera; wtyczka LATENT_ENTROPY | M10: `HW_RANDOM_TPM` |
| R14 | **Łańcuch dostaw budowy:** kompilator i musl z Ubuntu, runner GitHub Actions; artefakty CI niepodpisane. | Ś | przypięte sumy i podpis PGP jądra; powtarzalny build (można odtworzyć i porównać `SHA256SUMS`) | M10: podpisy obrazów |
| R15 | **Testy tylko w QEMU (TCG).** M1 nie startuje na prawdziwym sprzęcie (brak sterowników dysków, grafiki, klawiatury). | N (etap rozwojowy) | — | M2+ i testy właściciela na laptopie |
| R16 | **`ptrace_scope=3` jest nieodwracalne** — także debugowanie awarii jest niemożliwe w działającym systemie. | N (świadomy wybór) | logi | — |
| R17 | **Zatrzymanie po błędzie krytycznym widać tylko na porcie szeregowym** (brak ekranu w M1). | N | — | M2: komunikat na ekranie |

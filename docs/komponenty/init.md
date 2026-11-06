# Komponent: init (`/rdzen/init`, PID 1)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1) — `scripts/test-m1.sh`, scenariusze `start` i `testy-w-systemie` (33 sprawdzenia w systemie) |
| **Kod** | `src/init/main.c` (+ `src/wspolne/k1_log.c`) |
| **Decyzje** | ADR 0004, ADR 0005; M4: `/dane_uzytkownika` z `mode=0711` (aplikacje wchodzą do własnego podkatalogu, ADR 0008) |

## Cel
Pierwszy proces świata natywnego: montuje pseudo-systemy plików pod `/rdzen/jadro`, ustawia sysctl
bezpieczeństwa, uruchamia nadzorcę (M3), zbiera osierocone procesy, obsługuje sygnały i kontrolowane
zamknięcie. Celowo bez logiki aplikacyjnej, plików konfiguracyjnych i parsowania danych z zewnątrz.

## Granica zaufania
PID 1 z pełnymi uprawnieniami; jądro chroni go przed sygnałami bez obsługi (SIGKILL od roota nie
działa — test). Nie przyjmuje żadnych danych wejściowych poza sygnałami i statusami dzieci.
Awaria init = kernel panic, więc kod jest mały i nie przetwarza niezaufanych danych.

## Uprawnienia
UID 0, wszystkie capabilities. Nadzorca (od M2 w obrazie) też startuje jako root — podział
uprawnień to M3.

## Wejścia i wyjścia
- Wejście: sygnały (SIGCHLD, SIGTERM, SIGINT), statusy zakończenia procesów potomnych.
- Wyjście: komunikaty `K1: …` na konsoli (m.in. `K1: init OK`), montowania, sysctl, procesy.

## Co ustawia
Montowania i ich flagi: tabela w ADR 0004. Sysctl (wszystkie wymagane, inaczej awaria):

| sysctl | wartość | po co |
|---|---|---|
| `kernel.kptr_restrict` | 2 | adresy jądra ukryte także przed rootem |
| `kernel.dmesg_restrict` | 1 | log jądra tylko dla uprzywilejowanych |
| `kernel.yama.ptrace_scope` | 3 | brak ptrace dla nikogo (nieodwracalne do restartu) |
| `kernel.perf_event_paranoid` | 2 | perf bez danych jądra dla zwykłych procesów (wartość maks. w głównym jądrze) |
| `kernel.randomize_va_space` | 2 | pełne ASLR |
| `fs.protected_symlinks/hardlinks` | 1/1 | ochrona przed atakami przez dowiązania |
| `fs.protected_fifos/regular` | 2/2 | ochrona plików w katalogach współdzielonych |
| `fs.suid_dumpable` | 0 | brak zrzutów pamięci programów z podwyższonymi prawami |
| `vm.memfd_noexec` | 2 | programów z pamięci (memfd) nie da się uruchomić |
| `dev.tty.ldisc_autoload` | 0 | brak automatycznego ładowania dyscyplin linii |
| `dev.tty.legacy_tiocsti` | 0 | brak wstrzykiwania znaków do terminala |

## Zależności
musl (statycznie); jądro: devtmpfs, proc, sysfs, cgroup2, tmpfs, Yama, PID_NS (memfd_noexec).

## Wywołania systemowe
`getpid`, `umask`, `chdir`, `rt_sigprocmask`, `reboot` (`RB_DISABLE_CAD`, `RB_POWER_OFF`,
`RB_AUTOBOOT`), `mount`, `umount2`, `open`, `write`, `close`, `stat`, `fork`, `setsid`, `execve`,
`rt_sigtimedwait` (`sigwaitinfo`/`sigtimedwait`), `wait4` (`waitpid`), `kill`, `sync`, `nanosleep`.

## Obsługa błędów
Montowanie/sysctl/`fork` nieudane → `k1_awaria()` (ADR 0005). Zakończenie nadzorcy (M1) →
kontrolowane wyłączenie. SIGTERM → wyłączenie, SIGINT → restart, inne sygnały → wpis w logu.

## Powierzchnia ataku
Sygnały od roota (w M1 każdy proces to root — M3). Status zakończenia dzieci (liczby, bez parsowania).
Brak plików konfiguracyjnych i IPC.

## Testy (w systemie, `testy/m1/k1_test_m1.c`)
Układ korzenia, flagi montowań, ograniczony `/proc` (`subset=pid`, `hidepid=invisible`), sysctl,
zbieranie sierot, odporność na SIGKILL, zwykły użytkownik nie może zabić init — pełna lista w `docs/TESTY.md`.

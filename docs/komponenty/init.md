# Komponent: init (`/rdzen/init`, PID 1)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1) — `scripts/test-m1.sh`, scenariusze `start` i `testy-w-systemie` (sprawdzenia w systemie: `testy/system/t_system.c`) |
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
UID 0, wszystkie capabilities. Nadzorca też działa jako root, bez klatki (RYZYKA R23); usługi i aplikacje
uruchamia już w klatkach (od M3, ADR 0007).

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
| `net.ipv4.icmp_echo_ignore_all`, `…_broadcasts` | 1, 1 | (0.9.0, ADR 0039) brak odpowiedzi na ping |
| `net.ipv4.icmp_ratemask`, `icmp_msgs_per_sec`, `icmp_msgs_burst` | 524287, 0, 0 | jądro nie wysyła **żadnego** ICMP: każdy typ (0–18) przez globalny limit 0 na sekundę |
| `net.ipv4.icmp_ignore_bogus_error_responses` | 1 | błędne komunikaty ICMP od routerów pomijane bez logu |
| `net.ipv4.ip_forward` | 0 | K1 nie przekazuje pakietów |
| `net.ipv4.tcp_syncookies`, `tcp_rfc1337` | 1, 1 | zalew SYN nie wyczerpuje pamięci; ochrona połączeń w TIME-WAIT |
| `net.ipv4.conf.{all,default}.rp_filter` | 1 | ścisłe sprawdzanie nadawcy (pakiet tylko z karty, przez którą wraca odpowiedź) |
| `net.ipv4.conf.{all,default}.accept_redirects`, `secure_redirects`, `send_redirects` | 0 | bez przekierowań ICMP |
| `net.ipv4.conf.{all,default}.accept_source_route` | 0 | bez trasy wybranej przez nadawcę |
| `net.ipv4.conf.{all,default}.arp_ignore`, `arp_announce` | 1, 2 | ARP tylko o własny adres na tej karcie |

`default` dociera też do kart znalezionych przez jądro przed initem (`net/ipv4/devinet.c: devinet_copy_dflt_conf`) i do
kart podłączonych później (przejściówki USB). Bez `log_martians` — wpisy z adresami (niezmiennik 8).

## Zależności
musl (statycznie); jądro: devtmpfs, proc, sysfs, cgroup2, tmpfs, Yama, PID_NS (memfd_noexec), INET (sysctl sieci, 0.9.0).

## Wywołania systemowe
`getpid`, `umask`, `chdir`, `rt_sigprocmask`, `reboot` (`RB_DISABLE_CAD`, `RB_POWER_OFF`,
`RB_AUTOBOOT`), `mount`, `umount2`, `open`, `write`, `close`, `stat`, `fork`, `setsid`, `execve`,
`rt_sigtimedwait` (`sigwaitinfo`/`sigtimedwait`), `wait4` (`waitpid`), `kill`, `sync`, `nanosleep`.

## Obsługa błędów
Montowanie/sysctl/`fork` nieudane → `k1_awaria()` (ADR 0005). Zakończenie nadzorcy (M1) →
kontrolowane wyłączenie. SIGTERM → wyłączenie, SIGINT → restart, inne sygnały → wpis w logu.

## Wyłączanie (kolejność od 0.8.6)
Od 0.10.7 krok 6 init nie czeka w nieskończoność na procesy po `SIGKILL` (do tej pory `waitpid` bez limitu): najwyżej
okres łaski po ostatnim, który się skończył, potem „nie wszystkie procesy skończyły się w czasie — mimo to dalej”, `sync`
i wyłączenie (fail closed: proces zawieszony w jądrze nie zostawia włączonego komputera).

1. `SIGTERM` tylko do nadzorcy; init czeka na jego koniec (najwyżej 3 s). Nadzorca zapisuje w tym czasie to, co
   trzyma (historię baterii — skarbiec jeszcze działa), i zatrzymuje swoje usługi.
2. `SIGTERM` do wszystkich pozostałych procesów; init zbiera zakończone i czeka, aż nie zostanie żaden — najwyżej
   3 s od ostatniego zakończenia.
3. `SIGKILL` do reszty, `sync`, `reboot`.

Do 0.8.5 wszystkie procesy dostawały `SIGTERM` naraz (skarbiec znikał przed ostatnim zapisem nadzorcy), a po
zakończeniu ostatniego procesu init zawsze czekał jeszcze pełne 3 s — każde wyłączenie i restart trwały o 3 s dłużej
(zmierzone w QEMU: 3,1 s → 0,1 s).

## Powierzchnia ataku
Sygnały od roota (init, nadzorca; usługi w klatkach nie widzą PID 1 i nie mogą wysłać mu sygnału). Status zakończenia dzieci (liczby, bez parsowania).
Brak plików konfiguracyjnych i IPC.

## Testy (w systemie, `testy/system/t_system.c`)
Układ korzenia, flagi montowań, ograniczony `/proc` (`subset=pid`, `hidepid=invisible`), sysctl,
zbieranie sierot, odporność na SIGKILL, zwykły użytkownik nie może zabić init — pełna lista w `docs/TESTY.md`.

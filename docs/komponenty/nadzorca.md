# Komponent: nadzorca (`/rdzen/system/nadzorca`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M3) — testy w systemie (`scripts/test-m1.sh`, scenariusz `testy-w-systemie`) |
| **Kod** | `src/nadzorca/main.c`, klatka `src/wspolne/k1_klatka.c`, kanały `src/wspolne/k1_kanal.c`, protokół `src/wspolne/k1_komunikat.c` |
| **Decyzje** | ADR 0006 (wersja M2), ADR 0007 (M3) |

## Cel
Uruchamia usługi świata natywnego, każdą w osobnej klatce, łączy je kanałami, przekazuje ich logi na
konsolę i uruchamia ponownie po awarii. W obrazie testowym uruchamia program testowy i kończy się jego
kodem wyjścia (init wyłącza wtedy maszynę).

## Granica zaufania
Uruchamiany przez init jako root, **bez klatki** (musi zmieniać UID, tworzyć cgroups, otwierać
urządzenia) — część bazy zaufania. Od usług przyjmuje tylko: wiersze logu (oczyszczane, z limitem),
statusy zakończenia, a od M4 — komunikaty 32 B od `gui` (`URUCHOM`, `WYLACZ`; nadawca sprawdzany
`SO_PASSCRED`). Lista usług i profili jest stała, w kodzie.

## Uprawnienia
root, wszystkie capabilities (RYZYKA R23). Usługi — patrz tabela.

| Usługa | UID | Przekazane deskryptory | seccomp (poza bazą) | Landlock | cgroup |
|---|---|---|---|---|---|
| `ekran` | 101 | 3: kanał sterujący, 4: karta graficzna (DRM master) | `ioctl` typu `'d'` | tylko własny program | 128 MiB, pids 1 |
| `wejscie` | 100 | 3: kanał do ekranu, 4…: urządzenia `event*` | `ioctl` typu `'E'` | tylko własny program | 8 MiB, pids 1 |

Wspólne dla usług: fd 0 i 1 = `/dev/null`, fd 2 = pipe logu do nadzorcy; brak capabilities (także
zbiór graniczny), `NO_NEW_PRIVS`, securebits zablokowane, `RLIMIT_CORE=0`, niezrzucalne
(`PR_SET_DUMPABLE=0`), `PR_SET_PDEATHSIG=SIGKILL`, drugi filtr seccomp bez `execve`.
Baza seccomp: `read write close exit exit_group rt_sig* brk munmap mremap madvise clock_gettime
clock_nanosleep nanosleep poll ppoll fcntl fstat newfstatat lseek getpid gettid sendmsg recvmsg sendto
recvfrom seccomp set_tid_address restart_syscall execve` + `mmap`/`mprotect` bez `PROT_EXEC`,
`arch_prctl(ARCH_SET_FS)`, `prctl(PR_SET_DUMPABLE)`.

## Wejścia i wyjścia
Wejście: statusy procesów (`signalfd`), logi usług, kanał testowy (obraz testowy). Wyjście: procesy
usług, kanały (`socketpair`, strona odbiorcy z `SO_PASSCRED`), komunikaty `KLIENT` do ekranu, log.

## Zachowanie
- Start: ekran (gdy brak karty — ponowna próba za 4 s), potem jego klienci (wejscie).
- Zakończenie usługi → wpis w logu (kod 159 = zabronione wywołanie systemowe) → restart po
  0,2 / 1 / 2 / 4 / 4 s; 6. zakończenie w ciągu 60 s → „wyłączony”.
- Zakończenie ekranu → klienci zatrzymani (bez liczenia jako awarię) i uruchomieni ponownie.
- `SIGTERM`/`SIGINT` (od init przy wyłączaniu) → zatrzymanie usług i koniec.

## Wywołania systemowe
`signalfd`, `poll`, `pipe2`, `socketpair`, `setsockopt`, `sendmsg`/`recvmsg`, `fork`, `dup2`, `fcntl`,
`open` (karta, `event*`, `/dev/null`, pliki cgroup), `mkdir` (cgroup), `kill`, `wait4`, `execve`;
w dziecku: `prctl`, `setgroups`, `setres[ug]id`, `capget`, `setrlimit`, `landlock_*`, `seccomp`.

## Obsługa błędów
Brak `/dev/null`, `signalfd` albo kontrolerów cgroup → `k1_koniec()` (init wyłącza system). Nieudane
nałożenie klatki w dziecku → wpis w logu i kod 126 (usługa **nie** startuje bez klatki — fail closed),
potem zwykłe reguły restartu.

## Powierzchnia ataku
Wiersze logu od usług (bufor 400 B, znaki sterujące zamieniane, limit 20/s), komunikaty 32 B
(`k1_komunikat_sprawdz`, testy + fuzzing), statusy procesów.

## Testy
| Test | Rodzaj |
|---|---|
| ekran i wejscie: własny UID/GID, brak grup i capabilities, `NoNewPrivs`, 2 filtry seccomp, własna cgroup z limitami, tylko przekazane deskryptory (`/proc/<pid>/status`, `fd`, `cgroup`) | funkcjonalny |
| próby w klatkach ekran i wejscie: 22 zabronione działania (inny program, `open`, `setuid`, `socket`, `fork`, `clone`, `kill`, `ptrace`, `mount`, `unshare`, `chroot`, `perf_event_open`, `keyctl`, `memfd_create`, pamięć wykonywalna, obcy `ioctl`, `execve`) | negatywny |
| awaria ekranu → nowy ekran i nowe wejscie | funkcjonalny |
| 6 zabić wejscie → usługa wyłączona | negatywny |

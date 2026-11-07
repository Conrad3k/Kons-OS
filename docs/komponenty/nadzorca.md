# Komponent: nadzorca (`/rdzen/system/nadzorca`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M3, M4) — testy w systemie (`scripts/test-m1.sh`, scenariusz `testy-w-systemie`), `scripts/test-m4.sh` (uruchamianie aplikacji, wyłączanie) |
| **Kod** | `src/nadzorca/main.c`, klatka `src/wspolne/k1_klatka.c`, kanały `src/wspolne/k1_kanal.c`, protokół `src/wspolne/k1_komunikat.c` |
| **Decyzje** | ADR 0006 (wersja M2), ADR 0007 (M3), ADR 0008 (pulpit i aplikacje) |

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
| `gui` | 102 | 3: kanał do ekranu, 4: kanał do nadzorcy | `memfd_create`, `ftruncate` | tylko własny program | 48 MiB, pids 1 |
| `zegar` (na żądanie) | 200 | 3: kanał do ekranu | `memfd_create`, `ftruncate` | tylko własny program | 32 MiB, pids 1 |
| `notatki` (na żądanie) | 201 | 3: kanał do ekranu | + `open`, `openat`, `fsync`, `rename` | + `/dane_uzytkownika/notatki` | 32 MiB, pids 1 |
| `ustawienia` (na żądanie) | 202 | 3: kanał do ekranu | `memfd_create`, `ftruncate` | tylko własny program | 32 MiB, pids 1 |
| `zasoby` (na żądanie, M4.1) | 203 | 3: kanał do ekranu, 4: dane od nadzorcy (tylko do odczytu) | `memfd_create`, `ftruncate` | tylko własny program | 32 MiB, pids 1 |

`RLIMIT_FSIZE`: 0 dla ekranu i wejścia, 16 MiB dla gui i aplikacji (ogranicza też memfd powierzchni).

Wspólne dla usług: fd 0 i 1 = `/dev/null`, fd 2 = pipe logu do nadzorcy; brak capabilities (także
zbiór graniczny), `NO_NEW_PRIVS`, securebits zablokowane, `RLIMIT_CORE=0`, niezrzucalne
(`PR_SET_DUMPABLE=0`), `PR_SET_PDEATHSIG=SIGKILL`, drugi filtr seccomp bez `execve`.
Baza seccomp: `read write close exit exit_group rt_sig* brk munmap mremap madvise clock_gettime
clock_nanosleep nanosleep poll ppoll fcntl fstat newfstatat lseek getpid gettid sendmsg recvmsg sendto
recvfrom seccomp set_tid_address restart_syscall execve` + `mmap`/`mprotect` bez `PROT_EXEC`,
`arch_prctl(ARCH_SET_FS)`, `prctl(PR_SET_DUMPABLE)`.

## Wejścia i wyjścia
Wejście: statusy procesów (`signalfd`), logi usług, prośby od gui, kanał testowy (obraz testowy). Wyjście: procesy
usług, kanały (`socketpair`, strona odbiorcy z `SO_PASSCRED`), komunikaty `KLIENT` do ekranu, log.

## Zachowanie
- Start: katalog notatek (`/dane_uzytkownika/notatki`, właściciel 201), ekran (karta `card0`…`card7`;
  gdy brak — ponowna próba za 4 s), potem jego klienci (wejscie, gui).
- Prośby od `gui` (fd 4, nadawca UID 102): `URUCHOM` — aplikacja, jeśli jeszcze nie działa
  (aplikacje nie są restartowane); `WYLACZ` — `SIGTERM` (wyłączenie) albo `SIGINT` (restart) do init.
- Urządzenia wejścia (0.5.1): co 2 s sprawdza, czy pojawiło się nowe `event*` (szybki start może
  wyprzedzić rejestrację myszy przez jądro; później — urządzenia USB); jeśli tak, uruchamia `wejscie`
  ponownie z pełnym zestawem (bez liczenia jako awaria). Od 0.5.2 (M5a) zapomina urządzenia, których
  węzeł zniknął (odłączone USB) — ponownie podłączone pod tym samym numerem jest zauważane. Pełny
  hotplug bez restartu (zdarzenia jądra) — M5b.
- Skarbiec (0.5.6, ADR 0015/0016): uruchamiany **przed** ekranem; partycja danych = dokładnie jedna
  partycja GPT o nazwie „K1-DANE” (sysfs `PARTNAME`), otwierana `O_RDWR|O_EXCL` i przekazywana jako
  fd 4; czeka na nią do 4 s po starcie (do 15 s, gdy usb-storage ma urządzenie). Kanały klientów
  (`kanal_do_skarbca`: ekran fd 5, Notatki fd 4) zgłasza skarbcowi przez `SKARB_KLIENT` z właścicielem;
  oba końce mają `SO_PASSCRED`. Koniec skarbca → restart ekranu (klienci dostają nowe kanały).
- Bateria (0.5.3, ADR 0014): co 15 s `src/nadzorca/bateria.c` czyta `sys/class/power_supply` (baterie
  systemowe: procent z energii/ładunku/`capacity`, stan, zasilacz) i przy zmianie wysyła `BATERIA` do
  gui kanałem fd 5 (jednokierunkowo, `SHUT_RD`); test na hoście `test_bateria` (także śmieciowe dane).
  Od 0.5.5: historia od uruchomienia — próbka co 30 s (procent + bit zasilacza), do 1440 próbek; po
  zapełnieniu łączy pary (odstęp ×2), więc zawsze obejmuje cały czas od startu. Ustawienia dostają stan
  i historię kanałem fd 4 (jednokierunkowo); przy pełnym buforze gniazda reszta idzie w następnej pętli.
- Zasoby (M4.1, ADR 0012): gdy aplikacja działa, co sekundę `ZASOBY_SYSTEM` (procesor ‰ z
  `cpu.stat` korzenia cgroup, pamięć: `sysinfo` minus odzyskiwalna pamięć podręczna z `memory.stat`,
  liczba CPU, czas pracy) i `ZASOBY_USLUGA` dla każdej usługi (`cpu.stat`, `memory.current` jej
  cgroup). Kanał jednokierunkowy: po stronie nadzorcy `shutdown(SHUT_RD)` — nic od aplikacji nie czyta.
- Logi usług: wiersz oczyszczany przez `k1_log_oczysc` (znaki sterujące C0/DEL/C1, błędne UTF-8 → `?`),
  sprawdzany prefiks usługi, limit 20 wierszy/s.
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

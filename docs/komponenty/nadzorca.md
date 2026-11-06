# Komponent: nadzorca (`/rdzen/system/nadzorca`) — wersja minimalna M2

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (M2, wersja minimalna), PRZETESTOWANE w zakresie M2; pełny nadzorca — PLANOWANE (M3) |
| **Kod** | `src/nadzorca/main.c` |
| **Decyzje** | ADR 0006 |

## Cel
Uruchamia usługi świata natywnego. **M2:** `wejscie` i `ekran`, połączone kanałem (pipe
w trybie pakietowym, fd 3 w obu usługach: zapis w `wejscie`, odczyt w `ekran`). Zapisuje w logu
zakończenie usługi (kod / sygnał). W obrazie testowym uruchamia `/rdzen/system/test-systemu`
i kończy się jego kodem wyjścia.

## Granica zaufania
Uruchamiany przez init jako root. Nie przyjmuje danych z zewnątrz (stała lista usług w kodzie).

## Uprawnienia
**M2:** root, usługi dziedziczą roota — to główna luka M2 (RYZYKA R3). M3: osobne UID/GID,
capabilities, `PR_SET_NO_NEW_PRIVS`, seccomp, Landlock, cgroups dla każdej usługi.

## Wejścia i wyjścia
Wejście: statusy zakończenia procesów potomnych. Wyjście: procesy usług, kanał między nimi, log.

## Wywołania systemowe
`pipe2` (O_CLOEXEC | O_DIRECT), `fork`, `dup2`, `fcntl`, `rt_sigprocmask`, `execve`, `close`, `stat`,
`wait4`, `write` (log).

## Obsługa błędów
Nieudany `pipe2`/`fork` → `k1_koniec()` (init wyłącza wtedy system — polityka M1). Zakończenie usługi
→ wpis w logu, **bez restartu** (M3).

## Testy
W systemie: program testowy jest dzieckiem nadzorcy, a nadzorca dzieckiem init; awaria `ekran`
→ nadzorca zapisuje „ekran zakończony sygnałem 11” i działa dalej.

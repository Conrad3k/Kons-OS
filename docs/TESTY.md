# Testy K1

Zasada (SPEC §2.4, §13): status **PRZETESTOWANE** tylko z dowodem — log z portu szeregowego,
zrzut ekranu albo wynik testu. Każdy mechanizm bezpieczeństwa ma test negatywny (co ma się NIE udać).

## Jak uruchomić

```bash
scripts/przygotuj-srodowisko.sh   # raz w nowym kontenerze (pakiety Ubuntu)
scripts/zbuduj.sh                 # jądro + programy + obrazy → build/wynik/
scripts/testy.sh                  # wszystkie testy (~2,5 min w emulacji TCG): test-m1.sh + test-m2.sh
```

Logi każdego scenariusza: `build/testy/<nazwa>.log`. Limit czasu na uruchomienie: `K1_TEST_CZAS`
(domyślnie 240 s), czas fuzzingu: `K1_FUZZ_CZAS` (domyślnie 20 s).

Ręcznie: `scripts/uruchom-qemu.sh [--obraz build/wynik/rdzen-test.squashfs]` i w drugim oknie
`tail -f build/qemu/serial.log`; zrzut ekranu: `scripts/zrzut-ekranu.sh plik.png`.

## M0 — kryteria zaliczenia

| Kryterium | Dowód |
|---|---|
| Archiwum wersji debianowej | gałąź `archiwum/debian-0.1.2` na GitHubie |
| Nowa struktura repo, dokumenty §2.9 | `docs/SPEC.md`, `docs/adr/`, `docs/ZALEZNOSCI.md`, `docs/RYZYKA.md`, `CHANGELOG.md` |
| Toolchain musl, statyczne linkowanie | `scripts/sprawdz-binaria.sh`: static-pie, bez interpretera i bibliotek |
| Skrypty: build, QEMU z logiem, zrzut ekranu | `scripts/zbuduj.sh`, `scripts/uruchom-qemu.sh`, `scripts/zrzut-ekranu.sh` |
| CI na main | `.github/workflows/k1.yml` — build + `scripts/testy.sh` |

## M1 — kryteria zaliczenia
1. W QEMU pojawia się `K1: init OK`, bez `Kernel panic`.
2. Wszystkie sprawdzenia w systemie (`K1-TEST`) zaliczone.
3. Każdy błąd startu kończy się kontrolowanym komunikatem i wyłączeniem maszyny, bez kernel panic.
4. Konfiguracja jądra zgodna z `jadro/k1.config`; binaria z wymaganymi zabezpieczeniami.
5. Build powtarzalny: drugi build w innym katalogu daje identyczne pliki (`scripts/sprawdz-powtarzalnosc.sh`).

## M1 — scenariusze (`scripts/test-m1.sh`)

| Scenariusz | Oczekiwane | Rodzaj |
|---|---|---|
| jednostkowe | parser linii poleceń (32), protokół zdarzeń (32), renderer (3 rozmiary) — ASan/UBSan | funkcjonalny + negatywny |
| fuzzing | linia poleceń i protokół zdarzeń, libFuzzer po 20 s | fuzzing |
| zabezpieczenia binariów | static-pie, RELRO, BIND_NOW, NX, brak RWX | konfiguracja |
| konfiguracja jądra | każda linia `jadro/k1.config` w `.config` | konfiguracja |
| qemu: start | `K1: init OK`, nadzorca uruchomiony, pierwsza klatka ekranu | funkcjonalny |
| qemu: testy-w-systemie | `K1-TEST: WYNIK: OK`, `odrzucone 5)`, obcy nadawca odrzucony, próby klatek bez błędów, restart ekranu, wyłączenie padającej usługi, brak „zabronione wywołanie systemowe”, kontrolowane wyłączenie | funkcjonalny + negatywne |
| qemu: neg-brak-dysku | `nie znaleziono urządzenia…`, wyłączenie | negatywny |
| qemu: neg-dysk-bez-obrazu | `nie udało się zamontować obrazu rdzenia…` | negatywny |
| qemu: neg-obraz-bez-init | `obraz rdzenia nie zawiera programu init` | negatywny |
| qemu: neg-brak-parametru | `brak parametru k1.rdzen` | negatywny |
| qemu: neg-zly-parametr | `k1.rdzen=/dev/../vda` odrzucony | negatywny |
| qemu: neg-powtorzony-parametr | dwa `k1.rdzen=` odrzucone | negatywny |

We wszystkich scenariuszach QEMU: brak `Kernel panic`; w negatywnych maszyna musi sama się wyłączyć
przed upływem limitu.

### Sprawdzenia w systemie (`testy/system/k1_test_systemu.c`, obraz `rdzen-test.squashfs`)
Program testowy jest w obrazie testowym jako `/rdzen/system/test-systemu` (uruchamia go nadzorca).
NEGATYWNE = coś, co ma się nie udać.

- uruchomiony przez nadzorcę, a nadzorca przez init (PID 1)
- korzeń zawiera tylko `aplikacje dane_uzytkownika rdzen`; korzeń i `/rdzen` tylko do odczytu (EROFS)
- `/rdzen`: SquashFS, `ro,nosuid,nodev`; `/dane_uzytkownika`: zapisywalne, `noexec,nosuid,nodev`
- NEGATYWNE: program skopiowany do `/dane_uzytkownika` nie uruchamia się (EACCES)
- NEGATYWNE: `memfd_create(MFD_EXEC)` odrzucone; program w memfd nie uruchamia się (EACCES)
- NEGATYWNE: skrypt `#!` w obrazie nie uruchamia się (ENOEXEC, brak `BINFMT_SCRIPT`)
- `/rdzen/jadro/proc`: procfs bez `sys`, `kallsyms`, `cmdline`; `self` jest
- NEGATYWNE: zwykły użytkownik (UID 65534) nie widzi `/proc/1` i nie może wysłać sygnału do init
- sysfs tylko do odczytu; cgroup2 zamontowane; devtmpfs `noexec,nosuid`
- NEGATYWNE: brak `ptmx`, `pts`, `tty0`, `tty1`, `mem`, `kmem`, `port`
- 13 sysctl ma wartości ustawione przez init; brak sysctl modułów, kexec, BPF, io_uring, SysRq,
  userfaultfd; brak `/proc/modules`, `/proc/kcore`
- LSM: landlock, lockdown, yama; lockdown `[confidentiality]`; Landlock ABI ≥ 1 (jest 7)
- NEGATYWNE: `init_module`, `finit_module`, `kexec_load`, `kexec_file_load`, `io_uring_setup`, `bpf`,
  `userfaultfd`, `modify_ldt`, `iopl`, `ioperm` → ENOSYS; gniazda AF_INET/AF_UNIX → EAFNOSUPPORT
- init zbiera osierocone procesy (wnuk po zakończeniu nie zostaje zombie)
- NEGATYWNE: SIGKILL wysłany do init nie zabija go
- w `/rdzen` są tylko programy K1 (`init`, `nadzorca`, `wejscie`, `ekran`) i pliki testowe;
  NEGATYWNE: brak plików setuid/setgid

- M2: uruchomiony przez nadzorcę (a nadzorca przez init); karta `/rdzen/jadro/dev/dri/card0` i co
  najmniej 2 urządzenia `event*`; NEGATYWNE: brak `/dev/fb0` i `/dev/input/mice`; procesy `ekran`
  i `wejscie` działają, `ekran` trzyma kartę; NEGATYWNE: drugi proces nie przejmie ekranu
  (`SET_MASTER` → EBUSY); 4 złośliwe komunikaty wstrzyknięte do kanału ekranu są odrzucone
  (harness sprawdza w logu `odrzucone 4`), poprawny przyjęty; awaria `ekran` (SIGSEGV) nie zatrzymuje
  nadzorcy ani init.

- M3 (ADR 0007): dla `ekran` i `wejscie` — własny UID/GID, brak grup dodatkowych, wszystkie zbiory
  capabilities = 0 (także graniczny), `NoNewPrivs: 1`, `Seccomp: 2` z 2 filtrami, własna cgroup
  z `pids.max=1` i `memory.max`, tylko przekazane deskryptory (`/dev/null`, pipe logu, gniazda,
  karta / `event*`); brak sieci IP, gniazda UNIX są (kanały).
- M3 NEGATYWNE, złośliwe IPC przez kanał testowy od nadzorcy: 4 złe komunikaty odrzucone, poprawny
  przyjęty, poprawny od obcego UID odrzucony (harness: `odrzucone 5)` i wpis o obcym nadawcy).
- M3 NEGATYWNE, **próby w klatkach**: nadzorca uruchamia program testowy w klatce `wejscie` i `ekran`
  (`--proba`), a ten próbuje: uruchomić inny program (Landlock → EACCES), `open`, `openat`, `setuid`,
  `setresgid`, `socket`, `fork`, `clone`, `kill(1)`, `ptrace`, `mount`, `unshare`, `chroot`,
  `perf_event_open`, `keyctl`, `memfd_create`, `mmap`/`mprotect` z `PROT_EXEC`, obcy typ `ioctl`,
  `execve` po starcie usługi — każde musi zostać zablokowane (log `K1-PROBA: … 0 błędów`).
- M3: awaria `ekran` (SIGSEGV) → nadzorca i init działają, po chwili jest nowy `ekran` z kartą i nowy
  `wejscie`; 6 zabić `wejscie` → usługa wyłączona (limit restartów).

Program testowy sam działa jako root bez klatki (potrzebuje wglądu w `/proc` i jądro); izolację
sprawdza z zewnątrz (`/proc/<pid>`) i od środka (próby).

## M3 — kryteria zaliczenia
1. Każda usługa ma własny UID/GID, zero capabilities, `NO_NEW_PRIVS`, seccomp (biała lista),
   Landlock i własną cgroup z limitami — potwierdzone w systemie.
2. Próby w klatkach: wszystkie zabronione działania zablokowane.
3. Kanały między usługami sprawdzają nadawcę (`SO_PASSCRED`) — komunikat od obcego UID odrzucony.
4. Ekran wraca po awarii (z klientami); usługa padająca w kółko zostaje wyłączona.
5. Wszystkie testy M1 i M2 nadal przechodzą.

## M2 — kryteria zaliczenia
1. `ekran` ustawia tryb i pokazuje pierwszą klatkę: tapetę z logo K1 (dowód: zrzut ekranu).
2. Mysz przesuwa kursor, klawiatura (strzałki) też, przycisk myszy zmienia wygląd kursora —
   potwierdzone w logu i na zrzutach (sprawdzanych automatycznie po kolorach pikseli).
3. Brak śladów kursora po ruchu (poprawne odtwarzanie tła), brak „Kernel panic”.
4. Testy negatywne: przejęcie ekranu, złośliwe komunikaty IPC, awaria procesu ekran.
5. Wszystkie testy M1 nadal przechodzą.

## M2 — `scripts/test-m2.sh` (obraz produkcyjny, sterowanie przez monitor QEMU)

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| start | w logu tryb i „pierwsza klatka”; zrzut: rozmiar = tryb, terakota w tarczy, ciemne rogi, kursor tam, gdzie mówi log | `build/testy/m2-1-start.png` |
| mysz | 4 × `mouse_move -60 -30` → kursor (−240, −120); na zrzucie kursor w nowym miejscu, w starym znów tapeta | `m2-2-mysz.png` |
| klawiatura i przycisk | 3 × `sendkey right` → +60 px; `mouse_button 1` → kursor w kolorze akcentu | `m2-3-klawiatura-przycisk.png` |
| koniec | brak „Kernel panic”, błędów usług, odrzuconych i zgubionych zdarzeń | `m2-ekran.log` |

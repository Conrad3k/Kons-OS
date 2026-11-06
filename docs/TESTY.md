# Testy K1

Zasada (SPEC §2.4, §13): status **PRZETESTOWANE** tylko z dowodem — log z portu szeregowego,
zrzut ekranu albo wynik testu. Każdy mechanizm bezpieczeństwa ma test negatywny (co ma się NIE udać).

## Jak uruchomić

```bash
scripts/przygotuj-srodowisko.sh   # raz w nowym kontenerze (pakiety Ubuntu)
scripts/zbuduj.sh                 # jądro + programy + obrazy → build/wynik/
scripts/test-m1.sh                # wszystkie testy M1 (~1,5 min w emulacji TCG)
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
| CI na main | `.github/workflows/k1.yml` — build + `scripts/test-m1.sh` |

## M1 — kryteria zaliczenia
1. W QEMU pojawia się `K1: init OK`, bez `Kernel panic`.
2. Wszystkie sprawdzenia w systemie (`K1-TEST`) zaliczone.
3. Każdy błąd startu kończy się kontrolowanym komunikatem i wyłączeniem maszyny, bez kernel panic.
4. Konfiguracja jądra zgodna z `jadro/k1.config`; binaria z wymaganymi zabezpieczeniami.
5. Build powtarzalny: drugi build w innym katalogu daje identyczne pliki (`scripts/sprawdz-powtarzalnosc.sh`).

## M1 — scenariusze (`scripts/test-m1.sh`)

| Scenariusz | Oczekiwane | Rodzaj |
|---|---|---|
| jednostkowe: parser linii poleceń | 32 przypadki, ASan/UBSan bez błędów | funkcjonalny + negatywny |
| fuzzing: parser linii poleceń | libFuzzer 20 s bez błędu pamięci/logiki | fuzzing |
| zabezpieczenia binariów | static-pie, RELRO, BIND_NOW, NX, brak RWX | konfiguracja |
| konfiguracja jądra | każda linia `jadro/k1.config` w `.config` | konfiguracja |
| qemu: start | `K1: init OK`, `brak nadzorcy` | funkcjonalny |
| qemu: testy-w-systemie | `K1-TEST: WYNIK: OK`, kontrolowane wyłączenie | funkcjonalny + negatywne |
| qemu: neg-brak-dysku | `nie znaleziono urządzenia…`, wyłączenie | negatywny |
| qemu: neg-dysk-bez-obrazu | `nie udało się zamontować obrazu rdzenia…` | negatywny |
| qemu: neg-obraz-bez-init | `obraz rdzenia nie zawiera programu init` | negatywny |
| qemu: neg-brak-parametru | `brak parametru k1.rdzen` | negatywny |
| qemu: neg-zly-parametr | `k1.rdzen=/dev/../vda` odrzucony | negatywny |
| qemu: neg-powtorzony-parametr | dwa `k1.rdzen=` odrzucone | negatywny |

We wszystkich scenariuszach QEMU: brak `Kernel panic`; w negatywnych maszyna musi sama się wyłączyć
przed upływem limitu.

### Sprawdzenia w systemie (`testy/m1/k1_test_m1.c`, obraz `rdzen-test.squashfs`)
Program testowy jest w obrazie testowym jako `/rdzen/system/nadzorca` (init go uruchamia).
NEGATYWNE = coś, co ma się nie udać.

- uruchomiony przez init (PID 1)
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
- w `/rdzen` są tylko `init` i pliki testowe; NEGATYWNE: brak plików setuid/setgid

Ograniczenie: w M1 test działa jako root ze wszystkimi uprawnieniami — sprawdza stan systemu
i jądra, nie izolację usług (M3).

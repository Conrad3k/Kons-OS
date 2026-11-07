# Testy K1

Zasada (SPEC §2.4, §13): status **PRZETESTOWANE** tylko z dowodem — log z portu szeregowego,
zrzut ekranu albo wynik testu. Każdy mechanizm bezpieczeństwa ma test negatywny (co ma się NIE udać).

## Jak uruchomić

```bash
scripts/przygotuj-srodowisko.sh   # raz w nowym kontenerze (pakiety Ubuntu)
scripts/zbuduj.sh                 # jądro + programy + obrazy + ISO → build/wynik/
scripts/testy.sh                  # wszystkie testy (~7 min w emulacji TCG): test-m1.sh, test-m2.sh, test-m4.sh, test-m5.sh
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
| jednostkowe | parser linii poleceń (32), stan i historia baterii (26, także śmieciowe dane i zapełnienie historii), protokół zdarzeń (32), protokół usług i kanały na prawdziwych gniazdach (85), przybornik GUI (55: czas polski ze zmianami czasu, układ PL, UTF-8, rysowanie na krawędziach, tapety), renderer — ASan/UBSan | funkcjonalny + negatywny |
| fuzzing | linia poleceń, protokół zdarzeń, protokół usług (32 B, wszystkie kierunki), libFuzzer po 20 s | fuzzing |
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

### Sprawdzenia w systemie (`testy/system/*.c`, obraz `rdzen-test.squashfs`)
Pliki (od M4.1): `k1_test_systemu.c` (kolejność), `wspolne.c` (pomocnicze, kanał testowy),
`t_system.c` (M1), `t_uslugi.c` (M2–M3), `t_okna.c` (M4–M4.1), `t_sprzet.c` (M5a), `proba.c` (tryb `--proba`).
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

- M4.1 NEGATYWNE (ADR 0012): okno klienta połączonego > 10 s temu nie dostaje fokusu, gdy fokus ma
  okno innej aplikacji (harness: `bez fokusu (ochrona klawiatury)`); trzecie pełnoekranowe okno
  jednego klienta odrzucone (`przekroczony limit pamięci powierzchni`); próba w klatce `zasoby`;
  aplikacja Zasoby dostaje dane od nadzorcy (`K1-ZASOBY: dane od nadzorcy`).

- M5a (ADR 0013, `t_sprzet.c`): scenariusz `testy-w-systemie` startuje z dodatkowym sprzętem — dysk SATA
  (AHCI q35) z partycją GPT „K1-DANE”, dysk NVMe ze starą tablicą MBR, pendrive na EHCI, klawiatura USB
  na xHCI, mysz USB na OHCI. Sprawdzenia: dyski widoczne (rozmiary), partycja „K1-DANE” po nazwie
  z sysfs, kontrolery xHCI/EHCI/OHCI, 2 urządzenia wejścia USB, `wejscie` ma otwarte 4 urządzenia
  (PS/2 + USB). NEGATYWNE: MBR nieczytany (NVMe bez partycji), węzły dysków, surowego USB i NVMe tylko
  root 0600, brak `hidraw`/`hiddev`/`sg`/`bsg`/`sr0`, żaden proces nie trzyma otwartego dysku; w próbie
  klatki Notatek otwarcie dysku `/rdzen/jadro/dev/vda` → `EACCES`.
- 0.5.3 (ADR 0014, `t_bateria`): sztuczna bateria z tablicy ACPI (`testy/acpi/bateria.asl`, `iasl`) —
  `BAT0` 73 %, rozładowywanie, `AC` odłączony; harness czeka na log powłoki
  `K1-GUI: bateria 73%, rozładowywanie, zasilacz odłączony`. Scenariusz `start` (bez baterii):
  `K1-GUI: brak baterii`.

## M5c/M5d — kryteria zaliczenia (0.5.6)
1. Format magazynu na hoście: `test_magazyn` (62 przypadki) i `fuzz_magazyn` bez błędów.
2. Ustawienia (tapeta, motyw, rozdzielczość) i notatka wracają po wyłączeniu i ponownym starcie
   z tym samym dyskiem danych (`test-m4.sh`, scenariusz `ponownie`); na dysku brak jawnego tekstu.
3. Obraz USB startuje przez UEFI z pamięci USB; pierwszy start tworzy magazyn na pustej „K1-DANE”,
   drugi go otwiera (`test-m5.sh`).
4. Dysk trzyma tylko skarbiec (w systemie), próba klatki skarbca bez błędów, Notatki bez plików.

## M5a — kryteria zaliczenia
1. Jądro widzi dyski SATA (AHCI), NVMe i pamięci USB; tablice GPT (tylko), nazwy partycji w sysfs.
2. Kontrolery USB xHCI, EHCI, OHCI; klawiatura i mysz USB działają w pulpicie — także na maszynie bez
   PS/2 (`scripts/test-m5.sh`).
3. Mysz podłączona w trakcie pracy jest zauważana; odłączenie nie psuje wejścia; ponowne podłączenie
   (ten sam numer urządzenia) znów działa.
4. Klatki bez zmian (wszystkie próby), dostęp do dysków i surowego USB tylko dla roota, nikt nie trzyma dysku.
5. Wszystkie testy M1–M4.1 nadal przechodzą.

## M5a — `scripts/test-m5.sh` (obraz produkcyjny, maszyna bez PS/2: `-machine q35,i8042=off`)
| Krok | Sprawdzenie |
|---|---|
| start | pierwsza klatka; w logu jądra `i8042: PNP: No PS/2 controller found`; `wejscie` z 2 urządzeniami (klawiatura i mysz USB na xHCI) |
| mysz USB | `mouse_move` × 4 → kursor w miejscu z logu ekranu |
| klawiatura USB | 3 × `sendkey right` → kursor 60 px w prawo (strzałki, gdy nic nie ma fokusu) |
| podłączenie | `device_add usb-mouse` → nadzorca: „nowe urządzenie wejścia”, `wejscie` z 3 urządzeniami |
| odłączenie | `device_del` → nadzorca: „urządzenie wejścia odłączone”, `wejscie`: „urządzenie odłączone” |
| ponowne podłączenie | znów 3 urządzenia (ten sam numer `eventN` rozpoznany jako nowy) |
| zrzut `m5-usb.png` | kursor w miejscu z logu; brak błędów usług, odrzuconych i zgubionych zdarzeń |
| bateria (0.5.3) | tablica ACPI: 73 %, ładowanie, zasilacz podłączony → log powłoki i jasne piksele wskaźnika na górnym pasku obok dźwięku |
| Ustawienia → Bateria (0.5.5) | otwarcie Ustawień z paska i sekcji Bateria (mysz USB); log: 73 %, ≥ 2 próbki co 30 s, położenie wykresu; zrzut `m5-bateria.png`: linia akcentu na całą szerokość |

## M4.1 — kryteria zaliczenia
1. Ustawienia → Ekran pokazuje listę rozdzielczości od karty, wybór przełącza tryb w locie,
   pulpit układa się od nowa (QEMU `bochs`: zrzut w nowym rozmiarze).
2. Zasoby: dane od nadzorcy (procesor, pamięć, usługi), wykres pamięci widoczny na zrzucie.
3. Poprawki z przeglądu mają testy: ochrona fokusu i limit pamięci okien (w systemie), oczyszczanie
   logu (`test_log`, host), nowe komunikaty (`test_komunikat`, `fuzz_komunikat`).
4. Cień paneli bez „podstawy” pod dolnym paskiem i menu dźwięku (zrzuty).
5. Wszystkie testy M1–M4 nadal przechodzą.

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

- M4 (ADR 0008): klatki `gui` i trzech aplikacji (uruchomionych przez kanał testowy jak przez gui);
  próby w ich klatkach (dla gui i aplikacji: zwykły memfd tak, `MFD_EXEC` nie; Notatki: `/rdzen`
  i reszta `/dane_uzytkownika` → EACCES, cudze procesy niewidoczne, własny katalog — tak).
- M4 NEGATYWNE, złośliwe okna (kanał testowy jako aplikacja): powierzchnia bez deskryptora, z `/dev/null`,
  memfd bez pieczęci, za mały, panel od aplikacji, ustawienie od aplikacji innej niż Ustawienia,
  minimalizacja cudzego okna, poprawne okno od obcego UID — odrzucone (harness: powody w logu);
  poprawne okno przyjęte (`RAMKA`).
- M4: awaria `gui` → wraca, okna aplikacji zostają; zabita aplikacja nie wraca sama.

## M4 — kryteria zaliczenia
1. Pulpit: górny pasek (aplikacja, dźwięk, zegar), dolny wyśrodkowany pasek, menu Start — zrzuty.
2. Aplikacje Zegar, Notatki (polskie znaki, zapis), Ustawienia (motyw, tapeta, mysz, sekundy)
   uruchamiane z paska i menu, okna przeciągane, minimalizowane, wyłączenie z menu Start.
3. Każda aplikacja i gui w klatce; przejęte gui nie ma plików ani treści okien; Notatki tylko swój
   katalog — testy w systemie (próby).
4. Złośliwe komunikaty okien odrzucane; role (powłoka, Ustawienia) egzekwowane przez ekran.
5. Obraz ISO startuje przez UEFI i BIOS z grafiką z firmware (simpledrm).
6. Wszystkie testy M1–M3 nadal przechodzą.

## M4 — `scripts/test-m4.sh`

| Scenariusz | Sprawdzenie | Dowód |
|---|---|---|
| pulpit | rozmiar, ciemny górny pasek, jasny dolny, kolorowa tapeta | `m4-pulpit-1-pulpit.png` |
| menu Start | otwiera się po kliknięciu znaku K1 | `m4-2-menu.png` |
| Notatki | z menu; „Zażółć K1” przez AltGr; „zapisano notatkę (9 znaków)”; tekst na zrzucie; przeciągnięcie okna o (120, 60) | `m4-3-notatki.png` |
| Zegar | z dolnego paska; środek tarczy w kolorze akcentu | `m4-4-zegar.png` |
| Ustawienia | ciemny motyw (`ustawienie 1=1`, ciemne okno), tapeta „Zmierzch” (fiolet w rogu), minimalizacja kliknięciem na pasku | `m4-5-ciemny.png`, `m4-6-tapeta.png` |
| czystość logu | brak „Kernel panic”, „BŁĄD”, zablokowanych wywołań, odrzuconych komunikatów | `m4-pulpit.log` |
| wyłączenie | Start → Wyłącz → `K1: wyłączanie zasilania`, `reboot: Power down` | log |
| ISO UEFI | OVMF + `virtio-vga` (bez sterownika K1): rdzeń przez loop, simpledrm, pulpit | `m4-iso-uefi-1-pulpit.png` |
| ISO BIOS | SeaBIOS + `virtio-vga`: GRUB ustawia VESA, simpledrm, pulpit | `m4-iso-bios-1-pulpit.png` |

## M2 — `scripts/test-m2.sh` (obraz produkcyjny, sterowanie przez monitor QEMU)

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| start | w logu tryb i „pierwsza klatka”; zrzut: rozmiar = tryb, kolorowa tapeta, ciemny górny pasek, kursor tam, gdzie mówi log | `build/testy/m2-1-start.png` |
| mysz | 4 × `mouse_move -60 -30` → kursor (−240, −120); na zrzucie kursor w nowym miejscu | `m2-2-mysz.png` |
| klawiatura i przycisk | 3 × `sendkey right` (żadne okno nie ma fokusu) → +60 px; `mouse_button 1` → kursor w kolorze akcentu; w starym miejscu kursora na zrzutach 2 i 3 ta sama tapeta (brak śladu) | `m2-3-klawiatura-przycisk.png` |
| koniec | brak „Kernel panic”, błędów usług, odrzuconych i zgubionych zdarzeń | `m2-ekran.log` |

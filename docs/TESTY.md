# Testy K1

Zasada (SPEC §2.4, §13): status **PRZETESTOWANE** tylko z dowodem — log z portu szeregowego,
zrzut ekranu albo wynik testu. Każdy mechanizm bezpieczeństwa ma test negatywny (co ma się NIE udać).

## Jak uruchomić

```bash
scripts/przygotuj-srodowisko.sh   # raz w nowym kontenerze (pakiety Ubuntu)
scripts/zbuduj.sh                 # jądro + programy + obrazy + ISO → build/wynik/
scripts/testy.sh                  # wszystkie testy: test-m1.sh, potem test-m2/m4/m5/m5f/m6 (z KVM równolegle; bez KVM po kolei, ~60 min TCG)
```

Testy na hoście (`make testy-host`) dzielą pomocnicze makra `testy/host/k1_test_host.h` (`EXPECT`,
`K1_TEST_KONIEC`); zmiana nagłówka przebudowuje wszystkie. Brak narzędzia, którego test używa
(`grub-editenv` dla `test_uklad`, `sfdisk` dla `test_instalator`), to błąd, nie pominięcie. Fuzzing
(`make fuzz`): jedna reguła dla listy `FUZZ` w `Makefile` (plik `testy/host/fuzz_<nazwa>.c` + dwa wiersze);
korpus startowy linii poleceń w `testy/host/korpus-cmdline`. Sprawdzenia na hoście opisują zachowanie
(oczekiwane liczby, teksty, bajty formatu), a nie powtarzają wzorów z kodu (0.7.3). Testy w QEMU piszemy na
bibliotece `testy/qemu/k1qemu.py` (klasa `Maszyna`: `czekaj`, `czekaj_na`, `cisza`, `klik`, `przycisk`,
`zrzut`, `sprawdz`; maszynę zamyka `zamknij_qemu` z `scripts/wspolne.sh`).

**Bez stałych pauz (0.7.3).** Krok testu czeka na dowód, że się stał, nie na upływ czasu — wolna maszyna
czeka dłużej, ale nie zawodzi: polecenie monitora QEMU — na jego odpowiedź; kliknięcie — na wiersz ekranu
„przycisk wciśnięty/puszczony”; zrzut — aż ekran pokaże sprawdzany stan (`zrzut(nazwa, gdy=…)`), a zrzut
tylko na pamiątkę — aż obraz przestanie się zmieniać. To, że coś się *nie* dzieje, sprawdza `cisza(wzór,
s)` (może najwyżej zaliczyć za wcześnie, nigdy fałszywie zawieść). Jedyny wyjątek: klawisze w MokManagerze
(`test-m6`, tylko z sekretem) — oprogramowanie UEFI nie pisze logu, a hasło nie zmienia ekranu.

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
| qemu: neg-brak-skrotu | brak `k1.rdzen_skrot` → awaria (0.6.3, dm-verity) | negatywny |
| qemu: neg-zly-skrot | skrót innego obrazu → odczyt się nie udaje, awaria | negatywny |
| qemu: neg-zmieniony-obraz | jeden bajt obrazu zmieniony → „obraz rdzenia jest zmieniony…” | negatywny |

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

## M5e — kryteria zaliczenia (0.5.7)
1. Ustawienia → Hasło ustawia hasło (skarbiec: „hasło ustawione”), zmiana i wyłączenie wymagają obecnego.
2. Po ponownym starcie z tym samym dyskiem: ekran hasła zamiast pulpitu; złe hasło odrzucone; dobre →
   pulpit z zapisanymi ustawieniami (`test-m4.sh`, scenariusze `ponownie` i `haslo`, zrzuty `m4-haslo-*.png`).
3. Hasła brak w logu; próba klatki `logowanie` bez błędów; nowe komunikaty — testy na hoście.

## M5c/M5d — kryteria zaliczenia (0.5.6)
1. Format magazynu na hoście: `test_magazyn` (62 przypadki) i `fuzz_magazyn` bez błędów.
2. Ustawienia (tapeta, motyw, rozdzielczość) i notatka wracają po wyłączeniu i ponownym starcie
   z tym samym dyskiem danych (`test-m4.sh`, scenariusz `ponownie`); na dysku brak jawnego tekstu.
3. Obraz USB startuje przez UEFI z pamięci USB; pierwszy start tworzy magazyn na pustej „K1-DANE”,
   drugi go otwiera (`test-m5.sh`).
4. Dysk trzyma tylko skarbiec (w systemie), próba klatki skarbca bez błędów, Notatki bez plików.

## M5f — `scripts/test-m5f.sh` (instalator, ADR 0018)

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| 0. obraz `.iso` (UEFI) + dysk SATA (0.6.1) | Instalator: „system do skopiowania: brak”, bez przycisku — kliknięcie w jego miejscu nic nie uruchamia | `m5f-0-z-iso.png` |
| 1. pendrive + pusty dysk SATA 4 GiB | sesja z pendrive'a: w menu Start nie ma „Wyloguj” (0.7.2); Instalator z menu Start: 1 dysk, system z pendrive'a; bez „USUŃ” nic; po „usuń” nadzorca uruchamia program zapisujący (UID 105, cel `sda`, źródło K1-EFI pendrive'a); „gotowe”; „Wyłącz komputer” wyłącza | `m5f-1-lista.png`, `m5f-1-potwierdzenie.png`, `m5f-1-gotowe.png` |
| dysk po instalacji | `sfdisk -d`: K1-EFI i K1-DANE; `sfdisk --verify` bez błędów | log testu |
| 2. start z samego dysku (UEFI) | pulpit; skarbiec: nowy magazyn na K1-DANE dysku; pulpit wie, że K1 jest zainstalowany (Wyloguj); Instalator nie prosi o pendrive („K1 działa z dysku”, 0.7.2) | `m5f-2-z-dysku.png`, `m5f-2-instalator-z-dysku.png` |
| 3. pendrive + dysk | skarbiec dostaje `sda2` (dysk), Instalator: „Kons K1: tak (dane otwarte)” → aktualizacja tylko `sda1` → „zaktualizowano” | `m5f-3-aktualizacja.png` |
| 4. start z dysku (pendrive też podłączony) | magazyn z kroku 2 otwarty, żaden nowy — dane przetrwały aktualizację; GRUB bierze jądro z dysku (`$cmdpath`) | log |

Wszędzie: brak „Kernel panic”, „BŁĄD” i zabronionych wywołań.

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
| historia baterii (0.6.0) | dwa starty z pendrive'a z baterią ACPI: pierwszy — „historia baterii zapisana”, drugi — „historia baterii: 1 poprzednich uruchomień” |
| bateria (0.5.3) | tablica ACPI: 73 %, ładowanie, zasilacz podłączony → log powłoki i jasne piksele wskaźnika na górnym pasku obok dźwięku |
| pendrive po starcie (0.5.8) | start bez K1-DANE („dane tylko w pamięci”) → `drive_add` + `device_add usb-storage` z pustą partycją K1-DANE → nadzorca: „partycja K1-DANE pojawiła się po starcie”, skarbiec: nowy magazyn, nowy „pulpit gotowy” |
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
| Notatki: wiele (0.5.8) | „Nowa notatka” → „Druga” (5 znaków); po ponownym starcie „notatki wczytane: 2 (pierwsza: 5 B)”, kliknięcie drugiej → 13 B, „Usuń notatkę” + potwierdzenie → zostaje 1 | `m4-3-notatki-dwie.png`, `m4-ponownie-notatki.png` |
| Zegar | z dolnego paska; środek tarczy w kolorze akcentu | `m4-4-zegar.png` |
| Ustawienia | ciemny motyw (`ustawienie 1=1`, ciemne okno), tapeta „Zmierzch” (fiolet w rogu), minimalizacja kliknięciem na pasku | `m4-5-ciemny.png`, `m4-6-tapeta.png` |
| Kalkulator (0.7.1) | myszą i klawiaturą: 12 + 7 × 3 = 57, wynik w logu i na zrzucie | `m4-pulpit.log` |
| Wyloguj (0.7.2) | z dyskiem danych (pulpit, ponownie, hasło): „K1 zainstalowany… Wyloguj dostępne”; z ISO (bez K1-DANE): „sesja z pendrive'a: bez Wyloguj” | log |
| czystość logu | brak „Kernel panic”, „BŁĄD”, zablokowanych wywołań, odrzuconych komunikatów | `m4-pulpit.log` |
| wyłączenie | Start → Wyłącz → `K1: wyłączanie zasilania`, `reboot: Power down` | log |
| ISO UEFI | OVMF + `virtio-vga` (bez sterownika K1): rdzeń przez loop, simpledrm, pulpit | `m4-iso-uefi-1-pulpit.png` |
| ISO BIOS | SeaBIOS + `virtio-vga`: GRUB ustawia VESA, simpledrm, pulpit | `m4-iso-bios-1-pulpit.png` |

## 0.8.0 Pliki — `scripts/test-pliki.sh` (ADR 0029)

Obraz produkcyjny z dyskiem danych (pusta K1-DANE), dwa uruchomienia.

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| pulpit | Kosz i Pliki na pulpicie; podwójne kliknięcie otwiera Pliki, lista ze skarbca | `pliki-pierwszy.log` |
| nowy folder | „Nowy folder” → pole nazwy od razu, „Projekt” z klawiatury zastępuje zaznaczoną nazwę; zapis w skarbcu | `pliki-1-projekt.png` |
| zajęta nazwa | drugi folder → „projekt” (inna wielkość liter) — skarbiec odmawia (`-21`), komunikat w stopce | `pliki-2-zajeta-nazwa.png` |
| foldery | podwójne kliknięcie otwiera „Projekt” (0 elementów), „w górę” — Dokumenty (2) | log `widok:` |
| Kosz | Usuń → Kosz (1) → Przywróć (Kosz 0, Dokumenty 2) → Usuń → „Usuń na zawsze” → pytanie → „Tak, usuń” → Kosz 0 | `pliki-3-kosz.png`, `pliki-4-pytanie.png` |
| pulpit | folder w Pulpicie = ikona na pulpicie (w logu numer, nie nazwa); podwójne kliknięcie → Pliki w tym folderze; menu folderu (0.8.2 krok 5): „Otwórz”, „Pokaż w Plikach”, „Zmień nazwę”, „Usuń” → „Zmień nazwę” otwiera Pliki z polem nazwy, nowa nazwa z klawiatury w skarbcu; menu „Usuń” → ikona znika, Kosz pełny; menu Kosza „Opróżnij Kosz” → pusty | `pliki-5-pulpit-z-folderem.png`, `pliki-menu-zmień.png`, `pliki-5a-zmiana-nazwy.png`, `pliki-6-kosz-pelny.png` |
| dysk | nagłówek magazynu jest, nazwy „Projekt” jawnie nie ma | skrypt |
| ponownie | po wyłączeniu: Dokumenty z „Projekt”, Kosz pusty | `pliki-ponownie-dokumenty.png` |
| czystość logu | bez nazw plików, paniki, błędów, zablokowanych wywołań i odrzuconych komunikatów | log |

**Krok 2 (ADR 0030)** — osobny dysk: partycja K1-DANE z magazynem i trzema notatkami w formacie 0.5.8–0.7.6
(`testy/host/stare_notatki.c` — kod formatu skarbca, ten sam co w systemie), dwa uruchomienia.

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| przeniesienie | Notatki z paska: „stare notatki: 3 do przeniesienia”, „przeniesione (błędów: 0)”, 3 notatki z plików, najnowsza na górze (9 B) | `pliki-notatki-1-notatki.png` |
| okno wyboru | „Otwórz…” w Notatkach → skarbiec prosi nadzorcę, okno „Wybór pliku” dostaje prośbę od skarbca (aplikacja 2, typ tekst); Dokumenty (1 folder, 0 plików do wyboru) → folder Notatki (3) → „Zażółć K1” → Otwórz → Notatki otwierają ją (13 B) | `pliki-notatki-2-wybor.png`, `pliki-notatki-3-wybor-plik.png` |
| Anuluj | drugie „Otwórz…” → „Anuluj” → Notatki: „anulowano” | log |
| Pliki | Dokumenty › Notatki: 3 pliki | `pliki-notatki-4-pliki.png` |
| Otwórz za pomocą (0.8.2 krok 5) | prawy przycisk na pliku w Plikach → menu na ekranie (jasny wiersz) → „Otwórz za pomocą” → podmenu „Notatki” → skarbiec daje plik, nadzorca przekazuje go Notatkom (fd 5), Notatki otwierają go (20 B) | `pliki-notatki-5-menu.png`, `pliki-notatki-5-podmenu.png` |
| domyślna aplikacja | podwójne kliknięcie pliku w Plikach → Notatki (9 B); Ustawienia → Aplikacje: każdy typ i jego aplikacja („Dokument tekstowy — Notatki”, „Obrazy … — Podgląd”, PDF, ZIP, „Plik” — brak); po ponownym starcie podwójne kliknięcie dalej otwiera Notatki | `pliki-notatki-6-ustawienia.png` |
| dysk | jawnie nie ma treści, nazw notatek, „Notatki” ani zapisu ustawień | skrypt |
| ponownie | nic do przeniesienia, 3 notatki z plików, podwójne kliknięcie — Notatki | `pliki-notatki-ponownie-*.png` |

Na hoście: `test_dokumenty` (skarbiec: nazwy, typy, wpisy, kawałki treści, foldery, Kosz i 30 dni, prawa przez kanał;
krok 2: prawa Notatek, „Otwórz w…” (od 0.8.2 „Otwórz za pomocą”), okno wyboru, nazwa z pierwszego wiersza), `test_komunikat` (`DOK_*`,
`PLIKI_POKAZ`, `URUCHOM` z miejscem; krok 2: nowe komunikaty i domyślne aplikacje), `test_rozmieszczenie` (klucze
ikon, K1R2 i K1R1, „Wybór pliku” nigdy na pasku), `fuzz_dokument`, `fuzz_rozmieszczenie`.
0.8.2 krok 5: `test_menu` — menu pod prawym przyciskiem (przybornik `k1_menu`): wiersze dla każdego typu pliku i miejsca
(Pliki, pulpit, Kosz, pendrive), „Otwórz za pomocą” (bez Plików, ✓, „Zawsze otwieraj” od dwóch aplikacji), komunikat
bez aplikacji w całości, położenie przy brzegach, trafienie, rysowanie poza obrazem (ASan/UBSan); `test_komunikat` —
`URUCHOM` i `PLIKI_POKAZ` z polem nazwy (`K1_PLIKI_NAZWA` tylko z plikiem, Kosz tylko bez).
0.8.1 krok 1: `test_dokumenty` (naprawa wpisów, granica czasu, folder aplikacji, licznik Kosza, 64 pliki na
aplikację) i `test_notatki` — prawdziwy kod Notatek i skarbca przez gniazdo (bez sprawdzania UID nadawcy):
notatka nie do odczytu, plik do Kosza w trakcie pisania, 300 plików.

## 0.8.0 krok 3 i 4 — zdjęcia z pendrive'a, podgląd, tapeta, `scripts/test-pendrive.sh` (ADR 0031, 0032)

Bez prawdziwego sprzętu: obraz produkcyjny, dysk danych (pusta K1-DANE) i **wirtualny pendrive USB** (xHCI): FAT32 w
partycji MBR zrobiony przez mtools (jak inny system), z prawdziwymi zdjęciami JPEG i PNG (Pillow), plikiem z polską długą
nazwą, uszkodzonym JPEG-iem, notatką, plikiem 17 MB, plikiem ukrytym i śmieciem macOS „._…”. Drugi pendrive (NTFS) wkładany w trakcie.

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| miejsce | nadzorca widzi pendrive → Pliki pokazują „Pendrive” | `pendrive-1-miejsce.png` |
| czytnik | kliknięcie → nadzorca: czytnik dla `sdX` (tylko odczyt), UID 106; czytnik: FAT32; 4 elementy (bez ukrytego); przyciski tylko „Importuj”, „Importuj wszystkie” | `pendrive-2-pendrive.png` |
| foldery | DCIM (1) → 100K1 (9, bez „._…”) | `pendrive-3-100k1.png` |
| import | „Importuj wszystkie” → 9 plików w Obrazach (od 0.8.2 też GIF, BMP, WebP i zdjęcia 48/100 Mpx), skarbiec rozpoznał wszystkie z treści jako obrazy | `pendrive-4-zaimportowane.png` |
| za duży | plik 17 MB → pominięty, nic nie zapisane, komunikat w stopce | `pendrive-5-za-duzy.png` |
| pojedynczo | zdjęcie z polską nazwą → Obrazy, notatka → Dokumenty; w skarbcu 5 plików z pendrive'a, razem tyle bajtów, ile na pendrivie | log `pliki z pendrive'a:` |
| miejsca | Obrazy: 4, Dokumenty: 1 | log `widok:` |
| podgląd (krok 4) | zaznaczone zdjęcie JPEG → nadzorca: dekoder UID 107; dekoder: JPEG → 80×60; kolor zdjęcia w podglądzie; PNG → 32×32 | `pendrive-6-podglad-jpeg.png`, `-png.png` |
| uszkodzony obraz | dekoder: błąd -42; Pliki: „Podgląd niedostępny” | `pendrive-6-podglad-uszkodzony.png` |
| duże zdjęcia (0.8.2 krok 3) | maszyna 8 GB (jak X230): nadzorca daje dekoderowi 1152 MiB; zdjęcie 48 Mpx → 200×150 i 100 Mpx (tylko z KVM) → 150×150 (pliki po kilka MB: kawałkami po 1 MiB), kolor w podglądzie; czas dekodowania w logu | `pendrive-6-podglad-48mpx.png`, `-100mpx.png` |
| nowe formaty (0.8.2 krok 3) | GIF, BMP, WebP stratny → 64×48, kolor w podglądzie | `pendrive-6-podglad-gif.png`, `-bmp.png`, `-webp.png` |
| za duże zdjęcie | nagłówek 10050×10000: dekoder odmawia przed przydziałem (-41, 101 Mpx); Pliki: „Zdjęcie ma 101 Mpx — K1 pokazuje do 100 Mpx” | `pendrive-6-podglad-101mpx.png` |
| tapeta | „Ustaw jako tapetę” → ekran: zdjęcie od Plików 80×60, tapeta 31; kolor zdjęcia przy prawej krawędzi ekranu | `pendrive-6-tapeta.png` |
| bez aplikacji (0.8.2 krok 5) | Dokumenty: podwójne kliknięcie filmu (typ „Plik”) → stopka „K1 nie ma jeszcze aplikacji do tego typu pliku.”, żadna aplikacja nie dostaje pliku; prawy przycisk → menu tylko „Zmień nazwę”, „Usuń” („Otwórz” wyszarzone); Esc zamyka | `pendrive-6c-bez-aplikacji.png`, `pendrive-6c-menu-bez-aplikacji.png` |
| wyjęcie | `device_del` → nadzorca zatrzymuje czytnik (sygnał 9), Pliki: „Pendrive odłączony.”, Dokumenty | `pendrive-7-wyjety.png` |
| NTFS | `device_add` pendrive'a z NTFS → „Pendrive” wraca; czytnik: nie FAT; Pliki: K1 jeszcze go nie czyta | `pendrive-8-ntfs.png` |
| czystość logu | bez nazw i treści plików, paniki, błędów, zablokowanych wywołań i odrzuconych komunikatów | `pendrive.log` |
| ponowny start | ten sam dysk danych bez pendrive'a: ekran wczytuje zdjęcie ze skarbca (80×60) i rysuje je jako tapetę | `pendrive-9-ponownie.png`, `pendrive-ponownie.log` |

Na hoście: `test_fat` (czytnik na obrazach z mtools/sfdisk: FAT12 bez partycji, FAT16 w MBR, FAT32 w GPT, NTFS; obrazy
celowo uszkodzone; nazwy; lista `k1_pendrive`), `test_komunikat` (`PENDRIVE*`, `PEN_*`), `test_dokumenty` (pochodzenie
tylko od Plików), `test_instalator` (`k1_pendrive_znajdz`), `fuzz_fat`, `fuzz_pendrive`.

## M2 — `scripts/test-m2.sh` (obraz produkcyjny, sterowanie przez monitor QEMU)

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| start | w logu tryb i „pierwsza klatka”; zrzut: rozmiar = tryb, kolorowa tapeta, ciemny górny pasek, kursor tam, gdzie mówi log | `build/testy/m2-1-start.png` |
| mysz | 4 × `mouse_move -60 -30` → kursor (−240, −120); na zrzucie kursor w nowym miejscu | `m2-2-mysz.png` |
| klawiatura i przycisk | 3 × `sendkey right` (żadne okno nie ma fokusu) → +60 px; `mouse_button 1` → kursor w kolorze akcentu; w starym miejscu kursora na zrzutach 2 i 3 ta sama tapeta (brak śladu) | `m2-3-klawiatura-przycisk.png` |
| koniec | brak „Kernel panic”, błędów usług, odrzuconych i zgubionych zdarzeń | `m2-ekran.log` |

## M6c–M6d (0.7.0, ADR 0022) — A/B i Secure Boot

| Test | Co sprawdza | Rodzaj |
|---|---|---|
| host `test_uklad` | blok układu K1-EFI (zbuduj/odczytaj, nakładanie, wyrównanie, przepełnienie, suma), grubenv — te same bajty co `grub-editenv` (w `make testy-host`), ścisłe parsowanie, decyzja A/B | jednostkowy + negatywny |
| host `test_sha256` | SHA-256 (wektory FIPS 180-4, podział wejścia w każdym miejscu) — MokAuth | jednostkowy |
| host `test_manifest` | manifest formatu 2 (identyfikator wydania podpisany, granice, pola zarezerwowane) | jednostkowy + negatywny |
| host `test_komunikat` | `INST_AB`, `SB_STAN`, `SB_KLUCZ`, flaga nowego miejsca w `INST_GOTOWE`/`INST_WYNIK` | negatywny |
| `fuzz_uklad` | blok układu i grubenv: dowolne bajty, zapis z powrotem czyta się tak samo | fuzzing |
| `test-m5f.sh` | aktualizacja A/B do wolnego miejsca B; start z dysku: B na próbę i potwierdzone (grubenv: `k1_slot=b`); „nowe” miejsce A uszkodzone → GRUB wraca do B, K1 zgłasza, próba wyczyszczona; starsza wersja zablokowana; uszkodzony system na pendrivie → odmowa przed zapisem | funkcjonalny + negatywny |
| `test-m6.sh` | OVMF z Secure Boot i kluczami MS: klucz K1 w MOK → pulpit i „włączony, klucz wpisany”; bez klucza → odmowa z wyjaśnieniem na ekranie; zmieniony bajt `k1.efi` → odmowa; klucz wydania: „Przygotuj klucz K1” → MokManager (Enroll MOK, hasło) → MokList → Secure Boot → K1 startuje; wersja deweloperska: wpisanie niedostępne | funkcjonalny + negatywny |
| host `test_efi` | wspólny czytnik K1-EFI: format 1 i 2, miejsce z grubenv, nieczytelny grubenv → miejsce A i jego wersja (to pokazuje lista dysków i z tym porównuje Instalator), miejsce bez manifestu | jednostkowy + negatywny |
| `test-m6.sh` bez sekretu | wpisanie klucza wydania przez MokManagera wymaga podpisanego wydania (`K1_KLUCZ_PODPISU`); w wersji deweloperskiej ten krok jest pominięty celowo (sprawdzane jest tylko, że wpisanie jest niedostępne) | — |
| ręcznie (0.7.0) | GRUB z podsuniętym obcym initramfs i `rdinit=` → jądro uruchomiło tylko nasz initramfs; aktualizacja zainstalowanego 0.6.3 starym Instalatorem do 0.7.0 | negatywny |

## 0.8.1 krok 2 — skarbiec na duże dane, `scripts/test-skarbiec.sh` (ADR 0034)

Obraz produkcyjny, dysk danych przygotowany na hoście kodem skarbca (`testy/host/magazyn_testowy.c`):

| Uruchomienie | Co sprawdza |
|---|---|
| dziennik | K1-DANE 190 MiB, jeden obiekt zapisany 20 000 razy (78 MiB starych wersji) → „kompaktowanie (przy otwarciu): dziennik 78 → 0 MiB”, pulpit startuje |
| dziennik-ponownie | ten sam dysk: magazyn otwarty, bez kompaktowania (dziennik już krótki) |
| pełny | K1-DANE 64 MiB (jak pendrive'y 0.5.6–0.7.6), 6 MiB wolne → Pliki: „Nowy folder” odrzucony (−28, brak miejsca na pliki), stopka „mało miejsca” (0.8.1 krok 3); Ustawienia → Wygląd: ciemny motyw zapisany (rezerwa dla systemu; do 0.8.2 krok 5 — Ustawienia → Aplikacje) |
| powiększenie (krok 3, ADR 0033) | ta sama pełna K1-DANE na wirtualnym pendrivie USB o 512 MiB większym (obraz „nagrany” na większy pendrive) + drugi pendrive FAT32 z 16 zdjęciami po ok. 2,8 MB (Pillow, szum) → Ustawienia → Informacje: zajęte 23 z 23 MiB, pendrive ma 512 MiB więcej, przycisk → klik: nowa tablica (64 → 576 MiB), magazyn: obszar 31 → 287 MiB, Informacje: 279 MiB, bez przycisku → Pliki → Pendrive → DCIM › 100K1 → „Importuj wszystkie”: 16 z 16 (próba T6 planu: przed — 11 z 16 na dysku 64 MiB) |
| powiększenie-ponownie | ten sam pendrive: magazyn otwarty, nic już nie rośnie; potem `sfdisk -d`: K1-DANE od 1 MiB do ostatniego pełnego MiB |
| dokończenie | tablica powiększona przez `sfdisk`, magazyn nie (przerwa prądu między tablicą a nagłówkiem) → skarbiec przy starcie: „magazyn powiększony do całej partycji K1-DANE (dokończenie po starcie)” |

Na hoście: `test_indeks`, `test_magazyn` (65 536 obiektów, długi dziennik, kompaktowanie, awarie; krok 3: powiększenie
z obszarem 0 i 1, przerwa prądu po każdym zapisie), `test_dokumenty` (16 384 wpisy, duże drzewo, rezerwa; krok 3:
`SKARB_MIEJSCE` bez K1-DANE, odmowa powiększenia bez magazynu), `test_komunikat` (nowe komunikaty), `fuzz_magazyn`,
`fuzz_dokument`.

## 0.8.2 krok 1a — format 2 skarbca (ADR 0036), na hoście

Format 2 sprawdzają testy na hoście (w systemie od kroku 1c — niżej):

| Test | Co sprawdza |
|---|---|
| `test_odcinki` (ASan/UBSan) | zapis, odczyt, nowa wersja, usunięcie, ponowne otwarcie, brak jawnego tekstu, hasło; otwarcie 150 MiB plików czyta < 1/16 danych (spis i ogon, nie treść); zmieniony bajt spisu i metryczki → odmowa otwarcia, bajt treści → odmowa odczytu tego obiektu; zamienione rekordy; nagłówki z poprawnym MAC, które kłamią (liczba wpisów spisu, koniec ogona, długość spisu, generacja); stary odcinek podłożony po ponownym użyciu — stara treść nigdy nie wraca; **przerwa prądu po każdym zapisie** (pierwsze 120, potem co siódmy; i z rozdartym nagłówkiem) w ciągu 160 operacji na 4 odcinkach z kompaktowaniem i spisami — za każdym razem magazyn się otwiera, każdy obiekt jak przed albo po zapisie, praca idzie dalej; pełny magazyn (całe miejsce na pliki zajęte kawałkami 1 MiB, potem 500 nadpisań przy 80 % — kompaktowanie nadąża); kompaktowanie w krokach (najwyżej 2 zapisy na krok); powiększenie (przerwa po każdym zapisie); 70 000 obiektów (więcej niż format 1), spis w kawałkach |
| `fuzz_magazyn` tryby 3–5 | zmienione bajty magazynu formatu 2 (nagłówki, spis, ogon); pola nagłówka formatu 2 z poprawnym MAC; losowe ciągi zapisów, usunięć i kroków kompaktowania — po każdym ponowne otwarcie musi oddać każdy obiekt |
| `pomiar_magazynu` (ręcznie, bez ASan) | pomiar z planu (wzór próby T3): `make build/obj/pomiar_magazynu && build/obj/pomiar_magazynu PLIK MIB_K1_DANE MIB_PLIKÓW FORMAT` — miejsce na pliki, czas otwarcia (procesor) i odczytane MiB, najdłuższy zapis i krok kompaktowania; wyniki w ADR 0036 |

## 0.8.2 kroki 1b i 1c — kompaktowanie w przerwach, przejście na format 2 (ADR 0036)

Od 1c system tworzy format 2, a stary magazyn (format 1) przechodzi na format 2 po potwierdzonym starcie.

| Test | Co sprawdza |
|---|---|
| `test_przejscie` (host, ASan/UBSan) | format 1 (64 MiB, 120 operacji: nowe wersje, usunięcia, kawałki 1 MiB) → format 2: każdy obiekt jak był, także po ponownym otwarciu; kopie zaszyfrowane; oba sloty nagłówka w formacie 2; miejsce ok. 1,8 × większe; potem praca i kompaktowanie formatu 2. Kroki po 64 bloki z zapisem albo usunięciem po każdym (zmienione po kopii — kopiowane jeszcze raz); kompaktowanie formatu 1 w trakcie — kopiowanie od nowa. **Przerwa prądu po każdym zapisie** (pierwsze 30, potem co czwarty; i z rozdartym nagłówkiem) — za każdym razem magazyn się otwiera (stary albo nowy), każdy obiekt jak przed albo po przerwanej operacji, przejście i praca idą dalej. Pełny obszar formatu 1 → „za mało miejsca”, format 1 działa dalej; po powiększeniu partycji — przejście. Magazyn z hasłem |
| `fuzz_magazyn` tryb 6 | format 1 i losowy ciąg zapisów, usunięć, kroków kompaktowania i kroków przejścia — po każdym ciągu ponowne otwarcie oddaje każdy obiekt |
| `test_komunikat` | `START_POTWIERDZONY`: tylko zera, tylko kanał sterujący, bez deskryptora |
| `test-skarbiec.sh` dziennik | K1-DANE 190 MiB, format 1, dziennik 78 MiB → kompaktowanie przy otwarciu, potem (po „start tej wersji potwierdzony”) przejście na format 2: miejsce na dane 94 → 168 MiB, najdłuższy krok < 1 s |
| `test-skarbiec.sh` dziennik-ponownie | magazyn otwarty w formacie 2, bez kompaktowania i bez ponownego przejścia; na hoście (`magazyn_testowy --sprawdz`): format 2, ostatnia z 20 000 wersji |
| `test-skarbiec.sh` przejście | K1-DANE 64 MiB z 0.8.1 prawie pełna (format 1, 25 obiektów po 1 MiB) → przejście dopiero po potwierdzonym starcie, w krokach → miejsce na dane 31 → 58 MiB; Ustawienia → Informacje: na pliki 50 MiB (w 0.8.1: 23); na hoście: format 2, każdy obiekt bajt w bajt |
| `test-skarbiec.sh` pełny, powiększenie, dokończenie | jak w 0.8.1, ale na formacie 2: na pliki 50 MiB, po powiększeniu do 576 MiB — miejsce na dane 58 → 556 MiB, na pliki 545 MiB |
| `test-pendrive.sh` ponownie | magazyn z filmem 300 MB: „magazyn otwarty … (format 2, t s)” z t < 1 s i bez „skarbiec jeszcze otwiera magazyn” — ekran dostaje ustawienia od razu (R58) |
| `test-pliki.sh` | notatki z 0.5.8–0.7.6 w magazynie formatu 1 (`stare_notatki`) — Notatki przenoszą je do plików, a skarbiec w tym czasie przechodzi na format 2 |

## 0.8.2 krok 2 — pendrive i nazwy plików

| Test | Co sprawdza |
|---|---|
| `test_fat` (host) | „mały” FAT32 z `mformat -F -c 8` (64 MiB, ok. 16 000 klastrów — z samej liczby wyszedłby FAT16; próba T6) czytany jako FAT32, folder 3000 plików cały; ten sam sektor z 16-bitowym rozmiarem FAT — nie FAT. Nazwy: „    .jpg” → „Plik.jpg”, „.Trashes” zostaje ukryty, znaki niewidoczne → „_”, spacje innej szerokości z brzegów, NFD → NFC (ż, ó), dwie nazwy po 200 liter różne od 150. → ta sama nazwa (próba T4) |
| `test_dokumenty` (host) | zasady nazw (próba T1/6: 20 nazw-podróbek odrzuconych, NBSP i U+3000 w środku dozwolone, „ą” jako jedna litera), poprawianie (NFD → NFC, spacje z brzegów, „_”), porównanie Ä/ä, Č/č, Ÿ/ÿ (× ≠ ÷); wolna nazwa dla „a.<125 × x>” i dla długiej nazwy z „.jpg” (próba T1/2); wpisy z 0.8.1 z zakazaną nazwą poprawione przy wczytaniu, zajęta → „ (2)”, treść cała; `DOK_CZAS` — Pliki tak, plik z K1, Notatki, pulpit nie |
| `test_komunikat` | `PEN_SPIS` a (pierwszy element 0…16 383), `PEN_LISTA` d, `DOK_CZAS` (1 s … 2107, dwa pola, tylko plik użytkownika), `SKARB_MIEJSCE` e (0/1) |
| `test-pendrive.sh` | drugi „Importuj wszystkie” w DCIM › 100K1 → „zapisane 0 … pominięte 3”; pendrive z „małym” FAT32 (`mformat -F -c 8`, 124 MB w MBR) → czytnik: FAT32; folder 1100 zdjęć → dwie listy czytnika, Pliki: 1100 elementów; „Importuj wszystkie” → 1100 do Obrazów; drugi raz → „pominięte 1100”, Obrazy: 1104 (od kroku 3: „pominięte 9”, Obrazy: 1110); zrzuty `pendrive-4-pominiete.png`, `pendrive-9-folder-1100.png`, `pendrive-9-zaimportowane-1100.png` |

## 0.8.2 krok 3 — zdjęcia: duże i nowe formaty

| Test | Co sprawdza |
|---|---|
| `test_dekoder` (host) | GIF (też animowany: pierwsza klatka), BMP (też z paletą), WebP stratny, bezstratny i z przezroczystością — rozpoznane jak w skarbcu (`k1_dok_typ`) i zdekodowane; każdy typ rodziny „obrazy” ma dekoder; zdjęcia 48 i 100 Mpx zwykłe i progresywne (próba T5) → 2560×1600; nagłówek 101 Mpx i 30000×30000 → odmowa z rozmiarem; CMYK progresywny 100 Mpx → „za mało pamięci” przed przydziałem. Bez dużych zdjęć: `K1_DUZE_ZDJECIA=0` |
| `test_komunikat` (host) | `DEK_OBRAZ` bez deskryptora, plik do 128 MiB; `DEK_KAWALEK` (numer 0…127, 1 B…1 MiB, memfd); `DEK_BLAD` b = Mpx tylko przy „za duży”; `DOK_CZYTAJ`/`DOK_TRESC` najwyżej 1 MiB |
| `fuzz_dekoder` | korpus z GIF, BMP, WebP (stratny, bezstratny, z alfą) |
| w systemie (`t_proby`) | klatka dekodera: 1 GiB przestrzeni adresowej się mieści, 1,2 GiB — błąd przydziału (`RLIMIT_AS` 1152 MiB), nie zabicie procesu |
| `test-pendrive.sh` | maszyna 8 GB: nadzorca daje dekoderowi 1152 MiB; zdjęcia 48 i 100 Mpx (kilka MB — kilka kawałków) → podgląd, kolor na zrzucie (100 Mpx tylko z KVM: w TCG dekodowanie trwa ok. 20 s procesora — tyle, co limit dekodera; 48 Mpx w TCG: ok. 10 s); GIF, BMP, WebP → podgląd; nagłówek 101 Mpx → odmowa, „Zdjęcie ma 101 Mpx”; zrzuty `pendrive-6-podglad-{48mpx,100mpx,gif,bmp,webp,101mpx}.png` |
| `test-m4.sh`, `test-pliki.sh` | ustawienia zapisane w nowym formacie `K1U3` (miejsce na 32 typy plików) — w magazynie nie widać go jawnym tekstem |

## 0.8.2 krok 4 — aplikacja Podgląd (ADR 0037)

| Test | Co sprawdza |
|---|---|
| `test_podglad` (host) | kolejność zdjęć jak w Plikach (po nazwie, A–Z jak a–z; bez tekstu, PDF, folderów, Kosza i innych folderów), pozycja, ← / → dookoła, zdjęcie po wyrzuceniu do Kosza (następne, na końcu poprzednie, żadne), „3 z 27” (ucięte, nigdy poza bufor) |
| `test_dokumenty` (host) | prawa Podglądu przez prawdziwy kanał: bez gestu — nic; po `DOK_OTWORZ` z Plików — zdjęcia tego folderu (nie tekst obok, nie inny folder, nie Kosz); czyta, do Kosza (D2); przenoszenie, opróżnianie Kosza, usuwanie na zawsze, otwieranie w innej aplikacji — odmowa; Podgląd nie otwiera tekstu, Notatki zdjęć; „Otwórz…”: okno wyboru z rodziną „obrazy” — tekst odrzucony, zdjęcie z innego folderu daje i jego folder; po zamknięciu — nic; domyślna aplikacja PNG/WebP — Podgląd |
| `test_komunikat` (host) | `DOK_WYBIERZ` b = rodzina (nie razem z typem), `DOK_PROSBA` c; `DEK_PIKSELE` c, d — wymiary zdjęcia (≥ obraz, ≤ 100 Mpx) |
| `test_dekoder` (host) | wymiary zdjęcia po obrocie z EXIF (`zw` × `zh`) |
| `test_rozmieszczenie` (host) | Podgląd nie jest przypięty przy pierwszym starcie |
| w systemie | klatka Podglądu (UID 208), próba w klatce, spis plików obrazu — z listy aplikacji |
| `test-pendrive.sh` | dwuklik zdjęcia w Obrazach → okno Podglądu (UID 208): „zdjęcie 4 z 10”, 80×60 (nie powiększone), dekoder dla Podglądu w klatce, kolor na zrzucie; R → widok 60×80 (zrzut: kolor nad i pod środkiem, nie z boku); ← → zdjęcie uszkodzone (NEGATYWNY: „nie da się odczytać”); ‹ → PNG (zielony na zrzucie); + → 150 % (120×90, animacja), − → 100 %; Delete → do Kosza bez pytania, „2 z 9”, Pliki: Obrazy — 9; Ctrl+O → okno wyboru z rodziną „obrazy”, Esc — anulowano; › i „Ustaw jako tapetę” → ekran przyjmuje zdjęcie od Podglądu; × zamyka. Później Obrazy: 1109 (jedno w Koszu). Zrzuty `pendrive-6b-podglad*.png` |
| `test-m4.sh`, `test-pliki.sh` | ustawienia zapisane jako `K1U4` — w magazynie nie widać go jawnym tekstem |

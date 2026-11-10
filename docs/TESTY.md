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
| pulpit | folder w Pulpicie = ikona na pulpicie (w logu numer, nie nazwa); podwójne kliknięcie → Pliki w tym folderze; menu pliku „Usuń” → ikona znika, Kosz pełny; menu Kosza „Opróżnij Kosz” → pusty | `pliki-5-pulpit-z-folderem.png`, `pliki-6-kosz-pelny.png` |
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
| Otwórz w… | przyciski: „Notatki”, „Anuluj” → skarbiec daje plik, nadzorca przekazuje go Notatkom (fd 5), Notatki otwierają go (20 B) | `pliki-notatki-5-otworz-w.png` |
| domyślna aplikacja | podwójne kliknięcie pliku w Plikach → Notatki (9 B); Ustawienia → Aplikacje → „Pliki (podgląd)” (zapis w skarbcu) → podwójne kliknięcie zostaje w Plikach | `pliki-notatki-6-ustawienia.png` |
| dysk | jawnie nie ma treści, nazw notatek, „Notatki” ani zapisu ustawień | skrypt |
| ponownie | nic do przeniesienia, 3 notatki z plików, domyślnie Pliki (ustawienie zapamiętane) | `pliki-notatki-ponownie-*.png` |

Na hoście: `test_dokumenty` (skarbiec: nazwy, typy, wpisy, kawałki treści, foldery, Kosz i 30 dni, prawa przez kanał;
krok 2: prawa Notatek, „Otwórz w…”, okno wyboru, nazwa z pierwszego wiersza), `test_komunikat` (`DOK_*`,
`PLIKI_POKAZ`, `URUCHOM` z miejscem; krok 2: nowe komunikaty i domyślne aplikacje), `test_rozmieszczenie` (klucze
ikon, K1R2 i K1R1, „Wybór pliku” nigdy na pasku), `fuzz_dokument`, `fuzz_rozmieszczenie`.

## 0.8.0 krok 3 i 4 — zdjęcia z pendrive'a, podgląd, tapeta, `scripts/test-pendrive.sh` (ADR 0031, 0032)

Bez prawdziwego sprzętu: obraz produkcyjny, dysk danych (pusta K1-DANE) i **wirtualny pendrive USB** (xHCI): FAT32 w
partycji MBR zrobiony przez mtools (jak inny system), z prawdziwymi zdjęciami JPEG i PNG (Pillow), plikiem z polską długą
nazwą, uszkodzonym JPEG-iem, notatką, plikiem 17 MB, plikiem ukrytym i śmieciem macOS „._…”. Drugi pendrive (NTFS) wkładany w trakcie.

| Krok | Sprawdzenie | Dowód |
|---|---|---|
| miejsce | nadzorca widzi pendrive → Pliki pokazują „Pendrive” | `pendrive-1-miejsce.png` |
| czytnik | kliknięcie → nadzorca: czytnik dla `sdX` (tylko odczyt), UID 106; czytnik: FAT32; 4 elementy (bez ukrytego); przyciski tylko „Importuj”, „Importuj wszystkie” | `pendrive-2-pendrive.png` |
| foldery | DCIM (1) → 100K1 (3, bez „._…”) | `pendrive-3-100k1.png` |
| import | „Importuj wszystkie” → 3 pliki w Obrazach, skarbiec rozpoznał wszystkie z treści jako obrazy | `pendrive-4-zaimportowane.png` |
| za duży | plik 17 MB → pominięty, nic nie zapisane, komunikat w stopce | `pendrive-5-za-duzy.png` |
| pojedynczo | zdjęcie z polską nazwą → Obrazy, notatka → Dokumenty; w skarbcu 5 plików z pendrive'a, razem tyle bajtów, ile na pendrivie | log `pliki z pendrive'a:` |
| miejsca | Obrazy: 4, Dokumenty: 1 | log `widok:` |
| podgląd (krok 4) | zaznaczone zdjęcie JPEG → nadzorca: dekoder UID 107; dekoder: JPEG → 80×60; kolor zdjęcia w podglądzie; PNG → 32×32 | `pendrive-6-podglad-jpeg.png`, `-png.png` |
| uszkodzony obraz | dekoder: błąd -42; Pliki: „Podgląd niedostępny” | `pendrive-6-podglad-uszkodzony.png` |
| tapeta | „Ustaw jako tapetę” → ekran: zdjęcie od Plików 80×60, tapeta 31; kolor zdjęcia przy prawej krawędzi ekranu | `pendrive-6-tapeta.png` |
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


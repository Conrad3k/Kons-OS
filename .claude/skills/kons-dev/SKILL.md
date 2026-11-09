---
name: kons-dev
description: Budowa systemu Kons K1 Sovereign — własnego systemu operacyjnego na jądrze Linux (nie dystrybucji) z userspace w C (statycznie, musl). Architektura i niezmienniki bezpieczeństwa, przepisy (nowa usługa, opcja jądra, protokół IPC, testy w QEMU), etapy M0–M12, ADR, wersje, scalanie, CI, raport dla właściciela. Używaj przy każdej zmianie w repo (kod C, jądro, skrypty, testy, CI, dokumentacja) i gdy właściciel zgłasza uwagi.
---

# Praca nad K1

Źródła prawdy: `docs/SPEC.md` (nadrzędna, zmiany tylko przez ADR w `docs/adr/`), `docs/ARCHITEKTURA.md`
(mapa repo, przebieg startu, konwencje), `docs/TESTY.md`, `docs/RYZYKA.md`, `docs/DO_ZROBIENIA.txt`
(co dalej), `docs/komponenty/*.md`, `docs/WYGLAD.md` (ustalenia właściciela co do wyglądu).
Gdy ten skill i dokumenty się różnią — wygrywają dokumenty; popraw skill.

## 1. Czym jest K1 (architektura)

K1 **nie jest dystrybucją Linuksa**. Z Linuksa bierzemy tylko jądro (własna, minimalna konfiguracja).
Cała przestrzeń użytkownika to nasz kod w C, linkowany statycznie z musl. Nie ma pakietów, powłoki,
systemd, X11/Wayland, drzewa FHS (`/usr`, `/etc`, `/bin`…) ani bibliotek współdzielonych.
Docelowo są dwa światy (SPEC §4): **natywny** (nasz kod) i **strefa zgodności** (izolowany „równoległy
Linux” dla obcych programów, np. przeglądarki — od M7). Pliki przechodzą między nimi tylko przez
Gatekeepera (M9).

**Łańcuch startu** (stan: M4; od M10 — UEFI + Secure Boot + podpisany UKI + dm-verity):
```
jądro 6.18 LTS (jadro/k1.config) ──► /init = k1-initramfs (src/initramfs)
   k1.rdzen=/dev/XXX albo =wbudowany (ISO: /rdzen.squashfs z initramfs przez loop) → SquashFS jako /rdzen
   (ro) w korzeniu-tmpfs (ro) → switch_root   [ISO: firmware UEFI/BIOS → GRUB → jądro; ADR 0010]
 ──► /rdzen/init = PID 1 (src/init): sysctl, montowania pod /rdzen/jadro, sieroty, sygnały, wyłączanie
 ──► /rdzen/system/nadzorca (src/nadzorca, root): usługi w klatkach (k1_klatka), kanały UNIX, logi, restarty
       ├─ wejscie (100): evdev (fd 4… od nadzorcy) → k1_zdarzenie → kanał do ekranu
       ├─ ekran   (101): DRM master (fd 4), kompozytor: tapeta + okna + panele (memfd) + cienie + kursor
       ├─ skarbiec(103): jedyny z partycją „K1-DANE” (fd 4), kanały klientów od nadzorcy (SKARB_KLIENT);
       │          start PRZED ekranem; ekran fd 5 (ustawienia), Notatki fd 4 (notatka)
       ├─ gui     (102): powłoka — górny pasek, dolny pasek, menu Start; fd 4 → nadzorca (URUCHOM, WYLACZ),
       │          fd 5 ← nadzorca (BATERIA, jednokierunkowo)
       └─ aplikacje na prośbę gui: zegar (200), notatki (201, tylko /dane_uzytkownika/notatki), ustawienia (202),
          zasoby (203; fd 4: statystyki cgroup od nadzorcy, jednokierunkowo)
```
Planowane (SPEC §7–§9, ADR 0026): pulpit z ikonami i przypinaniem (0.7.5), Pliki — wspólne dokumenty,
Kosz, okno wyboru pliku, dekodery w jednorazowych procesach (0.8.0), `siec` (M7), strefy (M8),
Gatekeeper dla stref (M9). Przepływ plików (ADR 0027): Pliki = jedyny węzeł, oryginał zawsze
w Plikach, strefy dostają kopie; zgoda = gest w interfejsie rysowanym przez K1; pochodzenie przy pliku.

Stan: M5a (0.5.2) — dyski SATA/NVMe, GPT, USB (xHCI/EHCI/OHCI, HID, mass storage) (ADR 0013);
0.5.3 — wskaźnik baterii (nadzorca czyta sysfs `power_supply` → gui fd 5, ADR 0014; w QEMU bateria
z tablicy ACPI `testy/acpi/bateria.asl`, `bateria_aml` w `scripts/wspolne.sh`); 0.5.4–0.5.5 — płynny
obraz (MTRR/PAT, DIRTYFB z obszarami), Ustawienia → Bateria; 0.5.6 — **skarbiec** (Blob Vault, ADR 0015:
własny format na partycji GPT „K1-DANE”, Monocypher), trwałe ustawienia i notatka, obraz
`k1-<wersja>-usb.img.xz` (ADR 0016). 0.5.7 — **M5e**: hasło (Ustawienia → Hasło, `SKARB_HASLO`) i ekran hasła `src/logowanie/` (rola ekranu
`LOGOWANIE`, nadzorca uruchamia go zamiast gui, gdy skarbiec zgłosi `SKARB_STAN` zablokowany; ADR 0017).
0.5.8 — logo przy starcie (GRUB `background_image` z `narzedzia/ekran_startowy.c`), K1-DANE szukana też po
starcie, wiele notatek. 0.5.9 — **M5f: instalator** (ADR 0018): okno `src/aplikacje/instalacja/` (UID 204) +
program zapisujący `src/instalator/` (UID 105, tylko urządzenia od nadzorcy), GPT `K1-EFI` 512 MiB + `K1-DANE`
do końca dysku; nadzorca woli K1-DANE z dysku wewnętrznego; GRUB bierze jądro z partycji `$cmdpath`;
ekran: limit 60 kl./s bez page-flipa, cień z tablicy, łączenie obszarów (przesuwanie okien ~10× taniej).
0.6.0 — **Fundament**: nadzorca w modułach (`nadzorca.h`; etapy startu `przejdz()`/`etap_startu()` w `skarb.c`;
uevent w `urzadzenia.c`), wspólny odbiór `k1_odbierz_komunikat()` (`src/wspolne/k1_odbior.c` — używaj go w nowym
kodzie zamiast ręcznego `k1_kanal_odbierz` + sprawdzeń), historia baterii (`k1_bat_arch`, nadzorca = klient skarbca
`K1_WL_SYSTEM`), biblioteka testów `testy/qemu/k1qemu.py` (klasa `Maszyna`; nowe testy QEMU pisz na niej; od 0.7.3 **bez
`time.sleep`** — czekaj na log, `klik` czeka na potwierdzenie ekranu, `zrzut(nazwa, gdy=…)` na stan ekranu,
`cisza` dla „nic się nie dzieje”; `docs/TESTY.md`),
`testy.sh` równolegle z KVM. **Nie edytuj `scripts/testy.sh` ani innych skryptów, gdy testy działają w tle**
(powłoka czyta skrypt w trakcie — przebieg staje się niewiarygodny).
0.6.2 — **M6a** (ADR 0021): manifest w K1-EFI na 47 MiB (`k1_manifest.c`, FAT 47 MiB), podpis EdDSA (Monocypher)
narzędziem `narzedzia/podpisz.c` z sekretu CI `K1_KLUCZ_PODPISU` (właściciel dodaje sam — **nigdy nie proś o klucz w
czacie**); bez sekretu = wersja deweloperska (`build/gen/k1_klucz.h`: `K1_KLUCZ_JEST 0`). Instalator: skrót, podpis
(gdy klucz), zakaz starszej wersji. Kolejne kroki M6: b dm-verity, c A/B, d Secure Boot (MOK).
0.6.3 — **M6b**: `rdzen.squashfs` = SquashFS + drzewo dm-verity (`verity()` w `wspolne.sh`, `veritysetup
--no-superblock`, stała sól), skrót w `rdzen.squashfs.skrot` → `k1.rdzen_skrot=` w GRUB/QEMU (obowiązkowy; nowy
obraz testowy w QEMU = też `verity` i skrót). Initramfs czyta całe `/dev/dm-N` przed montowaniem. Start → Wyloguj
(`WYLACZ` a=2 → `zamknij_sesje()`), `SKARB_KLIENT` c / `SKARB_WYNIK` d bit 1 = K1-DANE na USB, `ZASOBY_BATERIA`.
0.7.0 — **M6c+M6d** (ADR 0022): wydania startują `k1.efi` = jądro z wbudowaną linią poleceń (`noinitrd`) i initramfs
z rdzeniem (`zbuduj-k1efi.sh`, `jadro/k1-zamkniete.config`, ten sam katalog jądra), podpisane `sbsign` (RSA z
`narzedzia/klucz_sb.py` — z sekretu albo publiczny klucz deweloperski; `faketime`). `bzImage` + `initramfs.cpio` tylko
dla `test-m1`. K1-EFI format 2 (`k1_uklad.h`): FAT32 280 MiB, blok układu (1 KiB), grubenv (`k1_slot/k1_nowy/k1_start/
k1_proba`), `/k1/a.img` `/k1/b.img` (128 MiB: wewn. FAT z k1.efi + manifest formatu 2 z identyfikatorem wydania
`/rdzen/system/wydanie` na 127 MiB); manifest formatu 1 na 47 MiB zostaje (aktualizacja z 0.6.2–0.6.3 starym
Instalatorem). Instalator: aktualizacja = wolne miejsce + `k1_nowy`; nadzorca `rozruch.c` potwierdza po 15 s
(to samo wydanie) albo zgłasza wycofanie; `efi.c`: efivarfs na chwilę, `MokNew`/`MokAuth` (SHA-256 `k1_sha256.c`),
tylko klucz wydania. Testy: `test-m6.sh` (`uruchom-qemu.sh --zmienne-uefi`, OVMF secboot, `virt-fw-vars --add-mok`;
MokManager: klawisz w 10 s, Enroll MOK, hasło „kons”). Zmiana zawartości obrazu na pendrivie w teście: nie psuj
miejsca A (z niego startuje) — psuj wolne miejsce w wewnętrznym FAT (pokrywa je skrót).
Pierwszy laptop właściciela: **ThinkPad X230** (8 GB, SATA/AHCI, PS/2, EHCI+xHCI, Intel HD 4000 przez
simpledrm, TPM 1.2); jedno konto na komputer (decyzja właściciela).

**System plików po starcie** (ADR 0004):
- `/` — tmpfs tylko do odczytu z dokładnie trzema katalogami: `rdzen`, `aplikacje`, `dane_uzytkownika`.
- `/rdzen` — SquashFS ro,nosuid,nodev: `init`, `system/<usługa>`, `jadro/{dev,proc,sys}`.
- `/rdzen/jadro/dev` devtmpfs (noexec), `/rdzen/jadro/proc` (`hidepid=invisible,subset=pid`),
  `/rdzen/jadro/sys` (ro) + `sys/fs/cgroup` (cgroup2).
- `/dane_uzytkownika` — jedyne miejsce zapisu, noexec,nosuid,nodev (do M5: tmpfs w RAM).
- `/dev/console` istnieje tylko w initramfs. Nie używaj ścieżek `/dev`, `/proc`, `/sys`, `/tmp`, `/etc`.

**Mapa komponentów:**

| Kod | Plik w obrazie | Dokument | Rola |
|---|---|---|---|
| `src/initramfs/` | `/init` (initramfs) | `komponenty/initramfs.md` | montuje rdzeń, przełącza korzeń |
| `src/init/` | `/rdzen/init` | `komponenty/init.md` | PID 1 |
| `src/nadzorca/` | `/rdzen/system/nadzorca` | `komponenty/nadzorca.md` | usługi w klatkach, kanały, logi, restarty (ADR 0007) |
| `src/wejscie/` | `/rdzen/system/wejscie` | `komponenty/wejscie.md` | klawiatura, mysz |
| `src/ekran/` + `src/grafika/` | `/rdzen/system/ekran` | `komponenty/ekran.md` | `main.c` (pętla, zmiana trybu), `scena.c` (składanie), `okna.c` (klienci, fokus, limity), `wskaznik.c` (wejście), `drm.c` (KMS, tryby); tapety i znak K1 |
| `src/gui/` | `/rdzen/system/gui` | `komponenty/gui.md` | powłoka pulpitu |
| `src/skarbiec/` | `/rdzen/system/skarbiec` | `komponenty/skarbiec.md` | Blob Vault: `magazyn.c` (format, Monocypher w `src/zewnetrzne/`), `main.c` (usługa); klient: `src/wspolne/k1_skarb_klient.c`, `k1_memfd.c` |
| `src/instalator/` | `/rdzen/system/instalator` | `komponenty/instalator.md` | zapis K1 na dysk (GPT `gpt.c`), uruchamiany na prośbę okna Instalatora; lista dysków: `src/nadzorca/dyski.c` |
| `src/aplikacje/*` | `/rdzen/aplikacje/*` | `komponenty/aplikacje.md` | Zegar, Notatki, Ustawienia, Zasoby, Instalator (`instalacja`) |
| `src/przybornik/` | (linkowane do gui i aplikacji) | ADR 0008, 0009 | rysowanie, tekst, okna, klient, czas PL, klawiatura PL |
| `narzedzia/czcionki.c` | (host, przy budowie) | ADR 0009 | FreeType → bitmapy IBM Plex (`build/gen/`) |
| `narzedzia/ekran_startowy.c` | (host) → `/boot/grub/start.png` | `WYGLAD.md` | logo startowe dla GRUB-a (zlib) |
| `src/wspolne/` | (linkowane) | — | `k1_log` (log, `k1_awaria`, `k1_koniec`), `k1_cmdline`, `k1_zdarzenie`, `k1_komunikat` (32 B, wszystkie kanały), `k1_kanal` (SEQPACKET, SCM_RIGHTS, SO_PASSCRED), `k1_klatka` (sandbox), `k1_uslugi.h` (UID-y, role) |
| `jadro/` | `bzImage` (testy), `k1.efi` (wydania) | `komponenty/jadro.md` | wersja, konfiguracja, linia poleceń, klucze, zamknięte jądro |
| `testy/system/` | `/rdzen/system/test-systemu` (tylko obraz testowy) | `TESTY.md` | sprawdzenia w systemie: `t_system.c` (M1), `t_uslugi.c` (M2–M3), `t_okna.c` (M4+), `t_sprzet.c` (M5a), `proba.c`, `wspolne.c`, nagłówek `k1_test.h` |

## 2. Niezmienniki — tego nie wolno złamać
Każdy z nich pilnuje test; jeśli zmiana go narusza, to jest zmiana architektury → ADR i decyzja.
1. **Nic nie jest jednocześnie zapisywalne i wykonywalne.** Korzeń i `/rdzen` ro; zapis tylko w noexec;
   `vm.memfd_noexec=2`; brak `BINFMT_SCRIPT` (żadnych `#!`) i `binfmt_misc`.
2. **Brak powłoki, terminali i interpreterów:** bez `/bin/sh`, getty, VT, pty/devpts, SysRq. Log tylko
   jednokierunkowo na port szeregowy (w developmencie).
3. **Jądro tylko przez fragment** `jadro/k1.config` na `allnoconfig`; każda linia sprawdzana. Bez modułów,
   swapu, hibernacji, io_uring, BPF, userfaultfd, kexec, 32-bit; lockdown confidentiality, Landlock, Yama.
4. **Programy: statyczne PIE z flagami SPEC §11** (`Makefile`, `-Werror`), sprawdzane przez
   `scripts/sprawdz-binaria.sh`. Bez zewnętrznych bibliotek bez wpisu i uzasadnienia w `docs/ZALEZNOSCI.md`
   (np. grafika bez libdrm — ioctl z nagłówków UAPI naszego jądra w `build/naglowki`).
5. **PID 1 nigdy nie wychodzi** — błąd krytyczny przy starcie → `k1_awaria()` (komunikat, 5 s, wyłączenie).
   **Usługi** przy błędzie → `k1_koniec()` (wyjście z kodem 1); awaria usługi nie może zatrzymać systemu.
6. **Każdy komunikat IPC sprawdzany pole po polu** po stronie odbiorcy (wersja, typ, długość, zakresy,
   pole zarezerwowane = 0), parser ma testy jednostkowe i fuzzing. Kanał od nadzorcy to zawsze fd 3.
7. **Jeden właściciel zasobu:** tylko `ekran` dotyka karty graficznej (DRM master), tylko `wejscie`
   urządzeń wejścia (EVIOCGRAB); urządzenia otwiera nadzorca i przekazuje deskryptory. Każda usługa
   w klatce (ADR 0007): własny UID, 0 capabilities, NO_NEW_PRIVS, Landlock (tylko własny program),
   seccomp (biała lista, kod 159 = zabronione wywołanie), cgroup; nowy syscall usługi = wpis w profilu
   w `src/nadzorca/main.c` + uzasadnienie; odbiorca kanału sprawdza UID nadawcy (`SO_PASSCRED`).
8. **Żadnych sekretów ani kodów klawiszy w logach** (najwyżej liczniki, ≤ 2 wpisy/s).
9. **Powtarzalny build:** `SOURCE_DATE_EPOCH`, stałe dane budowy jądra, `-frandom-seed`; brak `__DATE__`
   i losowości przy budowie (`scripts/sprawdz-powtarzalnosc.sh`).
10. **Fail closed:** nieudane ustawienie bezpieczeństwa (sysctl, montowanie) zatrzymuje start.

## 3. Przepisy

**Nowa usługa** (np. `gui`, `siec`):
1. `src/<nazwa>/main.c` — komentarz na górze: zadanie, granica zaufania, etap; `k1_log`/`k1_koniec`
   z prefiksem `K1-<NAZWA>`; kanały od nadzorcy na fd 3 (kolejne — ustal w ADR).
2. `Makefile` — cel `$(BIN)/<nazwa>` i dopisanie do `PROGRAMY`; `scripts/zbuduj-obraz.sh` — instalacja
   do `/rdzen/system/` (pętla w `przygotuj_rdzen`).
3. `src/nadzorca/main.c` — profil klatki (UID w `k1_uslugi.h`, syscalle, limity), wpis w tablicy usług
   i kanały; pierwsza linia `main()` usługi: `k1_klatka_w_usludze(TAG)`; próba klatki w teście (`t_proby`).
4. `testy/system/` — **zaktualizuj spis plików** (`t_inventory` w `t_system.c`), dodaj sprawdzenia
   funkcjonalne i **negatywne**; scenariusz QEMU w `scripts/test-mN.sh` (+ dopisz do `scripts/testy.sh`).
5. `docs/komponenty/<nazwa>.md` (cel, granica zaufania, uprawnienia, wejścia/wyjścia, zależności,
   syscalle, błędy, powierzchnia ataku, testy), ADR jeśli zmienia architekturę, `docs/RYZYKA.md`.

**Nowa aplikacja natywna** (0.7.1, wzór: `src/aplikacje/kalkulator/`): **jeden wiersz** `X(...)` w
`K1_APLIKACJE` (`src/wspolne/k1_uslugi.h`: numer, profil, UID, program, tag, nazwa, opis, ikona, na pasku)
i katalog `src/aplikacje/<program>/` (`main.c` z `struct k1_apl` + callbackami i `k1_apl_uruchom()`;
każdy `.c` w katalogu jest kompilowany). Z tej listy biorą się: usługa i profil klatki w nadzorcy, pulpit
(menu, pasek), nazwa okna w ekranie, Zasoby, instalacja w obrazie i spisy w testach w systemie. Dane spoza
okna — kanał zadeklarowany w tabeli `kanaly[]` (`src/nadzorca/uslugi.c`; wzór: Zasoby, fd 4); aplikacja
nie dostaje ścieżek. Do zrobienia ręcznie: ikona w `k1_rysuj.c` (`K1_IK_*`), scenariusz w `test-m4.sh`
(położenia elementów loguj — test klika według logu), opis w `docs/komponenty/aplikacje.md`. Liczby:
`k1_liczba.h`, przyciski: `k1_przycisk`, pole tekstowe: `k1_pole_klawisz`, kolory: `k1_motyw` / `K1_*` w
`k1_grafika.h`.

**Nowy protokół / komunikat:** nagłówek w `src/wspolne/` ze stałym układem (`_Static_assert` na rozmiar
i przesunięcia), funkcja sprawdzająca bez założeń o wyrównaniu (`memcpy`), testy w `testy/host/test_*.c`
(ASan/UBSan) i harness `testy/host/fuzz_*.c` (cele w `Makefile`: `testy-host`, `fuzz`), test negatywny
w systemie (wstrzyknięcie złych komunikatów, jak `t_malicious_ipc`).

**Opcja jądra:** znajdź ją w `build/zrodla/linux-*/**/Kconfig*` (zależności, `select`, `depends on`),
dopisz do `jadro/k1.config` z komentarzem po co, `scripts/zbuduj-jadro.sh` (przerwie, jeśli nie weszła),
przy wzmocnieniach `scripts/sprawdz-kspp.sh`, a odstępstwa opisz w `docs/komponenty/jadro.md`.
Zmiana wersji jądra: procedura w `jadro/README.md`.

**Wygląd:** tapety na hoście (`make build/obj/podglad_grafiki`, `podglad_grafiki W H -1 -1 0 WARIANT`,
PPM → `python3 -I scripts/ppm-na-png.py`), pulpit w QEMU + `scripts/zrzut-ekranu.sh` (mysz/klawisze:
monitor QEMU — `mouse_move`, `mouse_button`, `sendkey alt_r-a`); zrzuty pokazuj właścicielowi
(`SendUserFile`); ustalenia i paleta → `docs/WYGLAD.md`. Interfejs: neutralne tła, jeden akcent,
4-px siatka, Plex Sans/Serif, ikony liniowe z `k1_ikona`.

## 4. Testy — warstwy i pułapki
- **Host:** `make testy-host` (glibc + ASan/UBSan + `_FORTIFY_SOURCE=3`), `make fuzz` (clang, libFuzzer).
- **W systemie:** obraz testowy = produkcyjny + `/rdzen/system/test-systemu`; nadzorca uruchamia go po
  usługach, kończy się jego kodem, init wyłącza maszynę. Wynik: `K1-TEST: WYNIK: OK (n/n)`.
- **QEMU z zewnątrz:** `scripts/test-m1.sh` (start, testy w systemie, błędy startu — maszyna ma się sama
  wyłączyć bez „Kernel panic”), `scripts/test-m2.sh` (kursor: `mouse_move`, `sendkey`, `mouse_button`),
  `scripts/test-m4.sh` (pulpit i aplikacje — klikanie według położeń z logu, zrzuty sprawdzane po
  kolorach pikseli; start z ISO przez UEFI/OVMF i BIOS z `virtio-vga` → simpledrm), `scripts/test-m5.sh`
  (USB bez PS/2: `--maszyna ",i8042=off"`, `--qemu "-device …"`; `device_add`/`device_del`). Dyski i USB
  w teście w systemie: `SPRZET_TEST` w `test-m1.sh` (obrazy `sfdisk`: GPT „K1-DANE”, MBR). Wszystko:
  `scripts/testy.sh` (to uruchamia CI, ~6 min w TCG). Kanał testowy (fd 3 programu testowego):
  `TEST_POLACZ` (kanał do ekranu z dowolną rolą/UID), `TEST_PROBA` (program testowy w klatce
  usługi), `URUCHOM` (aplikacja).
- **Pułapki, które już wystąpiły:** usuń stary log/gniazdo **przed** startem QEMU w tle (wyścig ze starym
  logiem); pipe otwarty ponownie przez `/proc/<pid>/fd` nie jest pakietowy (a `O_DIRECT` przy open → EINVAL);
  rodzicem procesów usług jest nadzorca, nie init; musl ma `ioctl(int, int, …)` — rzutuj numer żądania
  na `int`; GCC `-Wformat-truncation` wymaga buforów na pełny `d_name`; niestabilny test = najpierw szukaj
  błędu w harnessie, potem w systemie, ale nigdy nie ignoruj.
  `SO_PASSCRED` ustawia się na końcu **odbiorcy** (`k1_kanal_utworz`: `pary[0]` dostaje ten, kto
  sprawdza); `RLIMIT_FSIZE` ogranicza też `ftruncate` memfd (SIGXFSZ = sygnał 25); `LO_FLAGS_AUTOCLEAR`
  odłącza loop przy zamknięciu ostatniego deskryptora — zamykaj po `mount`; filtr seccomp działa od
  `execve`, więc obejmuje start musl; `hidepid` daje ENOENT zamiast EACCES dla cudzych procesów. `read_file` w testach zwraca 0 przy
  sukcesie (nie liczbę bajtów); q35 ma wbudowany AHCI (`bus=ide.0`); HMP `mouse_move`/`sendkey` idą do
  ostatnio aktywnego urządzenia QEMU (z USB w maszynie — do USB).
  **Wydajności nie widać w QEMU (TCG):** emulator ignoruje typy pamięci (MTRR/PAT) — na X230 brak PAT
  dał niebuforowaną pamięć obrazu i zacinający się pulpit (0.5.4). allnoconfig wyłącza też opcje
  „default y” za `if EXPERT` (MTRR, PAT, ACPI_EC) — przy sprzęcie sprawdzaj je świadomie.
  `/proc` ma `subset=pid` — **`/proc/cmdline` nie istnieje** dla usług i nadzorcy (informację o nośniku
  bierz z sysfs). usb-storage czeka ~1 s, zanim pokaże dysk (pendrive w teście: `--pendrive`); w X230
  pendrive może zniknąć i wrócić (EHCI → xHCI) — K1-DANE szukamy też po starcie (0.5.8).
  **Ekran startowy (GRUB):** `background_image` rysuje się dopiero po wypisaniu czegoś (`echo ""`);
  `set color_normal=black/black` **po** `terminal_output gfxterm` (ono resetuje kolory) ukrywa kursor;
  `quiet` w linii poleceń wycisza „EFI stub: …” (na ekranie, nie tylko na porcie szeregowym).
  W QEMU fazy GRUB-a nie widać na zrzutach co sekundę — dodaj `sleep` do kopii grub.cfg (`mcopy -i obraz@@1M`).

## 5. Proces

**Start sesji:** `git fetch origin main`, gałąź od `origin/main` (gdy PR gałęzi jest scalony: `git checkout -B
<gałąź> origin/main`); zgłoszenia (`list_issues`, etykiety
`feedback`, `błąd`, `pomysł`); ostatni run „K1 — budowa i testy” na `main` (czerwony = napraw najpierw);
środowisko: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh && scripts/testy.sh` (~6 min).

**Kolejność etapów** (ADR 0019, 0020; zastępuje ADR 0011): M5 (do 0.5.9, z instalatorem) → **0.6.0 Fundament**
(nadzorca w modułach < 600 wierszy w main.c, jawna maszyna stanów startu, uevent zamiast odpytywania = dawne M5b,
wspólny odbiór komunikatów, wspólna biblioteka testów QEMU, testy równolegle, KVM w CI, historia baterii) →
**M6 Bezpieczne aktualizacje** (podpisane wydania, dm-verity, A/B w K1-EFI, Secure Boot z kluczem MOK) →
(ADR 0026, od 0.7.3) **0.7.5 Pulpit** (ikony na pulpicie jak w Windows w stylu K1, menu pod prawym przyciskiem,
przypinanie/odpinanie na pasku, układ w skarbcu) → **0.8.0 Pliki** (wspólne dokumenty w skarbcu, aplikacja Pliki,
Kosz: 30 dni albo ręcznie, okno wyboru pliku rysowane przez K1, import z pendrive'a czytnikiem FAT w klatce,
dekoder PNG/JPEG w jednorazowym procesie, tapeta z własnego zdjęcia) → **M7 Sieć** (e1000e + DHCP, potem Wi-Fi;
test: nic nie nasłuchuje) → **M8 Strefy** (mikro-VM na obcą aplikację, zaufana ramka w kolorze
strefy, przełączniki: stała/jednorazowa i pełna izolacja/szybka grafika; pierwsza — przeglądarka) →
**M9 Pliki ze stref** (Gatekeeper dla stref: „Do K1”, „Wyślij do…”, schowek) → M10 reszta (recovery, TPM) → M12 audyt. X230 = maszyna
testowa; szybka grafika i gry projektowane pod nowszy sprzęt (decyzja właściciela).

**CI na PR-ze:** dodaj etykietę `testy` (`issue_write`, labels) — workflow buduje i testuje PR bez scalania
(bez tagu i wydania). `workflow_dispatch` jest dla Claude niedostępny (403).

**Zasady etapu (SPEC §2):** jeden etap naraz, następny dopiero z powtarzalnym testem; nie wymyślaj nazw
syscalli, flag, opcji Kconfig ani zachowań jądra — sprawdzaj w źródłach (`build/zrodla/`), nagłówkach
(`build/naglowki/`), man-pages, a niepewność oznaczaj wprost; statusy ZAIMPLEMENTOWANE / PRZETESTOWANE
(tylko z dowodem) / PLANOWANE; konflikt SPEC z Linuksem → najmniejsza bezpieczna korekta, ADR, dopisek
`[korekta: ADR NNNN]` w SPEC.

**Przed scaleniem:** `make` bez ostrzeżeń, `sh -n` na skryptach, `scripts/testy.sh` lokalnie, przy zmianach
w budowie `scripts/sprawdz-powtarzalnosc.sh` (nie commituj w trakcie — data commita wchodzi do buildu).
**Wersja** (`VERSION`, raz na paczkę): etap → MINOR, poprawka/aktualizacja jądra → PATCH, docs/CI → bez
zmiany; wpis w `CHANGELOG.md`. Łącz zmiany w paczki — CI to ~10 min, a właściciel ma ograniczony limit Claude.

**Scalanie** (repo prywatne, zgoda właściciela): commit po angielsku → `git push -u origin <gałąź>` →
PR po polsku (najpierw co widzi użytkownik, potem technika, `Closes #N`) → squash. Tylko docs: `[skip ci]`
w tytule commita scalenia (workflow i tak startuje tylko przy zmianach w `src/`, `jadro/`, `testy/`,
`scripts/`, `Makefile`, `VERSION`, workflow).

**Squash zawsze z własnym `commit_message`** (bez „[skip ci]”, gdy zmiana dotyczy systemu): domyślny opis
sklejają wszystkie commity gałęzi — „[skip ci]” z wcześniejszego commita docs wyłączył CI dla 0.5.4 (brak
wydania). Gałąź po scaleniu: `git checkout -B <gałąź> origin/main` (wypychanie z nadpisaniem jest
zablokowane — wtedy `git merge -s ours origin/<gałąź>` i zwykły push).

**CI:** sprawdzaj przez `send_later` (~15 min po scaleniu); padł → `get_job_logs` (`failed_only`), napraw
(PATCH); udał się → CI zakłada tag `k1-v<wersja>` i wydanie (Releases) z `k1-<wersja>.iso` — to link
do pobrania dla właściciela. **Logi testów są zawsze** (decyzja właściciela, 0.5.1), niezależnie od
limitu artefaktów: w konsoli zadania (krok „Logi testów”, grupa na plik, bez wierszy kursora — czytaj
`get_job_logs` z `job_id`), w podsumowaniu zadania (linie `[OK]`/`[BŁĄD]`), w wydaniu
(`k1-<wersja>-logi.tar.gz`: logi, zrzuty, sumy) i jako artefakt (7 dni). Obraz jako artefakt tylko
na PR-ach (na `main` jest w wydaniu). Artefakty mają wspólny limit miejsca konta; stare (spoza `k1-…`) usuwa
workflow „Sprzątanie artefaktów” (uruchamia się przy zmianie jego pliku na `main` — Claude nie
może wywołać `workflow_dispatch`).

**Raport po etapie (SPEC §2.11):** zaktualizuj `docs/DO_ZROBIENIA.txt`; po polsku, prosto (skutki, nie
mechanika): cel i kryteria, co zrobiono, dowody (log, testy, zrzuty), testy negatywne, ograniczenia,
statusy, pytania do właściciela. Bez wklejania całych plików; linki pełnym URL-em.
**Każdy raport o nowej wersji kończ bezpośrednim linkiem do ISO** (decyzja właściciela, 0.5.8: „zawsze
dawaj link do iso”): `https://github.com/Conrad3k/Kons-OS/releases/download/k1-v<wersja>/k1-<wersja>.iso`
(obok link do `k1-<wersja>-usb.img.xz`). Przed podaniem sprawdź, że wydanie istnieje (`get_release_by_tag`);
jeśli CI jeszcze buduje — napisz to i podaj link po sprawdzeniu.

**Właściciel** (nie programista, pisze po polsku, jedyny z prawdziwym sprzętem): decyzje produktowe
(wygląd, zachowanie, aplikacje) — jego; techniczne — Twoje, z krótkim uzasadnieniem. Uwagi, których nie
robisz od razu → zgłoszenie (`issue_write`). Przy błędach dopytaj: wersja, gdzie uruchomione, co widać,
czego się spodziewał.

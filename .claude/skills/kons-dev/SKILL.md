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

**Łańcuch startu** (stan: M3; od M10 — UEFI + Secure Boot + podpisany UKI + dm-verity):
```
jądro 6.18 LTS (jadro/k1.config) ──► /init = k1-initramfs (src/initramfs)
   k1.rdzen=/dev/XXX → SquashFS jako /rdzen (ro) w korzeniu-tmpfs (ro) → switch_root
 ──► /rdzen/init = PID 1 (src/init): sysctl, montowania pod /rdzen/jadro, sieroty, sygnały, wyłączanie
 ──► /rdzen/system/nadzorca (src/nadzorca, root): usługi w klatkach (k1_klatka), kanały UNIX, logi, restarty
       ├─ wejscie (src/wejscie): evdev → komunikaty k1_zdarzenie → fd 3
       └─ ekran   (src/ekran):   jedyny DRM master; tapeta+logo+kursor (src/grafika), page flip
```
Planowane usługi (SPEC §7–§9): `gui` (M4, rysuje w pamięci współdzielonej, bez GPU), Blob Vault (M5),
dekodery w jednorazowych procesach (M6), `siec` (M7), Gatekeeper (M9).

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
| `src/ekran/` + `src/grafika/` | `/rdzen/system/ekran` | `komponenty/ekran.md` | KMS, składanie klatek, kursor |
| `src/wspolne/` | (linkowane) | — | `k1_log` (log, `k1_awaria`, `k1_koniec`), `k1_cmdline`, `k1_zdarzenie`, `k1_komunikat` (32 B, wszystkie kanały), `k1_kanal` (SEQPACKET, SCM_RIGHTS, SO_PASSCRED), `k1_klatka` (sandbox), `k1_uslugi.h` (UID-y, role) |
| `jadro/` | `bzImage` | `komponenty/jadro.md` | wersja, konfiguracja, linia poleceń, klucze |
| `testy/system/` | `/rdzen/system/test-systemu` (tylko obraz testowy) | `TESTY.md` | sprawdzenia w systemie |

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
4. `testy/system/k1_test_systemu.c` — **zaktualizuj spis plików** (`t_inventory`), dodaj sprawdzenia
   funkcjonalne i **negatywne**; scenariusz QEMU w `scripts/test-mN.sh` (+ dopisz do `scripts/testy.sh`).
5. `docs/komponenty/<nazwa>.md` (cel, granica zaufania, uprawnienia, wejścia/wyjścia, zależności,
   syscalle, błędy, powierzchnia ataku, testy), ADR jeśli zmienia architekturę, `docs/RYZYKA.md`.

**Nowy protokół / komunikat:** nagłówek w `src/wspolne/` ze stałym układem (`_Static_assert` na rozmiar
i przesunięcia), funkcja sprawdzająca bez założeń o wyrównaniu (`memcpy`), testy w `testy/host/test_*.c`
(ASan/UBSan) i harness `testy/host/fuzz_*.c` (cele w `Makefile`: `testy-host`, `fuzz`), test negatywny
w systemie (wstrzyknięcie złych komunikatów, jak `t_malicious_ipc`).

**Opcja jądra:** znajdź ją w `build/zrodla/linux-*/**/Kconfig*` (zależności, `select`, `depends on`),
dopisz do `jadro/k1.config` z komentarzem po co, `scripts/zbuduj-jadro.sh` (przerwie, jeśli nie weszła),
przy wzmocnieniach `scripts/sprawdz-kspp.sh`, a odstępstwa opisz w `docs/komponenty/jadro.md`.
Zmiana wersji jądra: procedura w `jadro/README.md`.

**Wygląd:** podgląd na hoście (`make build/obj/podglad_grafiki`, PPM → `python3 -I scripts/ppm-na-png.py`),
potem QEMU + `scripts/zrzut-ekranu.sh`; pokaż właścicielowi; ustalenia → `docs/WYGLAD.md`.

## 4. Testy — warstwy i pułapki
- **Host:** `make testy-host` (glibc + ASan/UBSan + `_FORTIFY_SOURCE=3`), `make fuzz` (clang, libFuzzer).
- **W systemie:** obraz testowy = produkcyjny + `/rdzen/system/test-systemu`; nadzorca uruchamia go po
  usługach, kończy się jego kodem, init wyłącza maszynę. Wynik: `K1-TEST: WYNIK: OK (n/n)`.
- **QEMU z zewnątrz:** `scripts/test-m1.sh` (start, testy w systemie, błędy startu — maszyna ma się sama
  wyłączyć bez „Kernel panic”), `scripts/test-m2.sh` (monitor QEMU: `mouse_move`, `sendkey`,
  `mouse_button`; zrzuty sprawdzane po kolorach pikseli). Wszystko: `scripts/testy.sh` (to uruchamia CI).
- **Pułapki, które już wystąpiły:** usuń stary log/gniazdo **przed** startem QEMU w tle (wyścig ze starym
  logiem); pipe otwarty ponownie przez `/proc/<pid>/fd` nie jest pakietowy (a `O_DIRECT` przy open → EINVAL);
  rodzicem procesów usług jest nadzorca, nie init; musl ma `ioctl(int, int, …)` — rzutuj numer żądania
  na `int`; GCC `-Wformat-truncation` wymaga buforów na pełny `d_name`; niestabilny test = najpierw szukaj
  błędu w harnessie, potem w systemie, ale nigdy nie ignoruj.

## 5. Proces

**Start sesji:** `git fetch origin main`, gałąź od `origin/main`; zgłoszenia (`list_issues`, etykiety
`feedback`, `błąd`, `pomysł`); ostatni run „K1 — budowa i testy” na `main` (czerwony = napraw najpierw);
środowisko: `scripts/przygotuj-srodowisko.sh && scripts/zbuduj.sh && scripts/testy.sh` (~6 min).

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

**CI:** sprawdzaj przez `send_later` (~10 min po scaleniu); padł → `get_job_logs` (`failed_only`), napraw
(PATCH); udał się → CI zakłada tag `k1-v<wersja>`. Artefakty mają wspólny limit miejsca konta — gdy pełny,
wysyłka pada bez blokowania tagu; powiedz właścicielowi, że stare artefakty usuwa się w Actions.

**Raport po etapie (SPEC §2.11):** zaktualizuj `docs/DO_ZROBIENIA.txt`; po polsku, prosto (skutki, nie
mechanika): cel i kryteria, co zrobiono, dowody (log, testy, zrzuty), testy negatywne, ograniczenia,
statusy, pytania do właściciela. Bez wklejania całych plików; linki pełnym URL-em.

**Właściciel** (nie programista, pisze po polsku, jedyny z prawdziwym sprzętem): decyzje produktowe
(wygląd, zachowanie, aplikacje) — jego; techniczne — Twoje, z krótkim uzasadnieniem. Uwagi, których nie
robisz od razu → zgłoszenie (`issue_write`). Przy błędach dopytaj: wersja, gdzie uruchomione, co widać,
czego się spodziewał.

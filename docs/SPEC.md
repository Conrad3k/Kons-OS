# Specyfikacja Kons K1 Sovereign

> Dokument nadrzędny projektu (zasada §2.1). Zmieniany **tylko przez ADR** (`docs/adr/`).
> Wersja 1 — przyjęta 2026-11-06, wraz z rozpoczęciem K1 (ADR 0001).
> Korekty wynikające z rzeczywistości Linuksa (zasada §2.8) są opisane w ADR-ach, a w tekście
> poniżej oznaczone dopiskiem **[korekta: ADR NNNN]**. Treść poza tymi dopiskami jest oryginalna.

## 0. Rola i kontekst
Jesteś głównym architektem i inżynierem systemu Kons K1 Sovereign (w skrócie K1): minimalistycznego, bezpiecznego systemu operacyjnego na jądrze Linux, z własnym userspace w C. Pracujesz jako agent w repozytorium conrad3k/kons-os: sam piszesz kod, kompilujesz, uruchamiasz w QEMU, commitujesz.

Właściciel projektu nie jest programistą (studiuje psychologię) i pisze po polsku. Rozmawiaj z nim po polsku i prosto, opisując skutki, a nie mechanikę. Decyzje produktowe podejmuje on. Techniczne podejmujesz Ty i krótko mówisz, co wybrałeś i dlaczego. Tylko on ma dostęp do prawdziwego sprzętu (testy na laptopie, pendrive).

Historia repo: wcześniej był tu „Kons-OS”, dystrybucja na bazie Debiana (live-build, XFCE), ostatnia wersja 0.1.2 (tag v0.1.2). Porzucamy ją, ale nie kasujemy.

## 1. Środowisko pracy (stan z poprzedniej sesji, zweryfikuj)
- Kontener chmurowy: Ubuntu 24.04, root, 4 CPU, ~15 GB RAM, ~30 GB dysku. Jest efemeryczny, więc wszystko ważne commituj i pushuj.
- Brak /dev/kvm: QEMU działa tylko w emulacji (TCG), wolno, ale działa. Sprawdzone: obraz się uruchamia, a zrzuty ekranu można robić przez monitor QEMU (screendump przez gniazdo unix) i oglądać.
- qemu-system-x86 i ovmf instaluje się z apt Ubuntu.
- Sieć działa tylko po HTTPS przez proxy. Dostępne: apt Ubuntu, deb.debian.org, security.debian.org, GitHub. Nie sprawdzano: cdn.kernel.org, git.kernel.org, musl.libc.org. Źródła jądra i musl można wziąć z pakietów Debiana/Ubuntu albo z oryginalnych serwerów. Jeśli host jest zablokowany, zaproponuj zmianę ustawień środowiska zamiast obchodzić blokadę.
- GitHub Actions: repo prywatne, minuty są ograniczone, więc CI buduje tylko main. **[korekta: ADR 0040 — od 2026-11-14 repo publiczne, minuty bez limitu; CI dalej buduje main i PR-y z etykietą „testy”]** Właściciel ma ograniczony limit użycia Claude: pracuj oszczędnie, łącz zmiany, nie powtarzaj bez potrzeby długich buildów.
- W repo są CLAUDE.md i skill .claude/skills/kons-dev z poprzedniej wersji. Zaktualizuj je pod K1.

## 2. Zasady pracy
1. Ta specyfikacja jest nadrzędna. Każda zmiana architektury wymaga ADR (docs/adr/NNNN-*.md) z uzasadnieniem i konsekwencjami.
2. Pracuj nad jednym milestone'em naraz. Następny zaczynaj dopiero, gdy bieżący ma działający, powtarzalny test (skrypt do ponownego uruchomienia).
3. Nie wymyślaj nazw funkcji, syscalli, flag, opcji Kconfig ani zachowań jądra. Sprawdzaj w man-pages, dokumentacji kernel.org, nagłówkach i źródłach. Niepewność oznaczaj wprost.
4. Używaj trzech statusów:
   - ZAIMPLEMENTOWANE: kod istnieje,
   - PRZETESTOWANE: uruchomione, z dowodem (log z portu szeregowego, zrzut ekranu, wynik testu),
   - PLANOWANE.
   Nie twierdź, że coś działa, jeśli tego nie uruchomiłeś.
5. Mechanizm bezpieczeństwa to nie gwarancja. Przy każdym opisz ograniczenia i dodaj test negatywny (co ma się NIE udać).
6. Każdy komponent dokumentuj: cel, granica zaufania, uprawnienia (UID, capabilities, urządzenia), wejścia i wyjścia, zależności, syscalle, obsługa błędów, powierzchnia ataku.
7. Pisz małe, audytowalne moduły w C. Każda zewnętrzna biblioteka wymaga uzasadnienia w docs/ZALEZNOSCI.md. Rust jest dopuszczalny dla komponentów parsujących niezaufane dane, tylko przez ADR.
8. Gdy specyfikacja kłóci się z rzeczywistością Linuksa: opisz problem, wybierz najmniejszą bezpieczną korektę, zapisz ADR i kontynuuj.
9. Prowadź w repo:
   - docs/SPEC.md (ta specyfikacja, zmieniana tylko przez ADR),
   - docs/adr/,
   - docs/ZALEZNOSCI.md,
   - docs/RYZYKA.md (znane ograniczenia i ryzyka),
   - CHANGELOG.md.
10. Przepływ pracy w repo:
   - gałąź robocza → commit (po angielsku) → PR po polsku (najpierw to, co widzi użytkownik, potem technika),
   - squash merge do main bez pytania (repo prywatne, właściciel się zgodził), **[korekta: ADR 0040 — repo publiczne; zgoda właściciela na scalanie bez zmian]**
   - wersje K1 w SemVer od 0.1.0, tagi k1-vX.Y.Z (stare tagi v0.1.x należą do wersji debianowej).
11. Po każdym milestone krótki raport w czacie: cel i kryteria, co zrobiono, dowody, testy negatywne, ograniczenia, statusy. Nie wklejaj całych plików, są w repo.

## 3. Cel i model zagrożeń
K1 nie jest dystrybucją Linuksa. To własne środowisko: własny init, własny interfejs graficzny, własny magazyn danych i rygorystyczna izolacja aplikacji.

Priorytety: bezpieczeństwo, minimalna baza zaufania (TCB), ograniczanie skutków włamań, także luk zero-day. Coraz więcej takich luk znajdują autonomiczne agenty AI. Właściciel wskazuje na doniesienia o setkach krytycznych luk znalezionych w Linuksie przez agentów LLM.

Nie obiecujemy odporności na zero-day. Projektujemy tak, by przejęcie jednego komponentu nie dawało przejęcia systemu. Jądro jest wspólną granicą zaufania świata natywnego, dlatego najbardziej ryzykowne obce programy dostają docelowo osobne jądro (§8).

## 4. Architektura
Dwa światy:
- Świat natywny K1: nasz kod w C, statycznie linkowany (musl). Bez powłoki, bez X11/Wayland, bez systemd, bez drzewa FHS.
- Strefa zgodności: „równoległy Linux” tworzony na żądanie dla obcych aplikacji (przeglądarka itd.), ze standardowymi bibliotekami Linuksa, w pełni odizolowany. Pliki przechodzą między światami tylko przez Gatekeepera i tylko za zgodą użytkownika. **[korekta: ADR 0042 — strefa to mikro-maszyna KVM prowadzona przez nasz VMM `strefa` (jeden proces w klatce na maszynę), z jądrem strefy i obrazem strefy w rdzeniu (`/rdzen/strefy/`) [korekta: ADR 0050 — obraz przeglądarki leży w miejscu A/B zaraz za k1.efi, w rdzeniu tylko jego skrót; sprawdza go dm-verity]; niezmienniki 1–4 (W^X, brak interpreterów, jądro z fragmentu, flagi SPEC §11) dotyczą świata natywnego — we wnętrzu strefy wolno JIT, obce programy strefy nie podlegają §11]**

Warstwy:
UEFI + Secure Boot → podpisany UKI (jądro + initramfs + cmdline) → jądro Linux (własna, minimalna konfiguracja) → K1 Core (init, nadzorca, ekran, GUI, wejście, Gatekeeper, Blob Vault, sieć, dekodery) → aplikacje natywne i strefy zgodności.

## 5. System plików świata natywnego
- /rdzen: system, obraz SquashFS tylko do odczytu, chroniony dm-verity.
- /aplikacje: natywne aplikacje K1, tylko do odczytu, podpisane. **[korekta: ADR 0008 — do M10 (podpisy) wbudowane aplikacje leżą w obrazie rdzenia jako `/rdzen/aplikacje/`; `/aplikacje` jest pusty]**
- /dane_uzytkownika: Blob Vault, jedyne miejsce zapisu. Zaszyfrowane (fscrypt albo dm-crypt, decyzja w ADR), montowane z MS_NOEXEC|MS_NOSUID|MS_NODEV. **[korekta: ADR 0015 — Blob Vault to własny format na surowej partycji GPT „K1-DANE”, szyfrowany i uwierzytelniany w przestrzeni użytkownika (usługa `skarbiec`, Monocypher: XChaCha20-Poly1305, Argon2id), bez systemu plików w jądrze; `/dane_uzytkownika` zostaje tmpfs na dane tymczasowe]**
- Nie ma /usr, /lib, /lib64, /etc, /home, /bin, /sbin. Programy są linkowane statycznie, więc nie potrzebują bibliotek współdzielonych.
- Korekta: jądro wymaga punktów montowania dla devtmpfs, proc, sysfs i cgroup2 oraz /dev/console przy starcie. Umieść je pod /rdzen (np. /rdzen/jadro/{dev,proc,sys}). Jeśli to niemożliwe, dopuść minimalny wyjątek (ADR po sprawdzeniu).
  **[korekta: ADR 0004]** Punkty montowania są w `/rdzen/jadro/{dev,proc,sys}` (cgroup2 w `/rdzen/jadro/sys/fs/cgroup`). `/dev/console` istnieje wyłącznie w initramfs (jądro otwiera go przed uruchomieniem `/init`); po przełączeniu korzenia nie ma go w systemie. Korzeń `/` to mały tmpfs tylko do odczytu z trzema katalogami. Do M5 `/dane_uzytkownika` to tmpfs w RAM (bez szyfrowania).

## 6. Start, integralność, aktualizacje
- Własny minimalny initramfs (C): aktywuje dm-verity z root hash z podpisanej cmdline w UKI, montuje SquashFS tylko do odczytu, robi switch_root. **[korekta: ADR 0010 — do M10 obraz ISO do maszyn wirtualnych startuje przez GRUB (UEFI i BIOS), a rdzeń jest wbudowany w initramfs i montowany przez loop]**
- Secure Boot z własnymi kluczami (test w QEMU z OVMF).
- Podpisane obrazy i ochrona przed downgrade'em (licznik wersji).
- Atomowe aktualizacje A/B z powrotem do poprzedniego zweryfikowanego obrazu.
- Recovery: osobny minimalny, podpisany obraz, bez powłoki, z GUI.
- Bez swapu i hibernacji (albo tylko szyfrowane, decyzja w ADR). **[korekta: ADR 0003 — bez swapu i hibernacji: `CONFIG_SWAP=n`, `CONFIG_HIBERNATION=n`]**

## 7. Rdzeń natywny
- /rdzen/init (PID 1): montowania, ustawienia bezpieczeństwa, start nadzorcy, sygnały i sieroty, kontrolowana obsługa błędów krytycznych. Możliwie mały, bez logiki aplikacyjnej.
- Nadzorca: uruchamia i restartuje usługi. Awaria GUI nie może powodować kernel panic.
- Brak terminala:
  - nie ma /bin/sh, basha, SSH, emulatorów terminala, getty, dostępnych TTY ani devpts,
  - w developmencie tylko jednokierunkowy log na port szeregowy (wydanie: log tylko w pamięci, raport na pendrive na prośbę użytkownika; błąd krytyczny startu — ekran awarii) [korekta: ADR 0059],
  - brak terminala sam w sobie nie jest zabezpieczeniem.
- Uprawnienia:
  - osobne UID/GID dla usług i minimalne capabilities,
  - PR_SET_NO_NEW_PRIVS,
  - seccomp-BPF (SECCOMP_MODE_FILTER, biała lista),
  - cgroups v2,
  - ograniczony dostęp do urządzeń.
- LSM: domyślnie Landlock + Yama + lockdown. SELinux albo AppArmor tylko przez ADR.
- Grafika rozdzielona na procesy:
  - ekran: jedyny proces z DRM/KMS; ustawia tryb, składa bufory, robi page flip, **[korekta: ADR 0049 — od 0.10.9 jądro ma i915; obrazem (węzeł card) dalej steruje
    tylko ekran; węzeł render karty dostają tylko dekoder filmów i pośrednik grafiki, każdy w klatce, jeden na strefę,
    przy włączonym przełączniku sesji]**
  - gui: pulpit, okna i menu; rysuje do pamięci współdzielonej, bez dostępu do GPU,
  - wejście: czyta evdev i przekazuje zdarzenia do ekranu.
  Renderer własny, programowy, w C. LVGL dopuszczalny po uzasadnieniu. Przejęcie gui nie może dawać dostępu do danych ani systemu.
- Sieć: osobna usługa. Na start Ethernet + DHCP; Wi-Fi później, w izolacji. **[korekta: ADR 0039 — jądro z IPv4 (bez IPv6
  i bez zapory); gniazdo sieciowe otwiera tylko nadzorca, usługa „siec” w klatce dostaje deskryptor gniazda pakietowego
  i o adres oraz zegar prosi nadzorcę; jądro nie wysyła ICMP; żadna klatka nie ma wywołania `socket`.]**
- Dekodery plików (PNG, JPG, PDF…):
  - każde zadanie to jednorazowy proces z seccomp-BPF, RLIMIT_AS/CPU/FSIZE i limitami cgroups,
  - bez sieci, GPU, dostępu do magazynu i innych procesów,
  - wejście jako deskryptor, wyjście jako memfd z F_SEAL_*.

## 8. Strefa zgodności (obce aplikacje, przeglądarka)
- Osobny obraz compat.squashfs z bibliotekami ze zweryfikowanego źródła (np. podpisane pakiety Debiana), chroniony dm-verity. Montowany tylko wewnątrz strefy, nigdy w świecie natywnym.
- Każda sesja (np. „bank”, „prywatna”, „ogólna”) to osobna, jednorazowa instancja: własny profil, cookies, cache i IPC, bez współdzielenia. Po zamknięciu instancja i jej dane są niszczone. Trwały profil może istnieć tylko jako zaszyfrowany obiekt w Blob Vault, jeśli użytkownik tak zdecyduje.
- Obraz: przeglądarka nie działa bez serwera wyświetlania. Wewnątrz strefy (i tylko tam) działa więc minimalny kompozytor Wayland, który oddaje klatki do K1 naszym protokołem (pamięć współdzielona, walidacja rozmiaru, wymiarów, formatu pikseli i deskryptorów). Klawiaturę i mysz K1 przekazuje do strefy. Świat natywny pozostaje bez Wayland/X11.
- Strefa nie ma dostępu do /dev/dri. Renderowanie jest programowe (llvmpipe/pixman); każdą ścieżkę renderowania trzeba zweryfikować.
- Pliki:
  - strefa zapisuje tylko do swojego katalogu tymczasowego,
  - „Pobrane” to tmpfs noexec/nosuid/nodev obserwowany przez Gatekeepera,
  - do Blob Vault plik trafia tylko po zgodzie użytkownika (tłumaczenie na obiekt tylko wtedy, gdy potrzebne),
  - w drugą stronę: użytkownik wybiera obiekt, a Gatekeeper przekazuje do strefy jedną kopię.
- Dwa zaplecza izolacji z tym samym interfejsem:
  - A: namespaces (user, mount, pid, ipc, net, uts) + pivot_root + seccomp + Landlock + cgroups v2. Testowalne bez KVM, używane w developmencie.
  - B, docelowe dla przeglądarki: mikro-VM KVM z własnym minimalnym jądrem, więc luka w jądrze strefy nie daje dostępu do jądra K1. Wybór VMM w ADR. Wymaga KVM: testy na sprzęcie właściciela albo na runnerze z KVM (do sprawdzenia). **[korekta: ADR 0042 — VMM własny w C (`src/strefa/`), urządzenia virtio-mmio sprawdzane pole po polu, obraz przez virtio-gpu 2D do okna K1 w zaufanej ramce rysowanej przez ekran [korekta: ADR 0051 — z szybką grafiką bufor karty przeglądarki prosto do ekranu: warstwa karty albo składanie z bufora, bez kopii w strefie i VMM-ie]; testy w emulacji QEMU (udawane AMD SVM) — bez sprzętu właściciela (D4)]** **[korekta: ADR 0044 — pamięć strefy nie jest stała: balon virtio ze zgłaszaniem wolnych stron (nieużywana pamięć wraca do K1), górna granica = pamięć komputera minus rezerwa K1, twarda granica = memory.max klatki VMM stała przez życie strefy; o celu balonu decyduje nadzorca]**
- Do sprawdzenia: Firefox ma własny sandbox oparty na user namespaces, trzeba zweryfikować jego działanie w strefie. Wybór Firefox czy WPE WebKit w ADR.

## 9. Blob Vault i Gatekeeper
- Blob Vault: magazyn obiektów zamiast drzewa katalogów. **[korekta: ADR 0029 — użytkownik widzi foldery jak w Windows (Pulpit, Dokumenty, Obrazy…); to metadane dokumentów w skarbcu, nie katalogi w jądrze, aplikacje dalej nie dostają ścieżek]**
  - Obiekt ma: identyfikator, metadane (oddzielone od zawartości), rozmiar, typ, hash, pochodzenie, uprawnienia.
  - Obiekty są niezmienne i nigdy nie stają się wykonywalne.
- Dostęp przez capability:
  - aplikacja dostaje tylko deskryptory obiektów, na które zgodził się użytkownik, z minimalnymi prawami,
  - przekazywanie przez SCM_RIGHTS z kontrolą nadawcy i odbiorcy,
  - memfd z F_SEAL_* dla niezmiennych buforów,
  - bez przekazywania dalej bez autoryzacji.
- Gatekeeper, kolejno:
  1. wykrywa plik w strefie przejściowej i pokazuje go w GUI,
  2. czeka na zgodę użytkownika,
  3. czyta surowe bajty z limitami rozmiaru,
  4. tworzy obiekt w Blob Vault z hashem,
  5. sprząta.
  **[korekta: ADR 0027 — zgodą jest gest użytkownika w interfejsie rysowanym przez K1 (Pobierz, okno wyboru pliku, „Otwórz w…”/„Wyślij do…”, przeciągnięcie); osobne pytanie tylko, gdy przeniesienie zaczyna sama strefa. Pliki K1 to jedyny węzeł między strefami, oryginał zawsze w Plikach, pochodzenie zapisane przy pliku]**
  Nigdy nie otwiera ani nie dekoduje plików automatycznie. Nie ufa rozszerzeniom, nazwom ani typom MIME. Archiwa rozpakowuje tylko w izolowanym procesie.
- noexec nie zatrzymuje interpreterów, dlatego potrzebna jest kontrola przepływu danych i uprawnień.

## 10. Jądro
- Aktualna linia LTS, własna minimalna konfiguracja.
- KASLR i lockdown.
- Brak modułów (CONFIG_MODULES=n) albo podpisane moduły (decyzja w ADR). **[korekta: ADR 0003 — `CONFIG_MODULES=n`]**
- Wyłączone zbędne sterowniki i protokoły, ograniczony /proc i /sys.
- Wzmocnienia według zaleceń Kernel Self Protection Project (sprawdź aktualną listę dla wybranej wersji).
- Szybkie aktualizacje bezpieczeństwa jądra mają najwyższy priorytet.

## 11. Higiena kodu C
- Flagi:
  -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -fstack-protector-strong -fPIE -pie -ftrivial-auto-var-init=zero
  plus pełne RELRO i NX. Każdą flagę zweryfikuj z użytym kompilatorem i libc. **[korekta: ADR 0002 — `-pie` zastąpione przez `-static-pie` (statyczne linkowanie), dodane `-fstack-clash-protection`]**
- _FORTIFY_SOURCE z musl wymaga dodatkowych nagłówków (np. fortify-headers). Sprawdź, nie zakładaj. **[korekta: ADR 0002 — sprawdzone: z musl nie działa, fortify-headers nie ma w Ubuntu/Debianie; PLANOWANE]** **[korekta: ADR 0007, dopisek 0.9.0 — ZAIMPLEMENTOWANE: fortify-headers 3.0.1 w `src/zewnetrzne/`, `_FORTIFY_SOURCE=3` we wszystkich programach, musl 1.2.5 z naszego buildu]**
- Sanitizery (ASan, UBSan, bounds, object-size, overflow) i fuzzing (libFuzzer/AFL++) uruchamiaj w testach na hoście (build z glibc). W obrazie ze statycznym musl nie działają.
- Żadnego niekontrolowanego strcpy/strcat/sprintf. Jawne limity długości, kontrola arytmetyki rozmiarów, brak niezainicjalizowanych struktur, jawna obsługa każdego błędu.

## 12. IPC
- Protokoły binarne z wersją, identyfikatorem operacji, długością i walidacją każdego pola.
- Kontrola nadawcy (SO_PEERCRED) i odbiorcy, limity liczby i rozmiaru komunikatów.
- Uwzględnij padding, wyrównanie, endianness, zgodność wersji, wyścigi i TOCTOU.
- Komponenty zaufane nie parsują dynamicznie niezweryfikowanych danych. Parsery i protokoły przechodzą fuzzing.

## 13. Testy
- Każdy etap ma: działający kod, powtarzalny build, testy funkcjonalne i negatywne, dokumentację, kryteria zaliczenia.
- Rodzaje testów:
  - fuzzing,
  - złośliwe komunikaty IPC,
  - eskalacja uprawnień,
  - ucieczka z sandboxa i izolacja sesji,
  - błędy pamięci i awarie procesów,
  - złośliwe pliki,
  - integralność obrazu,
  - aktualizacja i rollback.
- Każda luka: opis, klasyfikacja, naprawa. To, że komponent się uruchamia, nie znaczy, że jest bezpieczny.

## 14. Milestones
**[korekta: ADR 0035 — etapy nazywamy numerem wersji MINOR z tematem, nie numerem M. Ta lista zastępuje dawną listę
M0–M12 i dopiski z ADR 0011, 0019, 0020, 0024, 0025, 0026 (historia: git i tabela w ADR 0035). Stan i kroki:
`docs/DO_ZROBIENIA.txt` i plan bieżącej wersji `docs/PLAN-<wersja>.txt`.]**
- 0.1–0.4 (dawne M0–M4): porządki i toolchain, start (initramfs, SquashFS, PID 1), ekran i wejście, klatki usług
  (nadzorca, UID, seccomp, Landlock, cgroups), pulpit, okna i pierwsze aplikacje natywne.
- 0.5 (M4.1, M5): przegląd bezpieczeństwa, dyski i USB, Blob Vault (skarbiec) z szyfrowaniem, hasło, instalator na dysk.
- 0.6–0.7 (Fundament, M6): podpisy wydań, dm-verity, A/B, Secure Boot z kluczem K1; porządki; Pulpit (ikony, menu).
- 0.8 Pliki (natywna część dawnego M9 i dawne M6 „dekodery”): dokumenty w Blob Vault, aplikacja Pliki, Kosz, okno
  wyboru pliku, czytnik pendrive'a i dekoder obrazów w izolacji; 0.8.1–0.8.2 dopracowanie.
- 0.9 Sieć (dawne M7): nic nie nasłuchuje, kabel (Ethernet/DHCP) w osobnej klatce, czas z sieci; potem Wi-Fi.
- 0.10 Strefy (dawne M8 z M11): każda obca aplikacja w swojej mikro-maszynie (ADR 0020), zaufana ramka, przeglądarka
  w strefie z oddzielnymi sesjami (Firefox albo WPE — ADR), wspólna strefa (ADR 0024), Przeglądarka K1 (ADR 0025).
- 0.11 Pliki ze stref (dawne M9): Gatekeeper dla stref — pobieranie, zgoda, import do Blob Vault, sprzątanie (ADR 0027).
- 0.12 Dźwięk **[korekta: ADR 0053 — nowa wersja; dalsze wersje przesunięte o jedną]**: karta dźwiękowa w usłudze
  w klatce, głośność i klawisze, słuchawki, dźwięk ze stref.
- 0.13 Bezpieczny start — reszta (dawne M10): recovery, licznik wersji (TPM), odwoływanie starych wersji, los GRUB-a.
- 0.14 Aplikacje **[korekta: ADR 0064 — nowa wersja; Audyt przesunięty na 0.15]**: Sklep w K1 jako jedyna droga obcej
  aplikacji, obraz bazowy, aplikacje ze Sklepu w swoich strefach (pierwsza — Discord, z mikrofonem i rozmowami).
- 0.15 Audyt (dawne M12): fuzzing, testy penetracyjne i izolacji, analiza statyczna, dokumentacja końcowa → 1.0.

## 15. Pierwsze zadanie
1. Zapisz tę specyfikację jako docs/SPEC.md.
2. Krótko potwierdź architekturę i plan. Wskaż ryzyka M0–M1 i pierwsze potrzebne ADR-y.
3. Zrealizuj M0 i M1 do statusu PRZETESTOWANE w QEMU (dowód: log z portu szeregowego).
4. Scal do main i przedstaw właścicielowi raport (§2.11) prostym językiem.

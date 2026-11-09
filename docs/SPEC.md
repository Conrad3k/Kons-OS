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
- GitHub Actions: repo prywatne, minuty są ograniczone, więc CI buduje tylko main. Właściciel ma ograniczony limit użycia Claude: pracuj oszczędnie, łącz zmiany, nie powtarzaj bez potrzeby długich buildów.
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
   - squash merge do main bez pytania (repo prywatne, właściciel się zgodził),
   - wersje K1 w SemVer od 0.1.0, tagi k1-vX.Y.Z (stare tagi v0.1.x należą do wersji debianowej).
11. Po każdym milestone krótki raport w czacie: cel i kryteria, co zrobiono, dowody, testy negatywne, ograniczenia, statusy. Nie wklejaj całych plików, są w repo.

## 3. Cel i model zagrożeń
K1 nie jest dystrybucją Linuksa. To własne środowisko: własny init, własny interfejs graficzny, własny magazyn danych i rygorystyczna izolacja aplikacji.

Priorytety: bezpieczeństwo, minimalna baza zaufania (TCB), ograniczanie skutków włamań, także luk zero-day. Coraz więcej takich luk znajdują autonomiczne agenty AI. Właściciel wskazuje na doniesienia o setkach krytycznych luk znalezionych w Linuksie przez agentów LLM.

Nie obiecujemy odporności na zero-day. Projektujemy tak, by przejęcie jednego komponentu nie dawało przejęcia systemu. Jądro jest wspólną granicą zaufania świata natywnego, dlatego najbardziej ryzykowne obce programy dostają docelowo osobne jądro (§8).

## 4. Architektura
Dwa światy:
- Świat natywny K1: nasz kod w C, statycznie linkowany (musl). Bez powłoki, bez X11/Wayland, bez systemd, bez drzewa FHS.
- Strefa zgodności: „równoległy Linux” tworzony na żądanie dla obcych aplikacji (przeglądarka itd.), ze standardowymi bibliotekami Linuksa, w pełni odizolowany. Pliki przechodzą między światami tylko przez Gatekeepera i tylko za zgodą użytkownika.

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
  - w developmencie tylko jednokierunkowy log na port szeregowy,
  - brak terminala sam w sobie nie jest zabezpieczeniem.
- Uprawnienia:
  - osobne UID/GID dla usług i minimalne capabilities,
  - PR_SET_NO_NEW_PRIVS,
  - seccomp-BPF (SECCOMP_MODE_FILTER, biała lista),
  - cgroups v2,
  - ograniczony dostęp do urządzeń.
- LSM: domyślnie Landlock + Yama + lockdown. SELinux albo AppArmor tylko przez ADR.
- Grafika rozdzielona na procesy:
  - ekran: jedyny proces z DRM/KMS; ustawia tryb, składa bufory, robi page flip,
  - gui: pulpit, okna i menu; rysuje do pamięci współdzielonej, bez dostępu do GPU,
  - wejście: czyta evdev i przekazuje zdarzenia do ekranu.
  Renderer własny, programowy, w C. LVGL dopuszczalny po uzasadnieniu. Przejęcie gui nie może dawać dostępu do danych ani systemu.
- Sieć: osobna usługa. Na start Ethernet + DHCP; Wi-Fi później, w izolacji.
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
  - B, docelowe dla przeglądarki: mikro-VM KVM z własnym minimalnym jądrem, więc luka w jądrze strefy nie daje dostępu do jądra K1. Wybór VMM w ADR. Wymaga KVM: testy na sprzęcie właściciela albo na runnerze z KVM (do sprawdzenia).
- Do sprawdzenia: Firefox ma własny sandbox oparty na user namespaces, trzeba zweryfikować jego działanie w strefie. Wybór Firefox czy WPE WebKit w ADR.

## 9. Blob Vault i Gatekeeper
- Blob Vault: magazyn obiektów zamiast drzewa katalogów.
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
- _FORTIFY_SOURCE z musl wymaga dodatkowych nagłówków (np. fortify-headers). Sprawdź, nie zakładaj. **[korekta: ADR 0002 — sprawdzone: z musl nie działa, fortify-headers nie ma w Ubuntu/Debianie; PLANOWANE]**
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
**[korekta: ADR 0011 — kolejność po M4: M4.1 porządki i audyt → M5 rozszerzony o dyski i USB → M5.1 próba przeglądarki (bez sieci) → M5.2 dm-verity (z M10) → M6 → M7 → M8 → M9 → M10 → M11 → M12]**
**[korekta: ADR 0018 — M5f instalator na dysk; ADR 0019 — od 0.6.0: Fundament (porządki nadzorcy, uevent, szybsze testy) → M6 Bezpieczne aktualizacje (podpisy, dm-verity, A/B, Secure Boot z własnym kluczem — z M10) → M7 Sieć (najpierw kabel) → M8 Przeglądarka (z dawnym M5.1) → M9 Pliki (Gatekeeper + dekodery w izolacji z dawnego M6) → M10 reszta (recovery, TPM) → M11 → M12; ADR 0020 — M8 „Strefy”: każda obca aplikacja w swojej mikro-maszynie (zaplecze B jako główny model, dawne M11 w M8), trwałość i grafika do wyboru, pliki przez Gatekeepera (M9); ADR 0024 — wspólna strefa na drobne, zaufane aplikacje (przeglądarka zawsze osobno); ADR 0025 — Przeglądarka K1: własne okno i pasek adresu, silnik w strefie, sesje = strefy (po pierwszej przeglądarce w strefie); ADR 0026 — po 0.7.3: 0.7.5 Pulpit (ikony na pulpicie, menu pod prawym przyciskiem, przypinanie na pasku) → 0.8.0 Pliki (natywna część M9: wspólne dokumenty w Blob Vault, Kosz, okno wyboru pliku, import z pendrive'a, dekoder obrazów w izolacji — dawne M6) → M7 Sieć → M8 Strefy → M9 Pliki ze stref (Gatekeeper dla stref)]**
- M0 Porządki:
  - archiwum wersji debianowej (gałąź archiwum/debian-0.1.2), nowa struktura repo, dokumenty z §2.9,
  - toolchain musl ze statycznym linkowaniem,
  - skrypty: build obrazu, QEMU z logiem szeregowym, zrzut ekranu,
  - CI na main.
- M1 Start: konfiguracja jądra, initramfs, SquashFS (na razie bez dm-verity), PID 1, montowania. W QEMU log „K1: init OK” bez kernel panic. Testy negatywne (np. brak obrazu → kontrolowany komunikat).
- M2 Ekran: proces ekran (DRM/KMS), pierwsza klatka (tapeta i logo K1), klawiatura i mysz z kursorem. Dowód: zrzut ekranu.
- M3 Trusted Core: nadzorca, osobne UID, capabilities, seccomp, Landlock, cgroups, restart GUI po awarii.
- M4 GUI: pulpit, okna, pasek, pierwsze aplikacje natywne (zegar, notatki).
- M5 Blob Vault i szyfrowanie danych.
- M6 Dekodery w izolacji (np. tapeta PNG wczytywana przez sandboxowany dekoder).
- M7 Sieć (Ethernet/DHCP) i Strefa zgodności z zapleczem A, z prostą aplikacją testową wyświetlaną w K1.
- M8 Przeglądarka w strefie (Firefox albo WPE, decyzja w ADR) z oddzielnymi sesjami.
- M9 Gatekeeper: pobieranie, zgoda, import do Blob Vault, sprzątanie.
- M10 Verified boot: dm-verity, Secure Boot, podpisy, A/B, rollback, recovery.
- M11 Zaplecze B (mikro-VM) dla przeglądarki.
- M12 Audyt: fuzzing, testy penetracyjne i izolacji, analiza statyczna, dokumentacja końcowa.

## 15. Pierwsze zadanie
1. Zapisz tę specyfikację jako docs/SPEC.md.
2. Krótko potwierdź architekturę i plan. Wskaż ryzyka M0–M1 i pierwsze potrzebne ADR-y.
3. Zrealizuj M0 i M1 do statusu PRZETESTOWANE w QEMU (dowód: log z portu szeregowego).
4. Scal do main i przedstaw właścicielowi raport (§2.11) prostym językiem.

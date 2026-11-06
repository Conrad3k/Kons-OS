# 0002. Toolchain (musl, statyczne PIE) i flagi kompilacji

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §4 (statyczne linkowanie, musl), §11 (higiena kodu C), M0

## Kontekst
Świat natywny K1 to programy w C linkowane statycznie z musl, bez bibliotek współdzielonych.
SPEC §11 wymaga konkretnych flag i każe je zweryfikować z użytym kompilatorem i libc.

Środowisko budowy: Ubuntu 24.04 (kontener deweloperski i runner `ubuntu-24.04` w CI).
Dostępne pakiety: `gcc-13` 13.3.0, `musl-tools`/`musl-dev` 1.2.4-2 (wrapper `musl-gcc`).
musl.libc.org jest zablokowany w sieci kontenera (proxy 403).

Weryfikacja (2026-11-06, gcc 13.3.0 + musl 1.2.4):
- `-static-pie` działa z `musl-gcc` (Debian/Ubuntu dodał obsługę w 1.2.4-1: „Apply patch … to allow
  linking with static-pie”). Wynik: `ELF … static-pie linked`, typ `DYN`, brak `INTERP` i `NEEDED`.
- Samo `-pie` ze SPEC dałoby PIE dynamiczne (z interpreterem) — sprzeczne ze statycznym linkowaniem.
- `-Wl,-z,relro,-z,now` → segment `GNU_RELRO` i `FLAGS BIND_NOW`, `FLAGS_1 NOW PIE` (pełne RELRO).
- `-Wl,-z,noexecstack` → `GNU_STACK` z prawami `RW` (NX).
- `-fstack-protector-strong` → deasemblacja `init` pokazuje wywołania `__stack_chk_fail` w naszych
  funkcjach (`main`, `k1_vlog`).
- `-ftrivial-auto-var-init=zero` — przyjmowane przez GCC 13 (od GCC 12).
- `_FORTIFY_SOURCE` — **z musl nie działa**: nagłówki musl nie implementują wersji `__*_chk`
  (w binariach brak takich symboli). Projekt `fortify-headers` nie ma pakietu w Ubuntu 24.04 ani
  w Debianie (sprawdzone: `apt-cache search`, pool deb.debian.org → 404).
- `-fcf-protection` — bez efektu: biblioteka musl z Ubuntu nie jest skompilowana z IBT/SHSTK,
  więc linker usuwa znacznik CET z wyniku; nie dodajemy.

## Decyzja
- Kompilator `musl-gcc` (gcc 13 + musl 1.2.4 z Ubuntu 24.04), `-std=c11 -D_GNU_SOURCE`.
- Flagi (Makefile, `CFLAGS_K1`/`LDFLAGS_K1`):
  `-O2 -Wall -Wextra -Werror -Wformat=2 -Wformat-security -Wconversion -Wshadow -Wstrict-prototypes
  -Wmissing-prototypes -fstack-protector-strong -fstack-clash-protection -fPIE
  -ftrivial-auto-var-init=zero` oraz linkowanie `-static-pie -Wl,-z,relro,-z,now,-z,noexecstack -s`.
  **Korekta SPEC:** `-pie` → `-static-pie`; dodane `-fstack-clash-protection` (ochrona przed
  „stack clash”, zgodna z gcc 13).
- Nagłówki jądra (UAPI) dla programów K1 pochodzą z **naszego** jądra (`make headers_install`),
  nie z systemu budującego — pasują dokładnie do wersji, na której program działa.
- `scripts/sprawdz-binaria.sh` sprawdza przy każdym buildzie: static-pie, brak interpretera
  i bibliotek, RELRO, BIND_NOW, NX, brak segmentów RWX.
- Testy na hoście: gcc + glibc z `-fsanitize=address,undefined -fno-sanitize-recover=all
  -D_FORTIFY_SOURCE=3`; fuzzing: clang 18 + libFuzzer (+ASan/UBSan).
- `_FORTIFY_SOURCE` w obrazie: **PLANOWANE** (M3: decyzja o dołączeniu `fortify-headers` z GitHuba
  jako zależności przejrzanej i przypiętej). Do tego czasu: jawne limity długości w kodzie,
  sanitizery i fuzzing na hoście.

## Konsekwencje
- Programy nie zależą od niczego w systemie plików (brak `/lib`, `/etc`).
- Zaufanie do archiwum Ubuntu (podpisane repozytoria apt) jako źródła kompilatora i musl.
- musl 1.2.4-2 z Ubuntu nie ma poprawki CVE-2025-26519 (iconv). Nasze programy nie używają
  `iconv` (sprawdzone `nm` na niestripowanym `init`: 0 symboli iconv). Ryzyko w `docs/RYZYKA.md`;
  przejście na własny build musl 1.2.5 z poprawkami (źródło: pakiet źródłowy Debiana, łańcuch
  podpisów archiwum) — PLANOWANE przed M3.
- Brak ochrony CET (IBT/shadow stack) w przestrzeni użytkownika (musl jej nie obsługuje).

## Rozważane alternatywy
- Własny build musl ze źródeł musl.libc.org — host zablokowany; odłożone (wyżej).
- glibc statycznie — odrzucone: glibc źle wspiera statyczne linkowanie (NSS, dlopen), większa TCB.
- Clang jako kompilator K1 — możliwy w przyszłości (lepsze sanitizery, CFI), ale GCC jest już
  potrzebny do jądra (wtyczki GCC); jeden kompilator = mniej zmiennych.

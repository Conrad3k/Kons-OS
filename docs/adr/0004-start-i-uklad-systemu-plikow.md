# 0004. Start (initramfs → init) i układ systemu plików

- Status: Przyjęty
- Data: 2026-11-06
- Dotyczy: SPEC §5 (korekta o punktach montowania), §6 (initramfs), §7 (PID 1, ograniczony /proc i /sys), M1

## Kontekst
SPEC §5: w korzeniu tylko `/rdzen`, `/aplikacje`, `/dane_uzytkownika`; jądro potrzebuje punktów
montowania dla devtmpfs, proc, sysfs, cgroup2 i `/dev/console` przy starcie. Sprawdzone w źródłach 6.18.60:
- `init/main.c: console_on_rootfs()` otwiera `"/dev/console"` w initramfs **przed** uruchomieniem
  `/init`; deskryptory 0–2 dziedziczą wszystkie kolejne procesy. Po przełączeniu korzenia ścieżka
  nie jest już potrzebna.
- Gdy `CONFIG_INITRAMFS_SOURCE=""`, jądro wbudowuje domyślny initramfs `usr/default_cpio_list`
  (`/dev`, `/dev/console`, `/root`), a nasz jest rozpakowywany na niego.
- Ścieżki pseudo-systemów plików nie są zaszyte w jądrze (poza `/dev/console` i programami
  pomocniczymi, które wyłączyliśmy: ADR 0003). Biblioteka musl używa `/proc/self/fd` w kilku
  funkcjach (np. `fexecve` jako zapas), których nie używamy.
- Parametry jądra z kropką w nazwie (np. `k1.rdzen=`) **nie** trafiają do argumentów ani środowiska
  `/init` (`init/main.c: unknown_bootoption`, „Unused module parameter”) — trzeba czytać `/proc/cmdline`.
- Każdy montaż proc ma własne opcje (od Linuksa 5.8); `subset=pid` ukrywa wszystko poza katalogami
  procesów, `hidepid=invisible` ukrywa cudze procesy (`fs/proc/root.c`).

## Decyzja
**Start:**
1. Jądro (`-kernel` w QEMU; od M10 podpisany UKI) uruchamia `/init` z naszego initramfs
   (nieskompresowany cpio z `gen_init_cpio`: `/init`, `/dev/console`, `/proc`, `/nowy_korzen`).
2. `k1-initramfs` (`src/initramfs/`): czyta `k1.rdzen=/dev/XXX` z `/proc/cmdline` (ścisła walidacja:
   `/dev/[a-z0-9]{1,31}`, bez powtórzeń i cudzysłowów), czeka do 10 s na urządzenie, tworzy nowy
   korzeń (tmpfs 16 KiB, 8 i-węzłów) z trzema katalogami, montuje SquashFS jako `/rdzen`
   (`ro,nosuid,nodev`), sprawdza `/rdzen/init`, przełącza korzeń w tryb tylko do odczytu, usuwa
   pliki initramfs, przenosi nowy korzeń na `/` (MS_MOVE + chroot) i uruchamia `/rdzen/init`
   z pustym środowiskiem.
3. `/rdzen/init` (`src/init/`) — PID 1, opis w ADR 0005 i `docs/komponenty/init.md`.

**Układ systemu plików po starcie:**

| Ścieżka | System plików | Flagi | Uwagi |
|---|---|---|---|
| `/` | tmpfs | `ro,nosuid,nodev,noexec` | tylko 3 katalogi |
| `/rdzen` | SquashFS (zstd) | `ro,nosuid,nodev` | od M10 przez dm-verity |
| `/rdzen/jadro/dev` | devtmpfs | `nosuid,noexec` | brak TTY/pts (ADR 0003) |
| `/rdzen/jadro/proc` | proc | `nosuid,nodev,noexec`, `hidepid=invisible,subset=pid` | brak `/proc/sys` itd. |
| `/rdzen/jadro/sys` | sysfs | `ro,nosuid,nodev,noexec` | |
| `/rdzen/jadro/sys/fs/cgroup` | cgroup2 | `nosuid,nodev,noexec`, `nsdelegate` | |
| `/aplikacje` | (pusty katalog) | — | M4+: podpisany obraz aplikacji |
| `/dane_uzytkownika` | tmpfs 16 MiB | `nosuid,nodev,noexec`, `mode=0700` | **tymczasowo do M5**: RAM, bez szyfrowania |

**Korekta SPEC §5:** `/dev/console` istnieje tylko w initramfs; w działającym systemie konsola to
odziedziczone deskryptory 0–2 oraz węzeł `/rdzen/jadro/dev/console`. Korzeń jest tmpfs-em tylko do
odczytu — to jedyny sposób, by w `/` były wyłącznie trzy katalogi, a obraz rdzenia był *pod*
`/rdzen`, a nie korzeniem.

**Sysctl:** init ustawia je przez **tymczasowy** pełny montaż proc, który odmontowuje przed
zamontowaniem docelowego, ograniczonego `/rdzen/jadro/proc`.

## Konsekwencje
- W systemie nie ma `/dev`, `/proc`, `/sys`, `/etc`, `/tmp` w zwykłych miejscach — obce programy
  (strefa zgodności, M7) dostaną własne drzewo w swojej przestrzeni nazw.
- Zapis możliwy tylko w `/dane_uzytkownika` (noexec) i w devtmpfs (noexec); w świecie natywnym nie ma
  miejsca, które byłoby jednocześnie zapisywalne i wykonywalne. Dodatkowo `vm.memfd_noexec=2`
  blokuje uruchamianie programów z pamięci (memfd).
- Do M10 obraz rdzenia nie ma weryfikacji integralności — jądro parsuje niezweryfikowany SquashFS
  (`docs/RYZYKA.md`).
- Parametr `k1.rdzen` wskazuje urządzenie po nazwie jądra (`/dev/vda`); na prawdziwym sprzęcie
  (M10) zastąpi go identyfikacja przez dm-verity/UUID partycji.

## Rozważane alternatywy
- SquashFS jako korzeń `/` (z katalogiem `rdzen` w środku) — wtedy obraz zawierałby korzeń, a nie
  `/rdzen`, i nie dałoby się zamontować `/dane_uzytkownika` bez katalogu w obrazie; korzeń tmpfs
  jest prostszy i zgodny z SPEC.
- `pivot_root` zamiast MS_MOVE+chroot — nie działa na rootfs initramfs (nie da się go odmontować);
  standardowy `switch_root` robi dokładnie MS_MOVE+chroot.
- Kompresja initramfs — dodaje dekompresor do jądra; initramfs ma ~40 KiB.

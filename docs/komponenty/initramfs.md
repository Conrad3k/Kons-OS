# Komponent: k1-initramfs (`/init` w initramfs)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1) — `scripts/test-m1.sh`: start + 5 scenariuszy negatywnych |
| **Kod** | `src/initramfs/main.c`, `src/wspolne/k1_cmdline.c`, `src/wspolne/k1_log.c` |
| **Decyzje** | ADR 0004 (start, układ plików), ADR 0005 (błędy krytyczne) |

## Cel
Jedno zadanie: znaleźć obraz rdzenia, zamontować go jako `/rdzen` w nowym, tylko-do-odczytu
korzeniu i przekazać sterowanie do `/rdzen/init`. W M10 dojdzie aktywacja dm-verity.

## Granica zaufania
Działa jako PID 1 z pełnymi uprawnieniami roota, zanim cokolwiek innego wystartuje. Ufa jądru
i własnemu kodowi (wbudowanemu w initramfs, od M10 podpisanemu w UKI). **Nie ufa**: linii poleceń
(walidacja), urządzeniu blokowemu i jego zawartości (do M10 brak weryfikacji — RYZYKA R1).

## Uprawnienia
UID 0, wszystkie capabilities (potrzebne: `CAP_SYS_ADMIN` do montowania i chroot, `CAP_SYS_BOOT`
do wyłączenia). Działa kilkadziesiąt milisekund, potem `execve` zastępuje go przez `/rdzen/init`.

## Wejścia i wyjścia
- Wejście: `/proc/cmdline` (≤ 4095 bajtów), parametr `k1.rdzen=/dev/[a-z0-9]{1,31}` (dokładnie raz,
  bez cudzysłowów), węzeł urządzenia w devtmpfs, obraz SquashFS.
- Wyjście: komunikaty `K1-INITRAMFS: …` na konsoli; zamontowany korzeń; `execve("/rdzen/init")`
  z pustym środowiskiem.

## Zależności
musl (statycznie), jądro: proc, devtmpfs, tmpfs, squashfs (zstd), virtio-blk.

## Wywołania systemowe
`getpid`, `umask`, `mount` (proc, devtmpfs, tmpfs, squashfs, remount ro, MS_MOVE), `umount2`, `open`,
`read`, `close`, `stat`, `nanosleep`, `mkdir`, `unlink`, `rmdir`, `chdir`, `chroot`, `execve`,
`write` (log), `sync`, `reboot` (przy błędzie).

## Obsługa błędów
Każdy błąd → `k1_awaria()`: komunikat `BŁĄD KRYTYCZNY`, 5 s, wyłączenie zasilania (ADR 0005).
Nigdy `exit()` (kernel panic).

## Powierzchnia ataku
- Parser linii poleceń — mały, czysta funkcja, testy jednostkowe z ASan/UBSan i fuzzing (libFuzzer).
- Jądrowy parser SquashFS na niezweryfikowanym obrazie (do M10).
- Ścieżka urządzenia: tylko `/dev/` + litery/cyfry — bez `..`, podkatalogów i dowiązań z innych miejsc.

## Testy
| Test | Co sprawdza |
|---|---|
| `qemu: start`, `qemu: testy-w-systemie` | poprawny start, układ korzenia (w systemie) |
| `qemu: neg-brak-dysku` | brak urządzenia → komunikat po 10 s, wyłączenie |
| `qemu: neg-dysk-bez-obrazu` | dysk bez SquashFS → komunikat, wyłączenie |
| `qemu: neg-obraz-bez-init` | SquashFS bez `/rdzen/init` → komunikat, wyłączenie |
| `qemu: neg-brak-parametru`, `neg-zly-parametr`, `neg-powtorzony-parametr` | odrzucenie linii poleceń |
| `jednostkowe`, `fuzzing` | parser `k1_cmdline` (32 przypadki, ~5 mln losowych wejść) |

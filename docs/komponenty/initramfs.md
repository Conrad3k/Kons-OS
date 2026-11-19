# Komponent: k1-initramfs (`/init` w initramfs)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M1, M4, M6b) — `scripts/test-m1.sh`: start + 8 scenariuszy negatywnych; `scripts/test-m4.sh`: start z ISO (UEFI, BIOS) |
| **Kod** | `src/initramfs/main.c`, `src/wspolne/k1_cmdline.c`, `src/wspolne/k1_log.c` |
| **Decyzje** | ADR 0004 (start, układ plików), ADR 0005 (błędy krytyczne), ADR 0010 (obraz wbudowany w ISO), ADR 0021 (dm-verity, M6b); od 0.10.9 urządzenie loop i dm-verity zakłada wspólny kod `src/wspolne/k1_verity.c` (ten sam robi obraz przeglądarki w nadzorcy, ADR 0050) |

## Cel
Jedno zadanie: znaleźć obraz rdzenia, zamontować go jako `/rdzen` w nowym, tylko-do-odczytu
korzeniu i przekazać sterowanie do `/rdzen/init`. Od 0.6.3 (M6b) obraz jest najpierw w całości
sprawdzany przez dm-verity.

## Granica zaufania
Działa jako PID 1 z pełnymi uprawnieniami roota, zanim cokolwiek innego wystartuje. Ufa jądru
i własnemu kodowi (wbudowanemu w initramfs, od 0.7.0 podpisanemu w `k1.efi`). **Nie ufa**: linii poleceń
(walidacja), urządzeniu blokowemu i jego zawartości (dm-verity ze skrótem z linii poleceń; skrót
niepodpisany do M6d — RYZYKA R1).

## Uprawnienia
UID 0, wszystkie capabilities (potrzebne: `CAP_SYS_ADMIN` do montowania i chroot, `CAP_SYS_BOOT`
do wyłączenia). Działa kilkadziesiąt milisekund, potem `execve` zastępuje go przez `/rdzen/init`.

## Wejścia i wyjścia
- Wejście: `/proc/cmdline` (≤ 4095 bajtów), parametr `k1.rdzen=/dev/[a-z0-9]{1,31}` albo
  `k1.rdzen=wbudowany` (dokładnie raz, bez cudzysłowów), węzeł urządzenia w devtmpfs, obraz SquashFS.
- Od 0.7.0 (ADR 0022) w wydaniach initramfs i `rdzen.squashfs` są **wbudowane w podpisane jądro `k1.efi`**
  razem z linią poleceń (`k1.rdzen=wbudowany`, `noinitrd` — initrd od programu ładującego jest ignorowany).
- `k1.rdzen_skrot=<64 znaki hex>` (0.6.3): skrót główny drzewa dm-verity; obowiązkowy. Obraz =
  SquashFS dopełniony do 4 KiB + drzewo SHA-256 (`--no-superblock`, stała sól); urządzenie `k1-rdzen`
  (ioctl `DM_DEV_CREATE`, `DM_TABLE_LOAD`, `DM_DEV_SUSPEND`), odczyt całości, montowanie `/dev/dm-N`.
- `wbudowany` (ISO, ADR 0010): plik `/rdzen.squashfs` z drugiego archiwum initramfs podpinany przez
  `/dev/loop-control` → `LOOP_CTL_GET_FREE` → `LOOP_CONFIGURE` (tylko do odczytu, auto-odłączanie;
  urządzenie zamykane dopiero po zamontowaniu); plik usuwany z initramfs (dane trzyma loop).
- Wyjście: komunikaty `K1-INITRAMFS: …` na konsoli; zamontowany korzeń; `execve("/rdzen/init")`
  z pustym środowiskiem.

## Zależności
musl (statycznie), jądro: proc, devtmpfs, tmpfs, squashfs (zstd), virtio-blk, loop, device-mapper
z `DM_VERITY` i SHA-256.

## Wywołania systemowe
`getpid`, `umask`, `mount` (proc, devtmpfs, tmpfs, squashfs, remount ro, MS_MOVE), `umount2`, `open`,
`read`, `close`, `stat`, `nanosleep`, `mkdir`, `unlink`, `rmdir`, `chdir`, `chroot`, `execve`,
`write` (log), `sync`, `reboot` (przy błędzie), `ioctl` (`/dev/mapper/control`).

## Obsługa błędów
Każdy błąd → `k1_awaria()`: komunikat `BŁĄD KRYTYCZNY`, 5 s, wyłączenie zasilania (ADR 0005).
Nigdy `exit()` (kernel panic).

## Powierzchnia ataku
- Parser linii poleceń — mały, czysta funkcja, testy jednostkowe z ASan/UBSan i fuzzing (libFuzzer).
- Jądrowy parser SquashFS — tylko na obrazie sprawdzonym przez dm-verity (od 0.6.3); initramfs
  czyta z obrazu przed weryfikacją jedynie `bytes_used` z nagłówka (rozmiar sprawdzany z urządzeniem).
- Ścieżka urządzenia: tylko `/dev/` + litery/cyfry — bez `..`, podkatalogów i dowiązań z innych miejsc.

## Testy
| Test | Co sprawdza |
|---|---|
| `qemu: start`, `qemu: testy-w-systemie` | poprawny start, układ korzenia (w systemie) |
| `qemu: neg-brak-dysku` | brak urządzenia → komunikat po 10 s, wyłączenie |
| `qemu: neg-dysk-bez-obrazu` | dysk bez SquashFS → komunikat, wyłączenie |
| `qemu: neg-obraz-bez-init` | SquashFS bez `/rdzen/init` → komunikat, wyłączenie |
| `qemu: neg-brak-parametru`, `neg-zly-parametr`, `neg-powtorzony-parametr` | odrzucenie linii poleceń |
| `qemu: neg-brak-skrotu`, `neg-zly-skrot` | brak / zły `k1.rdzen_skrot` → awaria (dm-verity) |
| `qemu: neg-zmieniony-obraz` | jeden zmieniony bajt obrazu → „obraz rdzenia jest zmieniony…”, wyłączenie |
| `jednostkowe`, `fuzzing` | parser `k1_cmdline` (32 przypadki, ~5 mln losowych wejść) |

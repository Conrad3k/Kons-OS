/* K1 — a verified image: dm-verity over a read-only loop device (M6b, ADR 0021; 0.10.9 step 4, ADR 0050).
 * One code for the core (k1-initramfs, before /rdzen exists) and for the zone images outside the core (nadzorca).
 * The layout of such an image: its data in K1_VERITY_BLOK blocks, then its hash tree, no superblock, the build's fixed
 * salt (scripts/wspolne.sh: verity). The root hash comes from what the signed kernel vouches for: the command line
 * (the core) or a file in the core (a zone image: k1_obraz_strefy.h). The kernel checks every block it reads against
 * the tree: a changed byte is an I/O error, never different data.
 * `dev` is the devtmpfs directory of the caller ("/dev" in the initramfs, /rdzen/jadro/dev in nadzorca). */
#ifndef K1_VERITY_H
#define K1_VERITY_H

#include <stddef.h>
#include <stdint.h>

#define K1_VERITY_BLOK 4096u
#define K1_VERITY_SOL "4b31207264657a656e2056657269747920736f6c00000000000000000000000000"

/* What failed (for the caller's message): */
enum { K1_VERITY_LOOP = 1, K1_VERITY_DM = 2, K1_VERITY_WEZEL = 3 };

/* A read-only loop device over `plik` from byte `od`, `bajty` long (0: to its end); it frees itself when its last user
 * closes it (LO_FLAGS_AUTOCLEAR). Its path into `out`; returns its open fd, or -K1_VERITY_* (errno set). */
int k1_loop_podlacz(const char *dev, int plik, uint64_t od, uint64_t bajty, char *out, size_t n);

/* The dm-verity device `nazwa` over the block device `urz` (`bloki` data blocks, the tree right after them), read
 * only, active. Its path ("<dev>/dm-N") into `out`. 0, or -K1_VERITY_* (errno set). */
int k1_verity_utworz(const char *dev, const char *nazwa, const char *urz, uint64_t bloki, const char *skrot, char *out,
                     size_t n);

/* Device `nazwa` goes away when its last user closes it (DM_DEFERRED_REMOVE): at once when nobody holds it. 0 or -1. */
int k1_verity_usun(const char *dev, const char *nazwa);

/* Waits up to `kroki` × 100 ms for the block device node `sciezka` (devtmpfs makes it a moment after the kernel).
 * 0, or -1 (errno: ENOTBLK when it is not a block device, ENOENT when it never came). */
int k1_czekaj_na_wezel(const char *sciezka, int kroki);

#endif

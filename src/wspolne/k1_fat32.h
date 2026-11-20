/* K1 — the FAT32 file system of a K1-EFI partition read as a tree of files (0.9.0 step 3, ADR 0038): what the boot
 * files are, whatever clusters they lie in. One reader for the build (narzedzia/podpisz.c: the digest in the manifest
 * of the boot files), the installer (the stick checked before anything is written, the disk after it) and its swap of
 * the boot files (src/instalator/start.c).
 * Nothing read is trusted: every number is checked before it is used, every chain ends where the entry's size says,
 * no cluster lies in two places (a loop is an error, never an endless walk), at most K1_FAT32_WEZLY entries.
 * Pure: the bytes come through `czytaj` (a device in the installer, a file on the build host, memory in fuzzing). */
#ifndef K1_FAT32_H
#define K1_FAT32_H

#include <stdint.h>

#define K1_FAT32_WEZLY 128                      /* files and folders at most (a K1-EFI holds about 20) */
#define K1_FAT32_KLASTRY (1u << 19)             /* clusters at most (2 GiB in 4 KiB clusters; a K1-EFI: 409 600 since 0.10.9 step 4b, 71 680 before) */
#define K1_FAT32_MAPA ((K1_FAT32_KLASTRY + 2 + 7) / 8) /* bytes of a map of clusters (one bit each) */
#define K1_FAT32_KLASTER_MAKS (64u << 10)
#define K1_FAT32_KONIEC 0x0FFFFFFFu             /* the value that ends a chain */

struct k1_fat32_we {
    int (*czytaj)(void *ctx, uint64_t od, void *buf, uint32_t n); /* 0, or -1 */
    void *ctx;
    uint64_t rozmiar; /* bytes of the partition */
};

struct k1_fat32 {
    struct k1_fat32_we we;
    uint32_t sektor, klaster;   /* bytes per sector, per cluster */
    uint32_t n_fat;             /* copies of the FAT */
    uint32_t zarezerwowane;     /* reserved sectors (the boot sector, FSInfo, its backup, the layout block) */
    uint64_t fat_od, fat_bajty; /* the first FAT, bytes of one copy */
    uint64_t dane_od;           /* cluster 2 */
    uint32_t klastrow;          /* valid cluster numbers: 2 .. klastrow + 1 */
    uint32_t korzen;            /* the root folder's first cluster (from the boot sector; the swap sets another) */
    uint8_t bufor[512];         /* one block of the FAT */
    uint64_t bufor_od;
    int bufor_jest;
};

/* A file or folder of the tree; w[0] is the root. */
struct k1_fat32_wezel {
    uint32_t pierwszy; /* first cluster (0: an empty file) */
    uint32_t rozmiar;  /* bytes (a folder: 0) */
    uint16_t rodzic;   /* index of its folder (the root: 0) */
    uint8_t folder;
};

struct k1_fat32_drzewo {
    int n;
    struct k1_fat32_wezel w[K1_FAT32_WEZLY];
};

/* The boot sector: FAT32 only (no FAT12/16, no exFAT), the FAT mirrored, everything inside the partition. 0 or -1. */
int k1_fat32_otworz(struct k1_fat32 *f, const struct k1_fat32_we *we);

/* Byte offset of cluster `k` (2 ..) from the partition start. */
uint64_t k1_fat32_klaster_od(const struct k1_fat32 *f, uint32_t k);

/* The cluster after `k` in its chain: *nast = the next one, or 0 when `k` ends it. -1 when the FAT says something
 * else (free, bad, out of range) or cannot be read. */
int k1_fat32_nastepny(struct k1_fat32 *f, uint32_t k, uint32_t *nast);

/* Every file and folder reachable from f->korzen, folders before their contents (in the order of their entries).
 * Skipped: ".", "..", deleted entries, long-name parts, the volume label. Every cluster of every chain is marked in
 * `zajete` (K1_FAT32_MAPA bytes, cleared here). 0, or -1 (anything wrong: the tree is not used). */
int k1_fat32_drzewo(struct k1_fat32 *f, struct k1_fat32_drzewo *d, uint8_t *zajete);

/* The cluster fields of the short entries in `n` bytes of a folder set to 0 (long-name parts left as they are). */
void k1_fat32_bez_klastrow(uint8_t *buf, uint32_t n);

/* BLAKE2b-256 of the tree, independent of where it lies: for every entry its place in the tree, kind and size, the
 * folders' bytes without cluster numbers, the files' contents — except of the files that start at one of `bez`
 * (byte offsets: grubenv and the slots, which change without the boot files changing), each of which must be there
 * exactly once and lie in one piece. 0, or -1. */
int k1_fat32_skrot(struct k1_fat32 *f, const struct k1_fat32_drzewo *d, const uint64_t *bez, int n_bez,
                   uint8_t skrot[32]);

/* Bit `k` of a map of clusters. */
static inline int k1_fat32_bit(const uint8_t *mapa, uint32_t k)
{
    return mapa[k >> 3] >> (k & 7) & 1;
}
static inline void k1_fat32_ustaw(uint8_t *mapa, uint32_t k)
{
    mapa[k >> 3] = (uint8_t)(mapa[k >> 3] | 1u << (k & 7));
}

#endif

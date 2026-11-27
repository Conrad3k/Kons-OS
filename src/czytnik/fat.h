/* K1 czytnik — reading the FAT file system of a USB stick (0.8.0 step 3, ADR 0031): FAT12, FAT16 and FAT32 with long
 * names (VFAT), on the whole stick or in a partition of an MBR or GPT table. Read only; nothing on the stick is trusted:
 * every number is checked before it is used (sizes, cluster numbers, chains — a loop or a chain that ends too early
 * is an error, never an endless loop), every name becomes a valid K1 name (k1_dok_nazwa_poprawna).
 * Pure: every byte comes through `czytaj` (the stick in czytnik, memory in the host test and fuzzing). */
#ifndef K1_FAT_H
#define K1_FAT_H

#include <stdint.h>

#include "k1_dokument.h"

/* Results besides 0 (k1_komunikat.h: the same numbers travel as K1_PEN_BLAD_*) */
#define FAT_BLAD_FORMAT (-30)     /* no FAT file system found (e.g. exFAT or NTFS) */
#define FAT_BLAD_ODCZYT (-31)     /* the stick did not give the bytes (removed?) */
#define FAT_BLAD_USZKODZONY (-34) /* a number in the file system is wrong (chain, cluster, size) */

#define FAT_SEKTOR_MAKS 4096
#define FAT_KLASTER_MAKS (256u << 10)
#define FAT_FOLDER_MAKS 65536 /* entries of one folder at most (as the FAT specification) */

struct fat_nosnik {
    int (*czytaj)(void *ctx, uint64_t od, void *buf, uint32_t n); /* 0, or -1 */
    void *ctx;
    uint64_t rozmiar; /* bytes */
};

struct fat {
    struct fat_nosnik n;
    uint64_t start;          /* the volume (partition) on the stick, bytes */
    uint32_t sektor, klaster; /* bytes per sector, per cluster */
    int typ;                 /* 12, 16, 32 */
    uint64_t fat_od;         /* the first FAT, from the volume start */
    uint64_t korzen_od;      /* FAT12/16: the root folder (fixed place) */
    uint32_t korzen_wpisow;  /* FAT12/16: its entries */
    uint32_t korzen_klaster; /* FAT32: the root folder's first cluster */
    uint64_t dane_od;        /* cluster 2 */
    uint32_t klastrow;       /* data clusters: valid numbers 2..klastrow+1 */
    uint32_t n_fat;          /* copies of the FAT (0.13.0 step 3: the report's writer, src/instalator/raport.c) */
    uint64_t fat_bajty;      /* bytes of one copy */
    uint32_t fsinfo;         /* FAT32: the FSInfo sector (0: none) */
    uint8_t bufor[FAT_SEKTOR_MAKS]; /* one block of the FAT */
    uint64_t bufor_od;
    int bufor_jest;
};

/* An entry of a folder. `polozenie` (where its short entry is, bytes from the volume start) is what tells it apart. */
struct fat_wpis {
    uint64_t polozenie;
    int folder;
    uint32_t klaster; /* first cluster (0: an empty file) */
    uint32_t rozmiar;
    int64_t czas;     /* last change as written on the stick (local time read as UTC), 0 when none */
    char nazwa[K1_DOK_NAZWA];
};

/* Finds the file system: at the start of the stick, else in the first partition (MBR, or GPT) that holds one.
 * 0, FAT_BLAD_FORMAT or FAT_BLAD_ODCZYT. */
int fat_otworz(struct fat *f, const struct fat_nosnik *n);

/* Calls `kazdy` for every entry of the folder whose first cluster is `klaster` (0: the root), in the order on the
 * stick; skips ".", "..", deleted ones, the volume label and hidden or system entries (as Windows shows them).
 * `kazdy` returns 0 to go on, 1 to stop. 0, or FAT_BLAD_*. */
int fat_folder(struct fat *f, uint32_t klaster, int (*kazdy)(void *ctx, const struct fat_wpis *w), void *ctx);

/* The first `ile` bytes (at most the file) of the file starting at `klaster` with `rozmiar` bytes, piece after piece
 * (at most 64 KiB each) to `pisz`, which returns 0 or -1. 0, or FAT_BLAD_* (-1 from `pisz`: FAT_BLAD_ODCZYT). */
int fat_czytaj(struct fat *f, uint32_t klaster, uint32_t rozmiar, uint32_t ile,
               int (*pisz)(void *ctx, const void *buf, uint32_t n), void *ctx);
/* The same from byte `od` of the file (0.8.1 step 4: a file in pieces of 1 MiB). `k` remembers where in the chain of
 * clusters the reading stopped: the next piece of the same file goes on from there, not from the first cluster
 * (a 4 GB file has a million clusters). `k` may be NULL; a `k` of another file is not used. `od` past the end:
 * nothing is read. */
struct fat_kursor {
    uint32_t pierwszy, nr, klaster; /* the file (its first cluster), the cluster's place in the chain, the cluster */
};
int fat_czytaj_od(struct fat *f, struct fat_kursor *k, uint32_t klaster, uint32_t rozmiar, uint32_t od, uint32_t ile,
                  int (*pisz)(void *ctx, const void *buf, uint32_t n), void *ctx);

/* The cluster after `k` in its chain: *nast = the next one, or 0 at its end. 0, or FAT_BLAD_* (0.13.0 step 3). */
int fat_nastepny(struct fat *f, uint32_t k, uint32_t *nast);

/* Names (exposed for the host test): a short name 8.3 (11 bytes, flags of entry byte 12) and a long name (UTF-16,
 * up to 0 or the end) into a valid K1 name; "Plik" when nothing usable is left. */
void fat_nazwa_krotka(const uint8_t n[11], uint8_t flagi, char out[K1_DOK_NAZWA]);
void fat_nazwa_dluga(const uint16_t *z, int n, char out[K1_DOK_NAZWA]);

#endif

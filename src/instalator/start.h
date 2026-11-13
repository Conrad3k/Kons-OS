/* K1 instalator — the boot files of a disk's K1-EFI swapped for the stick's (0.9.0 step 3, ADR 0038; start.c). */
#ifndef K1_START_H
#define K1_START_H

#include <stdint.h>

#include "k1_fat32.h"

/* The disk's K1-EFI: read as k1_fat32 does, written and flushed (0, or -1). */
struct start_cel {
    struct k1_fat32_we we;
    int (*pisz)(void *ctx, uint64_t od, const void *buf, uint32_t n);
    int (*synchronizuj)(void *ctx);
};

enum start_wynik {
    START_WYMIENIONE = 0, /* the stick's boot files are on the disk now */
    START_AKTUALNE = 1,   /* the disk had them already: nothing written (but FAT entries of clusters nothing uses,
                           * left by a swap cut off after its switch, set free) */
    START_INNY_UKLAD = 2, /* the disk's file system is laid out otherwise (another FAT, other places of the
                           * slots): nothing written, the old boot files stay */
    START_BLAD_ZRODLO = -1,  /* the stick's file system unreadable */
    START_BLAD_DYSK = -2,    /* the disk's file system unreadable or inconsistent: nothing written */
    START_BLAD_MIEJSCE = -3, /* no room beside the old boot files: nothing written */
    START_BLAD_ZAPIS = -4,   /* a write failed: the old boot files (or, after the switch, the new ones) run */
    START_BLAD_SPRAWDZ = -5, /* the new files read back differ from the manifest: not switched, the old ones run */
};

/* Makes the disk's tree of files the stick's, with grubenv and the slots (they start at the byte offsets `bez`, the
 * same on both: the caller compared the layout blocks) staying where they are on the disk:
 *   1. every other file and folder of the stick copied to clusters the disk's tree does not use, its FAT entries
 *      written (the old tree does not change: a power cut leaves the old boot files);
 *   2. the new tree read back from the disk: its k1_fat32_skrot must be `skrot` (the signed manifest);
 *   3. the switch — one write of the boot sector: its root folder is the new one;
 *   4. the FAT entries of the old files set free (each block written keeps the entries of the new tree as they are).
 * `etap(procent)` from 0 to 100 as it goes. */
enum start_wynik start_wymien(const struct k1_fat32_we *zrodlo, const struct start_cel *cel, const uint64_t bez[3],
                              const uint8_t skrot[32], void (*etap)(int procent));

#endif

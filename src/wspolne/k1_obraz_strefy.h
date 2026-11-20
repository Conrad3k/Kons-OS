/* K1 — a zone image outside the core (0.10.9 step 4, ADR 0050).
 *
 * The browser's zone image (SquashFS + its dm-verity tree, scripts/zbuduj-strefe.sh) is no longer a file of the core:
 * it lies in the system slot it belongs to (k1_uklad.h), right after the slot's inner FAT with k1.efi, and ends where
 * the slot's image ends (the slot manifest's fat_bajty). So the signed slot manifest covers it, updates and the
 * installer copy it with the slot, and A/B keeps the two versions apart without a new place on the disk.
 * The core holds only its SKRÓT OBRAZU STREFY — /rdzen/strefy/<image>.skrot, one line:
 *     <dm-verity root hash, 64 lowercase hex> <data blocks of 4 KiB> <whole image bytes>\n
 * The core is checked by dm-verity with the root hash on the signed kernel's command line, so the zone image is bound
 * to this very build: nadzorca (obraz_strefy.c) finds bytes that verify against this hash — or none.
 * Pure functions (host test: test_obraz_strefy). */
#ifndef K1_OBRAZ_STREFY_H
#define K1_OBRAZ_STREFY_H

#include <stddef.h>
#include <stdint.h>

#define K1_OBRAZ_STREFY_MAKS (1ull << 30) /* bytes of an image at most (data and tree) */
#define K1_OBRAZ_STREFY_FAT_MIN (1u << 20) /* the slot's inner FAT before it: at least this much */

struct k1_obraz_strefy {
    char skrot[65];  /* the root hash, as dm-verity takes it */
    uint64_t bloki;  /* data blocks (4 KiB): what the zone sees */
    uint64_t bajty;  /* the whole image: the data, then the tree */
};

/* Parses the file's `n` bytes: exactly one line as above; the data in whole blocks, a tree after them (at least one
 * block when there is more than one data block), the whole a multiple of 4 KiB, at most K1_OBRAZ_STREFY_MAKS. 0 or -1. */
int k1_obraz_strefy_czytaj(const char *t, size_t n, struct k1_obraz_strefy *o);

/* Where the image begins in a slot of `slot_bajty` whose image has `fat_bajty` bytes (its manifest): it ends there, a
 * 4 KiB aligned start with at least K1_OBRAZ_STREFY_FAT_MIN before it (the inner FAT), the whole before the slot's
 * manifests. The offset from the slot's start, or -1 when it cannot lie there. */
int64_t k1_obraz_strefy_w_miejscu(const struct k1_obraz_strefy *o, uint64_t fat_bajty, uint64_t slot_bajty);

#endif

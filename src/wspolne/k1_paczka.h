/* K1 — the update package and its waiting room (0.10.5 step 2, ADR 0046 T4, T7).
 *
 * The package (k1-<version>.k1a in a release) is what an update from a K1 stick takes from the stick, in one file:
 *     0 header (K1_PACZKA_NAGLOWEK bytes, little-endian):
 *         0 "K1A1"  4 format 1  8 version  12 flags 0  16 slot bytes (the slot's inner image = the slot manifest's
 *         fat_bajty)  24 K1-EFI bytes (its FAT32 = the boot manifest's fat_bajty)  32 front bytes  40 back from
 *         48 back bytes  56..4095 zeros
 *         4096 the slot manifest (format 2), 4608 the manifest of the boot files (format 3), 5120..8191 zeros
 *  8192 the slot's inner image, zeros up to a multiple of 4096
 *       the front of the release's K1-EFI: bytes 0 .. (the first slot) — boot sector, FATs, folders, grubenv
 *       its back: from the end of the second slot, `back bytes` of it (the boot files; what follows is zeros)
 * The two slots of the K1-EFI (A and B, one after the other) are not in it: the system goes into the disk's free slot,
 * and the boot files are read from the front and the back (start.c, ADR 0038). The package has no signature of its
 * own: the announcement signs its SHA-256, the manifests in it sign the system and the boot files.
 *
 * The waiting room: the bytes of the disk's K1-EFI partition after its FAT32 (the partition has 512 MiB, the FAT 280 —
 * src/instalator/gpt.h). From K1_POCZ_OD (the FAT's end rounded up to 1 MiB):
 *     0 the mark (K1_POCZ_ZNAK bytes): "K1W1"  4 format 1  8 version  12 flags 0  16 package bytes (u64)
 *       24 announcement bytes (u32)  28 zeros  32 SHA-256 of the package  64 build id (16)  80..4079 zeros
 *       4080 BLAKE2b-128 of bytes 0..4079 (damage, not attacks: everything is checked again before it is used)
 *  4096 the announcement it came with (k1_ogloszenie.h)
 *  1 MiB the package
 * The mark is written last and zeroed first: no mark = nothing waits. Nothing that starts reads these bytes.
 * Pure functions (host test + fuzzing). */
#ifndef K1_PACZKA_H
#define K1_PACZKA_H

#include <stdint.h>

#include "k1_manifest.h"
#include "k1_uklad.h"

#define K1_PACZKA_NAGLOWEK 8192u
#define K1_PACZKA_MAN_OFF 4096u
#define K1_PACZKA_PRZOD_MAKS (8u << 20)  /* both in the memory of the writing program at an install */
#define K1_PACZKA_TYL_MAKS (24u << 20)
#define K1_POCZ_ZNAK 4096u
#define K1_POCZ_OGL_OFF 4096u
#define K1_POCZ_PACZKA_OFF (1u << 20)

struct k1_paczka {
    uint32_t wersja;
    uint64_t slot_bajty, efi_bajty, przod_bajty, tyl_od, tyl_bajty;
    struct k1_manifest slot, start; /* parsed (their signatures: the caller) */
    uint8_t man_slot[K1_MANIFEST_BAJTY], man_start[K1_MANIFEST_BAJTY];
};

/* Where the slot's image, the front and the back begin in the package, and its whole length. */
static inline uint64_t k1_paczka_slot_od(void) { return K1_PACZKA_NAGLOWEK; }
static inline uint64_t k1_paczka_przod_od(const struct k1_paczka *p)
{
    return K1_PACZKA_NAGLOWEK + ((p->slot_bajty + 4095u) & ~4095ull);
}
static inline uint64_t k1_paczka_tyl_od(const struct k1_paczka *p) { return k1_paczka_przod_od(p) + p->przod_bajty; }
static inline uint64_t k1_paczka_bajty(const struct k1_paczka *p) { return k1_paczka_tyl_od(p) + p->tyl_bajty; }

/* Parses the header (K1_PACZKA_NAGLOWEK bytes): the fields, both manifests (formats 2 and 3, one release: the same
 * version, build id, the package's version), the sizes against them. 0 or -1. */
int k1_paczka_czytaj(const uint8_t *b, struct k1_paczka *p);

/* The front's layout block (at K1_UKLAD_OFF of the front) agrees with the package: the K1-EFI's size, grubenv inside
 * the front, the two slots one after the other right after it, the back right after them. 0 or -1. */
int k1_paczka_uklad(const struct k1_paczka *p, const uint8_t *przod, struct k1_uklad *u);

/* Builds the header from `p` (version, sizes and both manifests' bytes); 0, or -1 when they do not agree. */
int k1_paczka_zbuduj(struct k1_paczka *p, uint8_t *b);

/* The waiting room of a K1-EFI partition whose FAT has `fat_bajty`. */
static inline uint64_t k1_pocz_od(uint64_t fat_bajty) { return (fat_bajty + (1u << 20) - 1) & ~(uint64_t)((1u << 20) - 1); }

struct k1_pocz {
    uint32_t wersja, ogl_bajty;
    uint64_t paczka_bajty;
    uint8_t sha256[32], wydanie[16];
};
int k1_pocz_czytaj(const uint8_t *b, struct k1_pocz *z); /* K1_POCZ_ZNAK bytes; 0 or -1 */
void k1_pocz_zbuduj(const struct k1_pocz *z, uint8_t *b);

#endif

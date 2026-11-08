/* K1 — the manifest of a system image (M6a, ADR 0021).
 *
 * In the "K1-EFI" partition (USB image and installed disk) at K1_MANIFEST_OFF, behind the FAT file
 * system of K1_MANIFEST_OFF bytes, 512 bytes, little-endian:
 *    0  "K1M1"                       48  BLAKE2b-256 of bytes 0 .. fat_bajty of the partition
 *    4  format 1                     80  key id (BLAKE2b-64 of the public key), zeros if unsigned
 *    8  version (major<<16|minor<<8|patch)
 *   12  flags (bit 0: signed)        88  zeros up to 448
 *   16  fat_bajty (u64)             448  EdDSA signature (Monocypher, BLAKE2b) over bytes 0..447
 *   24  version text (24, NUL padded)    (zeros if unsigned)
 * Format 2 (0.7.0, ADR 0022) is the manifest of a system slot (k1_uklad.h), at K1_SLOT_MANIFEST_OFF of the
 * slot: the same fields, fat_bajty up to K1_SLOT_MANIFEST_OFF, and at 88 the build id (16 bytes: what the
 * running system finds in /rdzen/system/wydanie), zeros 104..447. Format 1 stays at 47 MiB of the stick's
 * K1-EFI so that K1 0.6.2–0.6.3 can update itself to 0.7.0.
 * Pure functions (host test + fuzzing); hashing of the FAT area is done by the caller in pieces. */
#ifndef K1_MANIFEST_H
#define K1_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define K1_MANIFEST_OFF (47u * 1024u * 1024u)
#define K1_MANIFEST_BAJTY 512u
#define K1_MANIFEST_PODPISANY 1u

struct k1_manifest {
    uint32_t format; /* 1 (K1-EFI, 0.6.2) or 2 (slot, 0.7.0); 0 when building = 1 */
    uint32_t wersja; /* major<<16 | minor<<8 | patch */
    uint32_t flagi;
    uint64_t fat_bajty;
    char tekst[24];
    uint8_t skrot[32];
    uint8_t klucz_id[8];
    uint8_t podpis[64];
    uint8_t wydanie[16]; /* format 2 */
};

/* "0.6.2" → 0x000602; -1 if not three numbers 0..255. */
int32_t k1_wersja_z_tekstu(const char *t);

/* Parses 512 bytes (format only, no signature check). 0 or -1. */
int k1_manifest_czytaj(const uint8_t *buf, struct k1_manifest *m);

/* Builds the 512 bytes; with `klucz_tajny` (64 bytes, crypto_eddsa_key_pair) the manifest is signed. */
void k1_manifest_zbuduj(struct k1_manifest *m, const uint8_t *klucz_tajny, const uint8_t *klucz_publiczny,
                        uint8_t *buf);

/* Whether `buf` (512 bytes) carries a valid signature of `klucz_publiczny`: 0 yes, -1 no. */
int k1_manifest_sprawdz_podpis(const uint8_t *buf, const uint8_t klucz_publiczny[32]);

/* BLAKE2b-64 of a public key: what the manifest names as its key. */
void k1_klucz_id(const uint8_t klucz_publiczny[32], uint8_t id[8]);

#endif

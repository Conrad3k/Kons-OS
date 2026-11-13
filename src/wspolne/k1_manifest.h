/* K1 — the manifests of a release (M6a, ADR 0021; format 2: ADR 0022; format 3: 0.9.0, ADR 0038).
 *
 * 512 bytes, little-endian:
 *    0  "K1M1"                       48  BLAKE2b-256 (format 2: of the slot's inner image; 3: of the boot files)
 *    4  format (2 or 3)              80  key id (BLAKE2b-64 of the public key), zeros if unsigned
 *    8  version (major<<16|minor<<8|patch)
 *   12  flags (bit 0: signed)        88  the build id (16 bytes: what the running system finds in
 *   16  fat_bajty (u64)                  /rdzen/system/wydanie), zeros 104..447
 *   24  version text (24, NUL padded) 448  EdDSA signature (Monocypher, BLAKE2b) over bytes 0..447 (zeros if unsigned)
 * Format 2 — the manifest of a system slot (k1_uklad.h), at K1_SLOT_MANIFEST_OFF of the slot: fat_bajty up to
 * K1_SLOT_MANIFEST_OFF bytes of the slot are its inner image.
 * Format 3 — the manifest of the boot files, right after it (K1_SLOT_START_OFF): fat_bajty = the K1-EFI file
 * system it was made for, the digest = k1_fat32_skrot of its files (shim, GRUB, grub.cfg … — everything but
 * grubenv and the slots). The same version and build id as the slot manifest beside it.
 * (Format 1, the manifest of a whole K1-EFI at 47 MiB for K1 0.6.2–0.6.3, is gone since 0.9.0.)
 * Pure functions (host test + fuzzing); hashing is done by the caller in pieces. */
#ifndef K1_MANIFEST_H
#define K1_MANIFEST_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define K1_MANIFEST_BAJTY 512u
#define K1_MANIFEST_PODPISANY 1u
#define K1_MANIFEST_SLOT 2u  /* format: a system slot */
#define K1_MANIFEST_START 3u /* format: the boot files */
#define K1_MANIFEST_EFI_MAKS (1ull << 30) /* format 3: fat_bajty at most */

struct k1_manifest {
    uint32_t format; /* K1_MANIFEST_SLOT or K1_MANIFEST_START */
    uint32_t wersja; /* major<<16 | minor<<8 | patch */
    uint32_t flagi;
    uint64_t fat_bajty;
    char tekst[24];
    uint8_t skrot[32];
    uint8_t klucz_id[8];
    uint8_t podpis[64];
    uint8_t wydanie[16];
};

/* "0.6.2" → 0x000602; -1 if not three numbers 0..255. */
int32_t k1_wersja_z_tekstu(const char *t);
/* 0x000602 → "0.6.2" (inline: the Instalator window shows versions without linking the manifest code) */
static inline void k1_wersja_tekst(uint32_t w, char *buf, size_t n)
{
    snprintf(buf, n, "%u.%u.%u", (unsigned)(w >> 16 & 0xff), (unsigned)(w >> 8 & 0xff), (unsigned)(w & 0xff));
}

/* Parses 512 bytes (format only, no signature check). 0 or -1. */
int k1_manifest_czytaj(const uint8_t *buf, struct k1_manifest *m);

/* Builds the 512 bytes (m->format: 2 or 3); with `klucz_tajny` (64 bytes, crypto_eddsa_key_pair) the manifest is signed. */
void k1_manifest_zbuduj(struct k1_manifest *m, const uint8_t *klucz_tajny, const uint8_t *klucz_publiczny,
                        uint8_t *buf);

/* Whether `buf` (512 bytes) carries a valid signature of `klucz_publiczny`: 0 yes, -1 no. */
int k1_manifest_sprawdz_podpis(const uint8_t *buf, const uint8_t klucz_publiczny[32]);

/* BLAKE2b-64 of a public key: what the manifest names as its key. */
void k1_klucz_id(const uint8_t klucz_publiczny[32], uint8_t id[8]);

#endif

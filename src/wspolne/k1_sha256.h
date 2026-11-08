/* K1 — SHA-256 (FIPS 180-4), for what other software expects it: MokManager's password check (MokAuth,
 * 0.7.0, ADR 0022). Everything of K1's own uses BLAKE2b (Monocypher). */
#ifndef K1_SHA256_H
#define K1_SHA256_H

#include <stddef.h>
#include <stdint.h>

struct k1_sha256 {
    uint32_t h[8];
    uint64_t dl; /* bytes so far */
    uint8_t blok[64];
    size_t n;
};

void k1_sha256_init(struct k1_sha256 *c);
void k1_sha256_dodaj(struct k1_sha256 *c, const void *dane, size_t n);
void k1_sha256_koniec(struct k1_sha256 *c, uint8_t out[32]);

#endif

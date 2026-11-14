/* K1 — SHA-1 and what Wi-Fi builds on it (0.9.1 step 2, ADR 0041 T4): HMAC-SHA1, PBKDF2-SHA1 (the password of a
 * network → its key) and the PRF of IEEE 802.11 (the keys of one connection). Only for WPA2, which fixes these
 * algorithms; everything of K1's own uses BLAKE2b (Monocypher). Checked with the official test vectors (FIPS 180-4,
 * RFC 2202, RFC 6070, IEEE 802.11 annex J) in testy/host/test_krypto.c. Pure functions. */
#ifndef K1_SHA1_H
#define K1_SHA1_H

#include <stddef.h>
#include <stdint.h>

#include "k1_wymaz.h"

#define K1_SHA1_DL 20

struct k1_sha1 {
    uint32_t h[5];
    uint64_t dl; /* bytes so far */
    uint8_t blok[64];
    size_t n;
};

void k1_sha1_init(struct k1_sha1 *c);
void k1_sha1_dodaj(struct k1_sha1 *c, const void *dane, size_t n);
void k1_sha1_koniec(struct k1_sha1 *c, uint8_t out[K1_SHA1_DL]);

/* HMAC-SHA1 (RFC 2104) of `n` bytes of `dane` with a key of any length. */
void k1_hmac_sha1(const void *klucz, size_t dk, const void *dane, size_t n, uint8_t out[K1_SHA1_DL]);
/* The same over two pieces (dane1 || dane2) — the PRF and the MIC of an EAPOL frame need it. */
void k1_hmac_sha1_2(const void *klucz, size_t dk, const void *d1, size_t n1, const void *d2, size_t n2,
                    uint8_t out[K1_SHA1_DL]);

/* PBKDF2 with HMAC-SHA1 (RFC 8018 5.2): `wynik` of `dl` bytes from the password and the salt after `powtorzenia`. */
void k1_pbkdf2_sha1(const void *haslo, size_t dh, const void *sol, size_t ds, unsigned powtorzenia, uint8_t *wynik,
                    size_t dl);

/* The PRF of IEEE 802.11-2020 12.7.1.2: `dl` bytes from key K, the label A (without its final zero) and data B —
 * HMAC-SHA1(K, A || 0 || B || i) for i = 0, 1, … */
void k1_prf_80211(const uint8_t *k, size_t dk, const char *etykieta, const uint8_t *b, size_t db, uint8_t *wynik,
                  size_t dl);


#endif

/* K1 — AES-128 (FIPS 197) and its key wrap (RFC 3394), only for Wi-Fi (0.9.1 step 2, ADR 0041 T4): the router sends
 * the group key of the network wrapped with a key of the connection (EAPOL-Key, descriptor version 2). The data itself
 * is encrypted by the kernel or the card (CCMP), not here. Small and plain (tables, not constant time: the keys it
 * handles live for one connection, and nothing else runs with them in the sandbox of siec). Checked with FIPS 197 and
 * RFC 3394 vectors (testy/host/test_krypto.c). Pure functions. */
#ifndef K1_AES_H
#define K1_AES_H

#include <stddef.h>
#include <stdint.h>

struct k1_aes {
    uint8_t rk[176]; /* the round keys of AES-128 */
};

void k1_aes_klucz(struct k1_aes *a, const uint8_t klucz[16]);
void k1_aes_szyfruj(const struct k1_aes *a, const uint8_t we[16], uint8_t wy[16]);
void k1_aes_odszyfruj(const struct k1_aes *a, const uint8_t we[16], uint8_t wy[16]);

/* RFC 3394 with the default initial value A6…A6: `n` bytes (a multiple of 8, at least 16) → n + 8 bytes. */
void k1_aes_zawin(const uint8_t kek[16], const uint8_t *dane, size_t n, uint8_t *wynik);
/* The reverse: `n` bytes (a multiple of 8, at least 24) → n - 8 bytes. 0, or -1 when the integrity check fails (a wrong
 * key or changed data — then nothing of `wynik` may be used). */
int k1_aes_rozwin(const uint8_t kek[16], const uint8_t *dane, size_t n, uint8_t *wynik);

#endif

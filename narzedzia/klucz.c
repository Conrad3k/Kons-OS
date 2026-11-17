/* K1 — the release key at build time (narzedzia/podpisz.c, narzedzia/wydanie.c; ADR 0021): from the environment variable
 * K1_KLUCZ_PODPISU (32 random bytes in base64 — the GitHub Actions secret; never in the repository). */
#include "klucz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monocypher.h"

static int b64(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+' || c == '-')
        return 62;
    if (c == '/' || c == '_')
        return 63;
    return -1;
}

/* The seed from the environment: 1 = there, 0 = none (developer build). Exits on a malformed one. */
static int ziarno(uint8_t out[32])
{
    const char *s = getenv("K1_KLUCZ_PODPISU");
    uint32_t akumulator = 0;
    int bity = 0, n = 0;
    if (s == NULL || *s == '\0')
        return 0;
    for (; *s != '\0'; s++) {
        int v;
        if (*s == '=' || *s == '\n' || *s == '\r' || *s == ' ')
            continue;
        v = b64((unsigned char)*s);
        if (v < 0) {
            fprintf(stderr, "podpisz: K1_KLUCZ_PODPISU nie jest base64\n");
            exit(2);
        }
        akumulator = akumulator << 6 | (uint32_t)v;
        bity += 6;
        if (bity >= 8) {
            bity -= 8;
            if (n >= 32) {
                fprintf(stderr, "podpisz: K1_KLUCZ_PODPISU ma więcej niż 32 bajty\n");
                exit(2);
            }
            out[n++] = (uint8_t)(akumulator >> bity);
        }
    }
    if (n != 32) {
        fprintf(stderr, "podpisz: K1_KLUCZ_PODPISU musi mieć dokładnie 32 bajty (ma %d)\n", n);
        exit(2);
    }
    return 1;
}

int klucze(uint8_t tajny[64], uint8_t publiczny[32])
{
    uint8_t z[32];
    if (!ziarno(z))
        return 0;
    crypto_eddsa_key_pair(tajny, publiczny, z); /* wipes z */
    return 1;
}


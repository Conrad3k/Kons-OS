/* K1 — the release key at build time (klucz.c). */
#ifndef K1_NARZ_KLUCZ_H
#define K1_NARZ_KLUCZ_H

#include <stdint.h>

/* The key pair from K1_KLUCZ_PODPISU: 1, or 0 when there is none (a developer build). Exits on a malformed one. */
int klucze(uint8_t tajny[64], uint8_t publiczny[32]);

#endif

/* K1 — the signature of the release announcement (0.10.5 step 2, ADR 0046 T3): the build's tool signs, the writing
 * program checks. The format: k1_ogloszenie.h. */
#ifndef K1_OGLOSZENIE_PODPIS_H
#define K1_OGLOSZENIE_PODPIS_H

#include <stddef.h>
#include <stdint.h>

#include "k1_ogloszenie.h"

/* The 64 bytes after the signed part (`dl` bytes, k1_ogloszenie_zbuduj): the signature, or zeros without a key
 * (a developer build; o->klucz_id must be the key's id — or zeros — before building). */
void k1_ogloszenie_podpisz(uint8_t *b, size_t dl, const uint8_t klucz_tajny[64]);

/* 0: `n` bytes are an announcement signed by `klucz_publiczny`; -1 otherwise. */
int k1_ogloszenie_sprawdz_podpis(const uint8_t *b, size_t n, const uint8_t klucz_publiczny[32]);

#endif

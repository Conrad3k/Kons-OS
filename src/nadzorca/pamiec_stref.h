/* K1 nadzorca — the memory of the zones (0.10.1, ADR 0044): how much a zone may get. Pure (test_pamiec_stref). */
#ifndef K1_PAMIEC_STREF_H
#define K1_PAMIEC_STREF_H

#include <stdint.h>

/* What K1 keeps for itself, by the computer's memory (D3's recommendation; one table: pamiec_stref.c). */
uint32_t pamiec_rezerwa_k1(uint64_t ram_mib);

/* A zone's upper limit in MiB (D2, the owner 2026-11-16: the computer's memory minus K1's reserve), never below its
 * memory at the start (its row of K1_STREFY), whole 2 MiB, at most PAMIEC_STREFY_MAKS. */
#define PAMIEC_STREFY_MAKS 65536u /* = K1_STREFA_PAMIEC_MAKS (k1_komunikat.h) */
uint32_t pamiec_granica(uint64_t ram_mib, uint32_t start_mib);

#endif

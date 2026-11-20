/* K1 — the numbers of the graphics card in Zasoby (0.10.10 step 2): pure arithmetic on what nadzorca reads in sysfs —
 * the time the card spent idle (i915: power/rc6_residency_ms, RC6 = the card asleep) and the energy counter of RAPL's
 * "uncore" domain (powercap: energy_uj, wraps at max_energy_range_uj). Host test: test_karta_pomiar. */
#ifndef K1_KARTA_POMIAR_H
#define K1_KARTA_POMIAR_H

#include <stdint.h>

#include "k1_komunikat.h" /* K1_KARTA_MHZ_MAKS, K1_MOC_MAKS_MW */

/* How busy the card was between two readings, ‰ (0..1000): the share of the time it was not asleep. -1: unknown
 * (no time between them, the counter went back). */
int32_t k1_karta_obciazenie(uint64_t rc6_ms0, uint64_t rc6_ms1, double dt_s);

/* The card's power draw between two readings of the energy counter, mW (0..K1_MOC_MAKS_MW); the counter wraps at
 * `zakres_uj` (0: unknown — a counter that went back is then -1). -1: unknown. */
int32_t k1_karta_moc_mw(uint64_t e0_uj, uint64_t e1_uj, uint64_t zakres_uj, double dt_s);

#endif

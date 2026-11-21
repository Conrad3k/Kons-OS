/* K1 — the numbers of the graphics card in Zasoby (0.10.10 step 2): pure arithmetic on what nadzorca reads in sysfs —
 * the time the card spent idle (i915: rc6_residency_ms, RC6 = the card asleep) and the energy counter of RAPL's
 * "uncore" domain (powercap: energy_uj, wraps at max_energy_range_uj) — and (0.10.11 step 3) which file of the card each
 * reading comes from. Host test: test_karta_pomiar. */
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

/* The readings of an i915 card (0.10.11 step 3, P17) — one table K1_KARTA_PLIKI in k1_karta_pomiar.c: each from the
 * first file of its row that exists under the card's directory (drm/cardN). First gt/gt0/ — the graphics part: Meteor /
 * Arrow Lake have a second part (gt1, media), and the card's own files then give the smaller (RC6) or the larger (clocks)
 * of both (i915: gt/intel_gt_sysfs_pm.c). Then the card's own files (every i915 has them). */
enum k1_karta_odczyt {
    K1_KARTA_SEN,       /* the time the card slept since it started, ms */
    K1_KARTA_TAKT,      /* its clock now, MHz */
    K1_KARTA_TAKT_NAJW, /* its highest clock, MHz */
    K1_KARTA_ODCZYTY
};

/* The reading `co` of the card whose directory is open as `katalog`: 0 and *v, -1 (no such file, not a number). */
int k1_karta_czytaj(int katalog, enum k1_karta_odczyt co, uint64_t *v);

#endif

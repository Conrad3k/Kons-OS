/* K1 — the sound path ("tor dźwięku", 0.12.3 step 1, ADR 0055): what the sound service does with the clients' frames
 * between their rings and the card. Pure functions (host test test_dzw_tor, fuzzing fuzz_dzwiek).
 *
 *   rings (S16, F32) → floats, a session at its volume → the sum → × the volume of K1 → the limiter → the card's format
 *                                                                                       (TPDF dither, 16/20/24 bits)
 *
 * Inside, a sample is a float, ±1.0 the full scale: a float has 24 bits of precision at any loudness, so a quiet sound
 * keeps every bit until the end of the path (0.12.0 divided integers and lost them — at 5 % the harmonics were 14 dB
 * below the tone). The sum may go beyond ±1.0 (two loud streams): the limiter lowers it smoothly instead of clipping.
 * At the card the samples are rounded to its bits with a TPDF dither — but only in a period where some sample is not
 * already a whole step of the card: one 16-bit stream at 100 % reaches a 16-bit card sample for sample ("przezroczystość")
 * and silence stays digital silence. The dither's noise is deterministic (xorshift32): the same numbers on every computer.
 * No libm, no -ffast-math: the same results on the host, in the fuzzer and in K1. */
#ifndef K1_DZW_TOR_H
#define K1_DZW_TOR_H

#include <stdint.h>

#include "k1_dzwiek.h"

/* The gain of a volume 0..100 (a cube: the ear hears the loudness in steps of the power, ~ -18 dB at 50 % — exactly
 * 0.125), 0..1; muted: 0. Every step of the volume is louder than the one before. */
float k1_dzw_wzmocnienie(int glosnosc, int wyciszone);

/* Adds `ramki` frames of a ring's samples (`probki`: K1_DZW_FORMAT_*, no alignment assumed) at the gain `wzm` into the
 * sum (2 × ramki floats). S16: s / 32768, exact. F32: a sample that is not a finite number becomes 0, one beyond ±1.0
 * becomes ±1.0 — the samples of a zone are numbers from a hostile guest. Returns how many samples were such (0 for S16). */
uint32_t k1_dzw_dodaj(float *suma, const uint8_t *probki, int format, uint32_t ramki, float wzm);

/* The sum × gain (the volume of K1), in place. */
void k1_dzw_glosnosc(float *suma, uint32_t ramki, float wzm);

/* The limiter: the sum never beyond ±1.0 after it. Where a frame would go beyond, the gain of both channels comes down to
 * what it needs — gently, starting up to K1_DZW_OGR_ATAK frames before it within the same period (the service has the
 * whole period before it writes the card, so this adds no delay) — and comes back up to 1.0 slowly (6 dB in
 * K1_DZW_OGR_POWROT frames). Below full scale and at the gain 1.0 it changes nothing. */
#define K1_DZW_OGR_ATAK 48u     /* 1 ms: 6 dB down at most per this many frames before a loud frame */
#define K1_DZW_OGR_POWROT 2400u /* 50 ms: 6 dB back up */
struct k1_dzw_ogranicznik {
    float g; /* the gain at the end of the last period (1.0: not limiting) */
};
void k1_dzw_ogranicznik_start(struct k1_dzw_ogranicznik *o);
/* `ramki` ≤ K1_DZW_OKRES. Returns 1 when it lowered anything in this period (the service counts such periods). */
int k1_dzw_ogranicz(struct k1_dzw_ogranicznik *o, float *suma, uint32_t ramki);

/* The card's samples: `bajty` each (2: int16, 3: 24 bits packed, 4: an int32 word, its top `bity` bits used — so HDA
 * takes 20 and 24 bits), little endian; `bity` 16..24 the card's real resolution (≤ bajty × 8). Rounded to a step of
 * the card with a TPDF dither (±1 step), except in a period whose every sample already is a whole step: then as it is.
 * Returns 1 when it dithered. */
struct k1_dzw_dither {
    uint32_t los; /* xorshift32, never 0 */
};
void k1_dzw_dither_start(struct k1_dzw_dither *d);
int k1_dzw_na_karte(uint8_t *wyj, const float *suma, uint32_t ramki, int bajty, int bity, struct k1_dzw_dither *d);

#endif

/* K1 — the microphone (0.13.5 step 1, ADR 0065): what the sound service does with the frames the card recorded before a
 * listener gets them. Pure functions (host test test_mikrofon, fuzzing fuzz_mikrofon).
 *
 *   the card (S32_LE or S16_LE, 1 or 2 channels) → floats, stereo → × the level of the microphone → silence when muted or
 *   held → the level for the bar (Ustawienia, the sign) → each listener's ring in its format (K1_DZW_FORMATY)
 *
 * The listener's ring is the listener's own memfd (sealed against shrinking and growing): the service only writes into it,
 * never reads it, and counts the frames itself (it takes no position from the client). A slow listener loses its own
 * oldest frames — nobody else's. Inside, a sample is a float, ±1.0 the full scale; no libm (the same numbers on the host,
 * in the fuzzer and in K1). The level is a number 0..100, never a sample: what Ustawienia gets (D3, T4). */
#ifndef K1_MIKROFON_H
#define K1_MIKROFON_H

#include <stdint.h>

#include "k1_dzwiek.h"

/* The gain of the microphone's level 0..100 (Ustawienia → Dźwięk → Mikrofon): 0 — silence, 50 — 0 dB (as the card gives
 * it), 100 — +20 dB; 0.4 dB a step (a built-in microphone is often quiet: its boost K1 leaves as the kernel sets it). */
float k1_mik_wzmocnienie(int poziom);

/* `ramki` frames of the card (`bajty` 2: S16_LE, 4: S32_LE; `kanaly` 1 or 2; no alignment assumed) into 2 × ramki
 * floats — one channel goes to both. */
void k1_mik_z_karty(float *wyj, const uint8_t *we, uint32_t ramki, int bajty, int kanaly);

/* × gain, in place, then within ±1.0 (the microphone has no limiter: +20 dB on a loud voice clips, as on any device). */
void k1_mik_wzmocnij(float *s, uint32_t ramki, float wzm);

/* The level of the loudest sample of `ramki` frames: 0..100 for −60..0 dBFS (0.6 dB a step), 0 below. */
int k1_mik_poziom(const float *s, uint32_t ramki);

/* The test program's tone (the test image only, DZWIEK_MIKROFON TESTOWY — QEMU's card records silence): 1 kHz at
 * −12 dBFS added to both channels; *faza counts its frames (one period of the tone is 48 frames). */
#define K1_MIK_TON_HZ 1000u
#define K1_MIK_TON_SZCZYT 0.25f
void k1_mik_ton(float *s, uint32_t ramki, uint32_t *faza);

/* Writes `ramki` frames into the ring `pierscien` of a listener (K1_DZW_PIERSCIEN frames of format `format`) from frame
 * *zapisane (modulo the ring), wrapping at its end, and counts them. S16: rounded, within ±32767. */
void k1_mik_do_pierscienia(uint8_t *pierscien, int format, uint32_t *zapisane, const float *s, uint32_t ramki);

#endif

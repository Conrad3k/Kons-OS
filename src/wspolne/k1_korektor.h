/* K1 — the system equalizer ("korektor", 0.12.3 step 2, ADR 0055 points 9–13; the owner's D2: A — ten bands as in a
 * graphic equalizer). Pure functions (host test test_korektor, fuzzing fuzz_korektor): the sound service runs it on the sum
 * of the streams, before the volume of K1; Ustawienia → Dźwięk → Korektor and gui show and remember its settings.
 *
 * Ten bands an octave apart (31 Hz … 16 kHz, the table K1_KOR_PASMA), each −12…+12 dB in steps of 0.5 dB — kept as a
 * whole number of half-decibels (int8_t −24…24). Each band is a peaking filter (biquad, R. Bristow-Johnson's "Audio EQ
 * Cookbook": at its middle exactly its gain, Q = √2 — one octave wide). Before the filters the level comes down by the
 * loudest point of the whole curve (computed on a grid of 20 Hz … 20 kHz and at each band's middle), so the equalizer
 * never makes anything louder than 100 % of K1 (no clipping; the limiter of step 1 stays at the end of the path). A flat
 * equalizer (all bands 0) computes nothing: the path stays transparent sample for sample. A change of the settings crosses
 * from the old filters to the new ones over one period (no click).
 *
 * The coefficients are computed once per change, not per sample; their sine, cosine, exponent and root are our own series
 * (no libm, no -ffast-math: the same numbers on the host, in the fuzzer and in K1 — as in k1_dzw_tor.c). The filters run
 * in doubles (a 31 Hz filter at 48 kHz in floats would add noise). The settings come from a steering client: every band is
 * checked (k1_kor_z_pol) before anything is computed from it. */
#ifndef K1_KOREKTOR_H
#define K1_KOREKTOR_H

#include <stdint.h>

#define K1_KOR_PASMA_N 10
#define K1_KOR_MAKS 24 /* half-decibels: ±12 dB */

/* The bands (rule 5: one table): X(index, the middle in Hz, the label under its slider). Octaves of 31.25 Hz. */
#define K1_KOR_PASMA(X)                                                                                            \
    X(0, 31.25, "31") X(1, 62.5, "62") X(2, 125.0, "125") X(3, 250.0, "250") X(4, 500.0, "500") X(5, 1000.0, "1k") \
    X(6, 2000.0, "2k") X(7, 4000.0, "4k") X(8, 8000.0, "8k") X(9, 16000.0, "16k")
double k1_kor_srodek(int pasmo);           /* Hz; 0 outside 0..K1_KOR_PASMA_N-1 */
const char *k1_kor_etykieta(int pasmo);    /* "31" … "16k"; "" outside */

/* The ready settings (D2; rule 5: a new one is a row): X(NAME, the name in K1, ten bands in half-decibels). */
#define K1_KOR_GOTOWE(X)                                                                                  \
    X(PLASKI, "Płaski", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)                                                     \
    X(WIECEJ_BASU, "Więcej basu", 12, 10, 8, 4, 2, 0, 0, 0, 0, 0)          /* +6 dB at 31 Hz, down to 0 at 1k */ \
    X(WYRAZNY_GLOS, "Wyraźny głos", -4, -4, -2, 0, 2, 4, 6, 6, 2, 0)       /* the voice's 1–4 kHz up */         \
    X(LAPTOP, "Laptop", -12, -8, 0, 2, -4, -2, 2, 4, 4, 2)  /* no bass small speakers cannot play, less "box" */ \
    X(SLUCHAWKI, "Słuchawki", 4, 4, 2, 0, 0, 0, -2, -2, 2, 4)              /* softer 2–4 kHz, air on top */     \
    X(MUZYKA, "Muzyka", 8, 6, 4, 0, -2, -2, 0, 4, 6, 6)                    /* a gentle "V" */                   \
    X(FILM, "Film", 6, 6, 4, 0, 0, 2, 4, 4, 2, 0)                          /* the rumble and the dialogue */
#define K1_X_KOR_GOTOWE(n, ...) K1_KOR_##n,
enum { K1_KOR_GOTOWE(K1_X_KOR_GOTOWE) K1_KOR_GOTOWYCH };
const char *k1_kor_gotowe_nazwa(int nr);                    /* "" outside 0..K1_KOR_GOTOWYCH-1 */
void k1_kor_gotowe_pasma(int nr, int8_t pasma[K1_KOR_PASMA_N]); /* flat outside */
/* Which ready setting these bands are (the first equal row), or -1: "Własne" (the name of any other). */
int k1_kor_rozpoznaj(const int8_t pasma[K1_KOR_PASMA_N]);
const char *k1_kor_nazwa(const int8_t pasma[K1_KOR_PASMA_N]); /* the ready setting's name, or "Własne" */
int k1_kor_plaski(const int8_t pasma[K1_KOR_PASMA_N]);       /* 1: every band 0 */

/* In a message (DZWIEK_KOREKTOR, DZWIEK_KOREKTOR_STAN: b, c, d): four bands a field, a byte each (two's complement), band
 * i in byte i % 4 of field i / 4, the last field's two top bytes 0. Inline: the check of the messages (k1_kom_dzwiek.c,
 * in every program) needs it without the filters. */
static inline void k1_kor_do_pol(const int8_t pasma[K1_KOR_PASMA_N], int32_t pola[3])
{
    uint32_t u[3] = { 0, 0, 0 };
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        u[i / 4] |= (uint32_t)(uint8_t)pasma[i] << (8 * (i % 4));
    for (int j = 0; j < 3; j++)
        pola[j] = (int32_t)u[j];
}
/* 0 and the bands, or -1 (pasma unchanged) when a band is beyond ±K1_KOR_MAKS or the unused bytes are not 0. */
static inline int k1_kor_z_pol(const int32_t pola[3], int8_t pasma[K1_KOR_PASMA_N])
{
    int8_t p[K1_KOR_PASMA_N];
    if (((uint32_t)pola[2] >> 16) != 0)
        return -1; /* bands 10 and 11 do not exist */
    for (int i = 0; i < K1_KOR_PASMA_N; i++) {
        p[i] = (int8_t)(uint8_t)((uint32_t)pola[i / 4] >> (8 * (i % 4)));
        if (p[i] > K1_KOR_MAKS || p[i] < -K1_KOR_MAKS)
            return -1;
    }
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        pasma[i] = p[i];
    return 0;
}

/* One band's filter, its coefficients divided by a0. */
struct k1_kor_filtr {
    double b0, b1, b2, a1, a2;
};
/* The filters of ten bands and the level before them (`wzm` ≤ 1). Computed once per change. */
struct k1_kor_ustawienie {
    int8_t pasma[K1_KOR_PASMA_N];
    int plaski;
    double wzm;
    struct k1_kor_filtr f[K1_KOR_PASMA_N];
};
void k1_kor_policz(struct k1_kor_ustawienie *u, const int8_t pasma[K1_KOR_PASMA_N]);
/* The curve's gain at `hz` (0..24 kHz) as a power ratio |H|² — before the level `wzm` (tests: the dB at a band's middle). */
double k1_kor_moc(const struct k1_kor_ustawienie *u, double hz);

/* The running equalizer of the service: what it plays now, the filters' memory (two inputs and outputs of each band and
 * channel) and the two last frames of the sum (a filter that starts from a flat equalizer starts from them, without a
 * jump). */
struct k1_korektor {
    struct k1_kor_ustawienie teraz, nowe;
    int zmiana; /* `nowe` takes over in the next period */
    double x[K1_KOR_PASMA_N][2][2], y[K1_KOR_PASMA_N][2][2];
    double ostatnie[2][2]; /* [0]: the last frame, [1]: the one before it; by channel */
};
void k1_kor_start(struct k1_korektor *k); /* flat */
/* New bands (from the next period; equal to those playing: nothing). */
void k1_kor_ustaw(struct k1_korektor *k, const int8_t pasma[K1_KOR_PASMA_N]);
/* The equalizer on `ramki` ≤ K1_DZW_OKRES stereo frames of the sum, in place. Flat and no change: nothing at all.
 * Returns 1 when it computed anything (the service measures that time). */
int k1_kor_licz(struct k1_korektor *k, float *suma, uint32_t ramki);

/* Our own functions (exported for the test): sine and cosine for |x| ≤ 4, e^x for |x| ≤ 8, the square root for x ≥ 0. */
double k1_kor_sin(double x);
double k1_kor_cos(double x);
double k1_kor_exp(double x);
double k1_kor_pierw(double x);

#endif

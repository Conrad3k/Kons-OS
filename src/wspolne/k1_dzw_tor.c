/* K1 — the sound path (0.12.3 step 1, ADR 0055): floats from the rings, the sum, the volume, the limiter and the card's
 * samples with a TPDF dither. Pure functions (host test test_dzw_tor, fuzzing fuzz_dzwiek). See k1_dzw_tor.h. */
#include "k1_dzw_tor.h"

#include <math.h>
#include <string.h>

#define S16_SKALA (1.0f / 32768.0f) /* an int16 into ±1.0: exact (a power of two) */

static float bez_znaku(float x)
{
    return x < 0.0f ? -x : x;
}

float k1_dzw_wzmocnienie(int glosnosc, int wyciszone)
{
    double v;
    if (wyciszone || glosnosc <= 0)
        return 0.0f;
    if (glosnosc >= K1_DZW_GLOSNOSC_MAKS)
        return 1.0f;
    v = (double)glosnosc / K1_DZW_GLOSNOSC_MAKS;
    return (float)(v * v * v);
}

uint32_t k1_dzw_dodaj(float *suma, const uint8_t *probki, int format, uint32_t ramki, float wzm)
{
    uint32_t zle = 0;
    for (size_t i = 0; i < (size_t)ramki * K1_DZW_KANALY; i++) {
        float x;
        if (format == K1_DZW_FORMAT_F32) {
            memcpy(&x, probki + 4 * i, sizeof x); /* a hostile guest's number: checked before it counts */
            if (!isfinite(x) || x > 1.0f || x < -1.0f) {
                x = !isfinite(x) ? 0.0f : x > 0.0f ? 1.0f : -1.0f;
                zle++;
            }
        } else {
            int16_t s;
            memcpy(&s, probki + 2 * i, sizeof s);
            x = (float)s * S16_SKALA;
        }
        suma[i] += x * wzm;
    }
    return zle;
}

void k1_dzw_glosnosc(float *suma, uint32_t ramki, float wzm)
{
    if (wzm == 1.0f)
        return;
    for (size_t i = 0; i < (size_t)ramki * K1_DZW_KANALY; i++)
        suma[i] *= wzm;
}

void k1_dzw_ogranicznik_start(struct k1_dzw_ogranicznik *o)
{
    o->g = 1.0f;
}

int k1_dzw_ogranicz(struct k1_dzw_ogranicznik *o, float *suma, uint32_t ramki)
{
    const float atak = 1.01454533f;    /* 2^(1/48): 6 dB per K1_DZW_OGR_ATAK frames */
    const float powrot = 1.00028885f;  /* 2^(1/2400): 6 dB per K1_DZW_OGR_POWROT frames */
    float g[K1_DZW_OKRES], a = 1.0f, poprz = o->g;
    int bylo = 0;
    if (ramki > K1_DZW_OKRES)
        ramki = K1_DZW_OKRES;
    for (uint32_t i = ramki; i-- > 0;) { /* backwards: the gain each frame needs, and gently before a loud one */
        float m = bez_znaku(suma[2 * i]) > bez_znaku(suma[2 * i + 1]) ? bez_znaku(suma[2 * i]) : bez_znaku(suma[2 * i + 1]);
        a *= atak;
        if (a > 1.0f)
            a = 1.0f;
        if (m > 1.0f && 1.0f / m < a)
            a = 1.0f / m;
        g[i] = a;
    }
    for (uint32_t i = 0; i < ramki; i++) { /* forwards: back up slowly, never above what a frame needs */
        float x = poprz * powrot;
        if (x > 1.0f)
            x = 1.0f;
        if (g[i] < x)
            x = g[i];
        poprz = x;
        if (x < 1.0f) {
            bylo = 1;
            for (int k = 0; k < K1_DZW_KANALY; k++) {
                float *s = &suma[2 * i + (uint32_t)k];
                *s *= x;
                if (*s > 1.0f || *s < -1.0f) /* the rounding of x · (1/m) */
                    *s = *s > 0.0f ? 1.0f : -1.0f;
            }
        }
    }
    o->g = poprz;
    return bylo;
}

void k1_dzw_dither_start(struct k1_dzw_dither *d)
{
    d->los = 0x4b31u; /* "K1" */
}

/* A uniform number in [0, 1). */
static double rowny(struct k1_dzw_dither *d)
{
    uint32_t x = d->los;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    d->los = x;
    return (double)(x >> 8) * (1.0 / 16777216.0);
}

/* floor(y + 0.5) for |y| ≤ 2^23 + 2 (no libm) */
static int64_t zaokraglij(double y)
{
    double z = y + 0.5;
    int64_t t = (int64_t)z;
    return (double)t > z ? t - 1 : t;
}

static void zapisz(uint8_t *wyj, int bajty, int bity, int64_t v)
{
    uint32_t u = (uint32_t)v; /* two's complement */
    if (bajty == 4)
        u <<= 32 - bity; /* the word's top bits: HDA plays them */
    for (int b = 0; b < bajty; b++)
        wyj[b] = (uint8_t)(u >> (8 * b));
}

int k1_dzw_na_karte(uint8_t *wyj, const float *suma, uint32_t ramki, int bajty, int bity, struct k1_dzw_dither *d)
{
    size_t n = (size_t)ramki * K1_DZW_KANALY;
    double skala, maks, min;
    int dokladne = 1;
    if (bajty < 2 || bajty > 4)
        bajty = 2;
    if (bity > 8 * bajty)
        bity = 8 * bajty;
    bity = bity < 16 ? 16 : bity > 24 ? 24 : bity;
    skala = (double)(1u << (bity - 1));
    maks = skala - 1.0;
    min = -skala;
    for (size_t i = 0; i < n && dokladne; i++) {
        double y = (double)suma[i] * skala;
        dokladne = y >= min && y <= maks && y == (double)(int64_t)y;
    }
    for (size_t i = 0; i < n; i++) {
        double y = (double)suma[i] * skala;
        int64_t v;
        y = y != y ? 0.0 : y > skala ? skala : y < min ? min : y; /* past the limiter never beyond; NaN only in a test */
        if (!dokladne)
            y += rowny(d) - rowny(d); /* TPDF: ±1 step, a triangle */
        v = zaokraglij(y);
        v = v > (int64_t)maks ? (int64_t)maks : v < (int64_t)min ? (int64_t)min : v;
        zapisz(wyj + i * (size_t)bajty, bajty, bity, v);
    }
    return !dokladne;
}

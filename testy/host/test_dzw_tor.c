/* K1 — host test: the sound path (src/wspolne/k1_dzw_tor.c; 0.12.3 step 1, ADR 0055). The measurements of the plan (step
 * 1a and 1g) — a tone of 1 kHz at -6 dBFS through the volume 100 %, 50 %, 10 %, 5 % to a 16-bit and a 24-bit card: the
 * error against the ideal (its loudness, its harmonics, its mean) and the quietest tone (3 steps at 50 %) — with the
 * bounds the path must keep; "przezroczystość" (one S16 stream at 100 % to a 16-bit card: sample for sample, no dither;
 * silence stays digital silence); the limiter (never beyond full scale, nothing below it, back to 1.0); F32 from a
 * hostile ring (NaN, infinities, beyond ±1.0 — counted, never in the sum); the card's formats (16 bits, 24 packed, 20 and
 * 24 in a 32-bit word); the dither the same on every run. The numbers are printed: the plan's "Ustalenia" quote them. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "k1_dzw_tor.h"
#include "k1_test_host.h"

#define N 48000u /* one second */

static int16_t we[N * 2];

/* One second of `we` (S16) at the volume `v` through the whole path to a card of `bajty`/`bity`; the card's samples of
 * the left channel back as numbers (steps of the card) into `wy`. Returns how many periods were dithered. */
static int tor(int v, int bajty, int bity, double *wy)
{
    static float suma[K1_DZW_OKRES * 2];
    static uint8_t karta[K1_DZW_OKRES * 2 * 4];
    struct k1_dzw_ogranicznik o;
    struct k1_dzw_dither d;
    int dithered = 0;
    k1_dzw_ogranicznik_start(&o);
    k1_dzw_dither_start(&d);
    for (uint32_t od = 0; od < N; od += K1_DZW_OKRES) {
        uint32_t n = N - od < K1_DZW_OKRES ? N - od : K1_DZW_OKRES;
        memset(suma, 0, sizeof suma);
        k1_dzw_dodaj(suma, (const uint8_t *)(we + 2 * od), K1_DZW_FORMAT_S16, n, 1.0f);
        k1_dzw_glosnosc(suma, n, k1_dzw_wzmocnienie(v, 0));
        k1_dzw_ogranicz(&o, suma, n);
        dithered += k1_dzw_na_karte(karta, suma, n, bajty, bity, &d);
        for (uint32_t i = 0; i < n; i++) {
            int32_t x = 0;
            const uint8_t *b = karta + (size_t)2 * i * (size_t)bajty;
            for (int k = 0; k < bajty; k++)
                x |= (int32_t)((uint32_t)b[k] << (8 * k));
            if (bajty < 4) /* the sign of 16 and 24 bits */
                x = (int32_t)((uint32_t)x << (32 - 8 * bajty)) >> (32 - 8 * bajty);
            else
                x >>= 32 - bity;
            wy[od + i] = x;
        }
    }
    return dithered;
}

/* Goertzel: the squared amplitude of `f` Hz in `n` samples */
static double ton(const double *x, uint32_t n, double f)
{
    double w = 2 * M_PI * f / K1_DZW_CZESTOTLIWOSC, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (uint32_t i = 0; i < n; i++) {
        double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / ((double)n * n / 4);
}

struct pomiar {
    double szum_dbfs, harmoniczne_db, srednia, blad_rms;
};

/* The error of the path against the ideal (the input × the gain, in the card's steps). */
static struct pomiar zmierz(int v, int bajty, int bity)
{
    static double wy[N], r[N], e[N];
    double skala = (double)(1u << (bity - 1)) / 32768.0, g = k1_dzw_wzmocnienie(v, 0), se = 0, h = 0, m = 0;
    struct pomiar p;
    tor(v, bajty, bity, wy);
    for (uint32_t i = 0; i < N; i++) {
        r[i] = we[2 * i] * g * skala;
        e[i] = wy[i] - r[i];
        se += e[i] * e[i];
        m += e[i];
    }
    for (int f = 2; f <= 10; f++)
        h += ton(e, N, f * 1000.0);
    p.blad_rms = sqrt(se / N);
    p.szum_dbfs = 20 * log10(p.blad_rms / (double)(1u << (bity - 1)));
    p.harmoniczne_db = 10 * log10(ton(r, N, 1000.0) / h);
    p.srednia = m / N;
    printf("  %d bitów, %3d%%: błąd %.3f kroku (%.1f dBFS), harmoniczne %.1f dB pod tonem, średnia %.4f\n", bity, v,
           p.blad_rms, p.szum_dbfs, p.harmoniczne_db, p.srednia);
    return p;
}

static void pomiary(void)
{
    static double wy[N];
    for (uint32_t i = 0; i < N; i++)
        we[2 * i] = we[2 * i + 1] = (int16_t)lrint(16384.0 * sin(2 * M_PI * 1000.0 * i / K1_DZW_CZESTOTLIWOSC));
    /* 0.12.0 (the plan's step 1a, "przed"): 50 % — 79 dB, 10 % — 35 dB, 5 % — 14 dB of harmonics below the tone */
    {
        struct pomiar p50 = zmierz(50, 2, 16), p10 = zmierz(10, 2, 16), p5 = zmierz(5, 2, 16);
        EXPECT(p50.harmoniczne_db > 95 && p10.harmoniczne_db > 55 && p5.harmoniczne_db > 38);
        /* TPDF: the error's power is 1/4 step² (rounding 1/12 + the dither 1/6), white, without a mean */
        EXPECT(fabs(p10.blad_rms - 0.5) < 0.05 && fabs(p5.blad_rms - 0.5) < 0.05 && fabs(p5.srednia) < 0.02);
    }
    {
        struct pomiar p100 = zmierz(100, 4, 24), p10 = zmierz(10, 4, 24), p5 = zmierz(5, 4, 24), p3 = zmierz(5, 3, 24);
        EXPECT(p100.szum_dbfs < -140 && p10.szum_dbfs < -140 && p10.harmoniczne_db > 100 && p5.harmoniczne_db > 85);
        EXPECT(fabs(p5.szum_dbfs - p3.szum_dbfs) < 0.5); /* 24 bits packed: the same as in a 32-bit word */
        struct pomiar p20 = zmierz(5, 4, 20);
        EXPECT(p20.szum_dbfs < -110 && p20.szum_dbfs > -130);
    }
    /* "przezroczystość": 100 % to a 16-bit card — sample for sample, never dithered */
    EXPECT(tor(100, 2, 16, wy) == 0);
    {
        int rowne = 1;
        for (uint32_t i = 0; i < N; i++)
            rowne &= wy[i] == we[2 * i];
        EXPECT(rowne);
    }
    /* the quietest: a tone of 3 steps at 50 % (1/8) — 0.12.0 gave silence (the gain 0.0000) */
    {
        double num = 0, den = 0;
        for (uint32_t i = 0; i < N; i++)
            we[2 * i] = we[2 * i + 1] = (int16_t)lrint(3.0 * sin(2 * M_PI * 1000.0 * i / K1_DZW_CZESTOTLIWOSC));
        tor(50, 2, 16, wy);
        for (uint32_t i = 0; i < N; i++) {
            num += wy[i] * we[2 * i];
            den += (double)we[2 * i] * we[2 * i];
        }
        printf("  ton 3 kroków przy 50 %%: wzmocnienie %.4f (idealnie 0,125)\n", num / den);
        EXPECT(fabs(num / den - 0.125) < 0.01);
    }
    /* silence stays digital silence at any volume */
    memset(we, 0, sizeof we);
    EXPECT(tor(37, 2, 16, wy) == 0 && tor(37, 4, 24, wy) == 0 && wy[0] == 0 && wy[N - 1] == 0);
}

static void ogranicznik(void)
{
    static float s[K1_DZW_OKRES * 2], kopia[K1_DZW_OKRES * 2];
    struct k1_dzw_ogranicznik o;
    float maks = 0;
    int okresy = 0;
    k1_dzw_ogranicznik_start(&o);
    for (uint32_t i = 0; i < K1_DZW_OKRES * 2; i++) /* below full scale: nothing changes */
        s[i] = kopia[i] = 0.99f * (float)sin(i * 0.05);
    EXPECT(k1_dzw_ogranicz(&o, s, K1_DZW_OKRES) == 0 && memcmp(s, kopia, sizeof s) == 0 && o.g == 1.0f);
    for (uint32_t i = 0; i < K1_DZW_OKRES * 2; i++) /* two loud streams: 1.6 at the top */
        s[i] = 1.6f * (float)sin(i * 0.05);
    EXPECT(k1_dzw_ogranicz(&o, s, K1_DZW_OKRES) == 1 && o.g < 0.7f);
    for (uint32_t i = 0; i < K1_DZW_OKRES * 2; i++)
        maks = fabsf(s[i]) > maks ? fabsf(s[i]) : maks;
    EXPECT(maks <= 1.0f && maks > 0.95f); /* never beyond, and not much lower than needed */
    {
        /* smooth: no step of the gain bigger than the attack's between two frames */
        float g1 = s[2 * 10] / (1.6f * (float)sin(20 * 0.05)), g2 = s[2 * 11] / (1.6f * (float)sin(22 * 0.05));
        EXPECT(g2 / g1 > 0.98f && g2 / g1 < 1.02f);
    }
    for (int k = 0; k < 20 && o.g < 1.0f; k++) { /* quiet again: back to 1.0 within 50 ms per 6 dB */
        memset(s, 0, sizeof s);
        okresy++;
        k1_dzw_ogranicz(&o, s, K1_DZW_OKRES);
    }
    EXPECT(o.g == 1.0f && okresy <= 4);
    /* a lone loud frame at the start of a period (nothing before it to soften): still within full scale */
    memset(s, 0, sizeof s);
    s[0] = 7.0f;
    s[1] = -3.0f;
    k1_dzw_ogranicznik_start(&o);
    EXPECT(k1_dzw_ogranicz(&o, s, K1_DZW_OKRES) == 1 && s[0] <= 1.0f && s[1] >= -1.0f);
}

static void probki_f32(void)
{
    const float wejscie[8] = { 0.5f, -0.25f, NAN, INFINITY, -INFINITY, 5.0f, -3.0f, 1.0f };
    uint8_t b[sizeof wejscie];
    float suma[8] = { 0 };
    memcpy(b, wejscie, sizeof b);
    EXPECT(k1_dzw_dodaj(suma, b, K1_DZW_FORMAT_F32, 4, 1.0f) == 5); /* NaN, ±inf, 5.0, -3.0 */
    EXPECT(suma[0] == 0.5f && suma[1] == -0.25f && suma[2] == 0 && suma[3] == 0 && suma[4] == 0 && suma[5] == 1.0f &&
           suma[6] == -1.0f && suma[7] == 1.0f);
    EXPECT(k1_dzw_dodaj(suma, b + 1, K1_DZW_FORMAT_F32, 1, 0.5f) <= 2); /* no alignment assumed (UBSan) */
}

static void formaty_karty(void)
{
    float s[4] = { 0.5f, -1.0f, 32767.0f / 32768.0f, -1.0f / 32768.0f };
    uint8_t w[4 * 4];
    struct k1_dzw_dither d;
    k1_dzw_dither_start(&d);
    EXPECT(k1_dzw_na_karte(w, s, 2, 2, 16, &d) == 0); /* all whole steps: as they are */
    EXPECT(w[0] == 0x00 && w[1] == 0x40 && w[2] == 0x00 && w[3] == 0x80 && w[4] == 0xff && w[5] == 0x7f &&
           w[6] == 0xff && w[7] == 0xff);
    EXPECT(k1_dzw_na_karte(w, s, 2, 3, 24, &d) == 0); /* 24 bits packed */
    EXPECT(w[0] == 0 && w[1] == 0 && w[2] == 0x40 && w[3] == 0 && w[4] == 0 && w[5] == 0x80 && w[9] == 0 &&
           w[10] == 0xff && w[11] == 0xff);
    EXPECT(k1_dzw_na_karte(w, s, 2, 4, 24, &d) == 0); /* 24 bits at the top of a 32-bit word: HDA */
    EXPECT(w[0] == 0 && w[1] == 0 && w[2] == 0 && w[3] == 0x40 && w[7] == 0x80 && w[12] == 0 && w[13] == 0 &&
           w[14] == 0xff && w[15] == 0xff);
    EXPECT(k1_dzw_na_karte(w, s, 2, 4, 20, &d) == 0 && (w[13] & 0x0f) == 0 && w[15] == 0xff);
    /* full scale +1.0 is one step too far: the top step; and the dither is the same on every run */
    {
        float j[2] = { 1.0f, 0.3f };
        uint8_t a[4], b[4];
        struct k1_dzw_dither d1, d2;
        k1_dzw_dither_start(&d1);
        k1_dzw_dither_start(&d2);
        EXPECT(k1_dzw_na_karte(a, j, 1, 2, 16, &d1) == 1 && k1_dzw_na_karte(b, j, 1, 2, 16, &d2) == 1 &&
               memcmp(a, b, 4) == 0 && a[0] == 0xff && a[1] == 0x7f);
    }
    /* nonsense of a caller: bytes and bits brought into range, never past the buffer (ASan) */
    EXPECT(k1_dzw_na_karte(w, s, 2, 9, 99, &d) == 0);
}

static void glosnosc(void)
{
    float poprz = 0;
    EXPECT(k1_dzw_wzmocnienie(0, 0) == 0 && k1_dzw_wzmocnienie(100, 0) == 1.0f && k1_dzw_wzmocnienie(100, 1) == 0);
    EXPECT(k1_dzw_wzmocnienie(-5, 0) == 0 && k1_dzw_wzmocnienie(500, 0) == 1.0f);
    EXPECT(k1_dzw_wzmocnienie(50, 0) == 0.125f); /* a cube: an eighth (-18 dB) at 50 % — test-dzwiek measures it */
    for (int v = 1; v <= 100; v++) { /* every step louder than the one before */
        EXPECT(k1_dzw_wzmocnienie(v, 0) > poprz);
        poprz = k1_dzw_wzmocnienie(v, 0);
    }
    {
        /* 16 streams at the loudest: the sum is exact (floats hold 20 bits of whole steps) */
        float suma[2] = { 0, 0 };
        int16_t a[2] = { 32767, -32768 };
        for (int i = 0; i < 16; i++)
            k1_dzw_dodaj(suma, (const uint8_t *)a, K1_DZW_FORMAT_S16, 1, 1.0f);
        EXPECT(suma[0] * 32768.0f == 16.0f * 32767 && suma[1] == -16.0f);
        k1_dzw_glosnosc(suma, 1, 0.0f);
        EXPECT(suma[0] == 0 && suma[1] == 0);
    }
}

int main(void)
{
    pomiary();
    ogranicznik();
    probki_f32();
    formaty_karty();
    glosnosc();
    return K1_TEST_KONIEC("test_dzw_tor");
}

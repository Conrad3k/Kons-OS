/* K1 — the system equalizer (0.12.3 step 2, ADR 0055 points 9–13): the bands, the ready settings, the filters and their
 * coefficients, the crossing between two settings. Pure functions (host test test_korektor, fuzzing fuzz_korektor). See
 * k1_korektor.h. */
#include "k1_korektor.h"

#include <string.h>

#include "k1_dzwiek.h"

#define PI 3.14159265358979323846
#define LN10 2.30258509299404568402
#define LN1000 6.90775527898213705205
#define Q_PASMA 1.41421356237309504880 /* √2: one octave between the points 3 dB from the middle's gain */
#define SIATKA 241                     /* the grid of the curve's loudest point: 20 Hz … 20 kHz */
#define NAD_20K 8                      /* …and above it, up to 24 kHz (0.13.0) */
#define MALE 1e-30                     /* below this the filters' memory is 0 (no denormal numbers after silence) */

#define K1_X_KOR_SRODEK(i, hz, ...) hz,
#define K1_X_KOR_ETYKIETA(i, hz, et) et,
static const double srodki[K1_KOR_PASMA_N] = { K1_KOR_PASMA(K1_X_KOR_SRODEK) };
static const char *const etykiety[K1_KOR_PASMA_N] = { K1_KOR_PASMA(K1_X_KOR_ETYKIETA) };

#define K1_X_KOR_NAZWA(n, nazwa, ...) nazwa,
#define K1_X_KOR_WARTOSCI(n, nazwa, ...) { __VA_ARGS__ },
static const char *const gotowe_nazwy[K1_KOR_GOTOWYCH] = { K1_KOR_GOTOWE(K1_X_KOR_NAZWA) };
static const int8_t gotowe[K1_KOR_GOTOWYCH][K1_KOR_PASMA_N] = { K1_KOR_GOTOWE(K1_X_KOR_WARTOSCI) };

double k1_kor_srodek(int pasmo)
{
    return pasmo >= 0 && pasmo < K1_KOR_PASMA_N ? srodki[pasmo] : 0.0;
}

const char *k1_kor_etykieta(int pasmo)
{
    return pasmo >= 0 && pasmo < K1_KOR_PASMA_N ? etykiety[pasmo] : "";
}

const char *k1_kor_gotowe_nazwa(int nr)
{
    return nr >= 0 && nr < K1_KOR_GOTOWYCH ? gotowe_nazwy[nr] : "";
}

void k1_kor_gotowe_pasma(int nr, int8_t pasma[K1_KOR_PASMA_N])
{
    if (nr >= 0 && nr < K1_KOR_GOTOWYCH)
        memcpy(pasma, gotowe[nr], K1_KOR_PASMA_N);
    else
        memset(pasma, 0, K1_KOR_PASMA_N);
}

int k1_kor_rozpoznaj(const int8_t pasma[K1_KOR_PASMA_N])
{
    for (int nr = 0; nr < K1_KOR_GOTOWYCH; nr++)
        if (memcmp(pasma, gotowe[nr], K1_KOR_PASMA_N) == 0)
            return nr;
    return -1;
}

const char *k1_kor_nazwa(const int8_t pasma[K1_KOR_PASMA_N])
{
    int nr = k1_kor_rozpoznaj(pasma);
    return nr >= 0 ? gotowe_nazwy[nr] : "Własne";
}

int k1_kor_plaski(const int8_t pasma[K1_KOR_PASMA_N])
{
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        if (pasma[i] != 0)
            return 0;
    return 1;
}

/* Our own series — deterministic everywhere (IEEE doubles, no libm). */
double k1_kor_sin(double x)
{
    double s = x, t = x;
    for (int n = 1; n < 30; n++) {
        t *= -x * x / ((2.0 * n) * (2.0 * n + 1.0));
        s += t;
    }
    return s;
}

double k1_kor_cos(double x)
{
    double s = 1.0, t = 1.0;
    for (int n = 1; n < 30; n++) {
        t *= -x * x / ((2.0 * n - 1.0) * (2.0 * n));
        s += t;
    }
    return s;
}

double k1_kor_exp(double x)
{
    double y = x / 16.0, s = 1.0, t = 1.0; /* |y| ≤ 0.5: the series is short; then squared four times */
    for (int n = 1; n < 25; n++) {
        t *= y / n;
        s += t;
    }
    for (int i = 0; i < 4; i++)
        s *= s;
    return s;
}

double k1_kor_pierw(double x)
{
    double y = x > 1.0 ? x : 1.0;
    if (!(x > 0.0))
        return 0.0;
    for (int i = 0; i < 80; i++) /* Newton: halves the error's exponent until close, then doubles the right digits */
        y = 0.5 * (y + x / y);
    return y;
}

/* The peaking filter of one band (the Audio EQ Cookbook), `pol` half-decibels. */
static struct k1_kor_filtr filtr(double hz, int pol)
{
    double w0 = 2.0 * PI * hz / K1_DZW_CZESTOTLIWOSC, a = k1_kor_exp((double)pol / 2.0 / 40.0 * LN10);
    double alfa = k1_kor_sin(w0) / (2.0 * Q_PASMA), c = k1_kor_cos(w0), a0 = 1.0 + alfa / a;
    struct k1_kor_filtr f = { (1.0 + alfa * a) / a0, -2.0 * c / a0, (1.0 - alfa * a) / a0, -2.0 * c / a0,
                              (1.0 - alfa / a) / a0 };
    return f;
}

/* |H|² of one biquad at the angle whose cosine is c. */
static double moc_filtra(const struct k1_kor_filtr *f, double c)
{
    double c2 = 2.0 * c * c - 1.0;
    double l = f->b0 * f->b0 + f->b1 * f->b1 + f->b2 * f->b2 + 2.0 * (f->b0 * f->b1 + f->b1 * f->b2) * c +
               2.0 * f->b0 * f->b2 * c2;
    double d = 1.0 + f->a1 * f->a1 + f->a2 * f->a2 + 2.0 * (f->a1 + f->a1 * f->a2) * c + 2.0 * f->a2 * c2;
    return l / d;
}

/* |H|² of all bands at the angle whose cosine is c. */
static double moc_przy(const struct k1_kor_ustawienie *u, double c)
{
    double m = 1.0;
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        m *= moc_filtra(&u->f[i], c);
    return m;
}

/* The K weighting of ITU-R BS.1770-4 (what LUFS measures), its two filters at 48 kHz as the standard gives them: the
 * shelf of the head (+4 dB above 2 kHz) and the high-pass RLB (the lowest octaves count less). */
_Static_assert(K1_DZW_CZESTOTLIWOSC == 48000, "k1_korektor: the coefficients of BS.1770 are those of 48 kHz");
static const struct k1_kor_filtr WAGA[2] = {
    { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585 },
    { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 },
};

double k1_kor_waga(double hz)
{
    double c;
    hz = hz < 0.0 ? 0.0 : hz > K1_DZW_CZESTOTLIWOSC / 2.0 ? K1_DZW_CZESTOTLIWOSC / 2.0 : hz;
    c = k1_kor_cos(2.0 * PI * hz / K1_DZW_CZESTOTLIWOSC);
    return moc_filtra(&WAGA[0], c) * moc_filtra(&WAGA[1], c);
}

double k1_kor_moc(const struct k1_kor_ustawienie *u, double hz)
{
    if (u->plaski)
        return 1.0;
    hz = hz < 0.0 ? 0.0 : hz > K1_DZW_CZESTOTLIWOSC / 2.0 ? K1_DZW_CZESTOTLIWOSC / 2.0 : hz;
    return moc_przy(u, k1_kor_cos(2.0 * PI * hz / K1_DZW_CZESTOTLIWOSC));
}

void k1_kor_policz(struct k1_kor_ustawienie *u, const int8_t pasma[K1_KOR_PASMA_N])
{
    double maks = 0.0, srednia = 0.0, wagi = 0.0;
    memset(u, 0, sizeof *u);
    for (int i = 0; i < K1_KOR_PASMA_N; i++) /* the caller checked them (k1_kor_z_pol); never trusted beyond ±12 dB */
        u->pasma[i] = (int8_t)(pasma[i] > K1_KOR_MAKS ? K1_KOR_MAKS : pasma[i] < -K1_KOR_MAKS ? -K1_KOR_MAKS : pasma[i]);
    u->plaski = k1_kor_plaski(u->pasma);
    u->wzm = 1.0;
    if (u->plaski)
        return;
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        u->f[i] = filtr(srodki[i], u->pasma[i]);
    /* the loudest point: the grid, every band's middle and (0.13.0) above 20 kHz up to the half of 48 kHz — where all
     * the bands come back to 0 dB, a level that lifted a curve cut at the top would lift those tones too */
    for (int k = 0; k < SIATKA + K1_KOR_PASMA_N + NAD_20K; k++) {
        double hz = k < SIATKA                   ? 20.0 * k1_kor_exp(LN1000 * k / (SIATKA - 1))
                    : k < SIATKA + K1_KOR_PASMA_N ? srodki[k - SIATKA]
                                                  : 20000.0 + 4000.0 * (k - SIATKA - K1_KOR_PASMA_N + 1) / NAD_20K;
        double m = k1_kor_moc(u, hz);
        if (m > maks)
            maks = m;
        if (k < SIATKA) { /* the grid is even in octaves (a music-like, pink sound), K-weighted: its loudness */
            srednia += k1_kor_waga(hz) * m;
            wagi += k1_kor_waga(hz);
        }
    }
    /* 0.13.0 (ADR 0055 point 21): the same loudness as without it — up to K1_DZW_ZAPAS above 100 % at the loudest point */
    u->wzm = k1_kor_pierw(wagi / srednia);
    if (u->wzm * k1_kor_pierw(maks) > K1_DZW_ZAPAS)
        u->wzm = K1_DZW_ZAPAS / k1_kor_pierw(maks);
}

void k1_kor_start(struct k1_korektor *k)
{
    static const int8_t plaski[K1_KOR_PASMA_N];
    memset(k, 0, sizeof *k);
    k1_kor_policz(&k->teraz, plaski);
    k->nowe = k->teraz;
}

void k1_kor_ustaw(struct k1_korektor *k, const int8_t pasma[K1_KOR_PASMA_N])
{
    const struct k1_kor_ustawienie *cel = k->zmiana ? &k->nowe : &k->teraz;
    if (memcmp(cel->pasma, pasma, K1_KOR_PASMA_N) == 0)
        return;
    k1_kor_policz(&k->nowe, pasma);
    k->zmiana = memcmp(k->teraz.pasma, k->nowe.pasma, K1_KOR_PASMA_N) != 0;
}

typedef double pamiec_t[K1_KOR_PASMA_N][2][2];

/* The filters of `u` on `ramki` frames of `s` in place, with their memory x, y. */
static void przebieg(const struct k1_kor_ustawienie *u, pamiec_t x, pamiec_t y, float *s, uint32_t ramki)
{
    for (uint32_t r = 0; r < ramki; r++)
        for (int c = 0; c < K1_DZW_KANALY; c++) {
            double v = (double)s[K1_DZW_KANALY * r + (uint32_t)c] * u->wzm;
            for (int i = 0; i < K1_KOR_PASMA_N; i++) {
                const struct k1_kor_filtr *f = &u->f[i];
                double w = f->b0 * v + f->b1 * x[i][c][0] + f->b2 * x[i][c][1] - f->a1 * y[i][c][0] - f->a2 * y[i][c][1];
                x[i][c][1] = x[i][c][0];
                x[i][c][0] = v;
                y[i][c][1] = y[i][c][0];
                y[i][c][0] = w;
                v = w;
            }
            s[K1_DZW_KANALY * r + (uint32_t)c] = (float)v;
        }
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        for (int c = 0; c < 2; c++)
            for (int j = 0; j < 2; j++) {
                if (x[i][c][j] < MALE && x[i][c][j] > -MALE)
                    x[i][c][j] = 0.0;
                if (y[i][c][j] < MALE && y[i][c][j] > -MALE)
                    y[i][c][j] = 0.0;
            }
}

/* The memory the new filters start from: the old one at the new level, or — after a flat equalizer — the sum's last two
 * frames as every band's input and output (as if each band had passed them on unchanged: no jump at the start). */
static void pamiec_nowych(const struct k1_korektor *k, pamiec_t x, pamiec_t y)
{
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        for (int c = 0; c < 2; c++)
            for (int j = 0; j < 2; j++) {
                if (k->teraz.plaski) {
                    x[i][c][j] = y[i][c][j] = k->ostatnie[j][c] * k->nowe.wzm;
                } else {
                    x[i][c][j] = k->x[i][c][j] * (k->nowe.wzm / k->teraz.wzm);
                    y[i][c][j] = k->y[i][c][j] * (k->nowe.wzm / k->teraz.wzm);
                }
            }
}

static void zapamietaj_ostatnie(struct k1_korektor *k, const float *wej, uint32_t ramki)
{
    for (int c = 0; c < 2; c++) {
        k->ostatnie[1][c] = ramki >= 2 ? (double)wej[2 * (ramki - 2) + (uint32_t)c] : k->ostatnie[0][c];
        k->ostatnie[0][c] = (double)wej[2 * (ramki - 1) + (uint32_t)c];
    }
}

int k1_kor_licz(struct k1_korektor *k, float *suma, uint32_t ramki)
{
    static float stare[K1_DZW_OKRES * K1_DZW_KANALY];
    pamiec_t x, y;
    if (ramki > K1_DZW_OKRES)
        ramki = K1_DZW_OKRES;
    if (ramki == 0)
        return 0;
    if (!k->zmiana) {
        if (k->teraz.plaski) {
            zapamietaj_ostatnie(k, suma, ramki);
            return 0;
        }
        zapamietaj_ostatnie(k, suma, ramki);
        przebieg(&k->teraz, k->x, k->y, suma, ramki);
        return 1;
    }
    /* A change: the old filters and the new ones on the same period, crossed over it. */
    memcpy(stare, suma, (size_t)ramki * K1_DZW_KANALY * sizeof *suma);
    pamiec_nowych(k, x, y);
    zapamietaj_ostatnie(k, suma, ramki);
    if (!k->teraz.plaski)
        przebieg(&k->teraz, k->x, k->y, stare, ramki);
    if (!k->nowe.plaski)
        przebieg(&k->nowe, x, y, suma, ramki);
    for (uint32_t r = 0; r < ramki; r++) {
        float t = (float)(r + 1) / (float)ramki;
        for (uint32_t c = 0; c < K1_DZW_KANALY; c++)
            suma[K1_DZW_KANALY * r + c] = stare[K1_DZW_KANALY * r + c] * (1.0f - t) + suma[K1_DZW_KANALY * r + c] * t;
    }
    k->teraz = k->nowe;
    memcpy(k->x, x, sizeof x);
    memcpy(k->y, y, sizeof y);
    k->zmiana = 0;
    return 1;
}

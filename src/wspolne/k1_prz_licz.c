/* K1 — Kons Spatial Audio running (0.12.3 step 3, ADR 0055 points 14–19): the crossovers (Linkwitz-Riley, 4th order),
 * the histories of the bands, the paths of a scene sample by sample, the crossing between two scenes over one period.
 * Pure functions (host test test_przestrzen, fuzzing fuzz_przestrzen). The scenes in numbers: k1_przestrzen.c. */
#include <string.h>

#include "k1_dzwiek.h"
#include "k1_korektor.h"
#include "k1_przestrzen.h"

#define PI 3.14159265358979323846
#define FS ((double)K1_DZW_CZESTOTLIWOSC)
#define MALE 1e-30 /* below this the filters' memory is 0 (no denormal numbers after silence) */
#define ODBICIA_HZ 5000.0

/* The crossovers: 0,1 low-pass and 2,3 high-pass at 500 Hz; 4 the all-pass of 4 kHz on the low half, 5 the all-pass of
 * 120 Hz on the high half (so that the four bands sum to one all-pass: no holes, no bumps); 6,7 / 8,9 low- and high-pass
 * at 120 Hz (Bas, Niski środek); 10,11 / 12,13 at 4 kHz (Środek, Góra). */
enum { DOLNE = 0, GORNE = 2, WSZ_4K = 4, WSZ_120 = 5, BAS = 6, NISKI = 8, SRODEK = 10, GORA = 12 };
enum { DP = 0, GP = 1, WP = 2 }; /* low-, high-, all-pass */

static double tg(double hz) /* tan(π·hz/fs): the bilinear transform's frequency */
{
    double x = PI * hz / FS;
    return k1_kor_sin(x) / k1_kor_cos(x);
}

/* A Butterworth section (Q = 1/√2) at `hz`: two of them in a row make a Linkwitz-Riley filter of the 4th order. */
static struct k1_prz_bq sekcja(double hz, int rodzaj)
{
    double t = tg(hz), a0 = 1.0 + 1.41421356237309504880 * t + t * t;
    double a1 = (2.0 * t * t - 2.0) / a0, a2 = (1.0 - 1.41421356237309504880 * t + t * t) / a0;
    struct k1_prz_bq q = { 0, 0, 0, a1, a2 };
    if (rodzaj == DP)
        q.b0 = t * t / a0, q.b1 = 2.0 * t * t / a0, q.b2 = t * t / a0;
    else if (rodzaj == GP)
        q.b0 = 1.0 / a0, q.b1 = -2.0 / a0, q.b2 = 1.0 / a0;
    else
        q.b0 = a2, q.b1 = a1, q.b2 = 1.0;
    return q;
}

void k1_prz_zwrotnica_start(struct k1_prz_zwrotnica *z)
{
    double f1 = k1_prz_pasmo_do(0), f2 = k1_prz_pasmo_do(1), f3 = k1_prz_pasmo_do(2);
    memset(z, 0, sizeof *z);
    z->bq[DOLNE] = z->bq[DOLNE + 1] = sekcja(f2, DP);
    z->bq[GORNE] = z->bq[GORNE + 1] = sekcja(f2, GP);
    z->bq[WSZ_4K] = sekcja(f3, WP);
    z->bq[WSZ_120] = sekcja(f1, WP);
    z->bq[BAS] = z->bq[BAS + 1] = sekcja(f1, DP);
    z->bq[NISKI] = z->bq[NISKI + 1] = sekcja(f1, GP);
    z->bq[SRODEK] = z->bq[SRODEK + 1] = sekcja(f3, DP);
    z->bq[GORA] = z->bq[GORA + 1] = sekcja(f3, GP);
}

static double bq(struct k1_prz_zwrotnica *z, int i, double x) /* transposed direct form II */
{
    const struct k1_prz_bq *q = &z->bq[i];
    double y = q->b0 * x + z->s[i][0];
    z->s[i][0] = q->b1 * x - q->a1 * y + z->s[i][1];
    z->s[i][1] = q->b2 * x - q->a2 * y;
    return y;
}

void k1_prz_zwrotnica_licz(struct k1_prz_zwrotnica *z, double x, double pasma[K1_PRZ_PASMA_N])
{
    double nisko = bq(z, WSZ_4K, bq(z, DOLNE + 1, bq(z, DOLNE, x)));
    double wysoko = bq(z, WSZ_120, bq(z, GORNE + 1, bq(z, GORNE, x)));
    pasma[0] = bq(z, BAS + 1, bq(z, BAS, nisko));
    pasma[1] = bq(z, NISKI + 1, bq(z, NISKI, nisko));
    pasma[2] = bq(z, SRODEK + 1, bq(z, SRODEK, wysoko));
    pasma[3] = bq(z, GORA + 1, bq(z, GORA, wysoko));
}

double k1_prz_pasmo_moc(int pasmo, double hz)
{
    double t = tg(hz < 0.0 ? 0.0 : hz > FS / 2.0 - 1.0 ? FS / 2.0 - 1.0 : hz), d[3];
    for (int i = 0; i < 3; i++) { /* each crossover's low-pass magnitude 1 / (1 + (t/tc)^4); its high-pass the rest */
        double r = t / tg(k1_prz_pasmo_do(i));
        d[i] = 1.0 / (1.0 + r * r * r * r);
    }
    switch (pasmo) {
    case 0: return d[0] * d[1];
    case 1: return (1.0 - d[0]) * d[1];
    case 2: return d[2] * (1.0 - d[1]);
    case 3: return (1.0 - d[2]) * (1.0 - d[1]);
    default: return 0.0;
    }
}

void k1_prz_start(struct k1_przestrzen *p)
{
    struct k1_prz_scena s;
    memset(p, 0, sizeof *p);
    k1_prz_scena_start(&s);
    s.wlaczona = 0;
    k1_prz_policz(&p->teraz, &s, K1_DZW_WYJSCIE_GLOSNIKI);
    p->nowe = p->teraz;
    p->odb_k = 1.0 - k1_kor_exp(-2.0 * PI * ODBICIA_HZ / FS);
}

static int gra_tak_samo(const struct k1_prz_ustawienie *u, const struct k1_prz_scena *s, int wyjscie)
{
    return k1_prz_rowne(&u->scena, s) && (!s->wlaczona || u->wyjscie == wyjscie);
}

void k1_prz_ustaw(struct k1_przestrzen *p, const struct k1_prz_scena *s, int wyjscie)
{
    const struct k1_prz_ustawienie *cel = p->zmiana ? &p->nowe : &p->teraz;
    int w = wyjscie == K1_DZW_WYJSCIE_SLUCHAWKI ? K1_DZW_WYJSCIE_SLUCHAWKI : K1_DZW_WYJSCIE_GLOSNIKI;
    if (gra_tak_samo(cel, s, w))
        return;
    k1_prz_policz(&p->nowe, s, w);
    p->zmiana = !gra_tak_samo(&p->teraz, &p->nowe.scena, p->nowe.wyjscie);
}

/* One frame of the sum into the crossovers and the histories. */
static void zasil(struct k1_przestrzen *p, double l, double r)
{
    double v[2][K1_PRZ_PASMA_N];
    uint32_t h = p->poz % K1_PRZ_HIST, o = p->poz % K1_PRZ_ODB_HIST;
    k1_prz_zwrotnica_licz(&p->z[0], l, v[0]);
    k1_prz_zwrotnica_licz(&p->z[1], r, v[1]);
    for (int b = 0; b < K1_PRZ_PASMA_N; b++) {
        p->hist[b][0][h] = v[0][b];
        p->hist[b][1][h] = v[1][b];
        p->odb[b][o] = 0.5 * (v[0][b] + v[1][b]);
    }
    p->poz++;
}

/* The frame just fed, through the paths of `u` (its filters' memory: set `nr`) — the two ears into `wyj`. */
static void graj(struct k1_przestrzen *p, const struct k1_prz_ustawienie *u, int nr, double wyj[2])
{
    double odb[2] = { 0, 0 };
    uint32_t ost = p->poz - 1u;
    wyj[0] = wyj[1] = 0.0;
    for (int b = 0; b < K1_PRZ_PASMA_N; b++)
        for (int i = 0; i < u->drog[b]; i++) {
            const struct k1_prz_droga *d = &u->d[b][i];
            const double *h = p->hist[b][d->zrodlo];
            double v = 0.0;
            for (int t = 0; t < 4; t++)
                v += d->h[t] * h[(ost - (uint32_t)(d->n - 1 + t)) % K1_PRZ_HIST];
            for (int f = 0; f < 2; f++) {
                double *s = p->stan[nr][b][i][f], y = d->f[f][0] * v + d->f[f][1] * s[0] - d->f[f][2] * s[1];
                s[0] = v;
                s[1] = y;
                v = y;
            }
            wyj[d->ucho] += d->g * v;
        }
    for (int t = 0; t < K1_PRZ_ODBICIA; t++) {
        int ucho, r = k1_prz_odbicie_ramki(t, &ucho);
        double v = 0.0;
        for (int b = 0; b < K1_PRZ_PASMA_N; b++)
            v += u->odbicie[b] * p->odb[b][(ost - (uint32_t)r) % K1_PRZ_ODB_HIST];
        odb[ucho] += u->odb_g[t] * v;
    }
    for (int e = 0; e < 2; e++) {
        p->odb_lp[nr][e] += p->odb_k * (odb[e] - p->odb_lp[nr][e]);
        wyj[e] += p->odb_lp[nr][e];
    }
}

static void bez_malych(double *x, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (x[i] < MALE && x[i] > -MALE)
            x[i] = 0.0;
}

/* A scene switched on after silence of the crossovers: they start from the last period (its thump dies out there). */
static void obudz(struct k1_przestrzen *p)
{
    k1_prz_zwrotnica_start(&p->z[0]);
    k1_prz_zwrotnica_start(&p->z[1]);
    memset(p->hist, 0, sizeof p->hist);
    memset(p->odb, 0, sizeof p->odb);
    memset(p->stan, 0, sizeof p->stan);
    memset(p->odb_lp, 0, sizeof p->odb_lp);
    p->poz = 0;
    for (uint32_t r = 0; r < p->poprzednich; r++)
        zasil(p, (double)p->poprzedni[2 * r], (double)p->poprzedni[2 * r + 1]);
    p->zasilone = 1;
}

/* The memory the new scene's filters start from: the old one when the paths are alike (the same output), else none (the
 * crossing hides the start). */
static void pamiec_nowych(struct k1_przestrzen *p)
{
    if (p->teraz.scena.wlaczona && p->teraz.wyjscie == p->nowe.wyjscie) {
        memcpy(p->stan[1], p->stan[0], sizeof p->stan[1]);
        memcpy(p->odb_lp[1], p->odb_lp[0], sizeof p->odb_lp[1]);
    } else {
        memset(p->stan[1], 0, sizeof p->stan[1]);
        memset(p->odb_lp[1], 0, sizeof p->odb_lp[1]);
    }
}

int k1_prz_licz(struct k1_przestrzen *p, float *suma, uint32_t ramki)
{
    int stara = p->teraz.scena.wlaczona, nowa = p->zmiana ? p->nowe.scena.wlaczona : stara;
    if (ramki > K1_PRZ_OKRES)
        ramki = K1_PRZ_OKRES;
    if (ramki == 0)
        return 0;
    if (!stara && !nowa) { /* off: transparent, only the last period kept */
        memcpy(p->poprzedni, suma, (size_t)ramki * 2 * sizeof *suma);
        p->poprzednich = ramki;
        if (p->zmiana)
            p->teraz = p->nowe; /* the places changed while off: nothing to hear */
        p->zmiana = 0;
        return 0;
    }
    if (!p->zasilone)
        obudz(p);
    if (p->zmiana)
        pamiec_nowych(p);
    memcpy(p->poprzedni, suma, (size_t)ramki * 2 * sizeof *suma);
    p->poprzednich = ramki;
    for (uint32_t r = 0; r < ramki; r++) {
        double dry[2] = { suma[2 * r], suma[2 * r + 1] }, a[2], b[2];
        zasil(p, dry[0], dry[1]);
        if (stara)
            graj(p, &p->teraz, 0, a);
        else
            a[0] = dry[0], a[1] = dry[1];
        if (p->zmiana) {
            double t = (double)(r + 1) / ramki;
            if (nowa)
                graj(p, &p->nowe, 1, b);
            else
                b[0] = dry[0], b[1] = dry[1];
            a[0] = a[0] * (1.0 - t) + b[0] * t;
            a[1] = a[1] * (1.0 - t) + b[1] * t;
        }
        suma[2 * r] = (float)a[0];
        suma[2 * r + 1] = (float)a[1];
    }
    if (p->zmiana) {
        p->teraz = p->nowe;
        memcpy(p->stan[0], p->stan[1], sizeof p->stan[0]);
        memcpy(p->odb_lp[0], p->odb_lp[1], sizeof p->odb_lp[0]);
        p->zmiana = 0;
    }
    p->zasilone = p->teraz.scena.wlaczona;
    bez_malych(&p->z[0].s[0][0], sizeof p->z[0].s / sizeof(double));
    bez_malych(&p->z[1].s[0][0], sizeof p->z[1].s / sizeof(double));
    bez_malych(&p->stan[0][0][0][0][0], sizeof p->stan / sizeof(double));
    bez_malych(&p->odb_lp[0][0], 4);
    return 1;
}

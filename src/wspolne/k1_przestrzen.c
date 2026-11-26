/* K1 — Kons Spatial Audio (0.12.3 step 3, ADR 0055 points 14–19): the bands and the ready scenes, the scene in numbers —
 * the head model, the wider stereo of the speakers, the distance — and the level that keeps a scene within 100 %. Pure
 * functions (host test test_przestrzen, fuzzing fuzz_przestrzen). The running part (the crossovers, the paths, the
 * crossing between two scenes): k1_prz_licz.c. See k1_przestrzen.h. */
#include "k1_przestrzen.h"

#include <string.h>

#include "k1_dzwiek.h"
#include "k1_korektor.h"

#define PI 3.14159265358979323846
#define LN10 2.30258509299404568402
#define LN1000 6.90775527898213705205
#define FS ((double)K1_DZW_CZESTOTLIWOSC)
#define GLOWA_S (0.0875 / 343.0)        /* the head's radius / the speed of sound (Brown, Duda: a sphere of 8.75 cm) */
#define D0 2.0                          /* frames every path is late (the Lagrange filter needs one frame before) */
#define ALFA_MIN 0.1                    /* the head's shadow: the far ear's highs at −20 dB at most… */
#define THETA_MIN (150.0 * PI / 180.0)  /* …150° from its axis */
#define TYL_HZ 4000.0                   /* a source behind: its highs softer above this… */
#define TYL_MIN 0.5                     /* …by up to 6 dB (straight behind) */
#define BLIZEJ_DB 6.0                   /* the distance 100 % is this much quieter than 0 % */
#define ODBICIA_MAKS 0.35               /* the reflections' share at the distance 100 % */
#define ODBICIA_HZ 5000.0               /* the reflections are softer above this (a one-pole low-pass) */
#define SZEROKOSC_STEREO 60.0           /* the width of a normal stereo (two speakers at ±30°) */
#define POSZERZENIE_MAKS 1.4            /* the speakers: the sides at most this much louder than the middle */
#define KRZYZ_MAKS 0.2                  /* the speakers: the cross-cancellation at most */
#define KRZYZ_RAMKI 3                   /* …this many frames late (62.5 µs: the ears ~20 cm apart, the speakers in front) */
#define SIATKA 481                      /* the grid of the level: 20 Hz … 20 kHz */

_Static_assert(K1_PRZ_OKRES == K1_DZW_OKRES, "k1_przestrzen: its period is the service's");

#define K1_X_PRZ_PASMO_NAZWA(i, n, od, dd, k) n,
#define K1_X_PRZ_PASMO_OD(i, n, od, dd, k) od,
#define K1_X_PRZ_PASMO_DO(i, n, od, dd, k) dd,
#define K1_X_PRZ_PASMO_KOLOR(i, n, od, dd, k) k,
static const char *const pasma_nazwy[K1_PRZ_PASMA_N] = { K1_PRZ_PASMA(K1_X_PRZ_PASMO_NAZWA) };
static const int pasma_od[K1_PRZ_PASMA_N] = { K1_PRZ_PASMA(K1_X_PRZ_PASMO_OD) };
static const int pasma_do[K1_PRZ_PASMA_N] = { K1_PRZ_PASMA(K1_X_PRZ_PASMO_DO) };
static const uint32_t pasma_kolory[K1_PRZ_PASMA_N] = { K1_PRZ_PASMA(K1_X_PRZ_PASMO_KOLOR) };

#define K1_X_PRZ_NAZWA(n, nazwa, ...) nazwa,
#define K1_X_PRZ_OPIS(n, nazwa, opis, ...) opis,
#define K1_X_PRZ_MIEJSCA(n, nazwa, opis, ...) { __VA_ARGS__ },
static const char *const sceny_nazwy[K1_PRZ_SCEN] = { K1_PRZ_SCENY(K1_X_PRZ_NAZWA) };
static const char *const sceny_opisy[K1_PRZ_SCEN] = { K1_PRZ_SCENY(K1_X_PRZ_OPIS) };
static const int16_t sceny[K1_PRZ_SCEN][3 * K1_PRZ_PASMA_N] = { K1_PRZ_SCENY(K1_X_PRZ_MIEJSCA) };

/* The early reflections (rule 5: a table): frames after the sound, the ear (0 L, 1 R), the gain — a small room's walls,
 * alternately left and right, 7…27 ms (no long reverb). */
static const struct {
    int ramki, ucho;
    double g;
} odbicia[K1_PRZ_ODBICIA] = { { 341, 0, 0.45 }, { 466, 1, 0.45 }, { 638, 1, 0.32 },
                              { 811, 0, 0.32 }, { 1070, 0, 0.22 }, { 1301, 1, 0.22 } };

const char *k1_prz_pasmo_nazwa(int pasmo)
{
    return pasmo >= 0 && pasmo < K1_PRZ_PASMA_N ? pasma_nazwy[pasmo] : "";
}

int k1_prz_pasmo_od(int pasmo)
{
    return pasmo >= 0 && pasmo < K1_PRZ_PASMA_N ? pasma_od[pasmo] : 0;
}

int k1_prz_pasmo_do(int pasmo)
{
    return pasmo >= 0 && pasmo < K1_PRZ_PASMA_N ? pasma_do[pasmo] : 0;
}

uint32_t k1_prz_pasmo_kolor(int pasmo)
{
    return pasmo >= 0 && pasmo < K1_PRZ_PASMA_N ? pasma_kolory[pasmo] : 0;
}

const char *k1_prz_scena_nazwa(int nr)
{
    return nr >= 0 && nr < K1_PRZ_SCEN ? sceny_nazwy[nr] : "";
}

const char *k1_prz_scena_opis(int nr)
{
    return nr >= 0 && nr < K1_PRZ_SCEN ? sceny_opisy[nr] : "";
}

void k1_prz_scena_gotowa(int nr, struct k1_prz_scena *s)
{
    int z = nr >= 0 && nr < K1_PRZ_SCEN ? nr : K1_PRZ_NATURALNY;
    s->wlaczona = z == nr;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        s->pasma[i].kierunek = sceny[z][3 * i];
        s->pasma[i].szerokosc = sceny[z][3 * i + 1];
        s->pasma[i].odleglosc = sceny[z][3 * i + 2];
    }
}

static int miejsca_rowne(const struct k1_prz_scena *a, const struct k1_prz_scena *b)
{
    for (int i = 0; i < K1_PRZ_PASMA_N; i++)
        if (a->pasma[i].kierunek != b->pasma[i].kierunek || a->pasma[i].szerokosc != b->pasma[i].szerokosc ||
            a->pasma[i].odleglosc != b->pasma[i].odleglosc)
            return 0;
    return 1;
}

int k1_prz_rozpoznaj(const struct k1_prz_scena *s)
{
    for (int nr = 0; nr < K1_PRZ_SCEN; nr++) {
        struct k1_prz_scena g;
        k1_prz_scena_gotowa(nr, &g);
        if (miejsca_rowne(s, &g))
            return nr;
    }
    return -1;
}

const char *k1_prz_nazwa(const struct k1_prz_scena *s)
{
    int nr = k1_prz_rozpoznaj(s);
    return !s->wlaczona ? "Wyłączony" : nr >= 0 ? sceny_nazwy[nr] : "Własna";
}

int k1_prz_rowne(const struct k1_prz_scena *a, const struct k1_prz_scena *b)
{
    return (a->wlaczona != 0) == (b->wlaczona != 0) && (!a->wlaczona || miejsca_rowne(a, b));
}

int k1_prz_poprawna(const struct k1_prz_scena *s)
{
    if (s->wlaczona != 0 && s->wlaczona != 1)
        return 0;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        const struct k1_prz_miejsce *m = &s->pasma[i];
        if (m->kierunek > K1_PRZ_KIERUNEK_MAKS || m->kierunek < -K1_PRZ_KIERUNEK_MAKS || m->kierunek % K1_PRZ_KROK ||
            m->szerokosc < 0 || m->szerokosc > K1_PRZ_SZEROKOSC_MAKS || m->szerokosc % K1_PRZ_KROK ||
            m->odleglosc < 0 || m->odleglosc > K1_PRZ_ODLEGLOSC_MAKS)
            return 0;
    }
    return 1;
}

/* ---- the numbers ---- */

/* sin and cos of any angle: reduced to −π…π for our series (|x| ≤ 4). */
static void sin_cos(double x, double *s, double *c)
{
    double k = x / (2.0 * PI);
    long n = (long)(k < 0 ? k - 0.5 : k + 0.5);
    x -= (double)n * 2.0 * PI;
    *s = k1_kor_sin(x);
    *c = k1_kor_cos(x);
}

static double stopnie_w_zakres(double s) /* −180 < s ≤ 180 */
{
    while (s > 180.0)
        s -= 360.0;
    while (s <= -180.0)
        s += 360.0;
    return s;
}

/* The angle between a source at `kierunek` and the axis of ear `ucho` (0 L at −90°, 1 R at +90°), in radians 0…π. */
static double kat_od_ucha(double kierunek, int ucho)
{
    double t = stopnie_w_zakres(kierunek - (ucho ? 90.0 : -90.0));
    return (t < 0 ? -t : t) * PI / 180.0;
}

double k1_prz_opoznienie(double kierunek, int ucho)
{
    double t = kat_od_ucha(kierunek, ucho), s, c;
    sin_cos(t, &s, &c);
    return t < PI / 2.0 ? GLOWA_S * (1.0 - c) : GLOWA_S * (1.0 + t - PI / 2.0); /* Woodworth: around the sphere */
}

/* The head's shadow at an ear (Brown, Duda): its highs × α (2: the near side, +6 dB … ALFA_MIN: −20 dB at THETA_MIN). */
static double alfa_cienia(double t)
{
    double s, c;
    sin_cos(t / THETA_MIN * PI, &s, &c);
    return (1.0 + ALFA_MIN / 2.0) + (1.0 - ALFA_MIN / 2.0) * c;
}

/* (β + α·s) / (β + s) at 48 kHz (bilinear): 1 at the bottom, α at the top, the turn around β (rad/s). */
static void filtr_pierwszy(double f[3], double beta, double alfa)
{
    double k = 2.0 * FS;
    f[0] = (beta + alfa * k) / (beta + k);
    f[1] = (beta - alfa * k) / (beta + k);
    f[2] = (beta - k) / (beta + k);
}

/* A path of band `b`: `opoznienie` s after the common D0, the shadow α and the back's α, the gain. */
static void droga(struct k1_prz_ustawienie *u, int b, int zrodlo, int ucho, double opoznienie, double alfa, double tyl,
                  double g)
{
    struct k1_prz_droga *d;
    double tau = D0 + opoznienie * FS, mi, x;
    if (u->drog[b] >= K1_PRZ_DROGI_MAKS || g == 0.0)
        return;
    d = &u->d[b][u->drog[b]++];
    d->zrodlo = zrodlo;
    d->ucho = ucho;
    d->n = (int)tau;
    mi = tau - d->n;
    x = 1.0 + mi; /* the place between the taps n−1 … n+2 (Lagrange, 3rd order) */
    d->h[0] = -(x - 1.0) * (x - 2.0) * (x - 3.0) / 6.0;
    d->h[1] = x * (x - 2.0) * (x - 3.0) / 2.0;
    d->h[2] = -x * (x - 1.0) * (x - 3.0) / 2.0;
    d->h[3] = x * (x - 1.0) * (x - 2.0) / 6.0;
    filtr_pierwszy(d->f[0], 2.0 / GLOWA_S, alfa);
    filtr_pierwszy(d->f[1], 2.0 * PI * TYL_HZ, tyl);
    d->g = g;
}

/* The bass, and a scene's band that keeps its stereo: left to left, right to right. */
static void bez_zmian(struct k1_prz_ustawienie *u, int b, double g)
{
    droga(u, b, 0, 0, 0.0, 1.0, 1.0, g);
    droga(u, b, 1, 1, 0.0, 1.0, 1.0, g);
}

/* One source at `kierunek` into ear `ucho`: its delay, the head's shadow, the softer back (headphones). */
static void zrodlo_sluchawek(struct k1_prz_ustawienie *u, int b, int wejscie, int ucho, double kierunek, double g)
{
    double k = stopnie_w_zakres(kierunek), a = k < 0 ? -k : k;
    double tyl = a > 90.0 ? 1.0 - (1.0 - TYL_MIN) * (a - 90.0) / 90.0 : 1.0;
    droga(u, b, wejscie, ucho, k1_prz_opoznienie(k, ucho), alfa_cienia(kat_od_ucha(k, ucho)), tyl, g);
}

/* Headphones: the band's two sources at its direction ± half its width. 0.13.0 (the owner's report: "it flattens the
 * sound"): what is the same in both channels (M = (L+R)/2: a voice, the middle of a song) comes from ONE source at the
 * band's direction — from two sources each ear got it twice, late by a different delay, and the two copies cancelled
 * (−6.6 dB at 1 kHz in Koncert: the comb that made a voice hollow); only the sides (S = (L−R)/2) come from the two
 * sources, as their difference. Each ear: M·2·H(direction) + S·(H(left source) − H(right source)) — the same as two
 * sources where they agree (the low tones, a width 0). */
static void sluchawki(struct k1_prz_ustawienie *u, int b, const struct k1_prz_miejsce *m, double g)
{
    double lewe = m->kierunek - 0.5 * m->szerokosc, prawe = m->kierunek + 0.5 * m->szerokosc;
    for (int e = 0; e < 2; e++) {
        zrodlo_sluchawek(u, b, 0, e, m->kierunek, g);
        zrodlo_sluchawek(u, b, 1, e, m->kierunek, g);
        if (m->szerokosc == 0)
            continue;
        zrodlo_sluchawek(u, b, 0, e, lewe, 0.5 * g);
        zrodlo_sluchawek(u, b, 1, e, lewe, -0.5 * g);
        zrodlo_sluchawek(u, b, 0, e, prawe, -0.5 * g);
        zrodlo_sluchawek(u, b, 1, e, prawe, 0.5 * g);
    }
}

/* The speakers (D3: A): a direction behind mirrored to the front, the pair panned there (equal power, 1 in the middle),
 * the sides × w (the width against a normal stereo) and, once wider than normal, a gentle cross-cancellation of the sides
 * only — the side (L−R)/2 once more, late, with the sign of each speaker: what is the same in both channels (a voice)
 * keeps its level. */
static void glosniki(struct k1_prz_ustawienie *u, int b, const struct k1_prz_miejsce *m, double g)
{
    double k = m->kierunek > 90 ? 180.0 - m->kierunek : m->kierunek < -90 ? -180.0 - m->kierunek : m->kierunek;
    double p, c, gl, gp, w = m->szerokosc / SZEROKOSC_STEREO, krzyz, wl[2][2], t = KRZYZ_RAMKI / FS;
    sin_cos(k * PI / 180.0, &p, &c);
    sin_cos((p + 1.0) * PI / 4.0, &gp, &gl);
    gl *= 1.41421356237309504880;
    gp *= 1.41421356237309504880;
    w = w > POSZERZENIE_MAKS ? POSZERZENIE_MAKS : w;
    krzyz = w > 1.0 ? KRZYZ_MAKS * (w - 1.0) / (POSZERZENIE_MAKS - 1.0) : 0.0;
    /* L' = gl·(M + w·S + krzyz·w·S late), R' = gp·(M − w·S − krzyz·w·S late); M = (L+R)/2, S = (L−R)/2 */
    wl[0][0] = gl * (1.0 + w) / 2.0, wl[1][0] = gl * (1.0 - w) / 2.0;
    wl[0][1] = gp * (1.0 - w) / 2.0, wl[1][1] = gp * (1.0 + w) / 2.0;
    for (int z = 0; z < 2; z++)
        for (int e = 0; e < 2; e++) {
            double bok = (z == e ? 1.0 : -1.0) * (e == 0 ? gl : gp) * krzyz * w / 2.0;
            droga(u, b, z, e, 0.0, 1.0, 1.0, g * wl[z][e]);
            droga(u, b, z, e, t, 1.0, 1.0, g * bok);
        }
}

/* ---- the level (ADR 0055 point 17): the bound of k1_prz_moc on a grid, the scene × 1/its maximum when above 1 ---- */

struct zesp {
    double re, im;
};

static struct zesp mnoz(struct zesp a, struct zesp b)
{
    struct zesp w = { a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re };
    return w;
}

static struct zesp dziel(struct zesp a, struct zesp b)
{
    double m = b.re * b.re + b.im * b.im;
    struct zesp w = { (a.re * b.re + a.im * b.im) / m, (a.im * b.re - a.re * b.im) / m };
    return w;
}

/* The response of a path at ω (rad per frame): its delay (the Lagrange taps), its two filters, its gain. */
static struct zesp odpowiedz(const struct k1_prz_droga *d, double w)
{
    struct zesp h = { 0, 0 }, krok, z, f = { 1, 0 };
    sin_cos(-w, &krok.im, &krok.re);
    sin_cos(-w * (d->n - 1), &z.im, &z.re);
    for (int k = 0; k < 4; k++) {
        h.re += d->h[k] * z.re;
        h.im += d->h[k] * z.im;
        z = mnoz(z, krok);
    }
    for (int i = 0; i < 2; i++) {
        struct zesp l = { d->f[i][0] + d->f[i][1] * krok.re, d->f[i][1] * krok.im };
        struct zesp m = { 1.0 + d->f[i][2] * krok.re, d->f[i][2] * krok.im };
        f = mnoz(f, dziel(l, m));
    }
    h = mnoz(h, f);
    h.re *= d->g;
    h.im *= d->g;
    return h;
}

static double modul(struct zesp z)
{
    return k1_kor_pierw(z.re * z.re + z.im * z.im);
}

double k1_prz_moc(const struct k1_prz_ustawienie *u, double hz, int ucho)
{
    double w, suma = 0.0, lp, k = 1.0 - k1_kor_exp(-2.0 * PI * ODBICIA_HZ / FS), odb = 0.0;
    struct zesp krok, l;
    if (!u->scena.wlaczona)
        return 1.0;
    hz = hz < 1.0 ? 1.0 : hz > FS / 2.0 - 1.0 ? FS / 2.0 - 1.0 : hz;
    w = 2.0 * PI * hz / FS;
    sin_cos(-w, &krok.im, &krok.re);
    l.re = 1.0 - (1.0 - k) * krok.re; /* the reflections' low-pass k / (1 − (1−k)·e^−jω) */
    l.im = -(1.0 - k) * krok.im;
    lp = k / modul(l);
    for (int t = 0; t < K1_PRZ_ODBICIA; t++)
        if (odbicia[t].ucho == ucho)
            odb += u->odb_g[t] < 0 ? -u->odb_g[t] : u->odb_g[t];
    for (int b = 0; b < K1_PRZ_PASMA_N; b++) {
        struct zesp z[2] = { { 0, 0 }, { 0, 0 } }, mono;
        double r = u->odbicie[b] * lp * odb, jeden, moc;
        for (int i = 0; i < u->drog[b]; i++)
            if (u->d[b][i].ucho == ucho) {
                struct zesp h = odpowiedz(&u->d[b][i], w);
                z[u->d[b][i].zrodlo].re += h.re;
                z[u->d[b][i].zrodlo].im += h.im;
            }
        mono.re = z[0].re + z[1].re;
        mono.im = z[0].im + z[1].im;
        jeden = modul(mono) + r;                                                /* the same in both inputs */
        moc = k1_kor_pierw(z[0].re * z[0].re + z[0].im * z[0].im + z[1].re * z[1].re + z[1].im * z[1].im) +
              r * 0.70710678118654752440;                                       /* two unrelated inputs */
        suma += k1_prz_pasmo_moc(b, hz) * (jeden > moc ? jeden : moc);          /* the bands' weights sum to 1 */
    }
    return suma;
}

/* 0.13.0 (ADR 0055 point 21): the power a music-like sound has at `hz` in ear `ucho` against 1 without the scene — half the
 * same in both inputs, half unrelated; the reflections add their power (they come late: unrelated to the sound). */
static double glosnosc(const struct k1_prz_ustawienie *u, double hz, int ucho)
{
    double w = 2.0 * PI * hz / FS, lp, k = 1.0 - k1_kor_exp(-2.0 * PI * ODBICIA_HZ / FS), odb = 0.0, suma = 0.0;
    struct zesp krok, l;
    sin_cos(-w, &krok.im, &krok.re);
    l.re = 1.0 - (1.0 - k) * krok.re;
    l.im = -(1.0 - k) * krok.im;
    lp = k / modul(l);
    for (int t = 0; t < K1_PRZ_ODBICIA; t++)
        if (odbicia[t].ucho == ucho)
            odb += u->odb_g[t] * u->odb_g[t];
    for (int b = 0; b < K1_PRZ_PASMA_N; b++) {
        struct zesp z[2] = { { 0, 0 }, { 0, 0 } };
        double jeden, dwa, r = u->odbicie[b] * lp;
        for (int i = 0; i < u->drog[b]; i++)
            if (u->d[b][i].ucho == ucho) {
                struct zesp h = odpowiedz(&u->d[b][i], w);
                z[u->d[b][i].zrodlo].re += h.re;
                z[u->d[b][i].zrodlo].im += h.im;
            }
        jeden = (z[0].re + z[1].re) * (z[0].re + z[1].re) + (z[0].im + z[1].im) * (z[0].im + z[1].im) + r * r * odb;
        dwa = z[0].re * z[0].re + z[0].im * z[0].im + z[1].re * z[1].re + z[1].im * z[1].im + 0.5 * r * r * odb;
        suma += k1_prz_pasmo_moc(b, hz) * k1_kor_pierw(0.5 * (jeden + dwa)); /* the bands add up in amplitude */
    }
    return suma * suma;
}

void k1_prz_policz(struct k1_prz_ustawienie *u, const struct k1_prz_scena *s, int wyjscie)
{
    double maks = 0.0, srednia = 0.0, wagi = 0.0;
    memset(u, 0, sizeof *u);
    u->scena = *s;
    u->wzm = 1.0;
    u->wyjscie = wyjscie == K1_DZW_WYJSCIE_SLUCHAWKI ? K1_DZW_WYJSCIE_SLUCHAWKI : K1_DZW_WYJSCIE_GLOSNIKI;
    if (!s->wlaczona || !k1_prz_poprawna(s)) { /* the caller checked it (k1_prz_z_pol); never trusted beyond */
        u->scena.wlaczona = 0;
        return;
    }
    for (int b = 0; b < K1_PRZ_PASMA_N; b++) {
        const struct k1_prz_miejsce *m = &s->pasma[b];
        double g = k1_kor_exp(-LN10 * BLIZEJ_DB / 20.0 * m->odleglosc / K1_PRZ_ODLEGLOSC_MAKS);
        if (b == 0)
            bez_zmian(u, b, g); /* the bass stays in the middle: the ear does not place it */
        else if (u->wyjscie == K1_DZW_WYJSCIE_SLUCHAWKI)
            sluchawki(u, b, m, g);
        else
            glosniki(u, b, m, g);
        u->odbicie[b] = ODBICIA_MAKS * m->odleglosc / K1_PRZ_ODLEGLOSC_MAKS;
    }
    for (int t = 0; t < K1_PRZ_ODBICIA; t++)
        u->odb_g[t] = odbicia[t].g;
    for (int k = 0; k < SIATKA + K1_PRZ_PASMA_N; k++) { /* the loudest point: the grid and every crossover */
        double hz = k < SIATKA ? 20.0 * k1_kor_exp(LN1000 * k / (SIATKA - 1)) : (double)pasma_od[k - SIATKA];
        for (int e = 0; e < 2; e++) {
            double m = k1_prz_moc(u, hz < 20.0 ? 20.0 : hz, e);
            maks = m > maks ? m : maks;
            if (k < SIATKA) { /* the grid is even in octaves (a music-like, pink sound), K-weighted: its loudness */
                srednia += k1_kor_waga(hz) * glosnosc(u, hz < 20.0 ? 20.0 : hz, e);
                wagi += k1_kor_waga(hz);
            }
        }
    }
    /* 0.13.0 (ADR 0055 point 21, the owner's report: "the whole sound much quieter, flattened"): the loudness the sound
     * had without the scene — up to K1_DZW_ZAPAS above 100 % at the loudest point, the limiter takes the rest */
    u->wzm = k1_kor_pierw(wagi / srednia);
    if (u->wzm * maks > K1_DZW_ZAPAS)
        u->wzm = K1_DZW_ZAPAS / maks;
    for (int b = 0; b < K1_PRZ_PASMA_N; b++)
        for (int i = 0; i < u->drog[b]; i++)
            u->d[b][i].g *= u->wzm;
    for (int t = 0; t < K1_PRZ_ODBICIA; t++)
        u->odb_g[t] *= u->wzm;
}

int k1_prz_odbicie_ramki(int t, int *ucho)
{
    if (t < 0 || t >= K1_PRZ_ODBICIA)
        return 0;
    *ucho = odbicia[t].ucho;
    return odbicia[t].ramki;
}

/* K1 Ustawienia — the scene of Kons Spatial Audio to drag (0.12.3 step 4): see scena.h. Part of Ustawienia (przestrzen.c
 * keeps the scene; this only draws it and turns the pointer into places). */
#include "scena.h"

#include <math.h>
#include <stdio.h>

#include "k1_opis_klient.h"
#include "k1_rysuj.h"

#define PI_F 3.14159265f
#define GLOWA 19.0f   /* the head's radius */
#define R_BLISKO 36.0f /* the distance 0 % of the first band with a direction (Niski środek) */
#define PAS 12.0f     /* each next band's lane, further out */
#define ZAKRES 58.0f  /* from the distance 0 % to 100 % */
#define R_BAS 26.0f   /* the bass's ring at the distance 0 % */
#define PUNKT_R 7.0f
#define UCHWYT_R 4.0f

static float promien(int pasmo, int odleglosc)
{
    float od = pasmo == 0 ? R_BAS : R_BLISKO + PAS * (float)(pasmo - 1);
    return od + ZAKRES * (float)odleglosc / K1_PRZ_ODLEGLOSC_MAKS;
}

static void miejsce(const struct scena_widok *w, float r, float stopnie, float *x, float *y)
{
    float a = stopnie * PI_F / 180.0f;
    *x = (float)w->cx + r * sinf(a);
    *y = (float)w->cy - r * cosf(a);
}

static uint32_t alfa(const struct scena_widok *w, uint32_t a)
{
    return w->wlaczona ? a : a * 2 / 5;
}

static void luk(struct k1_obraz *o, const struct scena_widok *w, float r, float od, float dd, float g, uint32_t kolor)
{
    int n = (int)((dd - od) / 3.0f) + 1;
    float x0, y0;
    miejsce(w, r, od, &x0, &y0);
    for (int i = 1; i <= n; i++) {
        float x1, y1;
        miejsce(w, r, od + (dd - od) * (float)i / (float)n, &x1, &y1);
        k1_kreska(o, x0, y0, x1, y1, g, kolor);
        x0 = x1;
        y0 = y1;
    }
}

/* The disc, the rings of the distance, "przód" and "tył", on the speakers the back half dimmed. */
static void tlo(struct k1_obraz *o, const struct k1_motyw *m, const struct scena_widok *w)
{
    float cx = (float)w->cx, cy = (float)w->cy;
    k1_kolo(o, cx, cy, SCENA_R + 6, k1_kolor(m->pole, 255));
    k1_okrag(o, cx, cy, SCENA_R + 6, 1.0f, k1_kolor(m->tekst, 22));
    for (int i = 0; i <= 2; i++)
        k1_okrag(o, cx, cy, promien(2, 50 * i), 1.0f, k1_kolor(m->tekst, 16));
    k1_kreska(o, cx, cy - SCENA_R, cx, cy + SCENA_R, 1.0f, k1_kolor(m->tekst, 10));
    k1_kreska(o, cx - SCENA_R, cy, cx + SCENA_R, cy, 1.0f, k1_kolor(m->tekst, 10));
    if (w->glosniki)
        for (int dy = 1; dy <= SCENA_R + 6; dy++) {
            int32_t p = (int32_t)sqrtf((float)((SCENA_R + 6) * (SCENA_R + 6) - dy * dy));
            k1_prostokat(o, w->cx - p, w->cy + dy, 2 * p, 1, k1_kolor(m->tlo, 150));
        }
    k1_tekst(o, K1_CZ_MALA, w->cx - k1_tekst_szer(K1_CZ_MALA, "przód") / 2, w->cy - SCENA_R + 14, "przód",
             k1_kolor(m->tekst3, 255), 0);
    if (w->glosniki) {
        const char *t = "na głośnikach tył gra z przodu";
        k1_tekst(o, K1_CZ_MALA, w->cx - k1_tekst_szer(K1_CZ_MALA, t) / 2, w->cy + SCENA_R - 6, t,
                 k1_kolor(m->tekst3, 255), 0);
    } else {
        k1_tekst(o, K1_CZ_MALA, w->cx - k1_tekst_szer(K1_CZ_MALA, "tył") / 2, w->cy + SCENA_R - 6, "tył",
                 k1_kolor(m->tekst3, 255), 0);
    }
}

/* The head from above: the nose to the front, the ears at the sides. */
static void glowa(struct k1_obraz *o, const struct k1_motyw *m, const struct scena_widok *w)
{
    float cx = (float)w->cx, cy = (float)w->cy;
    uint32_t linia = k1_kolor(m->tekst2, 255);
    k1_zaokraglony(o, cx - GLOWA - 5, cy - 7, 7, 14, 3.5f, linia);
    k1_zaokraglony(o, cx + GLOWA - 2, cy - 7, 7, 14, 3.5f, linia);
    k1_kolo(o, cx, cy, GLOWA, k1_kolor(m->tlo, 255));
    k1_okrag(o, cx, cy, GLOWA, 1.5f, linia);
    k1_kreska(o, cx - 6, cy - GLOWA + 1.5f, cx, cy - GLOWA - 7, 1.5f, linia);
    k1_kreska(o, cx, cy - GLOWA - 7, cx + 6, cy - GLOWA + 1.5f, 1.5f, linia);
}

static void bas(struct k1_obraz *o, const struct k1_motyw *m, const struct scena_widok *w, const struct k1_prz_scena *s)
{
    float r = promien(0, s->pasma[0].odleglosc);
    uint32_t k = k1_prz_pasmo_kolor(0);
    if (w->grajace == 0)
        k1_okrag(o, (float)w->cx, (float)w->cy, r, 9.0f, k1_kolor(k, alfa(w, 60)));
    k1_okrag(o, (float)w->cx, (float)w->cy, r, w->wybrane == 0 ? 4.0f : 3.0f,
             k1_kolor(k, alfa(w, w->wybrane == 0 || w->najechane == 0 ? 230 : 150)));
    if (w->wybrane == 0)
        k1_okrag(o, (float)w->cx, (float)w->cy, r + 5, 1.5f, k1_kolor(m->tekst, alfa(w, 150)));
    k1_el(K1_EL_PRZYCISK, "Bas", w->cx - (int32_t)r, w->cy - (int32_t)r - 3, 2 * (int32_t)r, 6,
          w->wybrane == 0 ? K1_EL_WYBRANY : 0);
}

static void pasmo(struct k1_obraz *o, const struct k1_motyw *m, const struct scena_widok *w,
                  const struct k1_prz_scena *s, int b)
{
    const struct k1_prz_miejsce *p = &s->pasma[b];
    float r = promien(b, p->odleglosc), k = (float)p->kierunek, pol = (float)p->szerokosc / 2.0f, x, y, lx, ly, px, py;
    uint32_t kol = k1_prz_pasmo_kolor(b);
    int wyb = w->wybrane == b, naj = w->najechane == b;
    luk(o, w, r, k - pol, k + pol, wyb ? 4.0f : 3.0f, k1_kolor(kol, alfa(w, wyb || naj ? 220 : 140)));
    miejsce(w, r, k - pol, &lx, &ly);
    miejsce(w, r, k + pol, &px, &py);
    miejsce(w, r, k, &x, &y);
    for (int i = 0; i < 2; i++) {
        float hx = i ? px : lx, hy = i ? py : ly;
        k1_kolo(o, hx, hy, UCHWYT_R, k1_kolor(m->pole, 255));
        k1_okrag(o, hx, hy, UCHWYT_R, 1.5f, k1_kolor(kol, alfa(w, 255)));
    }
    if (w->grajace == b)
        k1_kolo(o, x, y, PUNKT_R + 8, k1_kolor(kol, alfa(w, 70)));
    if (naj && !wyb)
        k1_okrag(o, x, y, PUNKT_R + 3, 2.0f, k1_kolor(kol, alfa(w, 120)));
    k1_kolo(o, x, y, PUNKT_R, k1_kolor(kol, alfa(w, 255)));
    if (wyb)
        k1_okrag(o, x, y, PUNKT_R + 4, 2.0f, k1_kolor(m->tekst, alfa(w, 200)));
    k1_el(K1_EL_PRZYCISK, k1_prz_pasmo_nazwa(b), (int32_t)x - 9, (int32_t)y - 9, 18, 18, wyb ? K1_EL_WYBRANY : 0);
}

void scena_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct scena_widok *w,
                 const struct k1_prz_scena *s)
{
    tlo(o, m, w);
    bas(o, m, w, s);
    glowa(o, m, w);
    for (int b = 1; b < K1_PRZ_PASMA_N; b++)
        if (b != w->wybrane)
            pasmo(o, m, w, s, b);
    if (w->wybrane > 0 && w->wybrane < K1_PRZ_PASMA_N)
        pasmo(o, m, w, s, w->wybrane); /* the selected one on top */
}

static float odl2(float x0, float y0, float x1, float y1)
{
    return (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
}

/* Band b's part under (x, y): SCENA_* or -1. */
static int czesc_pasma(const struct scena_widok *w, const struct k1_prz_scena *s, int b, float x, float y)
{
    const struct k1_prz_miejsce *p = &s->pasma[b];
    float r = promien(b, p->odleglosc), pol = (float)p->szerokosc / 2.0f, px, py, ex, ey;
    if (b == 0) {
        float d = sqrtf(odl2((float)w->cx, (float)w->cy, x, y));
        return d > GLOWA + 2 && fabsf(d - r) <= 6.0f ? SCENA_PUNKT : -1;
    }
    miejsce(w, r, (float)p->kierunek, &px, &py);
    if (odl2(px, py, x, y) <= 4.0f * 4.0f)
        return SCENA_PUNKT; /* the middle of the point: always the point (its ends can sit on it) */
    for (int i = 0; i < 2; i++) {
        miejsce(w, r, (float)p->kierunek + (i ? pol : -pol), &ex, &ey);
        if (odl2(ex, ey, x, y) <= 7.0f * 7.0f)
            return i ? SCENA_PRAWY : SCENA_LEWY;
    }
    return odl2(px, py, x, y) <= 10.0f * 10.0f ? SCENA_PUNKT : -1;
}

int scena_pod(const struct scena_widok *w, const struct k1_prz_scena *s, int32_t x, int32_t y, int *czesc)
{
    int c;
    if (w->wybrane >= 0 && w->wybrane < K1_PRZ_PASMA_N && (c = czesc_pasma(w, s, w->wybrane, (float)x, (float)y)) >= 0) {
        *czesc = c;
        return w->wybrane;
    }
    for (int b = K1_PRZ_PASMA_N - 1; b >= 0; b--) /* the one drawn last first */
        if ((c = czesc_pasma(w, s, b, (float)x, (float)y)) >= 0) {
            *czesc = c;
            return b;
        }
    return -1;
}

static int krokami(float v, int maks, int krok)
{
    int i = (int)lroundf(v / (float)krok) * krok;
    return i > maks ? maks : i < -maks ? -maks : i;
}

int scena_przeciagnij(const struct scena_widok *w, struct k1_prz_scena *s, int pasmo, int czesc, int32_t x, int32_t y)
{
    struct k1_prz_miejsce *p, stare;
    float dx = (float)(x - w->cx), dy = (float)(y - w->cy), d = sqrtf(dx * dx + dy * dy);
    float kat = atan2f(dx, -dy) * 180.0f / PI_F;
    if (pasmo < 0 || pasmo >= K1_PRZ_PASMA_N)
        return 0;
    p = &s->pasma[pasmo];
    stare = *p;
    if (czesc == SCENA_PUNKT) {
        float od = pasmo == 0 ? R_BAS : R_BLISKO + PAS * (float)(pasmo - 1);
        int o = (int)lroundf((d - od) / ZAKRES * K1_PRZ_ODLEGLOSC_MAKS);
        p->odleglosc = (int16_t)(o < 0 ? 0 : o > K1_PRZ_ODLEGLOSC_MAKS ? K1_PRZ_ODLEGLOSC_MAKS : o);
        if (pasmo > 0 && d > GLOWA) /* the bass has no direction */
            p->kierunek = (int16_t)krokami(kat, K1_PRZ_KIERUNEK_MAKS, K1_PRZ_KROK);
    } else if (pasmo > 0 && d > GLOWA) {
        float roznica = kat - (float)p->kierunek;
        int sz;
        while (roznica > 180.0f)
            roznica -= 360.0f;
        while (roznica < -180.0f)
            roznica += 360.0f;
        sz = krokami(2.0f * fabsf(roznica), K1_PRZ_SZEROKOSC_MAKS, K1_PRZ_KROK);
        p->szerokosc = (int16_t)(sz < SCENA_SZEROKOSC_MIN ? SCENA_SZEROKOSC_MIN : sz);
    }
    return p->kierunek != stare.kierunek || p->szerokosc != stare.szerokosc || p->odleglosc != stare.odleglosc;
}

static int16_t w_zakresie(int v, int od, int dd)
{
    return (int16_t)(v < od ? od : v > dd ? dd : v);
}

int scena_klawisz(struct k1_prz_scena *s, int pasmo, int kierunek, int odleglosc, int szerokosc)
{
    struct k1_prz_miejsce *p, stare;
    if (pasmo < 0 || pasmo >= K1_PRZ_PASMA_N)
        return 0;
    p = &s->pasma[pasmo];
    stare = *p;
    if (pasmo > 0) {
        int k = p->kierunek + kierunek * 10; /* 10° a press; past straight behind it goes on the other side */
        k = k > K1_PRZ_KIERUNEK_MAKS ? k - 360 : k < -K1_PRZ_KIERUNEK_MAKS ? k + 360 : k;
        p->kierunek = (int16_t)k;
        p->szerokosc = w_zakresie(p->szerokosc + szerokosc * 10, 0, K1_PRZ_SZEROKOSC_MAKS);
    }
    p->odleglosc = w_zakresie(p->odleglosc + odleglosc * 10, 0, K1_PRZ_ODLEGLOSC_MAKS);
    return p->kierunek != stare.kierunek || p->szerokosc != stare.szerokosc || p->odleglosc != stare.odleglosc;
}

void scena_kierunek_slowami(int kierunek, char *buf, unsigned n)
{
    if (kierunek == 0)
        snprintf(buf, n, "przód");
    else if (kierunek >= K1_PRZ_KIERUNEK_MAKS || kierunek <= -K1_PRZ_KIERUNEK_MAKS)
        snprintf(buf, n, "tył");
    else
        snprintf(buf, n, "%d° w %s", kierunek < 0 ? -kierunek : kierunek, kierunek < 0 ? "lewo" : "prawo");
}

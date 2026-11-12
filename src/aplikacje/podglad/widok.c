/* K1 Podgląd — the view of the picture (0.8.2 step 4): turned (R, a quarter at a time), fitted to the window (never
 * enlarged above 100 %), zoomed in and out (− +, the owner's wish: levels of the fitted size, each change a short
 * animation) and, when zoomed in, moved by dragging with the mouse. The picture from dekoder is at most BOK × BOK
 * pixels; zooming in shows those pixels larger (up to 4 × — beyond that only blocks would grow). The file stays as it
 * is. Part of Podgląd (podglad.h). */
#include <math.h>

#include "k1_log.h"
#include "k1_skaluj.h"
#include "k1_teraz.h"
#include "podglad.h"

#define ANIMACJA 0.18     /* s: one change of the zoom */
#define KLATKA 15         /* ms between frames of it */
#define MAKS_PIKSEL 4.0   /* a pixel of the picture is never shown larger than this many pixels of the screen */

static const double POZIOMY[] = { 1, 1.5, 2, 3, 4, 6, 8 }; /* × the fitted size */
#define N_POZIOMOW (int)(sizeof POZIOMY / sizeof POZIOMY[0])

static uint32_t obrocony[BOK * BOK], widok[OBSZAR_W * OBSZAR_H];
static const uint32_t *zrodlo; /* the picture as dekoder gave it (NULL: none) */
static int32_t zw, zh;         /* its size */
static int obrot;              /* quarter turns to the right, 0..3 */
static int32_t ow, oh;         /* obrocony: the picture turned */
static double skala = 1;       /* × the fitted size, as shown now */
static double cel = 1, od = 1, t0 = -1; /* the zoom wanted, where the animation started and when (-1: none) */
static double cx, cy;          /* the middle of the view, in pixels of obrocony */
static int32_t ww, wh;         /* the view: widok, ww × wh */

/* zrodlo turned `obrot` quarters into obrocony. */
static void obroc(void)
{
    int32_t x, y;
    ow = obrot % 2 ? zh : zw;
    oh = obrot % 2 ? zw : zh;
    for (y = 0; y < oh; y++)
        for (x = 0; x < ow; x++) {
            int32_t sx = obrot == 0 ? x : obrot == 1 ? y : obrot == 2 ? zw - 1 - x : zw - 1 - y;
            int32_t sy = obrot == 0 ? y : obrot == 1 ? zh - 1 - x : obrot == 2 ? zh - 1 - y : x;
            obrocony[y * ow + x] = zrodlo[sy * zw + sx];
        }
    cx = ow / 2.0;
    cy = oh / 2.0;
}

/* Screen pixels per pixel of obrocony when fitted (never above 1). */
static double dopasowanie(void)
{
    double f = fmin((double)OBSZAR_W / ow, (double)OBSZAR_H / oh);
    return f < 1 ? f : 1;
}

static double ogranicz(double v, double polowa, double calosc)
{
    return polowa * 2 >= calosc ? calosc / 2 : v < polowa ? polowa : v > calosc - polowa ? calosc - polowa : v;
}

/* The view at `skala`: the part of obrocony around (cx, cy) that fits the area, resampled (k1_skaluj). */
static void przelicz(void)
{
    double s = dopasowanie() * skala, vw, vh;
    ww = wh = 0;
    if (zrodlo == NULL)
        return;
    ww = (int32_t)fmin(floor(ow * s + 0.5), OBSZAR_W);
    wh = (int32_t)fmin(floor(oh * s + 0.5), OBSZAR_H);
    ww = ww < 1 ? 1 : ww;
    wh = wh < 1 ? 1 : wh;
    vw = fmin(ww / s, ow);
    vh = fmin(wh / s, oh);
    cx = ogranicz(cx, vw / 2, ow);
    cy = ogranicz(cy, vh / 2, oh);
    if (k1_skaluj(obrocony, (uint32_t)ow, (uint32_t)oh, (uint32_t)ow, (struct k1_wycinek){ cx - vw / 2, cy - vh / 2, vw, vh },
                  widok, (uint32_t)ww, (uint32_t)wh, (uint32_t)ww) != 0)
        ww = wh = 0;
}

void widok_nowy(const uint32_t *px, int32_t w, int32_t h)
{
    zrodlo = px;
    zw = w;
    zh = h;
    obrot = 0;
    skala = cel = 1;
    t0 = -1;
    if (px != NULL)
        obroc();
    przelicz();
}

void widok_obroc(void)
{
    if (zrodlo == NULL)
        return;
    obrot = (obrot + 1) % 4;
    skala = cel = 1; /* a turned picture starts fitted again */
    t0 = -1;
    obroc();
    przelicz();
    k1_log(TAG, "obrót %d°: widok %dx%d", obrot * 90, (int)ww, (int)wh);
}

int widok_mozna(int krok)
{
    if (zrodlo == NULL)
        return 0;
    if (krok < 0)
        return cel > 1;
    if (krok == 0)
        return cel != 1;
    for (int i = 0; i < N_POZIOMOW; i++)
        if (POZIOMY[i] > cel && dopasowanie() * POZIOMY[i] <= MAKS_PIKSEL)
            return 1;
    return 0;
}

void widok_powieksz(int krok)
{
    double nowy = 1;
    if (!widok_mozna(krok))
        return;
    if (krok > 0)
        for (int i = N_POZIOMOW - 1; i >= 0; i--)
            nowy = POZIOMY[i] > cel && dopasowanie() * POZIOMY[i] <= MAKS_PIKSEL ? POZIOMY[i] : nowy;
    else if (krok < 0)
        for (int i = 0; i < N_POZIOMOW; i++)
            nowy = POZIOMY[i] < cel ? POZIOMY[i] : nowy;
    od = skala;
    cel = nowy;
    t0 = k1_teraz();
}

int widok_powiekszony(void)
{
    return zrodlo != NULL && skala > 1;
}

void widok_przesun(int32_t dx, int32_t dy)
{
    double s = dopasowanie() * skala;
    if (!widok_powiekszony())
        return;
    cx -= dx / s;
    cy -= dy / s;
    przelicz();
}

int widok_zegar(void)
{
    double t;
    if (t0 < 0)
        return -1;
    t = (k1_teraz() - t0) / ANIMACJA;
    if (t >= 1) {
        skala = cel;
        t0 = -1;
    } else
        skala = od + (cel - od) * (1 - pow(1 - t, 3)); /* fast, then slower: ease-out */
    przelicz();
    k1_apl_odswiez(&apl);
    if (t0 < 0) /* the tests follow the zoom by this line */
        k1_log(TAG, "powiększenie %d%%: widok %dx%d", (int)(skala * 100 + 0.5), (int)ww, (int)wh);
    return t0 < 0 ? -1 : KLATKA;
}

int widok_rysuj(struct k1_obraz *o)
{
    if (zrodlo == NULL || ww == 0)
        return 0;
    k1_naloz_sprite(o, widok, (uint32_t)ww, (uint32_t)wh, OBSZAR_X + (OBSZAR_W - ww) / 2, OBSZAR_Y + (OBSZAR_H - wh) / 2);
    return 1;
}

void widok_rozmiar(int32_t *w, int32_t *h)
{
    *w = ww;
    *h = wh;
}

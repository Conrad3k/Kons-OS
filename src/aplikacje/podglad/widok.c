/* K1 Podgląd — the view of the picture (0.8.2 step 4): turned (R, a quarter at a time), fitted to the window (never
 * enlarged above 100 %), zoomed in and out (− +, the owner's wish: levels of the fitted size, each change a short
 * animation) and, when zoomed in, moved by dragging with the mouse. The picture from dekoder is at most BOK × BOK
 * pixels; zooming in shows those pixels larger (up to 4 × — beyond that only blocks would grow). The file stays as it
 * is. Part of Podgląd (podglad.h).
 *
 * Smooth (0.8.5 step 2): zooming and dragging only change the state; the pixels are computed once per frame, when the
 * window is drawn. While the picture moves they come from a smaller copy, bilinearly (skala.c: about 1/8 of the time);
 * ZOSTAJE after the last move the exact filter (k1_skaluj) draws it once more — sharp. The animation follows the clock,
 * not the frames: a slow frame skips ahead instead of making it longer. */
#include <math.h>
#include <string.h>

#include "k1_log.h"
#include "k1_skaluj.h"
#include "k1_teraz.h"
#include "podglad.h"
#include "skala.h"

#define ANIMACJA 0.18     /* s: one change of the zoom */
#define KLATKA 15         /* ms between frames of it */
#define ZOSTAJE 0.12      /* s without a move: the exact view */
#define MAKS_PIKSEL 4.0   /* a pixel of the picture is never shown larger than this many pixels of the screen */

static const double POZIOMY[] = { 1, 1.5, 2, 3, 4, 6, 8 }; /* × the fitted size */
#define N_POZIOMOW (int)(sizeof POZIOMY / sizeof POZIOMY[0])

static uint32_t obrocony[BOK * BOK], polowa[(BOK / 2) * (BOK / 2)], cwierc[(BOK / 4) * (BOK / 4)],
    widok[OBSZAR_W * OBSZAR_H];
static struct sk_obraz kopie[SK_KOPIE] = { { obrocony, 0, 0 }, { polowa, 0, 0 }, { cwierc, 0, 0 } };
static const uint32_t *zrodlo; /* the picture as dekoder gave it (NULL: none) */
static int32_t zw, zh;         /* its size */
static int obrot, pelne;       /* quarter turns to the right, 0..3; every pixel opaque */
static int32_t ow, oh;         /* obrocony: the picture turned */
static double skala = 1;       /* × the fitted size, as shown now */
static double cel = 1, od = 1, t0 = -1; /* the zoom wanted, where the animation started and when (-1: none) */
static double cx, cy;          /* the middle of the view, in pixels of obrocony */
static int32_t ww, wh;         /* the view: widok, ww × wh */
static double vx, vy, vw, vh;  /* the part of obrocony it shows */
static int nieaktualny, szybki; /* widok must be computed again; widok was computed the fast way */
static double ruch = -1;       /* when the view last moved (zoom or drag) */
static struct {
    unsigned long klatki;
    double najdluzsza, od;
} pomiar; /* frames of one zoom or drag, for the log */

/* zrodlo turned `obrot` quarters into obrocony, and its smaller copies. */
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
    kopie[0].w = ow;
    kopie[0].h = oh;
    sk_polowa(kopie[0], polowa, &kopie[1].w, &kopie[1].h);
    sk_polowa(kopie[1], cwierc, &kopie[2].w, &kopie[2].h);
    pelne = sk_nieprzezroczysty(kopie[0]);
    cx = ow / 2.0;
    cy = oh / 2.0;
}

/* Screen pixels per pixel of obrocony when fitted (never above 1). */
static double dopasowanie(void)
{
    double f = fmin((double)OBSZAR_W / ow, (double)OBSZAR_H / oh);
    return f < 1 ? f : 1;
}

static double ogranicz(double v, double polowa_, double calosc)
{
    return polowa_ * 2 >= calosc ? calosc / 2 : v < polowa_ ? polowa_ : v > calosc - polowa_ ? calosc - polowa_ : v;
}

/* The size of the view at `skala` and the part of obrocony around (cx, cy) it shows; the pixels come later. */
static void geometria(void)
{
    double s = dopasowanie() * skala;
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
    vx = cx - vw / 2;
    vy = cy - vh / 2;
    nieaktualny = 1;
}

/* The pixels of the view: fast from a copy while it moves, else exact. */
static void piksele(int szybko)
{
    if (szybko) {
        int k = sk_kopia(dopasowanie() * skala);
        double f = (double)kopie[k].w / ow;
        sk_szybko(kopie[k], vx * f, vy * f, vw * f, vh * f, widok, ww, wh, ww);
    } else if (k1_skaluj(obrocony, (uint32_t)ow, (uint32_t)oh, (uint32_t)ow, (struct k1_wycinek){ vx, vy, vw, vh }, widok,
                         (uint32_t)ww, (uint32_t)wh, (uint32_t)ww) != 0) {
        ww = wh = 0;
    }
    szybki = szybko;
    nieaktualny = 0;
}

static void ruszyl(void)
{
    double t = k1_teraz();
    if (ruch < 0 || t - ruch >= ZOSTAJE) {
        pomiar.klatki = 0;
        pomiar.najdluzsza = 0;
        pomiar.od = t;
    }
    ruch = t;
    k1_apl_odswiez(&apl);
}

void widok_nowy(const uint32_t *px, int32_t w, int32_t h)
{
    zrodlo = px;
    zw = w;
    zh = h;
    obrot = 0;
    skala = cel = 1;
    t0 = ruch = -1;
    if (px != NULL)
        obroc();
    geometria();
    if (px != NULL)
        piksele(0);
}

void widok_obroc(void)
{
    if (zrodlo == NULL)
        return;
    obrot = (obrot + 1) % 4;
    skala = cel = 1; /* a turned picture starts fitted again */
    t0 = ruch = -1;
    obroc();
    geometria();
    piksele(0);
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
    if (krok > 0) /* from the level wanted, not the one shown: quick clicks go on from the last one */
        for (int i = N_POZIOMOW - 1; i >= 0; i--)
            nowy = POZIOMY[i] > cel && dopasowanie() * POZIOMY[i] <= MAKS_PIKSEL ? POZIOMY[i] : nowy;
    else if (krok < 0)
        for (int i = 0; i < N_POZIOMOW; i++)
            nowy = POZIOMY[i] < cel ? POZIOMY[i] : nowy;
    od = skala;
    cel = nowy;
    t0 = k1_teraz();
    ruszyl();
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
    geometria(); /* only the numbers: the pixels once per frame (widok_rysuj) */
    ruszyl();
}

int widok_zegar(void)
{
    double t = k1_teraz(), postep;
    if (t0 >= 0) {
        postep = (t - t0) / ANIMACJA;
        if (postep >= 1) {
            skala = cel;
            t0 = -1;
        } else {
            skala = od + (cel - od) * (1 - pow(1 - postep, 3)); /* fast, then slower: ease-out */
        }
        geometria();
        ruch = t;
        k1_apl_odswiez(&apl);
        if (t0 < 0) /* the tests follow the zoom by this line */
            k1_log(TAG, "powiększenie %d%%: widok %dx%d", (int)(skala * 100 + 0.5), (int)ww, (int)wh);
        return t0 < 0 ? (int)(ZOSTAJE * 1000.0) + 1 : KLATKA;
    }
    if (ruch >= 0 && (szybki || nieaktualny)) { /* moving: wait; standing still: once more, exact */
        if (t - ruch < ZOSTAJE)
            return (int)((ruch + ZOSTAJE - t) * 1000.0) + 1;
        nieaktualny = 1;
        k1_apl_odswiez(&apl);
    }
    return -1;
}

int widok_rysuj(struct k1_obraz *o)
{
    int32_t x = OBSZAR_X + (OBSZAR_W - ww) / 2, y = OBSZAR_Y + (OBSZAR_H - wh) / 2;
    double t0_ = k1_teraz(), t;
    if (zrodlo == NULL || ww == 0)
        return 0;
    if (nieaktualny) {
        int szybko = t0 >= 0 || (ruch >= 0 && t0_ - ruch < ZOSTAJE);
        piksele(szybko);
        if (ww == 0)
            return 0;
        t = k1_teraz() - t0_;
        pomiar.klatki += szybko ? 1u : 0u;
        pomiar.najdluzsza = szybko && t > pomiar.najdluzsza ? t : pomiar.najdluzsza;
        if (!szybko && ruch >= 0 && pomiar.klatki > 0) /* at most once per zoom or drag (≤ 2 lines a second) */
            k1_log(TAG, "ruch: klatek %lu w %.0f ms, najdłuższa %.1f ms, ostra %.1f ms", pomiar.klatki,
                   (t0_ - pomiar.od) * 1000.0, pomiar.najdluzsza * 1000.0, t * 1000.0);
        if (!szybko)
            pomiar.klatki = 0;
    }
    if (pelne) /* opaque: rows copied, no blending */
        for (int32_t r = 0; r < wh; r++)
            memcpy(o->px + (size_t)(y + r) * o->krok + (size_t)x, widok + (size_t)r * (size_t)ww, (size_t)ww * 4);
    else
        k1_naloz_sprite(o, widok, (uint32_t)ww, (uint32_t)wh, x, y);
    return 1;
}

void widok_rozmiar(int32_t *w, int32_t *h)
{
    *w = ww;
    *h = wh;
}

int widok_prostokat(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = OBSZAR_X + (OBSZAR_W - ww) / 2;
    *y = OBSZAR_Y + (OBSZAR_H - wh) / 2;
    *w = ww;
    *h = wh;
    return zrodlo != NULL && ww > 0 && pelne;
}

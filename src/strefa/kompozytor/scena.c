/* K1 kompozytor — the scene (kompozytor.h): the windows of xdg.c, bottom to top, each its surface tree (subsurfaces
 * in their stacking order), drawn into the guest's display. Only the part that changed is drawn again (one bounding
 * box per round); then the zone kernel sends it to the VMM (DIRTYFB → virtio-gpu) and the frame callbacks are done. */
#include <string.h>

#include "kompozytor.h"

#define TLO 0xff2b2a27u /* K1's dark neutral, where no window is */

static int32_t z0, zy0, z1, zy1; /* the part to draw again (z1 <= z0: nothing) */
static int wywolania;             /* frame callbacks wait (a commit without damage still gets its "done") */

void scena_klatka(void)
{
    wywolania = 1;
}

void scena_zmiana(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t x1, y1;
    if (w <= 0 || h <= 0)
        return;
    x1 = x > ekran.szer - w ? ekran.szer : x + w;
    y1 = y > ekran.wys - h ? ekran.wys : y + h;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (x >= x1 || y >= y1)
        return;
    if (z1 <= z0) {
        z0 = x, zy0 = y, z1 = x1, zy1 = y1;
        return;
    }
    if (x < z0) z0 = x;
    if (y < zy0) zy0 = y;
    if (x1 > z1) z1 = x1;
    if (y1 > zy1) zy1 = y1;
}

int scena_pozycja(const struct powierzchnia *p, int32_t *x, int32_t *y)
{
    int32_t sx = 0, sy = 0;
    int glebokosc = 0;
    for (; p != NULL && glebokosc <= DZIECI_MAKS * 4; glebokosc++) {
        if (p->xdg != NULL) {
            if (!p->xdg->zmapowana)
                return 0;
            *x = sx + p->xdg->x - p->xdg->gx;
            *y = sy + p->xdg->y - p->xdg->gy;
            return 1;
        }
        if (p->rola != ROLA_PODPOW || p->rodzic == NULL)
            return 0;
        sx += p->px;
        sy += p->py;
        p = p->rodzic;
    }
    return 0;
}

void scena_zmiana_powierzchni(const struct powierzchnia *p)
{
    int32_t x, y;
    if (powierzchnia_widoczna(p) && scena_pozycja(p, &x, &y))
        scena_zmiana(x, y, p->szer, p->wys);
}

static void prostokat(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t kolor)
{
    int32_t x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            ekran.px[(size_t)y * (size_t)ekran.krok + (size_t)x] = kolor;
}

static uint32_t mieszaj(uint32_t s, uint32_t d)
{
    uint32_t a = s >> 24, r = 0;
    int i;
    if (a == 255)
        return s;
    if (a == 0)
        return d;
    for (i = 0; i < 32; i += 8) { /* premultiplied: s + d × (255 − a) / 255 */
        uint32_t c = ((s >> i) & 255u) + ((d >> i) & 255u) * (255u - a) / 255u;
        r |= (c > 255u ? 255u : c) << i;
    }
    return r | 0xff000000u;
}

static void wklej(const struct powierzchnia *p, int32_t ox, int32_t oy)
{
    int32_t x0 = ox > z0 ? ox : z0, y0 = oy > zy0 ? oy : zy0;
    int32_t x1 = ox + p->szer < z1 ? ox + p->szer : z1, y1 = oy + p->wys < zy1 ? oy + p->wys : zy1, x, y;
    for (y = y0; y < y1; y++) {
        const uint32_t *s = p->obraz + (size_t)(y - oy) * (size_t)p->krok + (size_t)(x0 - ox);
        uint32_t *d = ekran.px + (size_t)y * (size_t)ekran.krok + (size_t)x0;
        if (!p->alfa) {
            for (x = 0; x < x1 - x0; x++)
                d[x] = s[x] | 0xff000000u;
        } else {
            for (x = 0; x < x1 - x0; x++)
                d[x] = mieszaj(s[x], d[x]);
        }
    }
}

static void drzewo(const struct powierzchnia *p, int32_t ox, int32_t oy, int glebokosc)
{
    unsigned i;
    if (glebokosc > DZIECI_MAKS * 4)
        return;
    for (i = 0; i < p->n_stos; i++) {
        const struct powierzchnia *d = p->stos[i];
        if (d == p) {
            if (powierzchnia_widoczna(p))
                wklej(p, ox, oy);
        } else if (powierzchnia_widoczna(d)) {
            drzewo(d, ox + d->px, oy + d->py, glebokosc + 1);
        }
    }
}

int scena_rysuj(void)
{
    struct xdg_pow *x;
    uint64_t t0, t1;
    unsigned i;
    if (z1 <= z0 || ekran.px == NULL) {
        if (!wywolania)
            return 0;
        wywolania = 0;
        powierzchnie_klatka_gotowa(teraz_ms()); /* nothing to draw, but the client waits for its frame */
        return 1;
    }
    wywolania = 0;
    t0 = k1_pomiar_teraz_us();
    prostokat(z0, zy0, z1, zy1, TLO);
    for (i = 0; (x = xdg_okno(i)) != NULL; i++)
        if (x->pow != NULL && powierzchnia_widoczna(x->pow))
            drzewo(x->pow, x->x - x->gx, x->y - x->gy, 0);
    t1 = k1_pomiar_teraz_us();
    ekran_pokaz(z0, zy0, z1 - z0, zy1 - zy0);
    k1_pomiar_dodaj(&pomiar_scena, t1 - t0); /* 0.10.11 step 1: drawing the scene, then sending it (DIRTYFB) */
    k1_pomiar_dodaj(&pomiar_pokaz, k1_pomiar_teraz_us() - t1);
    pomiar_klatki++;
    z0 = zy0 = z1 = zy1 = 0;
    powierzchnie_klatka_gotowa(teraz_ms());
    return 1;
}

static int trafia(const struct powierzchnia *p, int32_t lx, int32_t ly)
{
    if (!powierzchnia_widoczna(p) || lx < 0 || ly < 0 || lx >= p->szer || ly >= p->wys)
        return 0;
    if (p->wej_jest && (p->wej.pusty || lx < p->wej.x0 || ly < p->wej.y0 || lx >= p->wej.x1 || ly >= p->wej.y1))
        return 0;
    return 1;
}

static struct powierzchnia *drzewo_pod(struct powierzchnia *p, int32_t lx, int32_t ly, int32_t *wx, int32_t *wy,
                                       int glebokosc)
{
    unsigned i;
    if (glebokosc > DZIECI_MAKS * 4)
        return NULL;
    for (i = p->n_stos; i-- > 0;) {
        struct powierzchnia *d = p->stos[i], *r;
        if (d == p) {
            if (trafia(p, lx, ly)) {
                *wx = lx, *wy = ly;
                return p;
            }
        } else if (powierzchnia_widoczna(d) &&
                   (r = drzewo_pod(d, lx - d->px, ly - d->py, wx, wy, glebokosc + 1)) != NULL) {
            return r;
        }
    }
    return NULL;
}

struct powierzchnia *scena_pod(int32_t x, int32_t y, int32_t *lx, int32_t *ly)
{
    struct xdg_pow *w;
    unsigned i, n = 0;
    while (xdg_okno(n) != NULL)
        n++;
    for (i = n; i-- > 0;) {
        struct powierzchnia *p;
        w = xdg_okno(i);
        if (w->pow != NULL && (p = drzewo_pod(w->pow, x - (w->x - w->gx), y - (w->y - w->gy), lx, ly, 0)) != NULL)
            return p;
    }
    return NULL;
}

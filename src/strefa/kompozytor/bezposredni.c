/* K1 kompozytor — the browser's frame as the zone's display itself (0.10.11 step 2, ADR 0051; kompozytor.h). When the
 * topmost thing on the display is a buffer of the card that covers all of it — the browser's window, maximized as always
 * in K1, with nothing over it (no menu, no tooltip, no preview of a tab) — the scene is not drawn: that buffer is the
 * display (kms.c: the zone kernel sends SET_SCANOUT_BLOB), the VMM hands it to ekran, no processor copies it anywhere.
 * Anything else, or a buffer the display refuses: the scene as before, into our own buffer. The buffer is opaque on
 * K1's screen (the window of a zone always is): the browser's maximized window has no see-through pixels. */
#include "kompozytor.h"

static int bylo; /* the display showed a buffer of the card the last frame */

/* The surface drawn last (on top) of tree p at ox, oy — the same walk as drzewo() in scena.c. */
static const struct powierzchnia *wierzch(const struct powierzchnia *p, int32_t ox, int32_t oy, int32_t *x, int32_t *y,
                                          int glebokosc)
{
    const struct powierzchnia *w = NULL;
    unsigned i;
    if (glebokosc > DZIECI_MAKS * 4)
        return NULL;
    for (i = 0; i < p->n_stos; i++) {
        const struct powierzchnia *d = p->stos[i], *t;
        int32_t dx, dy;
        if (d == p) {
            if (powierzchnia_widoczna(p))
                w = p, *x = ox, *y = oy;
        } else if (powierzchnia_widoczna(d) && (t = wierzch(d, ox + d->px, oy + d->py, &dx, &dy, glebokosc + 1)) != NULL) {
            w = t, *x = dx, *y = dy;
        }
    }
    return w;
}

int bezposredni_rysuj(void)
{
    const struct powierzchnia *g = NULL;
    struct xdg_pow *x;
    int32_t gx = 0, gy = 0;
    unsigned i;
    for (i = 0; (x = xdg_okno(i)) != NULL; i++) {
        int32_t tx, ty;
        const struct powierzchnia *t;
        if (x->pow != NULL && powierzchnia_widoczna(x->pow) &&
            (t = wierzch(x->pow, x->x - x->gx, x->y - x->gy, &tx, &ty, 0)) != NULL)
            g = t, gx = tx, gy = ty;
    }
    if (g != NULL && gx == 0 && gy == 0 && g->szer == ekran.szer && g->wys == ekran.wys && g->trzymany != NULL &&
        g->trzymany->dane != NULL) {
        struct bufor *b = g->trzymany->dane;
        if (b->dmabuf >= 0 && !b->bez_ekranu && b->pula == g->pula) {
            if (ekran_pokaz_bufor(b->dmabuf, b->szer, b->wys, b->krok, b->przes) == 0) {
                if (!bylo)
                    kloguj("obraz przeglądarki prosto z bufora karty (%dx%d) — bez sceny", b->szer, b->wys);
                bylo = 1;
                return 1;
            }
            b->bez_ekranu = 1;
            kloguj("bufor karty nie do pokazania jako wyświetlacz — rysuję scenę");
        }
    }
    if (bylo) { /* our own buffer again: drawn whole */
        ekran_pokaz_swoj();
        bylo = 0;
        scena_zmiana(0, 0, ekran.szer, ekran.wys);
    }
    return 0;
}

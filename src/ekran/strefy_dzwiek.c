/* K1 ekran — the speaker on a session's bar (0.12.0 step 3; the owner's D6: A). While a session plays (the shell says so:
 * STREFA_DZWIEK, which gui has from K1's sound service — never from the zone), its window's bar has a small speaker right
 * after the session's name; muted — the speaker with a × on the accent. A click on it asks the shell to mute the session
 * or to let it play again (STREFA_WYCISZ); the session's sound is muted by the sound service, not in the zone. The bar
 * is ekran's own (outside the zone's picture), so a page can neither cover the speaker nor fake it (RYZYKA R26). Its
 * place for the tests: the element "Wycisz sesję" of the window in the screen's description. Part of ekran (ekran.h). */
#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_opis.h"
#include "k1_rysuj.h"

#define SZER 30
#define WYS 24

static struct {
    int gra, wyciszona;
} z[K1_STREFY_MAKS + 1];
static struct prost miejsce[MAKS_POW]; /* where each window's speaker was drawn (w = 0: none) */

static int numer(const struct pow *p)
{
    const struct k1_strefa_opis *o = k1_strefa(s.kl[p->kl].apl);
    return o != NULL ? o->nr : 0;
}

int strefa_gra(const struct pow *p)
{
    int nr = numer(p);
    return nr >= 1 && nr <= K1_STREFY_MAKS && z[nr].gra;
}

void strefa_dzwiek(const struct k1_komunikat *m)
{
    if (z[m->a].gra == m->b && z[m->a].wyciszona == m->c)
        return;
    z[m->a].gra = m->b;
    z[m->a].wyciszona = m->c;
    k1_log(TAG, "strefa %d: %s", (int)m->a, !m->b ? "nie gra" : m->c ? "gra, wyciszona" : "gra");
    for (int i = 0; i < MAKS_POW; i++)
        if (okno_strefy(&s.p[i]) && numer(&s.p[i]) == m->a) {
            struct prost f = strefa_obszar(&s.p[i]);
            uszkodz(f.x, f.y, f.w, strefa_nad());
        }
}

/* Into the bar of window p (its scene part `o`, whose corner is r's), at text position tx (the baseline ty), before
 * `kon`: the speaker while its session plays. Returns where the text goes on. */
int32_t strefa_glosnik_rysuj(struct k1_obraz *o, const struct pow *p, const struct prost *r, int32_t tx, int32_t ty,
                             int32_t kon, const struct k1_motyw *m, int fokus)
{
    int i = (int)(p - s.p), nr = numer(p);
    float x = (float)tx + 6, y = (float)ty - 5 - WYS / 2.0f;
    miejsce[i].w = 0;
    if (!strefa_gra(p) || tx + 6 + SZER > kon)
        return tx;
    if (z[nr].wyciszona)
        k1_zaokraglony(o, x, y, SZER, WYS, WYS / 2.0f, k1_kolor(m->akcent, 255));
    else if (s.strefa_okno == i && s.strefa_przycisk == STREFA_GLOSNIK)
        k1_zaokraglony(o, x, y, SZER, WYS, WYS / 2.0f, k1_kolor(m->najechanie, 255));
    k1_ikona(o, z[nr].wyciszona ? K1_IK_GLOSNIK_WYCISZONY : K1_IK_GLOSNIK, x + (SZER - 16) / 2.0f, y + (WYS - 16) / 2.0f,
             16, k1_kolor(z[nr].wyciszona ? m->na_akcencie : fokus ? m->tekst2 : m->tekst3, 255));
    miejsce[i] = (struct prost){ (int32_t)x + r->x, (int32_t)y + r->y, SZER, WYS };
    return tx + 6 + SZER + 6;
}

/* 1: (x, y) is on the speaker of window i. */
int strefa_glosnik_pod(int i, int32_t x, int32_t y)
{
    const struct prost *g = &miejsce[i];
    return g->w > 0 && strefa_gra(&s.p[i]) && x >= g->x && x < g->x + g->w && y >= g->y && y < g->y + g->h;
}

/* A press on it: the shell mutes the session or lets it play (it has the channel to the sound service). */
void strefa_glosnik_klik(int i)
{
    int sh = powloka(), nr = numer(&s.p[i]);
    if (sh < 0 || nr < 1)
        return;
    k1_log(TAG, "strefa %d: głośnik na pasku — %s", nr, z[nr].wyciszona ? "znów gra" : "wycisz");
    wyslij(sh, k1_komunikat_nowy(K1_KOM_STREFA_WYCISZ, 0, nr, 0, 0, 0, 0));
}

/* The speaker of window i for the screen's description (0 or 1 records). */
int strefa_glosnik_opis(struct k1_opis_el *el, int n, int i)
{
    const struct prost *g = &miejsce[i];
    if (n < 1 || g->w == 0 || !strefa_gra(&s.p[i]))
        return 0;
    return opis_rekord(el, K1_EL_PRZYCISK, z[numer(&s.p[i])].wyciszona ? K1_EL_WYBRANY : 0, s.p[i].okno, g->x, g->y, g->w,
                       g->h, "Wycisz sesję");
}

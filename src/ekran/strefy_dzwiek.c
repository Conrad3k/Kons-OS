/* K1 ekran — the speaker on a session's bar (0.12.0 step 3; the owner's D6: A). While a session plays (the shell says so:
 * STREFA_DZWIEK, which gui has from K1's sound service — never from the zone), its window's bar has a small speaker right
 * after the session's name; muted — the speaker with a × on the accent. A click on it asks the shell to mute the session
 * or to let it play again (STREFA_WYCISZ); the session's sound is muted by the sound service, not in the zone. The bar
 * is ekran's own (outside the zone's picture), so a page can neither cover the speaker nor fake it (RYZYKA R26). Its
 * place for the tests: the element "Wycisz sesję" of the window in the screen's description. 0.13.5 step 2 (ADR 0065,
 * D2: A): while the session hears the microphone (STREFA_DZWIEK d — the shell has it from K1's sound service, never from
 * the zone), after the speaker the sign "mikrofon włączony" — a terracotta pill; its microphone muted: the window's own
 * colour, crossed out. A click on it mutes / unmutes that session's microphone (STREFA_WYCISZ b = 1); the element
 * "Wycisz mikrofon sesji". Part of ekran (ekran.h). */
#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_opis.h"
#include "k1_rysuj.h"

#define SZER 30
#define WYS 24

static struct {
    int gra, wyciszona;
    int slucha, mik_wyciszony; /* 0.13.5 step 2: it hears the microphone; its microphone is muted */
} z[K1_STREFY_MAKS + 1];
static struct prost miejsce[MAKS_POW], mik[MAKS_POW]; /* where each window's speaker and microphone were drawn (w = 0) */

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
    if (z[m->a].gra == m->b && z[m->a].wyciszona == m->c && z[m->a].slucha == m->d && z[m->a].mik_wyciszony == m->e)
        return;
    if (z[m->a].gra != m->b || z[m->a].wyciszona != m->c)
        k1_log(TAG, "strefa %d: %s", (int)m->a, !m->b ? "nie gra" : m->c ? "gra, wyciszona" : "gra");
    if (z[m->a].slucha != m->d || z[m->a].mik_wyciszony != m->e) /* 0.13.5 step 2 */
        k1_log(TAG, "strefa %d: mikrofon %s", (int)m->a, !m->d ? "nie słucha" : m->e ? "słucha, wyciszony" : "słucha");
    z[m->a].gra = m->b;
    z[m->a].wyciszona = m->c;
    z[m->a].slucha = m->d;
    z[m->a].mik_wyciszony = m->e;
    for (int i = 0; i < MAKS_POW; i++)
        if (okno_strefy(&s.p[i]) && numer(&s.p[i]) == m->a) {
            struct prost f = strefa_obszar(&s.p[i]);
            uszkodz(f.x, f.y, f.w, strefa_nad());
        }
}

/* 0.13.5 step 2 (D2: A): the sign of the microphone at tx — a terracotta pill (muted: crossed out, the window's colour). */
static int32_t mikrofon_rysuj(struct k1_obraz *o, int i, int nr, const struct prost *r, int32_t tx, int32_t ty,
                              int32_t kon, const struct k1_motyw *m)
{
    float x = (float)tx + 6, y = (float)ty - 5 - WYS / 2.0f;
    int wyc = z[nr].mik_wyciszony, nad = s.strefa_okno == i && s.strefa_przycisk == STREFA_MIKROFON;
    mik[i].w = 0;
    if (!z[nr].slucha || tx + 6 + SZER > kon)
        return tx;
    k1_zaokraglony(o, x, y, SZER, WYS, WYS / 2.0f,
                   wyc ? k1_kolor(nad ? m->najechanie : m->pole, 255) : k1_kolor(nad ? K1_TERAKOTA_CIEMNA : K1_TERAKOTA, 255));
    k1_ikona(o, wyc ? K1_IK_MIKROFON_WYCISZONY : K1_IK_MIKROFON, x + (SZER - 16) / 2.0f, y + (WYS - 16) / 2.0f, 16,
             wyc ? k1_kolor(m->tekst2, 255) : k1_kolor(0xffffff, 255));
    mik[i] = (struct prost){ (int32_t)x + r->x, (int32_t)y + r->y, SZER, WYS };
    return tx + 6 + SZER + 6;
}

/* Into the bar of window p (its scene part `o`, whose corner is r's), at text position tx (the baseline ty), before
 * `kon`: the speaker while its session plays, the sign of the microphone while it hears (0.13.5). Returns where the text
 * goes on. */
int32_t strefa_glosnik_rysuj(struct k1_obraz *o, const struct pow *p, const struct prost *r, int32_t tx, int32_t ty,
                             int32_t kon, const struct k1_motyw *m, int fokus)
{
    int i = (int)(p - s.p), nr = numer(p);
    float x = (float)tx + 6, y = (float)ty - 5 - WYS / 2.0f;
    miejsce[i].w = 0;
    mik[i].w = 0;
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return tx;
    if (!strefa_gra(p) || tx + 6 + SZER > kon)
        return mikrofon_rysuj(o, i, nr, r, tx, ty, kon, m);
    if (z[nr].wyciszona)
        k1_zaokraglony(o, x, y, SZER, WYS, WYS / 2.0f, k1_kolor(m->akcent, 255));
    else if (s.strefa_okno == i && s.strefa_przycisk == STREFA_GLOSNIK)
        k1_zaokraglony(o, x, y, SZER, WYS, WYS / 2.0f, k1_kolor(m->najechanie, 255));
    k1_ikona(o, z[nr].wyciszona ? K1_IK_GLOSNIK_WYCISZONY : K1_IK_GLOSNIK, x + (SZER - 16) / 2.0f, y + (WYS - 16) / 2.0f,
             16, k1_kolor(z[nr].wyciszona ? m->na_akcencie : fokus ? m->tekst2 : m->tekst3, 255));
    miejsce[i] = (struct prost){ (int32_t)x + r->x, (int32_t)y + r->y, SZER, WYS };
    return mikrofon_rysuj(o, i, nr, r, tx + 6 + SZER + 6, ty, kon, m);
}

static int na(const struct prost *g, int32_t x, int32_t y)
{
    return g->w > 0 && x >= g->x && x < g->x + g->w && y >= g->y && y < g->y + g->h;
}

int strefa_glosnik_pod(int i, int32_t x, int32_t y)
{
    int nr = numer(&s.p[i]);
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return 0;
    if (strefa_gra(&s.p[i]) && na(&miejsce[i], x, y))
        return STREFA_GLOSNIK;
    return z[nr].slucha && na(&mik[i], x, y) ? STREFA_MIKROFON : 0;
}

/* A press on one of them: the shell mutes the session (or its microphone) or lets it play (it has the channel to the
 * sound service). */
void strefa_glosnik_klik(int i, int co)
{
    int sh = powloka(), nr = numer(&s.p[i]), m = co == STREFA_MIKROFON;
    if (sh < 0 || nr < 1)
        return;
    if (m)
        k1_log(TAG, "strefa %d: mikrofon na pasku — %s", nr, z[nr].mik_wyciszony ? "znów słychać" : "wycisz");
    else
        k1_log(TAG, "strefa %d: głośnik na pasku — %s", nr, z[nr].wyciszona ? "znów gra" : "wycisz");
    wyslij(sh, k1_komunikat_nowy(K1_KOM_STREFA_WYCISZ, 0, nr, m, 0, 0, 0));
}

/* The speaker and the microphone of window i for the screen's description (0..2 records). */
int strefa_glosnik_opis(struct k1_opis_el *el, int n, int i)
{
    const struct prost *g = &miejsce[i], *k = &mik[i];
    int nr = numer(&s.p[i]), w = 0;
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return 0;
    if (n > w && g->w > 0 && strefa_gra(&s.p[i]))
        w += opis_rekord(&el[w], K1_EL_PRZYCISK, z[nr].wyciszona ? K1_EL_WYBRANY : 0, s.p[i].okno, g->x, g->y, g->w, g->h,
                         "Wycisz sesję");
    if (n > w && k->w > 0 && z[nr].slucha)
        w += opis_rekord(&el[w], K1_EL_PRZYCISK, z[nr].mik_wyciszony ? K1_EL_WYBRANY : 0, s.p[i].okno, k->x, k->y, k->w,
                         k->h, "Wycisz mikrofon sesji");
    return w;
}

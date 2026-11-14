/* K1 ekran — the trusted frame of a zone's window (0.10.0, ADR 0042 T6; ADR 0020 p. 3; the owner's D1: sketch B2):
 * a 3 px outline in the zone's colour around the window, OUTSIDE the zone's picture (drawn before it: the zone can
 * neither cover nor draw it — it only ever has its own rectangle), and a small mark with the zone's icon on the
 * top-left corner, drawn over everything of its window. With the pointer on the mark it opens into a badge: the zone's
 * name and "osobna maszyna" (one-off: "zapomina po zamknięciu"), minimise and close. Grabbing the mark moves the window.
 * The mark, the badge and the outline are K1's: the pointer there never reaches the zone. One-off zone: the mark light
 * with an outline instead of filled. Part of ekran (ekran.h). */
#include <stdio.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

#define ZN_BOK 30   /* the mark */
#define ZN_OD 8     /* how far it sticks out of the window's top-left corner */
#define PRZYCISK 28 /* minimise / close in the badge */
#define TOLERANCJA 40

int okno_strefy(const struct pow *p)
{
    return p->uzyta && p->rodzaj == K1_POW_OKNO && s.kl[p->kl].rola == K1_ROLA_STREFA;
}

static const struct k1_strefa_opis *opis(const struct pow *p)
{
    return k1_strefa(s.kl[p->kl].apl);
}

static void tekst(const struct k1_strefa_opis *z, char *b, size_t n)
{
    snprintf(b, n, "%s%s%s · osobna maszyna%s", z->tytul, z->sesja[0] ? " · " : "", z->sesja,
             z->trwalosc == K1_JEDNORAZOWA ? " · zapomina po zamknięciu" : "");
}

static int rozwiniety(const struct pow *p)
{
    return (int)(p - s.p) == s.plakietka;
}

/* The mark, or the open badge with its buttons (screen coordinates); *tw = the text's width. */
static struct prost obszar(const struct pow *p, int32_t *tw)
{
    struct prost r = { p->x - ZN_OD, p->y - ZN_OD, ZN_BOK, ZN_BOK };
    char t[160];
    *tw = 0;
    if (rozwiniety(p)) {
        tekst(opis(p), t, sizeof t);
        *tw = k1_tekst_szer(K1_CZ_MALA_MOCNA, t);
        r.w = ZN_BOK - 2 + *tw + 12 + 1 + 8 + PRZYCISK + 2 + PRZYCISK + 4;
    }
    return r;
}

struct prost strefa_obszar(const struct pow *p)
{
    int32_t tw;
    struct prost r = obszar(p, &tw);
    r.x -= 4; /* its shadow */
    r.w += 8;
    r.h += 8;
    return r;
}

static uint32_t mieszaj(uint32_t a, uint32_t b, unsigned t) /* a·(255−t) + b·t, 0xRRGGBB */
{
    uint32_t w = 0;
    int k;
    for (k = 0; k < 24; k += 8)
        w |= ((((a >> k) & 255) * (255 - t) + ((b >> k) & 255) * t) / 255) << k;
    return w;
}

static uint32_t kolor(const struct pow *p)
{
    const struct k1_motyw *m = k1_motyw(s.ust[K1_UST_MOTYW]);
    return (int)(p - s.p) == s.fokus ? opis(p)->kolor : mieszaj(opis(p)->kolor, m->tlo, 115);
}

/* The part of the scene inside r as an image whose (0, 0) is r's corner: drawing there is clipped to r. */
static struct k1_obraz wycinek(const struct prost *r)
{
    struct k1_obraz o = { s.scena.px + (size_t)r->y * s.scena.krok + (size_t)r->x, (uint32_t)r->w, (uint32_t)r->h,
                          s.scena.krok };
    return o;
}

void strefa_ramka(const struct pow *p, const struct prost *r)
{
    struct k1_obraz o = wycinek(r);
    k1_zaokraglony(&o, (float)(p->x - 3 - r->x), (float)(p->y - 3 - r->y), (float)p->w + 6, (float)p->h + 6, 6,
                   k1_kolor(kolor(p), 255));
}

void strefa_znaczek(const struct pow *p, const struct prost *r)
{
    const struct k1_motyw *m = k1_motyw(s.ust[K1_UST_MOTYW]);
    const struct k1_strefa_opis *z = opis(p);
    struct k1_obraz o = wycinek(r);
    int32_t tw;
    struct prost a = obszar(p, &tw);
    float x = (float)(a.x - r->x), y = (float)(a.y - r->y);
    int jednorazowa = z->trwalosc == K1_JEDNORAZOWA;
    uint32_t k = kolor(p), tlo = jednorazowa ? mieszaj(m->pole, k, 36) : k, ink = jednorazowa ? mieszaj(k, m->tekst, 64) : 0xffffff;
    k1_cien(&o, x, y, (float)a.w, (float)a.h, 10, 2, 6, 60);
    k1_zaokraglony(&o, x, y, (float)a.w, (float)a.h, 10, k1_kolor(tlo, 255));
    if (jednorazowa)
        k1_obrys(&o, x, y, (float)a.w, (float)a.h, 10, 2, k1_kolor(k, 255));
    k1_ikona(&o, (enum k1_ikona)z->ikona, x + 6, y + 6, 18, k1_kolor(ink, 255));
    if (rozwiniety(p)) {
        char t[160];
        float bx = x + ZN_BOK - 2 + (float)tw + 12;
        tekst(z, t, sizeof t);
        k1_tekst(&o, K1_CZ_MALA_MOCNA, (int32_t)x + ZN_BOK - 2, (int32_t)y + 20, t, k1_kolor(ink, 255), 0);
        k1_prostokat(&o, (int32_t)bx, (int32_t)y + 7, 1, 16, k1_kolor(ink, 90));
        k1_ikona(&o, K1_IK_MINIMALIZUJ, bx + 9 + 4, y + 5, 20, k1_kolor(ink, 255));
        k1_ikona(&o, K1_IK_ZAMKNIJ, bx + 9 + PRZYCISK + 2 + 4, y + 5, 20, k1_kolor(ink, 255));
    }
}

int strefa_trafienie(const struct pow *p, int32_t x, int32_t y)
{
    int32_t tw;
    struct prost a = obszar(p, &tw);
    if (x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h) {
        int32_t bx = a.x + ZN_BOK - 2 + tw + 12 + 9;
        if (rozwiniety(p) && x >= bx && x < bx + PRZYCISK)
            return STREFA_MINIMALIZUJ;
        if (rozwiniety(p) && x >= bx + PRZYCISK + 2 && x < bx + 2 * PRZYCISK + 2)
            return STREFA_ZAMKNIJ;
        return STREFA_ZNACZEK;
    }
    if (x >= p->x - 3 && x < p->x + p->w + 3 && y >= p->y - 3 && y < p->y + p->h + 3 &&
        !(x >= p->x && x < p->x + p->w && y >= p->y && y < p->y + p->h))
        return STREFA_RAMKA;
    return 0;
}

/* The open badge stays open while the pointer is near it (TOLERANCJA px around): a hand, or a mouse that overshoots,
 * does not close it on the way to its buttons. */
static int przy_plakietce(int i)
{
    int32_t tw;
    struct prost a;
    if (i < 0 || !s.p[i].uzyta)
        return 0;
    a = obszar(&s.p[i], &tw);
    return s.x >= a.x - TOLERANCJA && s.x < a.x + a.w + TOLERANCJA && s.y >= a.y - TOLERANCJA &&
           s.y < a.y + a.h + TOLERANCJA;
}

/* The pointer is over the mark or the badge of window i (-1: of none): the badge opens or closes. */
void strefa_najechanie(int i)
{
    int stary = s.plakietka;
    if (i < 0 && przy_plakietce(stary))
        return;
    if (i == stary)
        return;
    if (stary >= 0 && s.p[stary].uzyta) {
        struct prost r = strefa_obszar(&s.p[stary]);
        uszkodz(r.x, r.y, r.w, r.h);
    }
    s.plakietka = i;
    if (i >= 0) {
        int32_t tw;
        struct prost r = strefa_obszar(&s.p[i]), a = obszar(&s.p[i], &tw);
        int32_t bx = a.x + ZN_BOK - 2 + tw + 12 + 9;
        uszkodz(r.x, r.y, r.w, r.h);
        k1_log(TAG, "strefa: plakietka okna %u (minimalizuj %d,%d, zamknij %d,%d)", s.p[i].okno,
               (int)(bx + PRZYCISK / 2), (int)(a.y + ZN_BOK / 2), (int)(bx + PRZYCISK + 2 + PRZYCISK / 2),
               (int)(a.y + ZN_BOK / 2));
    }
}

/* A press on the frame of window i (STREFA_*): K1's own buttons and moving the window. */
void strefa_wcisniecie(int i, int co)
{
    struct pow *p = &s.p[i];
    if (co == STREFA_MINIMALIZUJ) {
        minimalizuj(i);
    } else if (co == STREFA_ZAMKNIJ) {
        zamknij_okno(i);
    } else if (co == STREFA_ZNACZEK) {
        s.ciagniete = i;
        s.ciag_dx = s.x - p->x;
        s.ciag_dy = s.y - p->y;
        s.ciag_klatki = s.klatki;
        s.ciag_cpu = czas_procesora();
    }
}

int32_t strefa_znaczek_od(void)
{
    return ZN_OD;
}

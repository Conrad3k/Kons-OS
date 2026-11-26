/* K1 ekran — the trusted frame of a zone's window (0.10.0, ADR 0042 T6; ADR 0020 p. 3). A full title bar like every
 * K1 window — the zone's icon, "<title> · ● <session> · osobna maszyna" (one-off: "· zapomina"), minimise, maximise
 * and close — K1_STREFA_TYTUL px above the zone's picture, and a 3 px outline around both. 0.10.7 step 4 (ADR 0048,
 * D1: sketch A): the frame in the window's background with a hairline, the session's colour only in the dot and a
 * 2 px line under the bar (until 0.10.7 the whole frame was in it). Grabbing the bar moves the window. Everything here
 * lies OUTSIDE the zone's rectangle and is drawn before it: the zone only ever has its own rectangle, so it can neither
 * cover nor draw the frame (RYZYKA R26), and the pointer on the frame never reaches the zone. Without focus the
 * colour is mixed with the theme's background and the title is muted, as in K1 windows. 0.10.9 step 6 (D7, the owner's
 * choice: a chip): a zone that runs with its graphics broker (STREFY_GRAFIKA b from nadzorca — weaker isolation, so it
 * shows: ADR 0020 p. 3) has a small chip right after its session's name. 0.10.5 step 4 (D8): maximise
 * (a square) / restore (two squares); a maximized window has no outline (its bar right under the top bar, its picture
 * to the bottom bar); the grips of the edges: rozmiar.c. Part of ekran (ekran.h). */
#include <stdio.h>
#include <string.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

#define OBWODKA 3   /* the outline */
#define PROMIEN 10  /* the frame's corners: the picture's corners (3 px in) stay inside them */
#define PRZYCISK 28 /* minimise / maximise / close */
#define ODSTEP 32   /* from one button to the next */
#define MARGINES 8  /* the close button from the right edge */

int okno_strefy(const struct pow *p)
{
    return p->uzyta && p->rodzaj == K1_POW_OKNO && s.kl[p->kl].rola == K1_ROLA_STREFA;
}

static const struct k1_strefa_opis *opis(const struct pow *p)
{
    return k1_strefa(s.kl[p->kl].apl);
}

/* The sessions as the user set them (nadzorca, 0.10.0 step 5): the names, which zones remember. Until nadzorca says,
 * the names of the rows. */
static char nazwy[K1_STREFY_MAKS + 1][K1_STREFA_NAZWA_MAKS + 1];
static int32_t pamietaja, z_karta;

void strefy_sesje(const struct k1_komunikat *k)
{
    if (k->typ == K1_KOM_STREFY_STAN) {
        pamietaja = k->a;
        return;
    }
    if (k->typ == K1_KOM_STREFY_GRAFIKA) { /* 0.10.9 step 6: which zones run with their graphics broker */
        if (k->b != z_karta)
            uszkodz(0, 0, s.W, s.H);
        z_karta = k->b;
        return;
    }
    const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e }; /* checked: k1_strefa_nazwa_dobra */
    memcpy(nazwy[k->obiekt], p, K1_STREFA_NAZWA_MAKS);
    nazwy[k->obiekt][K1_STREFA_NAZWA_MAKS] = '\0';
}

int strefa_pamieta(int nr)
{
    return nr >= 1 && nr <= K1_STREFY_MAKS && (pamietaja >> (nr - 1) & 1);
}

/* The title in three parts around the session's dot and the mark of fast graphics: "<title> · ", "<session>" and
 * " · osobna maszyna" (one-off: "· zapomina"); without a session the dot stands before "osobna maszyna". */
const char *strefa_sesja(const struct pow *p)
{
    const struct k1_strefa_opis *z = opis(p);
    return nazwy[z->nr][0] ? nazwy[z->nr] : z->sesja;
}

static void tekst(const struct pow *p, char *przed, size_t n1, char *sesja_t, size_t n2, char *po, size_t n3)
{
    const struct k1_strefa_opis *z = opis(p);
    const char *sesja = strefa_sesja(p);
    snprintf(przed, n1, "%s · ", z->tytul);
    snprintf(sesja_t, n2, "%s", sesja);
    int z_ = schowek_notka(p); /* 0.11.0 step 5 (D5: A): text pasted from another session */
    if (z_ != 0)
        snprintf(po, n3, " · Wklejono z: %s", nazwy[z_][0] ? nazwy[z_] : k1_strefa(z_)->sesja);
    else
        snprintf(po, n3, "%sosobna maszyna%s", sesja[0] ? " · " : "", s.kl[p->kl].pamieta ? "" : " · zapomina");
}

static int z_karta_strefa(const struct pow *p)
{
    int nr = opis(p)->nr;
    return nr >= 1 && nr <= K1_STREFY_MAKS && (z_karta >> (nr - 1) & 1);
}

static int32_t obwodka(const struct pow *p)
{
    return p->maks ? 0 : OBWODKA;
}

struct prost strefa_obszar(const struct pow *p)
{
    int32_t o = obwodka(p);
    if (pelny_jest(p)) /* 0.11.0 step 1: full screen — the picture only, no K1 frame */
        return (struct prost){ p->x, p->y, p->w, p->h };
    struct prost r = { p->x - o, p->y - K1_STREFA_TYTUL - o, p->w + 2 * o, p->h + K1_STREFA_TYTUL + 2 * o };
    return r;
}

int32_t strefa_nad(void)
{
    return K1_STREFA_TYTUL + OBWODKA;
}

/* The button `co` (STREFA_MINIMALIZUJ / _MAKSYMALIZUJ / _ZAMKNIJ) of window p, in screen coordinates. */
static struct prost przycisk(const struct pow *p, int co)
{
    struct prost r = { p->x + p->w + obwodka(p) - MARGINES - PRZYCISK,
                       p->y - K1_STREFA_TYTUL + (K1_STREFA_TYTUL - PRZYCISK) / 2, PRZYCISK, PRZYCISK };
    r.x -= co == STREFA_MINIMALIZUJ ? 2 * ODSTEP : co == STREFA_MAKSYMALIZUJ ? ODSTEP : 0;
    return r;
}

static int w(const struct prost *r, int32_t x, int32_t y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

static uint32_t mieszaj(uint32_t a, uint32_t b, unsigned t) /* a·(255−t) + b·t, 0xRRGGBB */
{
    uint32_t wy = 0;
    int k;
    for (k = 0; k < 24; k += 8)
        wy |= ((((a >> k) & 255) * (255 - t) + ((b >> k) & 255) * t) / 255) << k;
    return wy;
}

/* The session's colour (the dot, the line under the bar); without focus mixed with the theme's background. */
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

static void rysuj_przycisk(struct k1_obraz *o, const struct pow *p, const struct prost *r, int co,
                           const struct k1_motyw *m)
{
    struct prost b = przycisk(p, co);
    float x = (float)(b.x - r->x), y = (float)(b.y - r->y);
    enum k1_ikona ik = co == STREFA_ZAMKNIJ ? K1_IK_ZAMKNIJ : co == STREFA_MINIMALIZUJ ? K1_IK_MINIMALIZUJ
                     : p->maks ? K1_IK_PRZYWROC : K1_IK_MAKSYMALIZUJ;
    if ((int)(p - s.p) == s.strefa_okno && s.strefa_przycisk == co)
        k1_zaokraglony(o, x, y, PRZYCISK, PRZYCISK, 8, k1_kolor(m->najechanie, 255));
    k1_ikona(o, ik, x + 6, y + 6, 16, k1_kolor(m->tekst2, 255));
}

/* 0.10.7 step 4 (ADR 0048, the owner's D1: sketch A): the frame of a K1 window — the theme's background, a hairline,
 * the title in the colours of text — and the session's colour in one place: a 10 px dot before the session's name
 * and a 2 px line under the bar, between the bar and the zone's picture. The outline keeps its 3 px, in the window's
 * background (the edges' grips and the sizes the VMM counts on stay as they were). */
void strefa_ramka(const struct pow *p, const struct prost *r)
{
    const struct k1_motyw *m = k1_motyw(s.ust[K1_UST_MOTYW]);
    const struct k1_strefa_opis *z = opis(p);
    struct k1_obraz o = wycinek(r);
    struct prost f = strefa_obszar(p);
    int fokus = (int)(p - s.p) == s.fokus;
    uint32_t k = kolor(p), napis = k1_kolor(fokus ? m->tekst : m->tekst3, 255);
    float x = (float)(f.x - r->x), y = (float)(f.y - r->y), promien = p->maks ? 0 : PROMIEN;
    char przed[64], sesja[K1_STREFA_NAZWA_MAKS + 1], po[K1_STREFA_NAZWA_MAKS + 32];
    int32_t ob = obwodka(p), tx, ty, szer = p->w - 36 - 3 * ODSTEP - MARGINES - 8, kon;
    if (pelny_jest(p)) /* 0.11.0 step 1: full screen — K1's pill and bar instead (pelny.c) */
        return;
    k1_zaokraglony(&o, x, y, (float)f.w, (float)f.h, promien, k1_kolor(m->tlo, 255));
    if (!p->maks)
        k1_obrys(&o, x, y, (float)f.w, (float)f.h, promien, 1.0f, k1_kolor(m->tekst, 26));
    k1_prostokat(&o, (int32_t)x + ob, (int32_t)y + ob + K1_STREFA_TYTUL - 2, p->w, 2, k1_kolor(k, 255)); /* the line */
    k1_ikona(&o, (enum k1_ikona)z->ikona, x + (float)ob + 10, y + (float)ob + (K1_STREFA_TYTUL - 18) / 2.0f, 18,
             k1_kolor(fokus ? m->tekst2 : m->tekst3, 255));
    tekst(p, przed, sizeof przed, sesja, sizeof sesja, po, sizeof po);
    tx = (int32_t)x + ob + 36;
    ty = (int32_t)y + ob + K1_STREFA_TYTUL / 2 + 5;
    kon = (int32_t)x + ob + 36 + szer;
    tx += k1_tekst(&o, K1_CZ_TEKST_MOCNA, tx, ty, przed, napis, szer);
    if (tx + 16 < kon) { /* the dot, the session's name, the mark of fast graphics (D7), the rest */
        k1_kolo(&o, (float)tx + 5, (float)ty - 5, 5, k1_kolor(k, 255));
        tx += 16;
        tx += k1_tekst(&o, K1_CZ_TEKST_MOCNA, tx, ty, sesja, napis, kon - tx);
        if (z_karta_strefa(p) && tx + 22 < kon) {
            k1_ikona(&o, K1_IK_KARTA, (float)tx + 5, (float)ty - 13, 16, k1_kolor(fokus ? m->tekst2 : m->tekst3, 255));
            tx += 22;
        }
        tx = strefa_glosnik_rysuj(&o, p, r, tx, ty, kon, m, fokus); /* 0.12.0 step 3 (D6: A): the session plays */
        if (tx < kon)
            k1_tekst(&o, K1_CZ_TEKST_MOCNA, tx, ty, po, napis, kon - tx);
    }
    rysuj_przycisk(&o, p, r, STREFA_MINIMALIZUJ, m);
    rysuj_przycisk(&o, p, r, STREFA_MAKSYMALIZUJ, m);
    rysuj_przycisk(&o, p, r, STREFA_ZAMKNIJ, m);
}

int strefa_trafienie(const struct pow *p, int32_t x, int32_t y)
{
    static const int przyciski[] = { STREFA_ZAMKNIJ, STREFA_MAKSYMALIZUJ, STREFA_MINIMALIZUJ };
    struct prost f = strefa_obszar(p), tresc = { p->x, p->y, p->w, p->h }, b;
    int kraw = pelny_jest(p) ? 0 : rozmiar_brzeg(p, x, y); /* 0.10.5 step 4: a grip — also 3 px outside the frame */
    if (pelny_jest(p))
        return 0;
    if (kraw != 0)
        return STREFA_BRZEG | kraw;
    if (!w(&f, x, y) || w(&tresc, x, y))
        return 0;
    if (strefa_glosnik_pod((int)(p - s.p), x, y))
        return STREFA_GLOSNIK;
    for (size_t i = 0; i < sizeof przyciski / sizeof przyciski[0]; i++) {
        b = przycisk(p, przyciski[i]);
        if (w(&b, x, y))
            return przyciski[i];
    }
    return y < p->y ? STREFA_TYTUL : STREFA_RAMKA;
}

/* The title bar of window i must be drawn again (a button lit or dimmed). */
static void uszkodz_pasek(int i)
{
    if (i >= 0 && s.p[i].uzyta) {
        struct prost f = strefa_obszar(&s.p[i]);
        uszkodz(f.x, f.y, f.w, strefa_nad());
    }
}

/* The pointer is over part `co` (STREFA_*, 0: none) of the frame of window i (-1: of none): a button lights up. The
 * first time the pointer comes to a window's bar, its buttons' places go to the log (the tests click by it). */
void strefa_najechanie(int i, int co)
{
    int przyc = co == STREFA_MINIMALIZUJ || co == STREFA_ZAMKNIJ || co == STREFA_MAKSYMALIZUJ || co == STREFA_GLOSNIK
                    ? co : 0,
        stary = s.strefa_okno;
    s.ksztalt = (co & STREFA_BRZEG) ? rozmiar_ksztalt(co & ~STREFA_BRZEG) : K1_KURSOR_STRZALKA;
    if (i == stary && przyc == s.strefa_przycisk)
        return;
    if (i >= 0 && i != stary) {
        struct prost mi = przycisk(&s.p[i], STREFA_MINIMALIZUJ), ma = przycisk(&s.p[i], STREFA_MAKSYMALIZUJ),
                     za = przycisk(&s.p[i], STREFA_ZAMKNIJ);
        k1_log(TAG, "strefa: pasek okna %u (minimalizuj %d,%d, maksymalizuj %d,%d, zamknij %d,%d)", s.p[i].okno,
               (int)(mi.x + PRZYCISK / 2), (int)(mi.y + PRZYCISK / 2), (int)(ma.x + PRZYCISK / 2),
               (int)(ma.y + PRZYCISK / 2), (int)(za.x + PRZYCISK / 2), (int)(za.y + PRZYCISK / 2));
    }
    s.strefa_okno = i;
    s.strefa_przycisk = przyc;
    uszkodz_pasek(stary);
    uszkodz_pasek(i);
}

/* A press on the frame of window i (STREFA_*): K1's own buttons and moving the window. */
void strefa_wcisniecie(int i, int co)
{
    struct pow *p = &s.p[i];
    if (co & STREFA_BRZEG) {
        rozmiar_start(i, co & ~STREFA_BRZEG); /* 0.10.5 step 4: rozmiar.c */
    } else if (co == STREFA_MINIMALIZUJ) {
        minimalizuj(i);
    } else if (co == STREFA_ZAMKNIJ) {
        zamknij_okno(i);
    } else if (co == STREFA_MAKSYMALIZUJ) {
        rozmiar_przelacz(i);
    } else if (co == STREFA_GLOSNIK) {
        strefa_glosnik_klik(i); /* 0.12.0 step 3 */
    } else if (co == STREFA_TYTUL && !rozmiar_tytul(i) && !p->maks) { /* a maximized window does not move */
        s.ciagniete = i;
        s.ciag_dx = s.x - p->x;
        s.ciag_dy = s.y - p->y;
        s.ciag_klatki = s.klatki;
        s.ciag_cpu = czas_procesora();
    }
}

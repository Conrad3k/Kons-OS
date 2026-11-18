/* K1 ekran — the trusted frame of a zone's window (0.10.0, ADR 0042 T6; ADR 0020 p. 3). Step 3, the owner's D1
 * change (sketch A, docs/WYGLAD.md): a full title bar like every K1 window, in the zone's colour — the zone's icon,
 * "<title> · <session> · osobna maszyna" (one-off: "· zapomina po zamknięciu"), minimise and close — K1_STREFA_TYTUL
 * px above the zone's picture, and a 3 px outline around both. Grabbing the bar moves the window. Everything here lies
 * OUTSIDE the zone's rectangle and is drawn before it: the zone only ever has its own rectangle, so it can neither
 * cover nor draw the frame (RYZYKA R26), and the pointer on the frame never reaches the zone. One-off zone: a light bar
 * with the zone's colour as the outline and the text. Without focus the colour is mixed with the theme's background.
 * 0.10.5 step 4 (D8): a third button between them — maximize (a square) / restore (two squares); a maximized window has
 * no outline (its bar right under the top bar, its picture to the bottom bar); the grips of the edges — rozmiar.c.
 * Part of ekran (ekran.h). */
#include <stdio.h>
#include <string.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

#define OBWODKA 3   /* the outline */
#define PROMIEN 10  /* the frame's corners */
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
static int32_t pamietaja;

void strefy_sesje(const struct k1_komunikat *k)
{
    if (k->typ == K1_KOM_STREFY_STAN) {
        pamietaja = k->a;
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

static void tekst(const struct pow *p, char *b, size_t n)
{
    const struct k1_strefa_opis *z = opis(p);
    const char *sesja = nazwy[z->nr][0] ? nazwy[z->nr] : z->sesja;
    snprintf(b, n, "%s%s%s · osobna maszyna%s", z->tytul, sesja[0] ? " · " : "", sesja,
             s.kl[p->kl].pamieta ? "" : " · zapomina po zamknięciu");
}

static int32_t obwodka(const struct pow *p)
{
    return p->maks ? 0 : OBWODKA;
}

struct prost strefa_obszar(const struct pow *p)
{
    int32_t o = obwodka(p);
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

static void rysuj_przycisk(struct k1_obraz *o, const struct pow *p, const struct prost *r, int co, uint32_t ink)
{
    struct prost b = przycisk(p, co);
    float x = (float)(b.x - r->x), y = (float)(b.y - r->y);
    enum k1_ikona ik = co == STREFA_ZAMKNIJ ? K1_IK_ZAMKNIJ : co == STREFA_MINIMALIZUJ ? K1_IK_MINIMALIZUJ
                     : p->maks ? K1_IK_PRZYWROC : K1_IK_MAKSYMALIZUJ;
    if ((int)(p - s.p) == s.strefa_okno && s.strefa_przycisk == co)
        k1_zaokraglony(o, x, y, PRZYCISK, PRZYCISK, 8, k1_kolor(ink, 40));
    k1_ikona(o, ik, x + 6, y + 6, 16, k1_kolor(ink, 255));
}

void strefa_ramka(const struct pow *p, const struct prost *r)
{
    const struct k1_motyw *m = k1_motyw(s.ust[K1_UST_MOTYW]);
    const struct k1_strefa_opis *z = opis(p);
    struct k1_obraz o = wycinek(r);
    struct prost f = strefa_obszar(p);
    int jednorazowa = !s.kl[p->kl].pamieta;
    uint32_t k = kolor(p), ink = jednorazowa ? mieszaj(k, m->tekst, 64) : 0xffffff;
    float x = (float)(f.x - r->x), y = (float)(f.y - r->y);
    char t[160];
    int32_t ob = obwodka(p);
    k1_zaokraglony(&o, x, y, (float)f.w, (float)f.h, p->maks ? 0 : PROMIEN, k1_kolor(k, 255));
    if (jednorazowa) /* the light bar inside the outline; its lower corners hide under the zone's picture */
        k1_zaokraglony(&o, x + (float)ob, y + (float)ob, (float)p->w, K1_STREFA_TYTUL + 8, p->maks ? 0 : PROMIEN - OBWODKA,
                       k1_kolor(mieszaj(m->pole, k, 36), 255));
    k1_ikona(&o, (enum k1_ikona)z->ikona, x + (float)ob + 10, y + (float)ob + (K1_STREFA_TYTUL - 18) / 2.0f, 18,
             k1_kolor(ink, 255));
    tekst(p, t, sizeof t);
    k1_tekst(&o, K1_CZ_TEKST_MOCNA, (int32_t)x + ob + 36, (int32_t)y + ob + K1_STREFA_TYTUL / 2 + 6, t,
             k1_kolor(ink, 255), p->w - 36 - 3 * ODSTEP - MARGINES - 8);
    rysuj_przycisk(&o, p, r, STREFA_MINIMALIZUJ, ink);
    rysuj_przycisk(&o, p, r, STREFA_MAKSYMALIZUJ, ink);
    rysuj_przycisk(&o, p, r, STREFA_ZAMKNIJ, ink);
}

int strefa_trafienie(const struct pow *p, int32_t x, int32_t y)
{
    static const int przyciski[] = { STREFA_ZAMKNIJ, STREFA_MAKSYMALIZUJ, STREFA_MINIMALIZUJ };
    struct prost f = strefa_obszar(p), tresc = { p->x, p->y, p->w, p->h }, b;
    int kraw = rozmiar_brzeg(p, x, y); /* 0.10.5 step 4: a grip — also 3 px outside the frame */
    if (kraw != 0)
        return STREFA_BRZEG | kraw;
    if (!w(&f, x, y) || w(&tresc, x, y))
        return 0;
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
    int przyc = co == STREFA_MINIMALIZUJ || co == STREFA_ZAMKNIJ || co == STREFA_MAKSYMALIZUJ ? co : 0,
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
    } else if (co == STREFA_TYTUL && !rozmiar_tytul(i) && !p->maks) { /* a maximized window does not move */
        s.ciagniete = i;
        s.ciag_dx = s.x - p->x;
        s.ciag_dy = s.y - p->y;
        s.ciag_klatki = s.klatki;
        s.ciag_cpu = czas_procesora();
    }
}

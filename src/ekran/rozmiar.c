/* K1 ekran — the size of a zone's window (0.10.5 step 4, T12–T14; the owner's D8: sketches accepted 2026-11-18).
 * Only ekran changes it, on the user's gesture on K1's frame: dragging an edge or a corner (grips 6 px, corners 16 px),
 * the button "maksymalizuj / przywróć" on the title bar, a double click on the title bar. While dragging, an outline in
 * the session's colour with the size shows the new rectangle; the zone gets only the final size (ROZMIAR), answers
 * with a surface of exactly that size (powierzchnie.c) — until then its old picture stays. A maximized window fills the
 * work area (the screen without the top and the bottom bar — from the shell's panels ekran composes, T13), without the
 * side outline; it cannot be moved or resized until restored. The zone has no message to change its size or place.
 * The size and maximization of each session are kept in skarbiec (ekran's object K1_OB_OKNA_STREF). Part of ekran. */
#include <stdio.h>
#include <string.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_rysuj.h"
#include "k1_skarb_klient.h"

#define CHWYT 3          /* px on each side of the frame's edge that grab it (6 in all) */
#define ROG 16           /* px along an edge from a corner that grab the corner */
#define DWUKLIK_S 0.45   /* two presses on the title bar within this: maximize / restore */
#define ZARYS 3          /* the outline's width while dragging */
#define FD_SKARBIEC 5
#define MAGIA "K1O1"

static struct {
    int okno, kraw;      /* the window being resized (-1: none), its edges (K_*) */
    int32_t sx, sy;      /* the pointer at the start */
    struct prost start, zarys; /* the picture at the start; the outline now (the new picture) */
} r = { -1, 0, 0, 0, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
static struct prost obszar;        /* the work area last seen */
static int tytul_okno = -1;        /* the last press on a title bar: its window and when */
static double tytul_t;

/* Each session's window as the user left it: picture size and maximized (saved in skarbiec). */
struct okno_sesji {
    int32_t w, h, maks;
};
static struct {
    char magia[4];
    struct okno_sesji o[K1_STREFY_MAKS];
} zapis;
static int wczytane;

struct prost obszar_roboczy(void)
{
    int32_t gora = K1_PASEK_GORNY, dol = s.H, i;
    for (i = 0; i < MAKS_POW; i++) {
        const struct pow *p = &s.p[i];
        if (!widac(p) || p->rodzaj != K1_POW_PANEL)
            continue;
        if (p->x == 0 && p->y == 0 && p->w == s.W && p->h < s.H / 4)
            gora = p->h; /* the top bar */
        else if (p->y > s.H / 2 && p->y + p->h >= s.H - K1_PANEL_MARGINES && p->y + K1_PANEL_MARGINES < dol)
            dol = p->y + K1_PANEL_MARGINES; /* the bottom bar: its visible edge (inside its shadow's margin, P3) */
    }
    return (struct prost){ 0, gora, s.W, dol - gora };
}

/* The edges of zone window p's frame under (x, y): K_* bits, 0 none. */
int rozmiar_brzeg(const struct pow *p, int32_t x, int32_t y)
{
    struct prost f = strefa_obszar(p);
    int k = 0;
    if (p->maks || x < f.x - CHWYT || y < f.y - CHWYT || x >= f.x + f.w + CHWYT || y >= f.y + f.h + CHWYT)
        return 0;
    if (x < f.x + CHWYT)
        k |= K_LEWA;
    else if (x >= f.x + f.w - CHWYT)
        k |= K_PRAWA;
    if (y < f.y + CHWYT)
        k |= K_GORA;
    else if (y >= f.y + f.h - CHWYT)
        k |= K_DOL;
    if ((k & (K_LEWA | K_PRAWA)) && !(k & (K_GORA | K_DOL)))
        k |= y < f.y + ROG ? K_GORA : y >= f.y + f.h - ROG ? K_DOL : 0;
    else if ((k & (K_GORA | K_DOL)) && !(k & (K_LEWA | K_PRAWA)))
        k |= x < f.x + ROG ? K_LEWA : x >= f.x + f.w - ROG ? K_PRAWA : 0;
    return k;
}

/* The cursor's shape over edges k (K1_KURSOR_* of k1_grafika.h). */
int rozmiar_ksztalt(int k)
{
    if ((k & (K_LEWA | K_GORA)) == (K_LEWA | K_GORA) || (k & (K_PRAWA | K_DOL)) == (K_PRAWA | K_DOL))
        return K1_KURSOR_UKOS_LG;
    if (k & (K_LEWA | K_PRAWA))
        return (k & (K_GORA | K_DOL)) ? K1_KURSOR_UKOS_PG : K1_KURSOR_POZIOMO;
    return k ? K1_KURSOR_PIONOWO : K1_KURSOR_STRZALKA;
}

/* ---- saved sizes ---- */

static void zapisz(void)
{
    memcpy(zapis.magia, MAGIA, 4);
    if (ustawienia_fd() >= 0 && k1_skarb_zapisz(ustawienia_fd(), K1_OB_OKNA_STREF, &zapis, sizeof zapis) != 0)
        k1_log(TAG, "zapis rozmiarów okien stref nieudany");
}

static int dobre(const struct okno_sesji *o)
{
    return (o->w == 0 && o->h == 0 && o->maks == 0) ||
           (o->w >= K1_OKNO_MIN_SZER && o->w <= K1_POW_MAKS_BOK && o->h >= K1_OKNO_MIN_WYS &&
            o->h <= K1_POW_MAKS_BOK && (o->maks == 0 || o->maks == 1));
}

/* Once, when the first zone window comes (the settings are read by then): what skarbiec keeps, each row checked. */
static void wczytaj(void)
{
    int tryb, n, i;
    if (wczytane++ || ustawienia_fd() < 0)
        return;
    n = k1_skarb_czytaj(ustawienia_fd(), K1_OB_OKNA_STREF, &zapis, sizeof zapis, 1000, &tryb);
    if (n != (int)sizeof zapis || memcmp(zapis.magia, MAGIA, 4) != 0) {
        memset(&zapis, 0, sizeof zapis);
        return;
    }
    for (i = 0; i < K1_STREFY_MAKS; i++)
        if (!dobre(&zapis.o[i]))
            zapis.o[i] = (struct okno_sesji){ 0, 0, 0 };
}

static struct okno_sesji *sesja(const struct pow *p)
{
    int nr = s.kl[p->kl].apl;
    return nr >= 1 && nr <= K1_STREFY_MAKS ? &zapis.o[nr - 1] : NULL;
}

/* ---- the size to the zone ---- */

static void wyslij_rozmiar(int i, int32_t w, int32_t h)
{
    struct pow *p = &s.p[i];
    struct okno_sesji *z = sesja(p);
    int pelny = pelny_jest(p);
    p->zadany_w = w;
    p->zadany_h = h;
    wyslij(p->kl, k1_komunikat_nowy(K1_KOM_ROZMIAR, p->id, w, h, pelny ? K1_ROZMIAR_PELNY : p->maks, 0, 0));
    k1_log(TAG, "okno %u: nowy rozmiar %dx%d%s", p->okno, (int)w, (int)h,
           pelny ? " (pełny ekran)" : p->maks ? " (zmaksymalizowane)" : "");
    if (z != NULL && !pelny && (z->w != w || z->h != h || z->maks != p->maks)) { /* the full screen is never saved */
        *z = (struct okno_sesji){ p->maks ? z->w : w, p->maks ? z->h : h, p->maks };
        if (z->w == 0) /* maximized before it was ever resized: restored to its first size */
            z->w = p->przed.w, z->h = p->przed.h;
        zapisz();
    }
}

/* Window i maximized: the work area to the pixel (0.11.0 step 1: any width — the zone's mode comes from the EDID,
 * edid.c in the VMM), its title bar right under the top bar, no side outline. */
static void maksymalizuj(int i)
{
    struct pow *p = &s.p[i];
    struct prost a = obszar_roboczy();
    int32_t w = a.w, h = a.h - K1_STREFA_TYTUL;
    uszkodz_pow(p);
    if (!p->maks)
        p->przed = (struct prost){ p->x, p->y, p->zadany_w ? p->zadany_w : p->w, p->zadany_h ? p->zadany_h : p->h };
    p->maks = 1;
    p->x = a.x + (a.w - w) / 2;
    p->y = a.y + K1_STREFA_TYTUL;
    obszar = a;
    k1_log(TAG, "obszar roboczy %d,%d %dx%d", (int)a.x, (int)a.y, (int)a.w, (int)a.h);
    uszkodz_pow(p);
    wyslij_rozmiar(i, w, h < K1_OKNO_MIN_WYS ? K1_OKNO_MIN_WYS : h);
}

static void przywroc(int i)
{
    struct pow *p = &s.p[i];
    uszkodz_pow(p);
    p->maks = 0;
    p->x = p->przed.x;
    p->y = p->przed.y;
    uszkodz_pow(p);
    wyslij_rozmiar(i, p->przed.w, p->przed.h);
}

void rozmiar_przelacz(int i)
{
    if (s.p[i].maks)
        przywroc(i);
    else
        maksymalizuj(i);
}

/* A press on window i's title bar: 1 when it was the second of a double click (maximize / restore done). */
int rozmiar_tytul(int i)
{
    double t = k1_teraz();
    int dwa = tytul_okno == i && t - tytul_t < DWUKLIK_S;
    tytul_okno = dwa ? -1 : i;
    tytul_t = t;
    if (dwa)
        rozmiar_przelacz(i);
    return dwa;
}

/* The first commit of zone window i: the size and maximization its session had. */
void rozmiar_nowe_okno(int i)
{
    struct pow *p = &s.p[i];
    const struct okno_sesji *z;
    wczytaj();
    if ((z = sesja(p)) == NULL || z->w == 0)
        return;
    k1_log(TAG, "okno %u: rozmiar sesji %dx%d%s", p->okno, (int)z->w, (int)z->h, z->maks ? ", zmaksymalizowane" : "");
    p->przed = (struct prost){ p->x, p->y, z->w, z->h };
    if (z->maks)
        maksymalizuj(i);
    else if (z->w != p->w || z->h != p->h)
        wyslij_rozmiar(i, z->w, z->h);
}

/* ---- dragging an edge ---- */

static void uszkodz_zarys(void)
{
    struct prost z = r.zarys;
    uszkodz(z.x - ZARYS - 2, z.y - K1_STREFA_TYTUL - ZARYS - 2, z.w + 2 * ZARYS + 4, z.h + K1_STREFA_TYTUL + 2 * ZARYS + 4);
}

void rozmiar_start(int i, int kraw)
{
    struct pow *p = &s.p[i];
    r.okno = i;
    r.kraw = kraw;
    r.sx = s.x;
    r.sy = s.y;
    r.start = (struct prost){ p->x, p->y, p->zadany_w ? p->zadany_w : p->w, p->zadany_h ? p->zadany_h : p->h };
    r.zarys = r.start;
    obszar = obszar_roboczy();
    uszkodz_zarys();
}

int rozmiar_trwa(void)
{
    return r.okno >= 0;
}

/* The outline follows the pointer: the moving edges only, from the smallest size to the work area. */
void rozmiar_ruch(void)
{
    struct prost n = r.start, a = obszar;
    int32_t dx = s.x - r.sx, dy = s.y - r.sy, maks_w = a.w - 2 * 3, maks_h = a.h - K1_STREFA_TYTUL - 2 * 3;
    if (r.okno < 0)
        return;
    if (r.kraw & K_PRAWA)
        n.w = r.start.w + dx;
    if (r.kraw & K_LEWA)
        n.w = r.start.w - dx;
    if (r.kraw & K_DOL)
        n.h = r.start.h + dy;
    if (r.kraw & K_GORA)
        n.h = r.start.h - dy;
    n.w = n.w < K1_OKNO_MIN_SZER ? K1_OKNO_MIN_SZER : n.w > maks_w ? maks_w : n.w;
    n.h = n.h < K1_OKNO_MIN_WYS ? K1_OKNO_MIN_WYS : n.h > maks_h ? maks_h : n.h;
    if (r.kraw & K_LEWA)
        n.x = r.start.x + r.start.w - n.w;
    if (r.kraw & K_GORA)
        n.y = r.start.y + r.start.h - n.h;
    if (n.y - K1_STREFA_TYTUL - 3 < a.y) /* never above the top bar */
        n.y = a.y + K1_STREFA_TYTUL + 3;
    if (n.x == r.zarys.x && n.y == r.zarys.y && n.w == r.zarys.w && n.h == r.zarys.h)
        return;
    uszkodz_zarys();
    r.zarys = n;
    uszkodz_zarys();
}

/* The button released: the window goes to the outline's place, the zone gets its size. */
void rozmiar_koniec(void)
{
    struct pow *p;
    if (r.okno < 0)
        return;
    p = &s.p[r.okno];
    uszkodz_zarys();
    if (p->uzyta && (r.zarys.w != r.start.w || r.zarys.h != r.start.h)) {
        uszkodz_pow(p);
        p->x = r.zarys.x;
        p->y = r.zarys.y;
        uszkodz_pow(p);
        wyslij_rozmiar(r.okno, r.zarys.w, r.zarys.h);
    }
    r.okno = -1;
}

/* Into the scene inside k (after the windows and panels): the outline and the size. */
void rozmiar_rysuj(const struct prost *k)
{
    struct prost z = r.zarys, f = { z.x - ZARYS, z.y - K1_STREFA_TYTUL - ZARYS, z.w + 2 * ZARYS,
                                    z.h + K1_STREFA_TYTUL + 2 * ZARYS };
    struct prost c = *k;
    struct k1_obraz o;
    uint32_t kolor;
    char t[32];
    int32_t tw;
    if (r.okno < 0 || !przetnij(&c, &(struct prost){ 0, 0, s.W, s.H }))
        return;
    o = (struct k1_obraz){ s.scena.px + (size_t)c.y * s.scena.krok + (size_t)c.x, (uint32_t)c.w, (uint32_t)c.h,
                           s.scena.krok };
    kolor = k1_strefa(s.kl[s.p[r.okno].kl].apl)->kolor;
    k1_obrys(&o, (float)(f.x - c.x) + 1.5f, (float)(f.y - c.y) + 1.5f, (float)f.w - 3, (float)f.h - 3, 10, ZARYS,
             k1_kolor(kolor, 255));
    k1_obrys(&o, (float)(f.x - c.x) + ZARYS + 0.5f, (float)(f.y - c.y) + ZARYS + 0.5f, (float)f.w - 2 * ZARYS - 1,
             (float)f.h - 2 * ZARYS - 1, 8, 1, k1_kolor(0xffffff, 200));
    snprintf(t, sizeof t, "%d × %d", (int)z.w, (int)z.h);
    tw = k1_tekst_szer(K1_CZ_TEKST, t);
    k1_zaokraglony(&o, (float)(f.x + f.w - tw - 44 - c.x), (float)(f.y + f.h - 46 - c.y), (float)tw + 20, 28, 8,
                   k1_kolor(0x2b2520, 235));
    k1_tekst(&o, K1_CZ_TEKST, f.x + f.w - tw - 34 - c.x, f.y + f.h - 27 - c.y, t, k1_kolor(0xffffff, 255), 0);
}

/* 0.11.0 step 1 (pelny.c): window i into the full screen — the whole screen, its rectangle and maximization kept — or
 * back to them. */
void rozmiar_pelny(int i, int tak)
{
    struct pow *p = &s.p[i];
    uszkodz_pow(p);
    if (tak) {
        p->przed_pelny = (struct prost){ p->x, p->y, p->zadany_w ? p->zadany_w : p->w, p->zadany_h ? p->zadany_h : p->h };
        p->pelny_maks = p->maks;
        p->maks = 0;
        p->x = 0;
        p->y = 0;
        wyslij_rozmiar(i, s.W, s.H);
        return;
    }
    if (p->pelny_maks) { /* maximized again; p->przed still holds its size from before the maximization */
        p->maks = 1;
        maksymalizuj(i);
        return;
    }
    p->x = p->przed_pelny.x;
    p->y = p->przed_pelny.y;
    uszkodz_pow(p);
    wyslij_rozmiar(i, p->przed_pelny.w, p->przed_pelny.h);
}

/* Each frame: the work area changed (the resolution, the bars): maximized windows follow it; a window in full screen
 * follows the screen's size. */
void rozmiar_sprawdz(void)
{
    struct prost a = obszar_roboczy();
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && pelny_jest(&s.p[i]) && (s.p[i].zadany_w != s.W || s.p[i].zadany_h != s.H))
            wyslij_rozmiar(i, s.W, s.H);
    if (a.x == obszar.x && a.y == obszar.y && a.w == obszar.w && a.h == obszar.h)
        return;
    obszar = a;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].maks && okno_strefy(&s.p[i]))
            maksymalizuj(i);
}

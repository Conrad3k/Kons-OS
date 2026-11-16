/* K1 kompozytor — xdg-shell (kompozytor.h): xdg_wm_base, xdg_positioner, xdg_surface, xdg_toplevel, xdg_popup.
 * The zone shows one application: its first toplevel fills the display (maximized, activated); a toplevel with a
 * parent (a dialog) is centred at its own size; popups are placed by their positioner and kept on the display. The
 * windows are in `okna`, bottom to top, in the order they were mapped. */
#include <stdlib.h>
#include <string.h>

#include "kompozytor.h"

#define OKNA_MAKS 16

/* xdg_wm_base.error, xdg_surface.error (xdg-shell.xml) */
enum { WM_ROLA = 0, WM_NIE_NAJWYZSZE = 2, WM_ZLY_POZYCJONER = 5, XS_NIESKONFIGUROWANA = 3 };
enum { STAN_ZMAKSYMALIZOWANE = 1, STAN_AKTYWNE = 4 };

static struct xdg_pow *okna[OKNA_MAKS];
static unsigned n_okien;

struct xdg_pow *xdg_okno(unsigned i)
{
    return i < n_okien ? okna[i] : NULL;
}

struct xdg_pow *xdg_fokus(void)
{
    unsigned i;
    for (i = n_okien; i-- > 0;)
        if (okna[i]->okno == 1 || okna[i]->chwyt)
            return okna[i];
    return NULL;
}

static void schowaj(struct xdg_pow *x)
{
    unsigned i;
    if (!x->zmapowana)
        return;
    for (i = 0; i < n_okien; i++)
        if (okna[i] == x) {
            memmove(okna + i, okna + i + 1, (n_okien - i - 1) * sizeof okna[0]);
            n_okien--;
            break;
        }
    x->zmapowana = 0;
    if (x->pow != NULL)
        scena_zmiana_powierzchni(x->pow);
    scena_zmiana(0, 0, ekran.szer, ekran.wys);
    wejscie_okno_zmiana();
}

void xdg_wyskakujace_zamknij(void)
{
    unsigned i;
    for (i = n_okien; i-- > 0;)
        if (okna[i]->okno == 2 && okna[i]->rola != NULL) {
            struct xdg_pow *x = okna[i];
            zdarzenie(x->rola, 1, ""); /* xdg_popup.popup_done */
            schowaj(x);
        }
}

/* Where the window geometry starts on the display. */
static void umiesc(struct xdg_pow *x)
{
    int32_t gw = x->gw > 0 ? x->gw : x->pow->szer, gh = x->gh > 0 ? x->gh : x->pow->wys;
    if (x->okno == 1 && x->rodzic == NULL) {
        x->x = 0;
        x->y = 0;
    } else if (x->okno == 1) {
        x->x = (ekran.szer - gw) / 2;
        x->y = (ekran.wys - gh) / 3;
    }
}

void xdg_po_zatwierdzeniu(struct powierzchnia *p)
{
    struct xdg_pow *x = p->xdg;
    if (x == NULL || x->rola == NULL)
        return;
    if (!x->skonfigurowana && x->okno != 0 && !x->zmapowana && p->piksele == NULL) {
        uint32_t s = kolejny_serial();
        if (x->okno == 1) {
            uint32_t stany[2] = { STAN_ZMAKSYMALIZOWANE, STAN_AKTYWNE };
            if (x->rodzic == NULL)
                zdarzenie(x->rola, 0, "iia", ekran.szer, ekran.wys, stany, (uint32_t)sizeof stany);
            else
                zdarzenie(x->rola, 0, "iia", 0, 0, stany + 1, (uint32_t)sizeof stany[0]);
        } else {
            zdarzenie(x->rola, 0, "iiii", x->x - (x->rodzic != NULL ? x->rodzic->x : 0),
                      x->y - (x->rodzic != NULL ? x->rodzic->y : 0), x->gw, x->gh);
        }
        zdarzenie(x->o, 0, "u", s);
        return;
    }
    if (p->piksele == NULL) {
        schowaj(x);
        return;
    }
    if (!x->zmapowana && x->skonfigurowana && n_okien < OKNA_MAKS) {
        umiesc(x);
        okna[n_okien++] = x;
        x->zmapowana = 1;
        if (x->okno == 1)
            zglos_okno();
        wejscie_okno_zmiana();
    } else if (x->zmapowana && x->okno == 1) {
        umiesc(x);
    }
    scena_zmiana_powierzchni(p);
}

void xdg_zapomnij(struct powierzchnia *p)
{
    struct xdg_pow *x = p->xdg;
    unsigned i;
    if (x == NULL)
        return;
    schowaj(x);
    for (i = 0; i < n_okien; i++) /* popups of this window go with it */
        if (okna[i]->rodzic == x && okna[i]->okno == 2 && okna[i]->rola != NULL) {
            zdarzenie(okna[i]->rola, 1, "");
            schowaj(okna[i]);
            i = (unsigned)-1;
        }
    x->pow = NULL;
    p->xdg = NULL;
}

/* --- xdg_wm_base --- */
static void nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

static void wm_pozycjoner(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_POSITIONER, o->wersja);
    struct pozycjoner *p = n != NULL ? calloc(1, sizeof *p) : NULL;
    if (p != NULL)
        n->dane = p;
}

static void wm_powierzchnia(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p;
    struct obiekt *n;
    struct xdg_pow *x;
    if (obiekt_typu(k, a[1].o, IF_SURFACE, 0) == NULL)
        return;
    p = a[1].o->dane;
    if (p->rola != ROLA_BRAK || p->piksele != NULL) {
        blad(k, o, WM_ROLA, "powierzchnia ma już rolę");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_XDG_SURFACE, o->wersja);
    x = n != NULL ? calloc(1, sizeof *x) : NULL;
    if (x == NULL)
        return;
    x->o = n;
    x->pow = p;
    n->dane = x;
    p->rola = ROLA_XDG;
    p->xdg = x;
}

/* --- xdg_positioner --- */
static void po_rozmiar(struct klient *k, struct obiekt *o, union arg *a)
{
    struct pozycjoner *p = o->dane;
    if (a[0].i <= 0 || a[1].i <= 0 || a[0].i > POWIERZCHNIA_MAKS || a[1].i > POWIERZCHNIA_MAKS) {
        blad(k, o, 0, "zły rozmiar");
        return;
    }
    p->szer = a[0].i, p->wys = a[1].i;
}

static void po_kotwica_prost(struct klient *k, struct obiekt *o, union arg *a)
{
    struct pozycjoner *p = o->dane;
    if (a[2].i < 0 || a[3].i < 0 || a[2].i > POWIERZCHNIA_MAKS || a[3].i > POWIERZCHNIA_MAKS ||
        a[0].i < -POWIERZCHNIA_MAKS || a[0].i > POWIERZCHNIA_MAKS || a[1].i < -POWIERZCHNIA_MAKS ||
        a[1].i > POWIERZCHNIA_MAKS) {
        blad(k, o, 0, "zły prostokąt kotwicy");
        return;
    }
    p->ax = a[0].i, p->ay = a[1].i, p->aw = a[2].i, p->ah = a[3].i;
}

static void po_kotwica(struct klient *k, struct obiekt *o, union arg *a)
{
    if (a[0].u > 8)
        blad(k, o, 1, "zła kotwica");
    else
        ((struct pozycjoner *)o->dane)->kotwica = a[0].u;
}

static void po_grawitacja(struct klient *k, struct obiekt *o, union arg *a)
{
    if (a[0].u > 8)
        blad(k, o, 1, "zła grawitacja");
    else
        ((struct pozycjoner *)o->dane)->grawitacja = a[0].u;
}

static void po_dopasowanie(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k;
    ((struct pozycjoner *)o->dane)->dopasowanie = a[0].u;
}

static void po_przesun(struct klient *k, struct obiekt *o, union arg *a)
{
    struct pozycjoner *p = o->dane;
    (void)k;
    if (a[0].i >= -POWIERZCHNIA_MAKS && a[0].i <= POWIERZCHNIA_MAKS && a[1].i >= -POWIERZCHNIA_MAKS &&
        a[1].i <= POWIERZCHNIA_MAKS)
        p->dx = a[0].i, p->dy = a[1].i;
}

/* x along one axis: the anchor point on the rectangle, then the gravity (1 towards the start, 2 the end, 0 centred). */
static int32_t os(int32_t a0, int32_t aw, int kotw, int graw, int32_t rozmiar)
{
    int32_t p = kotw == 1 ? a0 : kotw == 2 ? a0 + aw : a0 + aw / 2;
    return graw == 1 ? p - rozmiar : graw == 2 ? p : p - rozmiar / 2;
}

/* The anchor/gravity value (none, top, bottom, left, right, corners) on the x axis (1 left, 2 right) and on y. */
static int na_x(uint32_t v) { return v == 3 || v == 5 || v == 6 ? 1 : v == 4 || v == 7 || v == 8 ? 2 : 0; }
static int na_y(uint32_t v) { return v == 1 || v == 5 || v == 7 ? 1 : v == 2 || v == 6 || v == 8 ? 2 : 0; }

static void wyskakujace_umiesc(struct xdg_pow *x, const struct pozycjoner *p)
{
    int32_t bx = x->rodzic->x, by = x->rodzic->y, px, py;
    px = bx + os(p->ax, p->aw, na_x(p->kotwica), na_x(p->grawitacja), p->szer) + p->dx;
    py = by + os(p->ay, p->ah, na_y(p->kotwica), na_y(p->grawitacja), p->wys) + p->dy;
    if ((p->dopasowanie & 8) && py + p->wys > ekran.wys && na_y(p->grawitacja) == 2) /* flip_y */
        py = by + os(p->ay, p->ah, na_y(p->kotwica) == 2 ? 1 : na_y(p->kotwica), 1, p->wys) - p->dy;
    if (px + p->szer > ekran.szer) /* slide: always keep it on the display */
        px = ekran.szer - p->szer;
    if (py + p->wys > ekran.wys)
        py = ekran.wys - p->wys;
    x->x = px < 0 ? 0 : px;
    x->y = py < 0 ? 0 : py;
    x->gw = p->szer;
    x->gh = p->wys;
}

/* --- xdg_surface --- */
static void xs_okno(struct klient *k, struct obiekt *o, union arg *a)
{
    struct xdg_pow *x = o->dane;
    struct obiekt *n;
    if (x == NULL || x->okno != 0) {
        blad(k, o, WM_ROLA, "xdg_surface ma już rolę");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_TOPLEVEL, o->wersja);
    if (n == NULL)
        return;
    n->dane = x;
    x->rola = n;
    x->okno = 1;
}

static void xs_wyskakujace(struct klient *k, struct obiekt *o, union arg *a)
{
    struct xdg_pow *x = o->dane;
    struct pozycjoner *p;
    struct obiekt *n;
    if (x == NULL || x->okno != 0) {
        blad(k, o, WM_ROLA, "xdg_surface ma już rolę");
        return;
    }
    if (obiekt_typu(k, a[2].o, IF_POSITIONER, 0) == NULL ||
        (a[1].o != NULL && obiekt_typu(k, a[1].o, IF_XDG_SURFACE, 0) == NULL))
        return;
    p = a[2].o->dane;
    if (a[1].o == NULL || a[1].o->dane == NULL || p->szer <= 0) {
        blad(k, o, WM_ZLY_POZYCJONER, "wyskakujące bez rodzica albo rozmiaru");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_POPUP, o->wersja);
    if (n == NULL)
        return;
    n->dane = x;
    x->rola = n;
    x->okno = 2;
    x->rodzic = a[1].o->dane;
    wyskakujace_umiesc(x, p);
}

static void xs_geometria(struct klient *k, struct obiekt *o, union arg *a)
{
    struct xdg_pow *x = o->dane;
    if (a[2].i <= 0 || a[3].i <= 0 || a[2].i > POWIERZCHNIA_MAKS || a[3].i > POWIERZCHNIA_MAKS ||
        a[0].i < -POWIERZCHNIA_MAKS || a[0].i > POWIERZCHNIA_MAKS || a[1].i < -POWIERZCHNIA_MAKS ||
        a[1].i > POWIERZCHNIA_MAKS) {
        blad(k, o, 0, "zła geometria okna");
        return;
    }
    if (x != NULL && x->okno != 2)
        x->gx = a[0].i, x->gy = a[1].i, x->gw = a[2].i, x->gh = a[3].i;
    else if (x != NULL)
        x->gx = a[0].i, x->gy = a[1].i;
}

static void xs_potwierdz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct xdg_pow *x = o->dane;
    (void)k, (void)a;
    if (x != NULL)
        x->skonfigurowana = 1;
}

static void xs_zwolnij(struct obiekt *o)
{
    struct xdg_pow *x = o->dane;
    unsigned i;
    if (x == NULL)
        return;
    schowaj(x);
    if (x->rola != NULL)
        x->rola->dane = NULL;
    if (x->pow != NULL)
        x->pow->xdg = NULL;
    for (i = 0; i < n_okien; i++)
        if (okna[i]->rodzic == x)
            okna[i]->rodzic = NULL;
    free(x);
    o->dane = NULL;
}

static void po_zwolnij(struct obiekt *o)
{
    free(o->dane);
    o->dane = NULL;
}

/* --- xdg_toplevel, xdg_popup --- */
static void rola_zwolnij(struct obiekt *o)
{
    struct xdg_pow *x = o->dane;
    if (x == NULL)
        return;
    schowaj(x);
    x->rola = NULL;
    x->okno = 0;
    x->skonfigurowana = 0;
    o->dane = NULL;
}

static void t_rodzic(struct klient *k, struct obiekt *o, union arg *a)
{
    struct xdg_pow *x = o->dane;
    if (a[0].o != NULL && obiekt_typu(k, a[0].o, IF_TOPLEVEL, 0) == NULL)
        return;
    if (x != NULL)
        x->rodzic = a[0].o != NULL ? a[0].o->dane : NULL;
}

static void t_napis(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a; /* the title is K1's: the frame shows the zone's own */
}

static void t_seat(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)o;
    obiekt_typu(k, a[0].o, IF_SEAT, 0);
}

static void t_pelny(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)o;
    if (a[0].o != NULL)
        obiekt_typu(k, a[0].o, IF_OUTPUT, 0);
}

static void w_chwyt(struct klient *k, struct obiekt *o, union arg *a)
{
    struct xdg_pow *x = o->dane;
    if (obiekt_typu(k, a[0].o, IF_SEAT, 0) == NULL || x == NULL)
        return;
    if (x->zmapowana) {
        blad(k, o, 0, "chwyt po pokazaniu"); /* xdg_popup.error.invalid_grab */
        return;
    }
    x->chwyt = 1;
}

static const struct zadanie z_wm[] = { { "destroy", "", nic, 1 }, { "create_positioner", "n", wm_pozycjoner, 0 },
                                       { "get_xdg_surface", "no", wm_powierzchnia, 0 }, { "pong", "u", nic, 0 } };
static const struct zadanie z_po[] = { { "destroy", "", nic, 1 },        { "set_size", "ii", po_rozmiar, 0 },
                                       { "set_anchor_rect", "iiii", po_kotwica_prost, 0 },
                                       { "set_anchor", "u", po_kotwica, 0 }, { "set_gravity", "u", po_grawitacja, 0 },
                                       { "set_constraint_adjustment", "u", po_dopasowanie, 0 },
                                       { "set_offset", "ii", po_przesun, 0 } };
static const struct zadanie z_xs[] = { { "destroy", "", nic, 1 }, { "get_toplevel", "n", xs_okno, 0 },
                                       { "get_popup", "n?oo", xs_wyskakujace, 0 },
                                       { "set_window_geometry", "iiii", xs_geometria, 0 },
                                       { "ack_configure", "u", xs_potwierdz, 0 } };
static const struct zadanie z_t[] = {
    { "destroy", "", nic, 1 },          { "set_parent", "?o", t_rodzic, 0 },   { "set_title", "s", t_napis, 0 },
    { "set_app_id", "s", t_napis, 0 },  { "show_window_menu", "ouii", t_seat, 0 }, { "move", "ou", t_seat, 0 },
    { "resize", "ouu", t_seat, 0 },     { "set_max_size", "ii", nic, 0 },     { "set_min_size", "ii", nic, 0 },
    { "set_maximized", "", nic, 0 },    { "unset_maximized", "", nic, 0 },    { "set_fullscreen", "?o", t_pelny, 0 },
    { "unset_fullscreen", "", nic, 0 }, { "set_minimized", "", nic, 0 },
};
static const struct zadanie z_w[] = { { "destroy", "", nic, 1 }, { "grab", "ou", w_chwyt, 0 } };
const struct obsluga ob_wm_base = OBSLUGA(z_wm, NULL, NULL);
const struct obsluga ob_positioner = OBSLUGA(z_po, po_zwolnij, NULL);
const struct obsluga ob_xdg_surface = OBSLUGA(z_xs, xs_zwolnij, NULL);
const struct obsluga ob_toplevel = OBSLUGA(z_t, rola_zwolnij, NULL);
const struct obsluga ob_popup = OBSLUGA(z_w, rola_zwolnij, NULL);

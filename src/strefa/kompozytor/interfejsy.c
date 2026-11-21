/* K1 kompozytor — the table of interfaces (K1_INTERFEJSY in kompozytor.h) and the small ones: wl_display,
 * wl_registry, wl_output (the data objects: dane.c, 0.11.0 step 4). */
#include <string.h>
#include <unistd.h>

#include "kompozytor.h"

#define X_TAB(n, c, nazwa, wersja, glob) [IF_##n] = { nazwa, wersja, glob, &ob_##c },
const struct interfejs interfejsy[IF_LICZBA] = { [IF_BRAK] = { "", 0, 0, NULL }, K1_INTERFEJSY(X_TAB) };
#undef X_TAB

static void nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

/* --- wl_display --- */
static void d_sync(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *c = obiekt_nowy(k, a[0].u, IF_CALLBACK, 1);
    (void)o;
    if (c == NULL)
        return;
    zdarzenie(c, 0, "u", kolejny_serial());
    obiekt_usun(c);
}

static void d_rejestr(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *r = obiekt_nowy(k, a[0].u, IF_REGISTRY, 1);
    uint32_t i;
    (void)o;
    for (i = 1; r != NULL && i < IF_LICZBA; i++)
        if (interfejsy[i].globalny && (i != IF_DMABUF || dmabuf_dostepny())) /* the card's: fast graphics only */
            zdarzenie(r, 0, "usu", i, interfejsy[i].nazwa, interfejsy[i].wersja);
}

/* --- wl_registry.bind: the global's name is its row in the table --- */
static void r_przywiaz(struct klient *k, struct obiekt *o, union arg *a)
{
    uint32_t nazwa = a[0].u, wersja = a[2].u;
    struct obiekt *n;
    if (nazwa == 0 || nazwa >= IF_LICZBA || !interfejsy[nazwa].globalny || (nazwa == IF_DMABUF && !dmabuf_dostepny()) ||
        strcmp(a[1].s, interfejsy[nazwa].nazwa) != 0 || wersja == 0 || wersja > interfejsy[nazwa].wersja) {
        blad(k, o, BLAD_ZLY_OBIEKT, R_GLOBALNY);
        return;
    }
    n = obiekt_nowy(k, a[3].u, (int)nazwa, wersja);
    if (n != NULL && interfejsy[nazwa].ob->przywiaz != NULL)
        interfejsy[nazwa].ob->przywiaz(n);
}

/* --- wl_output: the guest's display --- */
static void w_przywiaz(struct obiekt *o)
{
    zdarzenie(o, 0, "iiiiissi", 0, 0, 0, 0, 0, "K1", "strefa", 0); /* geometry: size unknown, subpixel unknown */
    zdarzenie(o, 1, "uiii", 3u, ekran.szer, ekran.wys, 60000);    /* mode: current | preferred, 60 Hz */
    if (o->wersja >= 2) {
        zdarzenie(o, 3, "i", 1); /* scale */
        zdarzenie(o, 2, "");     /* done */
    }
    o->klient->wyjscie = o;
}

static void w_zwolnij(struct obiekt *o)
{
    if (o->klient->wyjscie == o)
        o->klient->wyjscie = NULL;
}

static const struct zadanie z_display[] = { { "sync", "n", d_sync, 0 }, { "get_registry", "n", d_rejestr, 0 } };
static const struct zadanie z_registry[] = { { "bind", "usun", r_przywiaz, 0 } };
static const struct zadanie z_output[] = { { "release", "", nic, 1 } };
const struct obsluga ob_display = OBSLUGA(z_display, NULL, NULL);
const struct obsluga ob_registry = OBSLUGA(z_registry, NULL, NULL);
const struct obsluga ob_output = OBSLUGA(z_output, w_zwolnij, w_przywiaz);

/* K1 kompozytor — the table of interfaces (K1_INTERFEJSY in kompozytor.h) and the small ones: wl_display,
 * wl_registry, wl_output, the clipboard objects (wl_data_device_manager: accepted, nothing is ever offered — the zone
 * shares no clipboard; 0.11, ADR 0027). */
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
        if (interfejsy[i].globalny)
            zdarzenie(r, 0, "usu", i, interfejsy[i].nazwa, interfejsy[i].wersja);
}

/* --- wl_registry.bind: the global's name is its row in the table --- */
static void r_przywiaz(struct klient *k, struct obiekt *o, union arg *a)
{
    uint32_t nazwa = a[0].u, wersja = a[2].u;
    struct obiekt *n;
    if (nazwa == 0 || nazwa >= IF_LICZBA || !interfejsy[nazwa].globalny ||
        strcmp(a[1].s, interfejsy[nazwa].nazwa) != 0 || wersja == 0 || wersja > interfejsy[nazwa].wersja) {
        blad(k, o, BLAD_ZLY_OBIEKT, "zły obiekt globalny");
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

/* --- wl_data_device_manager, wl_data_source, wl_data_device --- */
static void dm_zrodlo(struct klient *k, struct obiekt *o, union arg *a)
{
    obiekt_nowy(k, a[0].u, IF_DATA_SOURCE, o->wersja);
}

static void dm_urzadzenie(struct klient *k, struct obiekt *o, union arg *a)
{
    if (obiekt_typu(k, a[1].o, IF_SEAT, 0) != NULL)
        obiekt_nowy(k, a[0].u, IF_DATA_DEVICE, o->wersja);
}

static void dd_zaznacz(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)o;
    if (a[0].o != NULL)
        obiekt_typu(k, a[0].o, IF_DATA_SOURCE, 0);
}

static void dd_przeciagnij(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)o;
    if (a[0].o != NULL && obiekt_typu(k, a[0].o, IF_DATA_SOURCE, 0) == NULL)
        return;
    if (obiekt_typu(k, a[1].o, IF_SURFACE, 0) == NULL)
        return;
    if (a[2].o != NULL)
        obiekt_typu(k, a[2].o, IF_SURFACE, 0);
    if (a[0].o != NULL)
        zdarzenie(a[0].o, 2, ""); /* wl_data_source.cancelled: no drag and drop in the zone */
}

static const struct zadanie z_display[] = { { "sync", "n", d_sync, 0 }, { "get_registry", "n", d_rejestr, 0 } };
static const struct zadanie z_registry[] = { { "bind", "usun", r_przywiaz, 0 } };
static const struct zadanie z_output[] = { { "release", "", nic, 1 } };
static const struct zadanie z_dm[] = { { "create_data_source", "n", dm_zrodlo, 0 },
                                       { "get_data_device", "no", dm_urzadzenie, 0 } };
static const struct zadanie z_ds[] = { { "offer", "s", nic, 0 }, { "destroy", "", nic, 1 },
                                       { "set_actions", "u", nic, 0 } };
static const struct zadanie z_dd[] = { { "start_drag", "?oo?ou", dd_przeciagnij, 0 },
                                       { "set_selection", "?ou", dd_zaznacz, 0 }, { "release", "", nic, 1 } };
const struct obsluga ob_display = OBSLUGA(z_display, NULL, NULL);
const struct obsluga ob_registry = OBSLUGA(z_registry, NULL, NULL);
const struct obsluga ob_output = OBSLUGA(z_output, w_zwolnij, w_przywiaz);
const struct obsluga ob_data_manager = OBSLUGA(z_dm, NULL, NULL);
const struct obsluga ob_data_source = OBSLUGA(z_ds, NULL, NULL);
const struct obsluga ob_data_device = OBSLUGA(z_dd, NULL, NULL);

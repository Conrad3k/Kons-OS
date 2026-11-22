/* K1 kompozytor — the data objects of Wayland (0.11.0 step 4, ADR 0052): wl_data_device_manager, wl_data_source,
 * wl_data_device and wl_data_offer. Files the user dropped on the session's window in K1 come here as one line from the
 * agent (otworz.c: "upusc X Y <addresses>" — the copies are in "Z K1" already): the compositor plays the drop at that
 * point for the surface there — an offer of text/uri-list (wl_data_device.data_offer, enter, motion; drop once the client
 * said which actions it takes, or after DROP_MS), and on receive it writes the addresses. A drag started inside the zone
 * (start_drag: a tab, a link) is cancelled at once, as before: nothing leaves the zone that way. The clipboard (0.11.0
 * step 5, schowek.c): a source's text types are noted here (offer), its selection goes to schowek.c, and K1's text is
 * offered as a selection of ours. The objects we make (wl_data_offer) have ids from SERWEROWE_ID (protokol.c). */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kompozytor.h"

#define DROP_MS 600     /* the drop at the latest so long after enter */
#define GOTOWE_MS 40    /* after the client's set_actions */
#define AKCJA_KOPIA 1u  /* wl_data_device_manager.dnd_action.copy */
#define URI_LISTA "text/uri-list"

struct oferta {
    char *tresc;        /* what receive writes */
    size_t dl;
    int tekst;          /* 0.11.0 step 5: K1's text as a selection (else files dropped: text/uri-list) */
};
struct zrodlo {
    const char *tekst;  /* the best text type the browser's source offers (NULL: none) */
};

static struct {
    int etap;           /* 0: none, 1: entered (the drop waits), 2: dropped */
    struct obiekt *oferta, *urz;
    uint32_t od, gotowe; /* teraz_ms at enter, at set_actions (0: not yet) */
} up;

static void nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

/* --- wl_data_device_manager --- */
static void dm_zrodlo(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_DATA_SOURCE, o->wersja);
    if (n != NULL)
        n->dane = calloc(1, sizeof(struct zrodlo));
}

/* --- wl_data_source: what the browser offers (0.11.0 step 5: its text types) --- */
static void ds_oferuje(struct klient *k, struct obiekt *o, union arg *a)
{
    struct zrodlo *z = o->dane;
    (void)k;
    if (z != NULL && schowek_typ_tekstu(a[0].s) && (z->tekst == NULL || strcmp(a[0].s, "text/plain;charset=utf-8") == 0))
        z->tekst = strcmp(a[0].s, "text/plain;charset=utf-8") == 0 ? "text/plain;charset=utf-8"
                   : strcmp(a[0].s, "UTF8_STRING") == 0              ? "UTF8_STRING"
                                                                      : "text/plain";
}

static void ds_zwolnij(struct obiekt *o)
{
    schowek_zrodlo_znika(o);
    free(o->dane);
    o->dane = NULL;
}

static void dm_urzadzenie(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n;
    if (obiekt_typu(k, a[1].o, IF_SEAT, 0) == NULL || (n = obiekt_nowy(k, a[0].u, IF_DATA_DEVICE, o->wersja)) == NULL)
        return;
    for (unsigned i = 0; i < URZ_KLIENTA; i++)
        if (k->urz_danych[i] == NULL) {
            k->urz_danych[i] = n;
            return;
        }
}

/* --- wl_data_device --- */
/* set_selection: only the client with the keys may (as in the protocol); its text goes to K1 (schowek.c). */
static void dd_zaznacz(struct klient *k, struct obiekt *o, union arg *a)
{
    const struct zrodlo *z;
    (void)o;
    if (a[0].o != NULL && obiekt_typu(k, a[0].o, IF_DATA_SOURCE, 0) == NULL)
        return;
    if (k != wejscie_klient_klawiatury()) {
        if (a[0].o != NULL)
            zdarzenie(a[0].o, 2, ""); /* cancelled */
        return;
    }
    z = a[0].o != NULL ? a[0].o->dane : NULL;
    schowek_zrodlo(a[0].o, z != NULL ? z->tekst : NULL);
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
        zdarzenie(a[0].o, 2, ""); /* wl_data_source.cancelled: no drag and drop out of the zone's own windows */
}

static void dd_zwolnij(struct obiekt *o)
{
    for (unsigned i = 0; i < URZ_KLIENTA; i++)
        if (o->klient->urz_danych[i] == o)
            o->klient->urz_danych[i] = NULL;
    if (up.urz == o)
        up.urz = NULL;
}

/* --- wl_data_offer: what we offer the client --- */
static void of_odbierz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct oferta *of = o->dane;
    (void)k;
    if (of != NULL && of->tekst && schowek_typ_tekstu(a[0].s)) { /* 0.11.0 step 5: K1's text */
        schowek_daj(a[1].h, of->tresc, of->dl);
        return;
    }
    if (of != NULL && !of->tekst && strcmp(a[0].s, URI_LISTA) == 0 && fcntl(a[1].h, F_SETFL, O_NONBLOCK) == 0 &&
        write(a[1].h, of->tresc, of->dl) == (ssize_t)of->dl)
        kloguj("upuszczenie: przeglądarka odebrała adresy");
    close(a[1].h);
}

static void of_akcje(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k;
    if ((a[0].u & AKCJA_KOPIA) == 0)
        return;
    zdarzenie(o, 2, "u", AKCJA_KOPIA); /* wl_data_offer.action: copy — the only one (the files stay in Pliki too) */
    if (up.oferta == o && up.gotowe == 0)
        up.gotowe = teraz_ms() | 1u;
}

static void of_koniec(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)a;
    if (up.oferta == o)
        kloguj("upuszczenie: przeglądarka przyjęła pliki");
}

static void of_zwolnij(struct obiekt *o)
{
    struct oferta *of = o->dane;
    if (of != NULL)
        free(of->tresc);
    free(of);
    o->dane = NULL;
    if (up.oferta == o)
        up.etap = 0, up.oferta = NULL, up.urz = NULL;
}

void dane_upusc(int32_t x, int32_t y, const char *adresy)
{
    int32_t lx, ly;
    struct powierzchnia *p = scena_pod(x, y, &lx, &ly);
    struct klient *k = p != NULL ? p->o->klient : NULL;
    struct obiekt *u = NULL, *o = NULL;
    struct oferta *of;
    for (unsigned i = 0; k != NULL && i < URZ_KLIENTA && u == NULL; i++)
        u = k->urz_danych[i];
    if (u == NULL) {
        kloguj("upuszczenie: pod punktem nie ma okna, które przyjmuje pliki");
        return;
    }
    if (up.etap == 1 && up.urz != NULL)
        zdarzenie(up.urz, 2, ""); /* wl_data_device.leave: the drop before it did not happen */
    up.etap = 0, up.oferta = NULL;
    of = calloc(1, sizeof *of);
    if (of != NULL && (of->tresc = strdup(adresy)) == NULL)
        free(of), of = NULL;
    if (of == NULL || (o = obiekt_serwera(k, IF_DATA_OFFER, u->wersja)) == NULL) {
        if (of != NULL)
            free(of->tresc), free(of);
        kloguj("upuszczenie: brak miejsca na ofertę");
        return;
    }
    of->dl = strlen(adresy);
    o->dane = of;
    zdarzenie(u, 0, "u", o->id);                /* wl_data_device.data_offer: the new object */
    zdarzenie(o, 0, "s", URI_LISTA);            /* wl_data_offer.offer */
    if (o->wersja >= 3)
        zdarzenie(o, 1, "u", AKCJA_KOPIA);      /* wl_data_offer.source_actions */
    zdarzenie(u, 1, "uoffo", kolejny_serial(), p->o, lx * 256, ly * 256, o); /* enter */
    zdarzenie(u, 3, "uff", teraz_ms(), lx * 256, ly * 256);                 /* motion */
    up.etap = 1, up.oferta = o, up.urz = u, up.od = teraz_ms(), up.gotowe = 0;
    kloguj("upuszczenie: oferta plików dla okna przeglądarki");
}

/* 0.11.0 step 5: K1's text (Ctrl+V coming) — an offer of text as the selection of the client with the keys. */
int dane_zaznaczenie(const uint8_t *t, size_t n)
{
    static const char *const typy[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING" };
    struct klient *k = wejscie_klient_klawiatury();
    struct obiekt *u = NULL, *o = NULL;
    struct oferta *of;
    for (unsigned i = 0; k != NULL && i < URZ_KLIENTA && u == NULL; i++)
        u = k->urz_danych[i];
    if (u == NULL || (of = calloc(1, sizeof *of)) == NULL)
        return -1;
    if ((of->tresc = malloc(n)) == NULL || (o = obiekt_serwera(k, IF_DATA_OFFER, u->wersja)) == NULL) {
        free(of->tresc), free(of);
        return -1;
    }
    memcpy(of->tresc, t, n);
    of->dl = n, of->tekst = 1;
    o->dane = of;
    zdarzenie(u, 0, "u", o->id);                /* data_offer */
    for (size_t i = 0; i < sizeof typy / sizeof typy[0]; i++)
        zdarzenie(o, 0, "s", typy[i]);          /* offer */
    zdarzenie(u, 5, "o", o);                    /* wl_data_device.selection */
    return 0;
}

int dane_czekaj(void)
{
    uint32_t t = teraz_ms(), kiedy;
    if (up.etap != 1)
        return -1;
    kiedy = up.gotowe != 0 && up.gotowe + GOTOWE_MS < up.od + DROP_MS ? up.gotowe + GOTOWE_MS : up.od + DROP_MS;
    return (int32_t)(kiedy - t) > 0 ? (int)(kiedy - t) : 0;
}

void dane_czas(void)
{
    if (up.etap != 1 || dane_czekaj() > 0)
        return;
    up.etap = 2;
    if (up.urz != NULL)
        zdarzenie(up.urz, 4, ""); /* wl_data_device.drop */
    kloguj("upuszczenie: %s", up.gotowe != 0 ? "upuszczone" : "upuszczone bez odpowiedzi okna");
}

static const struct zadanie z_dm[] = { { "create_data_source", "n", dm_zrodlo, 0 },
                                       { "get_data_device", "no", dm_urzadzenie, 0 } };
static const struct zadanie z_ds[] = { { "offer", "s", ds_oferuje, 0 }, { "destroy", "", nic, 1 },
                                       { "set_actions", "u", nic, 0 } };
static const struct zadanie z_dd[] = { { "start_drag", "?oo?ou", dd_przeciagnij, 0 },
                                       { "set_selection", "?ou", dd_zaznacz, 0 }, { "release", "", nic, 1 } };
static const struct zadanie z_of[] = { { "accept", "u?s", nic, 0 }, { "receive", "sh", of_odbierz, 0 },
                                       { "destroy", "", nic, 1 }, { "finish", "", of_koniec, 0 },
                                       { "set_actions", "uu", of_akcje, 0 } };
const struct obsluga ob_data_manager = OBSLUGA(z_dm, NULL, NULL);
const struct obsluga ob_data_source = OBSLUGA(z_ds, ds_zwolnij, NULL);
const struct obsluga ob_data_device = OBSLUGA(z_dd, dd_zwolnij, NULL);
const struct obsluga ob_data_offer = OBSLUGA(z_of, of_zwolnij, NULL);

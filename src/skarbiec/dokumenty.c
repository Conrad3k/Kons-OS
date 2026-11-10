/* K1 skarbiec — the user's files and folders, Kosz (0.8.0, ADR 0029). Format and rules: dokumenty.h.
 * Names and content never reach the log — only numbers and sizes. Part of skarbiec (skarbiec.h). */
#include "dokumenty.h"

#include <stdio.h>
#include <string.h>

#include "k1_log.h"
#include "skarbiec.h"

#define WL K1_WL_DOKUMENTY
#define DZIEN 86400

static struct k1_dok_wpis wpisy[K1_DOK_MAKS];
static int n_wpisow;
static uint32_t nastepny = K1_DOK_PIERWSZY;
static uint8_t kawalek[K1_DOK_KAWALEK];

/* ---- looking ---- */

static int indeks(uint32_t id)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].id == id)
            return i;
    return -1;
}

const struct k1_dok_wpis *dok_wpis(uint32_t id)
{
    int i = indeks(id);
    return i < 0 ? NULL : &wpisy[i];
}

static int staly(uint32_t id)
{
    return id >= 1 && id <= K1_FOLDERY_MAKS;
}

int dok_widoczny(uint32_t id)
{
    for (int krok = 0; krok <= K1_DOK_GLEBOKOSC + 1 && !staly(id); krok++) {
        const struct k1_dok_wpis *w = dok_wpis(id);
        if (w == NULL || w->kosz != 0)
            return 0;
        id = w->rodzic;
    }
    return staly(id);
}

/* A folder that can take entries: a standard one, or a visible user folder. */
static int folder_otwarty(uint32_t id)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    return staly(id) || (w != NULL && w->rodzaj == K1_DOK_FOLDER && dok_widoczny(id));
}

/* User folders above `id` (a standard folder: 0). */
static int glebokosc(uint32_t id)
{
    int g = 0;
    while (!staly(id) && g <= K1_DOK_GLEBOKOSC) {
        const struct k1_dok_wpis *w = dok_wpis(id);
        if (w == NULL)
            break;
        g++;
        id = w->rodzic;
    }
    return g;
}

/* Whether `id` is `przodek` or lies inside it. */
static int wewnatrz(uint32_t id, uint32_t przodek)
{
    for (int krok = 0; krok <= K1_DOK_GLEBOKOSC + 1; krok++) {
        const struct k1_dok_wpis *w;
        if (id == przodek)
            return 1;
        w = dok_wpis(id);
        if (w == NULL)
            return 0;
        id = w->rodzic;
    }
    return 0;
}

/* Levels of folders in `id` itself and below it (a file: 0, an empty folder: 1). */
static int wysokosc(uint32_t id)
{
    int h = 0;
    const struct k1_dok_wpis *w = dok_wpis(id);
    if (w == NULL || w->rodzaj != K1_DOK_FOLDER)
        return 0;
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].rodzic == id && wpisy[i].rodzaj == K1_DOK_FOLDER) {
            int d = wysokosc(wpisy[i].id);
            if (d > h)
                h = d;
        }
    return h + 1;
}

/* A visible entry other than `oprocz` named `nazwa` in `folder`. */
static int nazwa_zajeta(uint32_t folder, const char *nazwa, uint32_t oprocz)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].rodzic == folder && wpisy[i].kosz == 0 && wpisy[i].id != oprocz &&
            k1_dok_nazwy_rowne(wpisy[i].nazwa, nazwa))
            return 1;
    return 0;
}

uint32_t dok_w_koszu(void)
{
    uint32_t n = 0;
    for (int i = 0; i < n_wpisow; i++)
        n += wpisy[i].kosz != 0;
    return n;
}

int dok_wybierz(int (*wybierz)(const struct k1_dok_wpis *w), struct k1_dok_wpis *out)
{
    int n = 0;
    for (int i = 0; i < n_wpisow; i++)
        if (wybierz(&wpisy[i]))
            out[n++] = wpisy[i];
    return n;
}

/* ---- objects ---- */

static void nazwa_wpisu(uint32_t id, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "m%u", id);
}

static void nazwa_kawalka(uint32_t id, uint32_t gen, uint32_t k, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "t%ug%uk%u", id, gen, k);
}

static uint32_t kawalkow(uint64_t rozmiar)
{
    return (uint32_t)((rozmiar + K1_DOK_KAWALEK - 1) / K1_DOK_KAWALEK);
}

static int zapisz_wpis(const struct k1_dok_wpis *w)
{
    uint8_t b[K1_DOK_WPIS_BAJTY];
    char n[K1_MAG_NAZWA];
    k1_dok_wpis_zapisz(w, b);
    nazwa_wpisu(w->id, n);
    return ob_zapisz(WL, n, b, sizeof b);
}

static void usun_kawalki(uint32_t id, uint32_t gen, uint32_t ile)
{
    char n[K1_MAG_NAZWA];
    for (uint32_t k = 0; k < ile; k++) {
        nazwa_kawalka(id, gen, k, n);
        ob_usun(WL, n);
    }
}

/* Writes the content of `z` as pieces of generation `gen` of entry `id`; *typ = its type. On failure the
 * pieces already written are deleted. */
static int zapisz_kawalki(uint32_t id, uint32_t gen, const struct dok_zrodlo *z, uint8_t *typ)
{
    uint32_t ile = kawalkow(z->dl), k;
    char n[K1_MAG_NAZWA];
    *typ = K1_TYP_TEKST;
    for (k = 0; k < ile; k++) {
        uint64_t od = (uint64_t)k * K1_DOK_KAWALEK;
        uint32_t dl = (uint32_t)(z->dl - od < K1_DOK_KAWALEK ? z->dl - od : K1_DOK_KAWALEK);
        int r = z->czytaj(z->ctx, od, kawalek, dl) == 0 ? K1_MAG_OK : K1_MAG_ARG;
        if (r == K1_MAG_OK && k == 0)
            *typ = (uint8_t)k1_dok_typ(kawalek, dl, z->dl);
        nazwa_kawalka(id, gen, k, n);
        if (r == K1_MAG_OK)
            r = ob_zapisz(WL, n, kawalek, dl);
        if (r != K1_MAG_OK) {
            usun_kawalki(id, gen, k);
            return r;
        }
    }
    return K1_MAG_OK;
}

/* ---- changes ---- */

/* `nazwa` made free in `folder`: "nazwa (2)", "nazwa (3)"… before the extension, as Windows does. */
static int wolna_nazwa(uint32_t folder, const char *nazwa, uint32_t oprocz, char out[K1_DOK_NAZWA])
{
    const char *kropka = strrchr(nazwa, '.');
    size_t baza = kropka != NULL && kropka != nazwa ? (size_t)(kropka - nazwa) : strlen(nazwa);
    snprintf(out, K1_DOK_NAZWA, "%s", nazwa);
    for (int i = 2; nazwa_zajeta(folder, out, oprocz); i++) {
        char dop[16];
        size_t d, b = baza;
        if (i > K1_DOK_MAKS + 1)
            return K1_DOK_BLAD_ZAJETA;
        d = (size_t)snprintf(dop, sizeof dop, " (%d)", i);
        while (b > 0 && b + d + strlen(nazwa + baza) >= K1_DOK_NAZWA)
            b--;
        while (b > 0 && ((unsigned char)nazwa[b] & 0xc0) == 0x80) /* never cut a letter in half */
            b--;
        snprintf(out, K1_DOK_NAZWA, "%.*s%s%s", (int)b, nazwa, dop, nazwa + baza);
    }
    return k1_dok_nazwa_poprawna(out, strlen(out)) ? 0 : K1_DOK_BLAD_ZAJETA;
}

static int sprawdz_nazwe(const char *nazwa, size_t nl, char out[K1_DOK_NAZWA])
{
    if (!k1_dok_nazwa_poprawna(nazwa, nl))
        return K1_DOK_BLAD_NAZWA;
    memset(out, 0, K1_DOK_NAZWA);
    memcpy(out, nazwa, nl);
    return 0;
}

int dok_nowy(uint32_t folder, int rodzaj, const char *nazwa, size_t nl, const struct dok_zrodlo *z, int tworca,
             int pochodzenie, int64_t teraz, uint32_t *id)
{
    struct k1_dok_wpis w;
    char n[K1_DOK_NAZWA];
    int r;
    *id = 0;
    if ((r = sprawdz_nazwe(nazwa, nl, n)) != 0)
        return r;
    if (!folder_otwarty(folder) || (rodzaj == K1_DOK_FOLDER && glebokosc(folder) >= K1_DOK_GLEBOKOSC))
        return K1_DOK_BLAD_MIEJSCE;
    if (n_wpisow == K1_DOK_MAKS || nastepny > K1_DOK_ID_MAKS)
        return K1_DOK_BLAD_PELNO;
    memset(&w, 0, sizeof w);
    w.id = nastepny;
    w.rodzic = folder;
    w.rodzaj = (uint8_t)rodzaj;
    w.pochodzenie = (uint8_t)pochodzenie;
    w.tworca = (uint8_t)tworca;
    w.utworzono = w.zmieniono = teraz;
    if ((r = wolna_nazwa(folder, n, 0, w.nazwa)) != 0)
        return r;
    if (rodzaj == K1_DOK_PLIK) {
        w.generacja = 1;
        w.rozmiar = z != NULL ? z->dl : 0;
        w.typ = K1_TYP_TEKST;
        if (z != NULL && (r = zapisz_kawalki(w.id, 1, z, &w.typ)) != 0)
            return r;
    }
    if ((r = zapisz_wpis(&w)) != 0) {
        usun_kawalki(w.id, w.generacja, kawalkow(w.rozmiar));
        return r;
    }
    wpisy[n_wpisow++] = w;
    nastepny++;
    *id = w.id;
    return 0;
}

int dok_zapisz_tresc(uint32_t id, const struct dok_zrodlo *z, int64_t teraz)
{
    int i = indeks(id), r;
    struct k1_dok_wpis w;
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    if (wpisy[i].rodzaj != K1_DOK_PLIK || !dok_widoczny(id))
        return K1_DOK_BLAD_MIEJSCE;
    w = wpisy[i];
    w.generacja++;
    w.rozmiar = z->dl;
    w.zmieniono = teraz;
    if ((r = zapisz_kawalki(id, w.generacja, z, &w.typ)) != 0)
        return r;
    if ((r = zapisz_wpis(&w)) != 0) {
        usun_kawalki(id, w.generacja, kawalkow(w.rozmiar));
        return r;
    }
    usun_kawalki(id, wpisy[i].generacja, kawalkow(wpisy[i].rozmiar));
    wpisy[i] = w;
    return 0;
}

int dok_czytaj_tresc(uint32_t id, uint64_t maks, const struct dok_cel *c, uint64_t *ile)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    char n[K1_MAG_NAZWA];
    uint64_t od = 0, do_ = maks;
    *ile = 0;
    if (w == NULL)
        return K1_DOK_BLAD_BRAK;
    if (w->rodzaj != K1_DOK_PLIK)
        return K1_DOK_BLAD_MIEJSCE;
    if (do_ > w->rozmiar)
        do_ = w->rozmiar;
    for (uint32_t k = 0; od < do_; k++) {
        uint32_t dl;
        int r;
        nazwa_kawalka(id, w->generacja, k, n);
        r = ob_czytaj(WL, n, kawalek, K1_DOK_KAWALEK, &dl);
        if (r != K1_MAG_OK)
            return r;
        if (dl != (w->rozmiar - od < K1_DOK_KAWALEK ? w->rozmiar - od : K1_DOK_KAWALEK))
            return K1_MAG_USZKODZONY;
        if (dl > do_ - od)
            dl = (uint32_t)(do_ - od);
        if (c->pisz(c->ctx, kawalek, dl) != 0)
            return K1_MAG_IO;
        od += dl;
    }
    *ile = od;
    return 0;
}

/* Common checks of a change to a user entry: it exists and is visible. */
static int zmienialny(uint32_t id)
{
    int i = indeks(id);
    if (i < 0)
        return -1;
    return dok_widoczny(id) ? i : -1;
}

int dok_zmien_nazwe(uint32_t id, const char *nazwa, size_t nl, int wolna, int64_t teraz)
{
    int i = zmienialny(id), r;
    struct k1_dok_wpis w;
    char n[K1_DOK_NAZWA];
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    w = wpisy[i];
    if ((r = sprawdz_nazwe(nazwa, nl, n)) != 0)
        return r;
    if (wolna) { /* "Lista (2)" when "Lista" is taken */
        if ((r = wolna_nazwa(w.rodzic, n, id, w.nazwa)) != 0)
            return r;
    } else if (nazwa_zajeta(w.rodzic, n, id)) {
        return K1_DOK_BLAD_ZAJETA;
    } else {
        memcpy(w.nazwa, n, sizeof n);
    }
    w.zmieniono = teraz;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

int dok_folder_aplikacji(const char *nazwa, int tworca, int64_t teraz, uint32_t *id)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].rodzic == K1_FOLDER_DOKUMENTY && wpisy[i].rodzaj == K1_DOK_FOLDER && wpisy[i].kosz == 0 &&
            k1_dok_nazwy_rowne(wpisy[i].nazwa, nazwa)) {
            *id = wpisy[i].id;
            return 0;
        }
    return dok_nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, nazwa, strlen(nazwa), NULL, tworca, K1_POCH_K1, teraz, id);
}

int dok_przenies(uint32_t id, uint32_t folder, int64_t teraz)
{
    int i = zmienialny(id), r;
    struct k1_dok_wpis w;
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    if (!folder_otwarty(folder) || wewnatrz(folder, id) || glebokosc(folder) + wysokosc(id) > K1_DOK_GLEBOKOSC)
        return K1_DOK_BLAD_MIEJSCE;
    if (wpisy[i].rodzic == folder)
        return 0;
    if (nazwa_zajeta(folder, wpisy[i].nazwa, id))
        return K1_DOK_BLAD_ZAJETA;
    w = wpisy[i];
    w.rodzic = folder;
    w.zmieniono = teraz;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

int dok_do_kosza(uint32_t id, int64_t teraz)
{
    int i = zmienialny(id), r;
    struct k1_dok_wpis w;
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    w = wpisy[i];
    w.kosz = teraz > 0 ? teraz : 1;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

int dok_przywroc(uint32_t id)
{
    int i = indeks(id), r;
    struct k1_dok_wpis w;
    if (i < 0 || wpisy[i].kosz == 0)
        return K1_DOK_BLAD_BRAK;
    w = wpisy[i];
    w.kosz = 0;
    if (!folder_otwarty(w.rodzic) || glebokosc(w.rodzic) + wysokosc(id) > K1_DOK_GLEBOKOSC)
        w.rodzic = K1_FOLDER_DOKUMENTY; /* its folder is gone (or in Kosz itself) */
    if ((r = wolna_nazwa(w.rodzic, wpisy[i].nazwa, id, w.nazwa)) != 0)
        return r;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

/* Deletes entry `i` for good: its content, then the entry. */
static int usun_wpis(int i)
{
    char n[K1_MAG_NAZWA];
    int r;
    nazwa_wpisu(wpisy[i].id, n);
    if ((r = ob_usun(WL, n)) != K1_MAG_OK && r != K1_MAG_BRAK)
        return r;
    if (wpisy[i].rodzaj == K1_DOK_PLIK)
        usun_kawalki(wpisy[i].id, wpisy[i].generacja, kawalkow(wpisy[i].rozmiar));
    wpisy[i] = wpisy[--n_wpisow];
    return 0;
}

/* `id` and everything inside it, the deepest first (a power loss in between leaves its folder intact). */
static int usun_drzewo(uint32_t id)
{
    int i, r;
    for (i = 0; i < n_wpisow;) {
        if (wpisy[i].rodzic == id) {
            if ((r = usun_drzewo(wpisy[i].id)) != 0)
                return r;
            i = 0; /* the table changed */
            continue;
        }
        i++;
    }
    i = indeks(id);
    return i < 0 ? 0 : usun_wpis(i);
}

int dok_usun_na_zawsze(uint32_t id)
{
    int i = indeks(id);
    if (i < 0 || wpisy[i].kosz == 0)
        return K1_DOK_BLAD_BRAK;
    return usun_drzewo(id);
}

int dok_oproznij_kosz(void)
{
    for (int i = 0; i < n_wpisow;) {
        if (wpisy[i].kosz != 0) {
            int r = usun_drzewo(wpisy[i].id);
            if (r != 0)
                return r;
            i = 0;
            continue;
        }
        i++;
    }
    return 0;
}

int dok_przeterminowane(int64_t teraz)
{
    int n = 0;
    for (int i = 0; i < n_wpisow;) {
        int64_t k = wpisy[i].kosz;
        /* a clock earlier than the moment it went to Kosz (moved back): keep it */
        if (k != 0 && teraz >= k && teraz - k >= (int64_t)K1_KOSZ_DNI * DZIEN) {
            if (usun_drzewo(wpisy[i].id) != 0)
                break;
            n++;
            i = 0;
            continue;
        }
        i++;
    }
    return n;
}

/* ---- loading ---- */

static void wczytaj_wpis(const char *nazwa, void *ctx)
{
    uint8_t b[K1_DOK_WPIS_BAJTY + 1];
    char oczekiwana[K1_MAG_NAZWA];
    struct k1_dok_wpis w;
    uint32_t dl;
    int *zle = ctx;
    if (nazwa[0] != 'm')
        return;
    if (n_wpisow == K1_DOK_MAKS || ob_czytaj(WL, nazwa, b, sizeof b, &dl) != K1_MAG_OK || dl != K1_DOK_WPIS_BAJTY ||
        k1_dok_wpis_czytaj(b, &w) != 0 || (nazwa_wpisu(w.id, oczekiwana), strcmp(oczekiwana, nazwa) != 0) ||
        indeks(w.id) >= 0) {
        (*zle)++;
        return;
    }
    wpisy[n_wpisow++] = w;
    if (w.id >= nastepny)
        nastepny = w.id + 1;
}

/* Pieces of content no entry points to (a write cut off): deleted. */
static void sprzataj_kawalek(const char *nazwa, void *ctx)
{
    unsigned id, gen, k;
    char koniec;
    const struct k1_dok_wpis *w;
    int *ile = ctx;
    if (nazwa[0] != 't')
        return;
    w = sscanf(nazwa, "t%ug%uk%u%c", &id, &gen, &k, &koniec) == 3 ? dok_wpis(id) : NULL;
    if (w == NULL || w->rodzaj != K1_DOK_PLIK || w->generacja != gen || k >= kawalkow(w->rozmiar)) {
        ob_usun(WL, nazwa);
        (*ile)++;
    }
}

/* An entry whose folder is missing, is a file, or makes a loop (changed data): it goes to Dokumenty. */
static int napraw_drzewo(void)
{
    int n = 0;
    for (int i = 0; i < n_wpisow; i++) {
        const struct k1_dok_wpis *r = dok_wpis(wpisy[i].rodzic);
        if (staly(wpisy[i].rodzic) || (r != NULL && r->rodzaj == K1_DOK_FOLDER && !wewnatrz(wpisy[i].rodzic, wpisy[i].id) &&
                                       glebokosc(wpisy[i].rodzic) + (wpisy[i].rodzaj == K1_DOK_FOLDER) <= K1_DOK_GLEBOKOSC))
            continue;
        wpisy[i].rodzic = K1_FOLDER_DOKUMENTY;
        zapisz_wpis(&wpisy[i]);
        n++;
    }
    return n;
}

void dok_wczytaj(int64_t teraz)
{
    int zle = 0, sieroty = 0, kawalki = 0, stare;
    n_wpisow = 0;
    nastepny = K1_DOK_PIERWSZY;
    ob_kazdy(WL, wczytaj_wpis, &zle);
    sieroty = napraw_drzewo();
    ob_kazdy(WL, sprzataj_kawalek, &kawalki);
    stare = dok_przeterminowane(teraz);
    k1_log(TAG, "pliki: %d wpisów (%u w Koszu)%s", n_wpisow, dok_w_koszu(),
           zle || sieroty || kawalki || stare ? " — sprzątanie przy otwarciu" : "");
    if (zle || sieroty || kawalki || stare)
        k1_log(TAG, "pliki: odrzucone wpisy %d, przeniesione do Dokumentów %d, kawałki bez wpisu %d, z Kosza po %d dniach %d",
               zle, sieroty, kawalki, K1_KOSZ_DNI, stare);
}

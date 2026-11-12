/* K1 skarbiec — the user's files and folders (0.8.0, ADR 0029): the entries in memory, their objects, new files and
 * changes. Format and rules: dokumenty.h; the modules: dok_wew.h. Names and content never reach the log — only
 * numbers and sizes. Part of skarbiec (skarbiec.h). */
#include "dokumenty.h"

#include <stdio.h>
#include <string.h>

#include "dok_wew.h"
#include "k1_bajty.h"
#include "skarbiec.h"

struct k1_dok_wpis wpisy[K1_DOK_MAKS];
int n_wpisow;
uint32_t nastepny = K1_DOK_PIERWSZY;
static uint8_t kawalek[K1_DOK_KAWALEK];

/* A whole content from `z` through a write in progress (dok_zapis.c); *id = the file. */
static int caly(struct dok_zapis *zp, const struct dok_zrodlo *z, int64_t teraz, uint32_t *id)
{
    for (uint64_t od = 0; od < z->dl; od += K1_DOK_KAWALEK) {
        uint32_t dl = (uint32_t)(z->dl - od < K1_DOK_KAWALEK ? z->dl - od : K1_DOK_KAWALEK);
        int r = z->czytaj(z->ctx, od, kawalek, dl) == 0 ? dok_zapis_kawalek(zp, kawalek, dl) : K1_MAG_ARG;
        if (r != 0) {
            dok_zapis_porzuc(zp);
            return r;
        }
    }
    return dok_zapis_koniec(zp, teraz, id);
}

/* ---- objects ---- */

void nazwa_wpisu(uint32_t id, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "m%u", id);
}

void nazwa_kawalka(uint32_t id, uint32_t gen, uint32_t k, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "t%ug%uk%u", id, gen, k);
}

uint32_t kawalkow(uint64_t rozmiar)
{
    return (uint32_t)((rozmiar + K1_DOK_KAWALEK - 1) / K1_DOK_KAWALEK);
}

int zapisz_wpis(const struct k1_dok_wpis *w)
{
    uint8_t b[K1_DOK_WPIS_BAJTY];
    char n[K1_MAG_NAZWA];
    k1_dok_wpis_zapisz(w, b);
    nazwa_wpisu(w->id, n);
    return ob_zapisz(WL, n, b, sizeof b);
}

void usun_kawalki(uint32_t id, uint32_t gen, uint32_t ile)
{
    char n[K1_MAG_NAZWA];
    for (uint32_t k = 0; k < ile; k++) {
        nazwa_kawalka(id, gen, k, n);
        ob_usun(WL, n);
    }
}

/* `nazwa` made free in `folder`: "nazwa (2)", "nazwa (3)"… before the extension, as Windows does. A long name is cut
 * before " (n)": its base, or all of it when the "extension" is longer than K1_DOK_KONCOWKA (0.8.2 step 2, L8: a base
 * never shrinks to nothing — "a.<125 × x>" got no free name before). */
int wolna_nazwa(uint32_t folder, const char *nazwa, uint32_t oprocz, char out[K1_DOK_NAZWA])
{
    const char *kropka = strrchr(nazwa, '.');
    size_t n = strlen(nazwa), baza = n;
    if (kropka != NULL && kropka != nazwa && n - (size_t)(kropka - nazwa) <= K1_DOK_KONCOWKA)
        baza = (size_t)(kropka - nazwa);
    snprintf(out, K1_DOK_NAZWA, "%s", nazwa);
    for (int i = 2; nazwa_zajeta(folder, out, oprocz); i++) {
        char dop[16];
        size_t d, b = baza;
        if (i > K1_DOK_MAKS + 1)
            return K1_DOK_BLAD_ZAJETA;
        d = (size_t)snprintf(dop, sizeof dop, " (%d)", i);
        while (b > 1 && b + d + (n - baza) >= K1_DOK_NAZWA)
            b--;
        while (b > 1 && ((unsigned char)nazwa[b] & 0xc0) == 0x80) /* never cut a letter in half */
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

int dok_ile(void)
{
    return n_wpisow;
}

#define REZERWA_MIN (8u << 20)

/* What files leave for the system (0.8.1, plan L6): 8 MiB, or 2 % of the vault when more — but at most a quarter of
 * it (a very small K1-DANE, e.g. in a virtual machine, still takes files). */
uint64_t dok_rezerwa(void)
{
    uint64_t calosc = ob_pojemnosc(), r = calosc / 50 > REZERWA_MIN ? calosc / 50 : REZERWA_MIN;
    return r > calosc / 4 ? calosc / 4 : r;
}

/* Room for `bajty` of new content: files never take the reserve — kept for the system (settings, the desktop's
 * layout, the wallpaper, the battery's history), which would stop saving otherwise. */
int dok_miejsce_na(uint64_t bajty)
{
    uint64_t rezerwa = dok_rezerwa();
    uint64_t potrzeba = bajty + (kawalkow(bajty) + 1) * K1_MAG_BLOK; /* the records' headers, the entry */
    return ob_wolne() >= potrzeba + rezerwa;
}

int dok_nowy(uint32_t folder, int rodzaj, const char *nazwa, size_t nl, const struct dok_zrodlo *z, int tworca,
             int pochodzenie, int64_t teraz, uint32_t *id)
{
    struct k1_dok_wpis w;
    struct dok_zapis zp;
    char n[K1_DOK_NAZWA];
    int r;
    *id = 0;
    if ((r = sprawdz_nazwe(nazwa, nl, n)) != 0)
        return r;
    if (rodzaj == K1_DOK_PLIK) { /* the content through a write in progress (dok_zapis.c), also when empty */
        static const struct dok_zrodlo pusty = { NULL, NULL, 0 };
        if (!dok_miejsce_na(z != NULL ? z->dl : 0))
            return K1_DOK_BLAD_BRAK_MIEJSCA;
        if ((r = dok_zapis_nowy(&zp, folder, n, nl, tworca, pochodzenie)) != 0)
            return r;
        return caly(&zp, z != NULL ? z : &pusty, teraz, id);
    }
    if (!folder_otwarty(folder) || glebokosc(folder) >= K1_DOK_GLEBOKOSC)
        return K1_DOK_BLAD_MIEJSCE;
    if (n_wpisow == K1_DOK_MAKS || nastepny > K1_DOK_ID_MAKS)
        return K1_DOK_BLAD_PELNO;
    if (!dok_miejsce_na(0))
        return K1_DOK_BLAD_BRAK_MIEJSCA;
    memset(&w, 0, sizeof w);
    w.id = nastepny;
    w.rodzic = folder;
    w.rodzaj = (uint8_t)rodzaj;
    w.pochodzenie = (uint8_t)pochodzenie;
    w.tworca = (uint8_t)tworca;
    w.utworzono = w.zmieniono = teraz;
    if ((r = wolna_nazwa(folder, n, 0, w.nazwa)) != 0 || (r = zapisz_wpis(&w)) != 0)
        return r;
    dok_dodaj(&w);
    nastepny++;
    *id = w.id;
    return 0;
}

int dok_zapisz_tresc(uint32_t id, const struct dok_zrodlo *z, int64_t teraz)
{
    struct dok_zapis zp;
    uint32_t ten;
    int r;
    if ((r = dok_zapis_tresci(&zp, id)) != 0)
        return r;
    if (!dok_miejsce_na(z->dl)) {
        dok_zapis_porzuc(&zp);
        return K1_DOK_BLAD_BRAK_MIEJSCA;
    }
    return caly(&zp, z, teraz, &ten);
}

int dok_czytaj_tresc(uint32_t id, uint32_t od_kawalka, uint64_t maks, const struct dok_cel *c, uint64_t *ile)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    char n[K1_MAG_NAZWA];
    uint64_t od = (uint64_t)od_kawalka * K1_DOK_KAWALEK, do_;
    *ile = 0;
    if (w == NULL)
        return K1_DOK_BLAD_BRAK;
    if (w->rodzaj != K1_DOK_PLIK)
        return K1_DOK_BLAD_MIEJSCE;
    if (od > w->rozmiar) /* past the end (the end itself: nothing, e.g. an empty file) */
        return K1_MAG_ARG;
    do_ = w->rozmiar - od < maks ? w->rozmiar : od + maks;
    for (uint32_t k = od_kawalka; od < do_; k++) {
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
        *ile += dl;
    }
    return 0;
}

/* Common checks of a change to a user entry: it exists and is visible. */
int zmienialny(uint32_t id)
{
    int i = indeks(id);
    if (i < 0)
        return -1;
    return dok_widoczny(id) ? i : -1;
}

int dok_ustaw_czas(uint32_t id, int64_t czas)
{
    int i = zmienialny(id), r;
    struct k1_dok_wpis w;
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    w = wpisy[i];
    if (w.rodzaj != K1_DOK_PLIK || w.pochodzenie == K1_POCH_K1 || czas < 1 || czas > K1_DOK_CZAS_MAKS)
        return K1_DOK_BLAD_MIEJSCE; /* only the time a file brought with it, never of one made in K1 */
    w.zmieniono = czas;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
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

/* The folder of an application is remembered in object "a<profile>" (0.8.1, plan L1): found by its number even when
 * the user renamed or moved it, or a file took its name. */
static void nazwa_folderu_apl(int tworca, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "a%d", tworca);
}

static void zapamietaj_folder(int tworca, uint32_t id)
{
    char n[K1_MAG_NAZWA];
    uint8_t b[4];
    nazwa_folderu_apl(tworca, n);
    k1_z32(b, id);
    ob_zapisz(WL, n, b, sizeof b); /* failing: found by its name next time, as before 0.8.1 */
}

static int folder_widoczny(uint32_t id)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    return w != NULL && w->rodzaj == K1_DOK_FOLDER && dok_widoczny(id);
}

int dok_folder_aplikacji(const char *nazwa, int tworca, int64_t teraz, uint32_t *id)
{
    char n[K1_MAG_NAZWA];
    uint8_t b[4];
    uint32_t dl;
    int r;
    nazwa_folderu_apl(tworca, n);
    if (ob_czytaj(WL, n, b, sizeof b, &dl) == K1_MAG_OK && dl == sizeof b) {
        if (folder_widoczny(k1_c32(b))) {
            *id = k1_c32(b);
            return 0;
        }
    } else { /* nothing remembered (files of 0.8.0): its folder by name, once */
        for (int i = 0; i < n_wpisow; i++)
            if (wpisy[i].rodzic == K1_FOLDER_DOKUMENTY && folder_widoczny(wpisy[i].id) && k1_dok_nazwy_rowne(wpisy[i].nazwa, nazwa)) {
                *id = wpisy[i].id;
                zapamietaj_folder(tworca, *id);
                return 0;
            }
    }
    /* none, or the remembered one is in Kosz or gone: a new folder */
    r = dok_nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, nazwa, strlen(nazwa), NULL, tworca, K1_POCH_K1, teraz, id);
    if (r == 0)
        zapamietaj_folder(tworca, *id);
    return r;
}

int dok_przenies(uint32_t id, uint32_t folder, int wolna, int64_t teraz)
{
    int i = zmienialny(id), r;
    struct k1_dok_wpis w;
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    if (!folder_otwarty(folder) || wewnatrz(folder, id) || glebokosc(folder) + wysokosc(id) > K1_DOK_GLEBOKOSC)
        return K1_DOK_BLAD_MIEJSCE;
    if (wpisy[i].rodzic == folder)
        return 0;
    if (!wolna && nazwa_zajeta(folder, wpisy[i].nazwa, id))
        return K1_DOK_BLAD_ZAJETA;
    w = wpisy[i];
    if (wolna && (r = wolna_nazwa(folder, wpisy[i].nazwa, id, w.nazwa)) != 0) /* "Zachowaj oba" (0.8.2 step 6) */
        return r;
    w.rodzic = folder;
    w.zmieniono = teraz;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

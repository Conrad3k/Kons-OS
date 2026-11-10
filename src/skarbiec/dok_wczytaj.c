/* K1 skarbiec — the user's files at start: the entries loaded, then repaired (a missing folder → Dokumenty),
 * pieces without an entry deleted, Kosz emptied of what is over 30 days old. Part of skarbiec (dok_wew.h). */
#include <stdio.h>
#include <string.h>

#include "dok_wew.h"
#include "k1_log.h"
#include "skarbiec.h"

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

/* K1 skarbiec — the user's files at start: the entries loaded, then repaired (a missing folder → Dokumenty),
 * pieces without an entry deleted, Kosz emptied of what is over 30 days old. Part of skarbiec (dok_wew.h).
 *
 * Nothing the user had is deleted because its entry does not pass the check (0.8.1, plan L2): an entry whose
 * number, kind, size and generation can be read is repaired (the name with "_" for bad characters, a field out of
 * range set to a safe value) and written again; one that cannot is left as it is, with its content — a later
 * version (or a fix) may read it. Only pieces of content with no entry object at all, or of an old generation of
 * an entry that loaded, are deleted. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dok_wew.h"
#include "k1_bajty.h"
#include "k1_log.h"
#include "skarbiec.h"

/* What loading found: numbers of entries left as they are (their content stays), repaired ones. */
static struct {
    uint32_t nieczytelne[K1_DOK_MAKS];
    int n_nieczytelnych, za_duzo; /* za_duzo: more than fit — then no piece is deleted at all */
    uint32_t naprawione[K1_DOK_MAKS];
    int n_naprawionych;
    uint8_t ma_tresc[K1_DOK_MAKS]; /* by the place in wpisy[]: piece 0 of its generation is there */
} st;

/* The number in an entry object's name "m<number>" (written by nazwa_wpisu), 0 for any other name. */
static uint32_t numer_wpisu(const char *nazwa)
{
    char oczekiwana[K1_MAG_NAZWA];
    unsigned long id;
    char *koniec;
    if (nazwa[0] != 'm' || nazwa[1] < '1' || nazwa[1] > '9')
        return 0;
    id = strtoul(nazwa + 1, &koniec, 10);
    if (*koniec != '\0' || id < K1_DOK_PIERWSZY || id > K1_DOK_ID_MAKS)
        return 0;
    nazwa_wpisu((uint32_t)id, oczekiwana);
    return strcmp(oczekiwana, nazwa) == 0 ? (uint32_t)id : 0;
}

/* Whether piece 0 of generation `gen` of file `id` is there (any size: a short buffer only says "too big"). */
static int jest_kawalek(uint32_t id, uint32_t gen)
{
    char n[K1_MAG_NAZWA];
    uint8_t b;
    uint32_t dl;
    int r;
    nazwa_kawalka(id, gen, 0, n);
    r = ob_czytaj(WL, n, &b, sizeof b, &dl);
    return r == K1_MAG_OK || r == K1_MAG_ZA_DUZY;
}

/* Entry `id` from bytes that did not pass k1_dok_wpis_czytaj: 0 and `w` when it can be repaired. What is needed to
 * find the content (kind, generation, size within the limit, the content itself) must be right; the rest is set. */
static int napraw_wpis(const uint8_t b[K1_DOK_WPIS_BAJTY], uint32_t id, struct k1_dok_wpis *w)
{
    uint8_t z[K1_DOK_WPIS_BAJTY];
    struct k1_dok_wpis s;
    memset(&s, 0, sizeof s);
    s.id = id;
    s.rodzaj = b[8];
    s.rodzic = k1_c32(b + 4);
    s.generacja = k1_c32(b + 12);
    s.rozmiar = k1_c64(b + 16);
    if (s.rodzaj == K1_DOK_PLIK) {
        if (s.rozmiar > K1_DOK_MAKS_BAJTY || (s.rozmiar > 0 && !jest_kawalek(id, s.generacja)))
            return -1;
        s.typ = b[9] >= 1 && b[9] <= K1_TYPY_MAKS ? b[9] : K1_TYP_INNY;
    } else if (s.rodzaj == K1_DOK_FOLDER) {
        s.generacja = 0, s.rozmiar = 0; /* a folder has no content: nothing to lose */
    } else {
        return -1;
    }
    if (s.rodzic < 1 || s.rodzic > K1_DOK_ID_MAKS || s.rodzic == id || (s.rodzic > K1_FOLDERY_MAKS && s.rodzic < K1_DOK_PIERWSZY))
        s.rodzic = K1_FOLDER_DOKUMENTY;
    s.pochodzenie = b[10] >= 1 && b[10] <= K1_POCH_MAKS ? b[10] : K1_POCH_K1;
    s.tworca = b[11] >= 1 && b[11] <= K1_PROFIL_MAKS ? b[11] : K1_PROFIL_PLIKI;
    s.utworzono = (int64_t)k1_c64(b + 24) < 0 ? 0 : (int64_t)k1_c64(b + 24);
    s.zmieniono = (int64_t)k1_c64(b + 32) < 0 ? 0 : (int64_t)k1_c64(b + 32);
    s.kosz = (int64_t)k1_c64(b + 40) < 0 ? 0 : (int64_t)k1_c64(b + 40);
    k1_dok_nazwa_popraw((const char *)b + 48, strnlen((const char *)b + 48, K1_DOK_NAZWA - 1), s.nazwa);
    if (strspn(s.nazwa, "_") == strlen(s.nazwa)) /* nothing of the name left */
        snprintf(s.nazwa, sizeof s.nazwa, "Odzyskany %s %u", s.rodzaj == K1_DOK_PLIK ? "plik" : "folder", id);
    k1_dok_wpis_zapisz(&s, z); /* the repaired entry must pass the same check as any other */
    if (k1_dok_wpis_czytaj(z, w) != 0)
        return -1;
    return 0;
}

static void zostaw(uint32_t id)
{
    if (st.n_nieczytelnych == K1_DOK_MAKS)
        st.za_duzo = 1;
    else
        st.nieczytelne[st.n_nieczytelnych++] = id;
}

static void wczytaj_wpis(const char *nazwa, void *ctx)
{
    uint8_t b[K1_DOK_WPIS_BAJTY + 1];
    struct k1_dok_wpis w;
    uint32_t dl, id = numer_wpisu(nazwa);
    int r, dobry, *zle = ctx;
    if (nazwa[0] != 'm')
        return;
    if (id >= nastepny) /* never a new entry with the number of one left as it is */
        nastepny = id + 1;
    r = ob_czytaj(WL, nazwa, b, sizeof b, &dl);
    dobry = r == K1_MAG_OK && dl == K1_DOK_WPIS_BAJTY && id != 0;
    if (dobry && k1_dok_wpis_czytaj(b, &w) == 0 && w.id == id) {
        dobry = 1;
    } else if (dobry && napraw_wpis(b, id, &w) == 0) {
        st.naprawione[st.n_naprawionych++] = id;
    } else {
        dobry = 0;
    }
    if (!dobry || n_wpisow == K1_DOK_MAKS) {
        (*zle)++;
        if (id != 0)
            zostaw(id);
        else if (nazwa[1] >= '0' && nazwa[1] <= '9')
            st.za_duzo = 1; /* a number we cannot read back: keep every piece */
        return;
    }
    wpisy[n_wpisow++] = w;
}

static int zostawiony(uint32_t id)
{
    for (int i = 0; i < st.n_nieczytelnych; i++)
        if (st.nieczytelne[i] == id)
            return 1;
    return st.za_duzo;
}

/* The piece named `nazwa`: 1 and its numbers, 0 when the name is not one of a piece. */
static int kawalek(const char *nazwa, unsigned *id, unsigned *gen, unsigned *k)
{
    char koniec;
    return nazwa[0] == 't' && sscanf(nazwa, "t%ug%uk%u%c", id, gen, k, &koniec) == 3;
}

static void zaznacz_kawalek(const char *nazwa, void *ctx)
{
    unsigned id, gen, k;
    int i;
    (void)ctx;
    if (kawalek(nazwa, &id, &gen, &k) && k == 0 && (i = indeks(id)) >= 0 && wpisy[i].generacja == gen)
        st.ma_tresc[i] = 1;
}

/* Pieces of content no entry points to (a write cut off): deleted — never those of an entry left as it is, nor of a
 * file whose own content is not there (its generation would be wrong: nothing is guessed). */
static void sprzataj_kawalek(const char *nazwa, void *ctx)
{
    unsigned id, gen, k;
    const struct k1_dok_wpis *w;
    int *ile = ctx, numer = kawalek(nazwa, &id, &gen, &k), i;
    if (nazwa[0] != 't' || (numer && zostawiony(id)))
        return;
    i = numer ? indeks(id) : -1;
    w = i >= 0 ? &wpisy[i] : NULL;
    if (w != NULL && w->rodzaj == K1_DOK_PLIK && w->rozmiar > 0 && !st.ma_tresc[i])
        return;
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

/* The repaired entries, in their folders for good: a free name, written (the next start reads them as they are). */
static void zapisz_naprawione(void)
{
    for (int j = 0; j < st.n_naprawionych; j++) {
        int i = indeks(st.naprawione[j]);
        char wolna[K1_DOK_NAZWA];
        if (i < 0)
            continue;
        if (wolna_nazwa(wpisy[i].rodzic, wpisy[i].nazwa, wpisy[i].id, wolna) == 0)
            memcpy(wpisy[i].nazwa, wolna, sizeof wolna);
        zapisz_wpis(&wpisy[i]);
    }
}

void dok_wczytaj(int64_t teraz)
{
    int zle = 0, sieroty = 0, kawalki = 0, stare;
    memset(&st, 0, sizeof st);
    n_wpisow = 0;
    nastepny = K1_DOK_PIERWSZY;
    dok_czas_wczytaj();
    ob_kazdy(WL, wczytaj_wpis, &zle);
    sieroty = napraw_drzewo();
    zapisz_naprawione();
    ob_kazdy(WL, zaznacz_kawalek, NULL);
    ob_kazdy(WL, sprzataj_kawalek, &kawalki);
    stare = dok_przeterminowane(teraz);
    k1_log(TAG, "pliki: %d wpisów (%u w Koszu)%s", n_wpisow, dok_w_koszu(),
           zle || sieroty || kawalki || stare || st.n_naprawionych ? " — sprzątanie przy otwarciu" : "");
    if (zle || sieroty || kawalki || stare || st.n_naprawionych)
        k1_log(TAG, "pliki: naprawione wpisy %d, nieczytelne wpisy %d (treść zostaje), przeniesione do Dokumentów %d, "
               "kawałki bez wpisu %d, z Kosza po %d dniach %d", st.n_naprawionych, zle, sieroty, kawalki, K1_KOSZ_DNI, stare);
}

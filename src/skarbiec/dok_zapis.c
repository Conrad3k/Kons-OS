/* K1 skarbiec — a write in progress (0.8.1 step 4, ADR 0034): the content of a file in pieces of K1_DOK_KAWALEK, the
 * entry only after the last one. A new file gets its number at the start (nobody else takes it); new content of a file
 * gets the next generation. Each piece is an object of its own at once (t<number>g<generation>k<piece>) — nothing of
 * the file is kept in memory; a power cut leaves pieces without an entry or of a newer generation, which dok_wczytaj
 * deletes at start; an abandoned write deletes them right away. Every write of content goes through here (also a whole
 * file in one message: dok_nowy, dok_zapisz_tresc). Part of skarbiec (skarbiec.h). */
#include <string.h>

#include "dok_wew.h"
#include "skarbiec.h"

#define W_TOKU 24 /* writes in progress at once: one per client channel (16), and the whole ones of dokumenty.c */

static struct dok_zapis *w_toku[W_TOKU];

static int zapisywany(uint32_t id)
{
    for (int i = 0; i < W_TOKU; i++)
        if (w_toku[i] != NULL && w_toku[i]->id == id)
            return 1;
    return 0;
}

static int nowych_w_toku(void)
{
    int n = 0;
    for (int i = 0; i < W_TOKU; i++)
        n += w_toku[i] != NULL && w_toku[i]->nowy;
    return n;
}

static int zarejestruj(struct dok_zapis *z)
{
    for (int i = 0; i < W_TOKU; i++)
        if (w_toku[i] == NULL) {
            w_toku[i] = z;
            z->jest = 1;
            return 0;
        }
    return K1_DOK_BLAD_ZAPIS;
}

static void wyrejestruj(struct dok_zapis *z)
{
    for (int i = 0; i < W_TOKU; i++)
        if (w_toku[i] == z)
            w_toku[i] = NULL;
    memset(z, 0, sizeof *z);
}

void dok_zapis_zapomnij(void)
{
    for (int i = 0; i < W_TOKU; i++)
        if (w_toku[i] != NULL) {
            memset(w_toku[i], 0, sizeof *w_toku[i]);
            w_toku[i] = NULL;
        }
}

int dok_zapis_nowy(struct dok_zapis *z, uint32_t folder, const char *nazwa, size_t nl, int tworca, int pochodzenie)
{
    memset(z, 0, sizeof *z);
    if (!k1_dok_nazwa_poprawna(nazwa, nl))
        return K1_DOK_BLAD_NAZWA;
    if (!folder_otwarty(folder))
        return K1_DOK_BLAD_MIEJSCE;
    if (n_wpisow + nowych_w_toku() >= K1_DOK_MAKS || nastepny > K1_DOK_ID_MAKS)
        return K1_DOK_BLAD_PELNO;
    z->nowy = 1;
    z->id = nastepny;
    z->gen = 1;
    z->folder = folder;
    z->tworca = (uint8_t)tworca;
    z->pochodzenie = (uint8_t)pochodzenie;
    memcpy(z->nazwa, nazwa, nl);
    if (zarejestruj(z) != 0)
        return K1_DOK_BLAD_ZAPIS;
    nastepny++; /* the number is taken now: a write in progress is never given another's */
    return 0;
}

int dok_zapis_tresci(struct dok_zapis *z, uint32_t id)
{
    int i = indeks(id);
    memset(z, 0, sizeof *z);
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    if (wpisy[i].rodzaj != K1_DOK_PLIK || !dok_widoczny(id))
        return K1_DOK_BLAD_MIEJSCE;
    if (zapisywany(id)) /* two new versions at once would take the same generation */
        return K1_DOK_BLAD_ZAPIS;
    z->id = id;
    z->gen = wpisy[i].generacja + 1;
    return zarejestruj(z);
}

int dok_zapis_kawalek(struct dok_zapis *z, const void *buf, uint32_t n)
{
    char nazwa[K1_MAG_NAZWA];
    int r;
    if (!z->jest || n > K1_DOK_KAWALEK || (z->kawalkow > 0 && !z->pelny) || z->kawalkow >= K1_DOK_KAWALKI_MAKS)
        return K1_DOK_BLAD_ZAPIS; /* only the last piece may be shorter */
    if (!dok_miejsce_na(n))
        return K1_DOK_BLAD_BRAK_MIEJSCA;
    if (z->kawalkow == 0) { /* the type from the start of the content; "more" decides a cut-off UTF-8 at its end */
        z->typ_calosc = (uint8_t)k1_dok_typ(buf, n, n);
        z->typ_dalej = (uint8_t)k1_dok_typ(buf, n, (uint64_t)n + 1);
    }
    nazwa_kawalka(z->id, z->gen, z->kawalkow, nazwa);
    if ((r = ob_zapisz(WL, nazwa, buf, n)) != K1_MAG_OK)
        return r;
    z->kawalkow++;
    z->rozmiar += n;
    z->pelny = n == K1_DOK_KAWALEK;
    return 0;
}

void dok_zapis_porzuc(struct dok_zapis *z)
{
    if (!z->jest)
        return;
    usun_kawalki(z->id, z->gen, z->kawalkow);
    wyrejestruj(z);
}

/* The entry of a new file, written after its content. */
static int wpis_nowego(struct dok_zapis *z, int64_t teraz, struct k1_dok_wpis *w)
{
    int r;
    if (!folder_otwarty(z->folder)) /* its folder went to Kosz or for good while the content came */
        return K1_DOK_BLAD_MIEJSCE;
    if (n_wpisow >= K1_DOK_MAKS)
        return K1_DOK_BLAD_PELNO;
    memset(w, 0, sizeof *w);
    w->id = z->id;
    w->rodzic = z->folder;
    w->rodzaj = K1_DOK_PLIK;
    w->pochodzenie = z->pochodzenie;
    w->tworca = z->tworca;
    w->utworzono = teraz;
    if ((r = wolna_nazwa(z->folder, z->nazwa, 0, w->nazwa)) != 0)
        return r;
    return 0;
}

int dok_zapis_koniec(struct dok_zapis *z, int64_t teraz, uint32_t *id)
{
    struct k1_dok_wpis w;
    int i, r;
    *id = 0;
    if (!z->jest)
        return K1_DOK_BLAD_ZAPIS;
    i = z->nowy ? -1 : zmienialny(z->id);
    r = K1_DOK_BLAD_MIEJSCE; /* new content of a file that went to Kosz or for good meanwhile is not kept */
    if (z->nowy)
        r = wpis_nowego(z, teraz, &w);
    else if (i >= 0 && wpisy[i].rodzaj == K1_DOK_PLIK)
        w = wpisy[i], r = 0;
    if (r == 0) {
        w.generacja = z->gen;
        w.rozmiar = z->rozmiar;
        /* empty: a new text file; one piece: its type as it is; more: a UTF-8 letter may go on in the next piece */
        w.typ = z->kawalkow == 0 ? K1_TYP_TEKST : z->kawalkow == 1 ? z->typ_calosc : z->typ_dalej;
        w.zmieniono = teraz;
        r = zapisz_wpis(&w);
    }
    if (r != 0) {
        dok_zapis_porzuc(z);
        return r;
    }
    if (z->nowy) {
        dok_dodaj(&w);
    } else {
        usun_kawalki(z->id, wpisy[i].generacja, kawalkow(wpisy[i].rozmiar));
        wpisy[i] = w;
    }
    *id = z->id;
    wyrejestruj(z);
    return 0;
}

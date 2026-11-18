/* K1 Podgląd — the picture shown and the wallpaper (0.8.2 step 4, ADR 0037). Podgląd never decodes a picture itself:
 * a dekoder does, through k1_dekodowanie (the dekoder from nadzorca on fd 6, the file piece by piece from skarbiec,
 * passed on unread). dekoder gives at most BOK × BOK pixels, so the picture fits the window in every turn and can be
 * zoomed in; turning, fitting and zooming are widok.c's. The wallpaper (at most
 * K1_DEK_MAKS_W × K1_DEK_MAKS_H) goes on to ekran unread (TAPETA_ZDJECIE), as from Pliki. Part of Podgląd (podglad.h). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_dekoder.h"
#include "k1_dekodowanie.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "kolejnosc.h"
#include "podglad.h"

enum cel { CEL_WIDOK = 1, CEL_TAPETA = 2 };
#define BLAD_PLIK (-1) /* larger than dekoder takes (K1_DEK_MAKS_BAJTY): not even asked */

static struct k1_dekodowanie dek;
static uint32_t zrodlo[BOK * BOK];
static struct stan {
    uint32_t id, gen;
    int blad, mpx;     /* K1_DEK_BLAD_* or BLAD_PLIK when there is no picture (mpx: how large the photo is) */
    int32_t w, h;      /* in zrodlo */
    int32_t zw, zh;    /* the photo itself, turned as seen */
} jest;                /* what zrodlo holds (id 0: nothing) */
static char tapeta_tekst[96];
const char *tapeta_stan;

static int do_dekodera(const struct k1_dok_wpis *w)
{
    return w->rozmiar > 0 && w->rozmiar <= K1_DEK_MAKS_BAJTY;
}

/* The file is still the picture it was, with the same content. */
static int aktualna(struct k1_dek_praca *p)
{
    const struct k1_dok_wpis *w = wpis(p->id);
    if (w == NULL || w->generacja != p->gen || !kol_zdjecie(w) || !do_dekodera(w))
        return 0;
    p->dlugosc = w->rozmiar;
    return 1;
}

static void zadaj(const struct k1_dok_wpis *w, enum cel cel)
{
    struct k1_dek_praca p = { w->id, w->generacja, w->rozmiar, cel == CEL_TAPETA ? K1_DEK_MAKS_W : BOK,
                              cel == CEL_TAPETA ? K1_DEK_MAKS_H : BOK, cel };
    k1_dek_zadaj(&dek, &p);
}

void obraz_zadaj(const struct k1_dok_wpis *w)
{
    const struct k1_dek_praca *robi = k1_dek_robi(&dek), *czeka = k1_dek_czeka(&dek);
    if (w == NULL || (jest.id == w->id && jest.gen == w->generacja) ||
        (robi != NULL && robi->id == w->id && robi->gen == w->generacja && robi->cel == CEL_WIDOK))
        return;
    if (!do_dekodera(w)) { /* too large for dekoder, or empty: no need to ask */
        jest = (struct stan){ w->id, w->generacja, w->rozmiar > 0 ? BLAD_PLIK : K1_DEK_BLAD_USZKODZONY, 0, 0, 0, 0, 0 };
        widok_nowy(NULL, 0, 0);
        return;
    }
    if (czeka == NULL || czeka->cel == CEL_WIDOK) /* a wallpaper asked for is not dropped for the next picture */
        zadaj(w, CEL_WIDOK);
}

void obraz_tapeta(const struct k1_dok_wpis *w)
{
    if (w == NULL || !do_dekodera(w))
        return;
    tapeta_stan = "Przygotowuję tapetę…";
    k1_log(TAG, "tapeta ze zdjęcia: przygotowanie");
    zadaj(w, CEL_TAPETA);
}

int obraz_gotowy(const struct k1_dok_wpis *w)
{
    return w != NULL && jest.id == w->id && jest.gen == w->generacja && jest.blad == 0;
}

int obraz_tresc(const struct k1_komunikat *k, int fd)
{
    return k1_dek_tresc(&dek, k, fd);
}

int obraz_wynik(const struct k1_komunikat *k)
{
    return k1_dek_wynik_skarbca(&dek, k);
}

static void tapeta(const struct k1_dek_wynik *w, int fd)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_TAPETA_ZDJECIE, 0, w->w, w->h, 0, 0, 0);
    if (k1_memfd_sprawdz(fd, (size_t)w->w * (size_t)w->h * 4) != 0 || /* ekran checks it again */
        k1_kanal_wyslij(apl.k.fd, &m, sizeof m, fd) != 0) {
        tapeta_stan = "Nie udało się ustawić tapety.";
        return;
    }
    tapeta_stan = "Tapeta ustawiona.";
    k1_log(TAG, "tapeta ze zdjęcia: %dx%d wysłana do ekranu", w->w, w->h);
}

static void blad_tapety(int kod, int mpx)
{
    if (kod == K1_DEK_BLAD_DUZY)
        snprintf(tapeta_tekst, sizeof tapeta_tekst, "Zdjęcie ma %d Mpx. K1 pokazuje do %d Mpx.", mpx, K1_DEK_MAKS_MPX);
    else
        snprintf(tapeta_tekst, sizeof tapeta_tekst, "%s",
                 kod == K1_DEK_BLAD_PAMIEC ? "Za mało pamięci, żeby pokazać to zdjęcie." : "Tego zdjęcia nie da się odczytać.");
    tapeta_stan = tapeta_tekst;
}

/* The answer of dekoder (k1_dekodowanie). */
static void zdekodowane(const struct k1_dek_praca *p, const struct k1_dek_wynik *w, int fd)
{
    if (w->blad != 0)
        k1_log(TAG, "obraz: dekoder nie odczytał pliku (%d)", w->blad);
    if (p->cel == CEL_TAPETA) {
        if (w->blad != 0)
            blad_tapety(w->blad, w->mpx);
        else
            tapeta(w, fd);
        return;
    }
    jest = (struct stan){ p->id, p->gen, w->blad, w->mpx, 0, 0, w->zw, w->zh };
    if (w->blad == 0 && (w->w > BOK || w->h > BOK || k1_memfd_czytaj(fd, zrodlo, (size_t)w->w * (size_t)w->h * 4) != 0)) {
        k1_log(TAG, "obraz: odrzucono obraz od dekodera");
        jest.blad = K1_DEK_BLAD_USZKODZONY;
    } else if (w->blad == 0) {
        jest.w = w->w;
        jest.h = w->h;
    }
    widok_nowy(jest.blad == 0 ? zrodlo : NULL, jest.w, jest.h);
    if (jest.blad == 0) {
        int32_t ww, wh;
        widok_rozmiar(&ww, &wh);
        k1_log(TAG, "obraz: %dx%d (zdjęcie %dx%d)", (int)ww, (int)wh, (int)jest.zw, (int)jest.zh);
    }
}

static void dekoder_odbierz(void)
{
    k1_dek_odbierz(&dek);
}

void obraz_nadzorca(void)
{
    for (int n = 0; n < 8; n++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            apl.kanaly[KANAL_NADZORCA].fd = -1;
            break;
        }
        if (w == K1_ODB_JEST && k.typ == K1_KOM_DEKODER_KANAL && fd >= 0) {
            k1_dek_kanal(&dek, fd); /* taken (or closed) there */
            fd = -1;
        }
        if (fd >= 0)
            close(fd);
    }
}

void obraz_start(void)
{
    dek = (struct k1_dekodowanie){ .apl = &apl, .kanal = KANAL_DEKODER, .nadzorca = FD_NADZORCA,
                                   .skarbiec = apl.kanaly[KANAL_SKARBIEC].fd, .aktualna = aktualna,
                                   .gotowe = zdekodowane, .fd = -1 };
    apl.kanaly[KANAL_DEKODER].fd = -1;
    apl.kanaly[KANAL_DEKODER].dane = dekoder_odbierz;
}

/* ---- drawing ---- */

static void tekst_bledu(char *t, size_t n)
{
    if (jest.blad == K1_DEK_BLAD_DUZY)
        snprintf(t, n, "Zdjęcie ma %d Mpx. K1 pokazuje do %d Mpx.", jest.mpx, K1_DEK_MAKS_MPX);
    else if (jest.blad == BLAD_PLIK)
        snprintf(t, n, "Plik jest za duży. K1 pokazuje zdjęcia do %u MB.", (unsigned)(K1_DEK_MAKS_BAJTY >> 20));
    else if (jest.blad == K1_DEK_BLAD_PAMIEC)
        snprintf(t, n, "Za mało pamięci, żeby pokazać to zdjęcie.");
    else
        snprintf(t, n, "Tego zdjęcia nie da się odczytać.");
}

void obraz_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t *zw, int32_t *zh)
{
    char t[96];
    int ten = w != NULL && jest.id == w->id && jest.gen == w->generacja;
    int32_t sx = OBSZAR_X + OBSZAR_W / 2, sy = OBSZAR_Y + OBSZAR_H / 2;
    *zw = ten && jest.blad == 0 ? jest.zw : 0;
    *zh = ten && jest.blad == 0 ? jest.zh : 0;
    if (w == NULL)
        return;
    if (ten && jest.blad == 0 && widok_rysuj(o))
        return;
    if (ten)
        tekst_bledu(t, sizeof t);
    else
        snprintf(t, sizeof t, "Wczytywanie…");
    k1_ikona(o, K1_IK_OBRAZ, (float)sx - 24, (float)sy - 56, 48, k1_kolor(0xaba79f, 255));
    k1_tekst(o, K1_CZ_TEKST, sx - k1_tekst_szer(K1_CZ_TEKST, t) / 2, sy + 24, t, k1_kolor(0xeeebe5, 255), OBSZAR_W - 32);
    (void)m;
}

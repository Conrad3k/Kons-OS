/* K1 Pliki — pictures (0.8.0 step 4, ADR 0032; pieces and more formats: 0.8.2 step 3): the thumbnail of a picture file
 * in the details (the types of family "obrazy": PNG, JPEG, GIF, BMP, WebP) and "Ustaw jako tapetę".
 *
 * Pliki never decode a picture themselves: a dekoder does, through k1_dekodowanie (przybornik, shared with Podgląd
 * since 0.8.2 step 4): the dekoder from nadzorca on fd 6, the file piece by piece from skarbiec, passed on unread. The
 * thumbnail (at most MIN_W × MIN_H) is copied in here; the wallpaper (at most K1_DEK_MAKS_W × K1_DEK_MAKS_H) goes on
 * to ekran unread (TAPETA_ZDJECIE). Part of Pliki (pliki.h). */
#include <stdio.h>
#include <string.h>

#include "k1_dekodowanie.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_uslugi.h"
#include "pliki.h"

enum cel { CEL_MINIATURA = 1, CEL_TAPETA = 2 };

static struct k1_dekodowanie dek;
static uint32_t miniatura[MIN_W * MIN_H];
static struct miniatura {
    uint32_t id, gen, w, h;
    int blad, mpx; /* K1_DEK_BLAD_* when the picture could not be shown (mpx: how large it is) */
} min;
static char tapeta_tekst[96];
const char *tapeta_stan; /* a message for the footer about the wallpaper (NULL: none) */

int obraz_pliku(const struct k1_dok_wpis *w)
{
    return w != NULL && !tu.pendrive && w->rodzaj == K1_DOK_PLIK && w->rozmiar > 0 && w->rozmiar <= K1_DEK_MAKS_BAJTY &&
           k1_typ_rodzina(w->typ) == K1_RODZINA_OBRAZY;
}

/* The file is still a picture of ours with the content it had when it was asked for. */
static int aktualna(struct k1_dek_praca *p)
{
    const struct k1_dok_wpis *w = wpis(p->id);
    if (w == NULL || w->generacja != p->gen || !obraz_pliku(w))
        return 0;
    p->dlugosc = w->rozmiar;
    return 1;
}

static void zadaj(const struct k1_dok_wpis *w, enum cel cel)
{
    struct k1_dek_praca p = { w->id, w->generacja, w->rozmiar, cel == CEL_TAPETA ? K1_DEK_MAKS_W : MIN_W,
                              cel == CEL_TAPETA ? K1_DEK_MAKS_H : MIN_H, cel };
    k1_dek_zadaj(&dek, &p);
}

void obraz_miniatura(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis(id);
    const struct k1_dek_praca *robi = k1_dek_robi(&dek), *czeka = k1_dek_czeka(&dek);
    if (!obraz_pliku(w) || (min.id == id && min.gen == w->generacja) ||
        (robi != NULL && robi->id == id && robi->cel == CEL_MINIATURA))
        return;
    if (czeka == NULL || czeka->cel == CEL_MINIATURA) /* a wallpaper asked for is not dropped for a thumbnail */
        zadaj(w, CEL_MINIATURA);
}

void obraz_tapeta(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis(id);
    if (!obraz_pliku(w))
        return;
    tapeta_stan = "Przygotowuję tapetę…";
    k1_log(TAG, "tapeta ze zdjęcia: przygotowanie");
    zadaj(w, CEL_TAPETA);
}

void obraz_kanal(int fd)
{
    k1_dek_kanal(&dek, fd);
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
    if (k1_memfd_sprawdz(fd, (size_t)w->w * (size_t)w->h * 4) != 0 || /* ekran checks it again; a bad one should not even get there */
        k1_kanal_wyslij(apl.k.fd, &m, sizeof m, fd) != 0) {
        tapeta_stan = "Nie udało się ustawić tapety.";
        return;
    }
    tapeta_stan = "Tapeta ustawiona.";
    k1_log(TAG, "tapeta ze zdjęcia: %dx%d wysłana do ekranu", w->w, w->h);
}

static void blad_dekodera(const struct k1_dek_praca *p, int kod, int mpx)
{
    if (kod == K1_DEK_BLAD_DUZY)
        k1_log(TAG, "obraz: dekoder nie odczytał pliku (%d): %d Mpx", kod, mpx);
    else
        k1_log(TAG, "obraz: dekoder nie odczytał pliku (%d)", kod);
    if (p->cel == CEL_MINIATURA) {
        min = (struct miniatura){ p->id, p->gen, 0, 0, kod, mpx };
        return;
    }
    if (kod == K1_DEK_BLAD_DUZY)
        snprintf(tapeta_tekst, sizeof tapeta_tekst, "Zdjęcie ma %d Mpx — K1 pokazuje do %d Mpx.", mpx, K1_DEK_MAKS_MPX);
    else
        snprintf(tapeta_tekst, sizeof tapeta_tekst, "%s",
                 kod == K1_DEK_BLAD_PAMIEC ? "Za mało pamięci, żeby pokazać to zdjęcie." : "Tego zdjęcia nie da się odczytać.");
    tapeta_stan = tapeta_tekst;
}

/* The answer of dekoder (k1_dekodowanie). */
static void zdekodowane(const struct k1_dek_praca *p, const struct k1_dek_wynik *w, int fd)
{
    if (w->blad != 0) {
        blad_dekodera(p, w->blad, w->mpx);
        return;
    }
    if (p->cel == CEL_TAPETA) {
        tapeta(w, fd);
        return;
    }
    if (w->w > MIN_W || w->h > MIN_H || k1_memfd_czytaj(fd, miniatura, (size_t)w->w * (size_t)w->h * 4) != 0) {
        k1_log(TAG, "obraz: odrzucono miniaturę od dekodera");
        return;
    }
    min = (struct miniatura){ p->id, p->gen, (uint32_t)w->w, (uint32_t)w->h, 0, 0 };
    k1_log(TAG, "miniatura: %dx%d", w->w, w->h);
}

static void dekoder_odbierz(void)
{
    k1_dek_odbierz(&dek);
}

void obrazy_start(int skarbiec)
{
    dek = (struct k1_dekodowanie){ .apl = &apl, .kanal = KANAL_DEKODER, .nadzorca = FD_PENDRIVE, .skarbiec = skarbiec,
                                   .aktualna = aktualna, .gotowe = zdekodowane, .fd = -1 };
    apl.kanaly[KANAL_DEKODER].fd = -1;
    apl.kanaly[KANAL_DEKODER].dane = dekoder_odbierz;
}

/* ---- in the details ---- */

int32_t obraz_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t x, int32_t y)
{
    const char *t = NULL, *t2 = NULL;
    char ile[48], gran[48];
    int ten = min.id == w->id && min.gen == w->generacja;
    if (!obraz_pliku(w))
        return 0;
    k1_zaokraglony(o, (float)x, (float)y, MIN_W, MIN_H, 8, k1_kolor(m->tekst, 12));
    if (ten && min.blad == 0)
        k1_naloz_sprite(o, miniatura, min.w, min.h, x + (int32_t)(MIN_W - min.w) / 2, y + (int32_t)(MIN_H - min.h) / 2);
    else if (ten && min.blad == K1_DEK_BLAD_DUZY) {
        snprintf(ile, sizeof ile, "Zdjęcie ma %d Mpx —", min.mpx);
        t = ile;
        snprintf(gran, sizeof gran, "K1 pokazuje do %d Mpx", K1_DEK_MAKS_MPX);
        t2 = gran;
    } else if (ten)
        t = min.blad == K1_DEK_BLAD_PAMIEC ? "Za mało pamięci na miniaturę" : "Miniatura niedostępna";
    else
        t = "Wczytywanie miniatury…";
    if (t != NULL) {
        int32_t ty = y + MIN_H - (t2 != NULL ? 42 : 24);
        k1_ikona(o, K1_IK_OBRAZ, (float)x + MIN_W / 2 - 20, (float)y + 40, 40, k1_kolor(m->tekst3, 255));
        k1_tekst(o, K1_CZ_MALA, x + 16, ty, t, k1_kolor(m->tekst3, 255), MIN_W - 32);
        if (t2 != NULL)
            k1_tekst(o, K1_CZ_MALA, x + 16, ty + 18, t2, k1_kolor(m->tekst3, 255), MIN_W - 32);
    }
    return MIN_H;
}

/* The button under the thumbnail (outside Kosz): 1 and its place in *p when the selected file is a picture. */
int obraz_przycisk(struct k1_przycisk *p)
{
    static int zalogowany;
    if (tu.kosz || zaznaczony == 0 || !obraz_pliku(wpis(zaznaczony)))
        return 0;
    *p = (struct k1_przycisk){ SZCZEGOLY_X + 16, SZCZEGOLY_TAPETA_Y, MIN_W, PRZYCISK_H, "Ustaw jako tapetę", K1_PRZ_ZWYKLY };
    if (!zalogowany++) /* the tests click by this line */
        k1_log(TAG, "przycisk tapety: %d,%d", p->x + p->w / 2, p->y + p->h / 2);
    return 1;
}

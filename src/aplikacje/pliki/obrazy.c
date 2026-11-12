/* K1 Pliki — pictures (0.8.0 step 4, ADR 0032; pieces and more formats: 0.8.2 step 3): the preview of a picture file
 * (the types of family "obrazy": PNG, JPEG, GIF, BMP, WebP) and "Ustaw jako tapetę".
 *
 * Pliki never decode a picture themselves. For each picture: a new dekoder from nadzorca (DEKODER on fd 6 →
 * DEKODER_KANAL), the request to it (DEK_OBRAZ: the length and the size wanted), then the file piece by piece from
 * skarbiec (DOK_CZYTAJ → DOK_TRESC, a sealed memfd of at most 1 MiB), each passed on to dekoder unread
 * (DEK_KAWALEK); its answer (DEK_PIKSELE: a sealed memfd of pixels, or DEK_BLAD) and dekoder ends. So a photo of
 * 100 MB costs Pliki and skarbiec no more memory than one piece. The preview (at most MIN_W × MIN_H) is copied in
 * here; the wallpaper (at most K1_DEK_MAKS_W × K1_DEK_MAKS_H) goes on to ekran unread (TAPETA_ZDJECIE). One picture
 * at a time; while one is decoded, only the latest wish waits. Part of Pliki (pliki.h). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "pliki.h"


enum cel { CEL_PODGLAD = 1, CEL_TAPETA = 2 };
enum etap { E_NIC = 0, E_KANAL, E_TRESC, E_WYNIK }; /* waiting for: the channel to dekoder, a piece, its answer */

struct praca {
    uint32_t id, gen;
    enum cel cel;
};

static struct praca chce, robi; /* the wish waiting (id 0: none), the picture being decoded */
static enum etap etap;
static int dekoder = -1;                 /* the channel to dekoder */
static uint64_t dlugosc;                 /* of the file being decoded */
static uint32_t kawalek;                 /* the next piece dekoder gets */
static struct {
    uint32_t id, kawalek;                /* the piece asked from skarbiec, not answered yet (id 0: none) */
} czytany;
static uint32_t miniatura[MIN_W * MIN_H];
static struct podglad_obrazu {
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

static void zacznij(void)
{
    const struct k1_dok_wpis *w = wpis(chce.id);
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0);
    if (etap != E_NIC || chce.id == 0 || czytany.id != 0) /* a piece of the last one still to come: after it */
        return;
    robi = chce;
    chce.id = 0;
    if (w == NULL || w->generacja != robi.gen || !obraz_pliku(w))
        return;
    dlugosc = w->rozmiar;
    etap = E_KANAL;
    if (k1_kanal_wyslij(FD_PENDRIVE, &m, sizeof m, -1) != 0)
        etap = E_NIC;
}

static void skoncz(void)
{
    if (dekoder >= 0)
        close(dekoder);
    dekoder = -1;
    apl.kanaly[KANAL_DEKODER].fd = -1;
    etap = E_NIC;
    zacznij();
}

static void czytaj_kawalek(void)
{
    czytany.id = robi.id;
    czytany.kawalek = kawalek;
    do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, robi.id, (int32_t)K1_DOK_KAWALEK, (int32_t)kawalek, 0, 0, 0), -1);
}

void obraz_podglad(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis(id);
    if (!obraz_pliku(w) || (min.id == id && min.gen == w->generacja) ||
        (etap != E_NIC && robi.id == id && robi.cel == CEL_PODGLAD))
        return;
    if (chce.id == 0 || chce.cel == CEL_PODGLAD) /* a wallpaper asked for is not dropped for a preview */
        chce = (struct praca){ id, w->generacja, CEL_PODGLAD };
    zacznij();
}

void obraz_tapeta(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis(id);
    if (!obraz_pliku(w))
        return;
    chce = (struct praca){ id, w->generacja, CEL_TAPETA };
    tapeta_stan = "Przygotowuję tapetę…";
    k1_log(TAG, "tapeta ze zdjęcia: przygotowanie");
    zacznij();
}

/* DEKODER_KANAL from nadzorca: the channel to the new dekoder — the request, then the first piece is asked for. */
void obraz_kanal(int fd)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DEK_OBRAZ, robi.id, (int32_t)dlugosc,
                                              robi.cel == CEL_TAPETA ? K1_DEK_MAKS_W : MIN_W,
                                              robi.cel == CEL_TAPETA ? K1_DEK_MAKS_H : MIN_H, 0, 0);
    if (etap != E_KANAL || fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        return;
    }
    dekoder = fd;
    apl.kanaly[KANAL_DEKODER].fd = dekoder;
    if (k1_kanal_wyslij(dekoder, &m, sizeof m, -1) != 0) {
        k1_log(TAG, "obraz: wysłanie do dekodera nieudane");
        skoncz();
        return;
    }
    etap = E_TRESC;
    kawalek = 0;
    czytaj_kawalek();
}

/* DOK_TRESC from skarbiec: 1 when it was ours (a piece of a picture), passed on to dekoder. */
int obraz_tresc(const struct k1_komunikat *k, int fd)
{
    struct k1_komunikat m;
    uint64_t od = (uint64_t)kawalek * K1_DOK_KAWALEK, ile = dlugosc - od < K1_DOK_KAWALEK ? dlugosc - od : K1_DOK_KAWALEK;
    if (czytany.id == 0 || k->obiekt != czytany.id || (uint32_t)k->b != czytany.kawalek)
        return 0;
    czytany.id = 0;
    if (etap != E_TRESC || k->obiekt != robi.id) { /* the picture is no longer wanted: the next one may start */
        zacznij();
        return 1;
    }
    m = k1_komunikat_nowy(K1_KOM_DEK_KAWALEK, robi.id, (int32_t)kawalek, k->a, 0, 0, 0);
    if (fd < 0 || (uint64_t)k->a != ile || k1_kom_u64(k->c, k->d) != dlugosc ||
        k1_kanal_wyslij(dekoder, &m, sizeof m, fd) != 0) { /* the file changed meanwhile, or dekoder is gone */
        skoncz();
        return 1;
    }
    kawalek++;
    if ((uint64_t)kawalek * K1_DOK_KAWALEK < dlugosc)
        czytaj_kawalek();
    else
        etap = E_WYNIK;
    return 1;
}

/* DOK_WYNIK from skarbiec: 1 when it was a piece of ours that could not be read. */
int obraz_wynik(const struct k1_komunikat *k)
{
    if (k->a != K1_KOM_DOK_CZYTAJ || czytany.id == 0 || k->obiekt != czytany.id)
        return 0;
    czytany.id = 0;
    k1_log(TAG, "obraz: skarbiec nie dał kawałka %u (%d)", kawalek, k->b);
    if (etap == E_TRESC && k->obiekt == robi.id)
        skoncz(); /* dekoder ends without the rest; it says so (DEK_BLAD) or just ends */
    else
        zacznij();
    return 1;
}

static void piksele(const struct k1_komunikat *k, int fd)
{
    size_t n = (size_t)k->a * (size_t)k->b * 4;
    if (robi.cel == CEL_PODGLAD) {
        if (k->a > MIN_W || k->b > MIN_H || k1_memfd_czytaj(fd, miniatura, n) != 0) {
            k1_log(TAG, "obraz: odrzucono podgląd od dekodera");
            return;
        }
        min = (struct podglad_obrazu){ robi.id, robi.gen, (uint32_t)k->a, (uint32_t)k->b, 0, 0 };
        k1_log(TAG, "podgląd obrazu: %dx%d", k->a, k->b);
        return;
    }
    if (k1_memfd_sprawdz(fd, n) != 0) { /* ekran checks it again; a bad one should not even get there */
        tapeta_stan = "Nie udało się ustawić tapety.";
        return;
    }
    {
        struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_TAPETA_ZDJECIE, 0, k->a, k->b, 0, 0, 0);
        if (k1_kanal_wyslij(apl.k.fd, &m, sizeof m, fd) != 0) {
            tapeta_stan = "Nie udało się ustawić tapety.";
            return;
        }
    }
    tapeta_stan = "Tapeta ustawiona.";
    k1_log(TAG, "tapeta ze zdjęcia: %dx%d wysłana do ekranu", k->a, k->b);
}

static void blad_dekodera(int kod, int mpx)
{
    if (kod == K1_DEK_BLAD_DUZY)
        k1_log(TAG, "obraz: dekoder nie odczytał pliku (%d): %d Mpx", kod, mpx);
    else
        k1_log(TAG, "obraz: dekoder nie odczytał pliku (%d)", kod);
    if (robi.cel == CEL_PODGLAD) {
        min = (struct podglad_obrazu){ robi.id, robi.gen, 0, 0, kod, mpx };
        return;
    }
    if (kod == K1_DEK_BLAD_DUZY)
        snprintf(tapeta_tekst, sizeof tapeta_tekst, "Zdjęcie ma %d Mpx — K1 pokazuje do %d Mpx.", mpx, K1_DEK_MAKS_MPX);
    else
        snprintf(tapeta_tekst, sizeof tapeta_tekst, "%s",
                 kod == K1_DEK_BLAD_PAMIEC ? "Za mało pamięci, żeby pokazać to zdjęcie." : "Tego zdjęcia nie da się odczytać.");
    tapeta_stan = tapeta_tekst;
}

static void dekoder_odbierz(void)
{
    struct k1_komunikat k;
    int fd = -1;
    enum k1_odbior_wynik w = k1_odbierz_komunikat(dekoder, K1_UID_DEKODER, K1_KIER_OD_DEKODERA, &k, &fd);
    if (w == K1_ODB_PUSTO)
        return;
    if (w == K1_ODB_JEST && k.obiekt == robi.id && k.typ == K1_KOM_DEK_PIKSELE && etap == E_WYNIK)
        piksele(&k, fd);
    else if (w == K1_ODB_JEST && k.obiekt == robi.id && k.typ == K1_KOM_DEK_BLAD)
        blad_dekodera(k.a, k.b);
    else
        blad_dekodera(K1_DEK_BLAD_USZKODZONY, 0); /* it ended without an answer (e.g. its time ran out) */
    if (fd >= 0)
        close(fd);
    skoncz(); /* one answer per dekoder */
    k1_apl_odswiez(&apl);
}

void obrazy_start(void)
{
    apl.kanaly[KANAL_DEKODER].fd = -1;
    apl.kanaly[KANAL_DEKODER].dane = dekoder_odbierz;
}

/* ---- in the preview ---- */

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
        t = min.blad == K1_DEK_BLAD_PAMIEC ? "Za mało pamięci na podgląd" : "Podgląd niedostępny";
    else
        t = "Wczytywanie podglądu…";
    if (t != NULL) {
        int32_t ty = y + MIN_H - (t2 != NULL ? 42 : 24);
        k1_ikona(o, K1_IK_OBRAZ, (float)x + MIN_W / 2 - 20, (float)y + 40, 40, k1_kolor(m->tekst3, 255));
        k1_tekst(o, K1_CZ_MALA, x + 16, ty, t, k1_kolor(m->tekst3, 255), MIN_W - 32);
        if (t2 != NULL)
            k1_tekst(o, K1_CZ_MALA, x + 16, ty + 18, t2, k1_kolor(m->tekst3, 255), MIN_W - 32);
    }
    return MIN_H;
}

/* The button under the preview (outside Kosz): 1 and its place in *p when the selected file is a picture. */
int obraz_przycisk(struct k1_przycisk *p)
{
    static int zalogowany;
    if (tu.kosz || zaznaczony == 0 || !obraz_pliku(wpis(zaznaczony)))
        return 0;
    *p = (struct k1_przycisk){ PODGLAD_X + 16, PODGLAD_TAPETA_Y, MIN_W, PRZYCISK_H, "Ustaw jako tapetę", K1_PRZ_ZWYKLY };
    if (!zalogowany++) /* the tests click by this line */
        k1_log(TAG, "przycisk tapety: %d,%d", p->x + p->w / 2, p->y + p->h / 2);
    return 1;
}

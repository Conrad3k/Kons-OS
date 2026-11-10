/* K1 Pliki — pictures (0.8.0 step 4, ADR 0032): the preview of a PNG or JPEG file and "Ustaw jako tapetę".
 *
 * Pliki never decode a picture themselves. For each picture: the whole file from skarbiec (DOK_CZYTAJ → DOK_TRESC,
 * a sealed memfd), a new dekoder from nadzorca (DEKODER on fd 6 → DEKODER_KANAL), the file to it (DEK_OBRAZ) and its
 * answer (DEK_PIKSELE: a sealed memfd of pixels, or DEK_BLAD); dekoder then ends. The preview (at most MIN_W × MIN_H)
 * is copied in here; the wallpaper (at most K1_DEK_MAKS_W × K1_DEK_MAKS_H) goes on to ekran unread
 * (TAPETA_ZDJECIE). One picture at a time; while one is decoded, only the latest wish waits. Part of Pliki (pliki.h). */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "pliki.h"


enum cel { CEL_PODGLAD = 1, CEL_TAPETA = 2 };
enum etap { E_NIC = 0, E_TRESC, E_KANAL, E_WYNIK }; /* waiting for: the file, the channel to dekoder, its answer */

struct praca {
    uint32_t id, gen;
    enum cel cel;
};

static struct praca chce, robi; /* the wish waiting (id 0: none), the picture being decoded */
static enum etap etap;
static int tresc = -1, dekoder = -1;    /* the file from skarbiec; the channel to dekoder */
static uint32_t tresc_dl;
static uint32_t miniatura[MIN_W * MIN_H];
static struct podglad_obrazu {
    uint32_t id, gen, w, h;
    int blad; /* the picture could not be read */
} min;
const char *tapeta_stan; /* a message for the footer about the wallpaper (NULL: none) */

int obraz_pliku(const struct k1_dok_wpis *w)
{
    return w != NULL && !tu.pendrive && w->rodzaj == K1_DOK_PLIK && w->rozmiar > 0 &&
           (w->typ == K1_TYP_PNG || w->typ == K1_TYP_JPEG);
}

static void zacznij(void)
{
    const struct k1_dok_wpis *w = wpis(chce.id);
    if (etap != E_NIC || chce.id == 0)
        return;
    robi = chce;
    chce.id = 0;
    if (w == NULL || w->generacja != robi.gen)
        return;
    etap = E_TRESC;
    do_skarbca(K1_KOM_DOK_CZYTAJ, robi.id, (int32_t)K1_DOK_MAKS_BAJTY);
}

static void skoncz(void)
{
    if (tresc >= 0)
        close(tresc);
    if (dekoder >= 0)
        close(dekoder);
    tresc = dekoder = -1;
    apl.kanaly[KANAL_DEKODER].fd = -1;
    etap = E_NIC;
    zacznij();
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

/* DOK_TRESC from skarbiec: 1 when it was ours (the file of the picture being decoded). */
int obraz_tresc(const struct k1_komunikat *k, int fd)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0);
    if (etap != E_TRESC || k->obiekt != robi.id || fd < 0)
        return 0;
    if (k->a != k->b || k->a <= 0 || (tresc = fcntl(fd, F_DUPFD_CLOEXEC, 0)) < 0) {
        skoncz();
        return 1;
    }
    tresc_dl = (uint32_t)k->a;
    etap = E_KANAL;
    if (k1_kanal_wyslij(FD_PENDRIVE, &m, sizeof m, -1) != 0)
        skoncz();
    return 1;
}

/* DEKODER_KANAL from nadzorca: the channel to the new dekoder — the file goes to it. */
void obraz_kanal(int fd)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DEK_OBRAZ, robi.id, (int32_t)tresc_dl,
                                              robi.cel == CEL_TAPETA ? K1_DEK_MAKS_W : MIN_W,
                                              robi.cel == CEL_TAPETA ? K1_DEK_MAKS_H : MIN_H, 0, 0);
    if (etap != E_KANAL || fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        return;
    }
    dekoder = fd;
    apl.kanaly[KANAL_DEKODER].fd = dekoder;
    etap = E_WYNIK;
    if (k1_kanal_wyslij(dekoder, &m, sizeof m, tresc) != 0) {
        k1_log(TAG, "obraz: wysłanie do dekodera nieudane");
        skoncz();
        return;
    }
    close(tresc);
    tresc = -1;
}

static void piksele(const struct k1_komunikat *k, int fd)
{
    size_t n = (size_t)k->a * (size_t)k->b * 4;
    if (robi.cel == CEL_PODGLAD) {
        if (k->a > MIN_W || k->b > MIN_H || k1_memfd_czytaj(fd, miniatura, n) != 0) {
            k1_log(TAG, "obraz: odrzucono podgląd od dekodera");
            return;
        }
        min = (struct podglad_obrazu){ robi.id, robi.gen, (uint32_t)k->a, (uint32_t)k->b, 0 };
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

static void blad_dekodera(int kod)
{
    k1_log(TAG, "obraz: dekoder nie odczytał pliku (%d)", kod);
    if (robi.cel == CEL_PODGLAD)
        min = (struct podglad_obrazu){ robi.id, robi.gen, 0, 0, 1 };
    else
        tapeta_stan = kod == K1_DEK_BLAD_DUZY ? "To zdjęcie jest za duże." : "Tego zdjęcia nie da się odczytać.";
}

static void dekoder_odbierz(void)
{
    struct k1_komunikat k;
    int fd = -1;
    enum k1_odbior_wynik w = k1_odbierz_komunikat(dekoder, K1_UID_DEKODER, K1_KIER_OD_DEKODERA, &k, &fd);
    if (w == K1_ODB_PUSTO)
        return;
    if (w == K1_ODB_JEST && k.obiekt == robi.id && k.typ == K1_KOM_DEK_PIKSELE)
        piksele(&k, fd);
    else if (w == K1_ODB_JEST && k.obiekt == robi.id && k.typ == K1_KOM_DEK_BLAD)
        blad_dekodera(k.a);
    else
        blad_dekodera(K1_DEK_BLAD_USZKODZONY); /* it ended without an answer (e.g. its time ran out) */
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
    const char *t = NULL;
    if (!obraz_pliku(w))
        return 0;
    k1_zaokraglony(o, (float)x, (float)y, MIN_W, MIN_H, 8, k1_kolor(m->tekst, 12));
    if (min.id == w->id && min.gen == w->generacja && !min.blad)
        k1_naloz_sprite(o, miniatura, min.w, min.h, x + (int32_t)(MIN_W - min.w) / 2, y + (int32_t)(MIN_H - min.h) / 2);
    else
        t = min.id == w->id && min.gen == w->generacja ? "Podgląd niedostępny" : "Wczytywanie podglądu…";
    if (t != NULL) {
        k1_ikona(o, K1_IK_OBRAZ, (float)x + MIN_W / 2 - 20, (float)y + 40, 40, k1_kolor(m->tekst3, 255));
        k1_tekst(o, K1_CZ_MALA, x + 16, y + MIN_H - 24, t, k1_kolor(m->tekst3, 255), MIN_W - 32);
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

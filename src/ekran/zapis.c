/* K1 ekran — the settings saved in skarbiec (M5d, ADR 0015).
 *
 * fd 5 (from nadzorca, optional): a channel to skarbiec. At start ekran reads object
 * K1_OB_USTAWIENIA (waiting at most 1.5 s — without the channel or the object: defaults; since 0.8.1 an answer
 * that comes later, while skarbiec opens a vault of many GB, is still applied) and after every change it saves a
 * new version. Every value read back is checked like a USTAW message
 * (k1_ustawienie_poprawne): a bad value leaves the default (fail safe). The resolution is saved as
 * width × height (mode numbers differ between computers) and restored after the first frame. */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_dokument.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"

#define FD_SKARBIEC 5
#define MAGIA "K1U4"   /* 0.8.2: room for 32 file types (with Podgląd for pictures) */
#define TYPY_ZAPIS 32  /* room for file types in the saved settings */
#define ZAPIS_STARY 28 /* the settings without file types */

struct zapis {
    char magia[4];
    int32_t motyw, sekundy, mysz, tapeta, szer, wys;
    int32_t otwieranie[TYPY_ZAPIS]; /* K1_UST_OTWIERANIE + i */
};
_Static_assert(sizeof(struct zapis) == ZAPIS_STARY + 4 * TYPY_ZAPIS, "zapis ustawień: unexpected size");
_Static_assert(K1_TYPY_MAKS <= TYPY_ZAPIS, "zapis ustawień: more file types than room");

/* Only K1U4 is read (0.11.0: the older K1U1–K1U3 went — every vault opened once with 0.10.x was saved again as K1U4;
 * older settings, never opened since, are defaults now: a fail-safe start). */
static int skarb = -1;
static int32_t zapisana_szer, zapisana_wys;
static int czeka; /* 0.8.1: skarbiec was still opening a large vault at start — its answer comes later */

/* The saved settings `z` (n bytes) into `ust` (a copy of s.ust), each value checked; 0, or -1 for an unknown format. */
static int odczytaj(const struct zapis *z, int n, int ust[K1_UST_MAKS + 1])
{
    if (n != (int)sizeof *z || memcmp(z->magia, MAGIA, 4) != 0) {
        k1_log(TAG, "zapisane ustawienia w nieznanym formacie — domyślne");
        return -1;
    }
    for (int i = 0; i < K1_TYPY_MAKS; i++)
        if (k1_ustawienie_poprawne(K1_UST_OTWIERANIE + i, z->otwieranie[i]))
            ust[K1_UST_OTWIERANIE + i] = z->otwieranie[i];
    if (k1_ustawienie_poprawne(K1_UST_MOTYW, z->motyw))
        ust[K1_UST_MOTYW] = z->motyw;
    if (k1_ustawienie_poprawne(K1_UST_SEKUNDY, z->sekundy))
        ust[K1_UST_SEKUNDY] = z->sekundy;
    if (k1_ustawienie_poprawne(K1_UST_MYSZ, z->mysz))
        ust[K1_UST_MYSZ] = z->mysz;
    if (k1_ustawienie_poprawne(K1_UST_TAPETA, z->tapeta))
        ust[K1_UST_TAPETA] = z->tapeta;
    if (z->szer >= K1_TRYB_MIN_SZER && z->szer <= K1_POW_MAKS_BOK && z->wys >= K1_TRYB_MIN_WYS && z->wys <= K1_POW_MAKS_BOK) {
        zapisana_szer = z->szer;
        zapisana_wys = z->wys;
    }
    k1_log(TAG, "ustawienia wczytane ze skarbca (tapeta %d, motyw %d, mysz %d, rozdzielczość %dx%d)", ust[K1_UST_TAPETA],
           ust[K1_UST_MOTYW], ust[K1_UST_MYSZ], (int)zapisana_szer, (int)zapisana_wys);
    return 0;
}

/* The settings now, with the resolution `szer` × `wys`, to skarbiec (K1U4); `dlaczego`: logged when not a change. */
static void zapisz(int32_t szer, int32_t wys, const char *dlaczego)
{
    struct zapis z;
    if (skarb < 0)
        return;
    memcpy(z.magia, MAGIA, 4);
    z.motyw = s.ust[K1_UST_MOTYW];
    z.sekundy = s.ust[K1_UST_SEKUNDY];
    z.mysz = s.ust[K1_UST_MYSZ];
    z.tapeta = s.ust[K1_UST_TAPETA];
    z.szer = szer;
    z.wys = wys;
    memset(z.otwieranie, 0, sizeof z.otwieranie);
    for (int i = 0; i < K1_TYPY_MAKS; i++)
        z.otwieranie[i] = s.ust[K1_UST_OTWIERANIE + i];
    if (k1_skarb_zapisz(skarb, K1_OB_USTAWIENIA, &z, sizeof z) != 0)
        k1_log(TAG, "zapis ustawień nieudany");
    else if (dlaczego != NULL)
        k1_log(TAG, "ustawienia zapisane: %s", dlaczego);
}

void ustawienia_wczytaj(void)
{
    struct zapis z;
    int tryb = -1, n;
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0 || fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) != 0) {
        k1_log(TAG, "brak kanału do skarbca — ustawienia domyślne, bez zapisu");
        return;
    }
    skarb = FD_SKARBIEC;
    n = k1_skarb_czytaj(skarb, K1_OB_USTAWIENIA, &z, sizeof z, 1500, &tryb);
    if (n == -2) { /* the first frame does not wait for a vault of many GB: the answer is taken later */
        czeka = 1;
        k1_log(TAG, "skarbiec jeszcze otwiera magazyn — na razie ustawienia domyślne, zapisane po jego odpowiedzi");
        return;
    }
    if (n < 0) {
        k1_log(TAG, "brak zapisanych ustawień — domyślne (%s)", tryb == K1_SKARB_RAM ? "zapis tylko w pamięci" : "na dysku");
        return;
    }
    odczytaj(&z, n, s.ust);
}

int ustawienia_czeka(void)
{
    return czeka;
}

/* The settings that came late (0.8.1): the photo, then every value that differs, as if Ustawienia changed it (drawn
 * again, sent to the clients); the saved resolution as after the first frame. */
static void spoznione(const struct zapis *z, int n)
{
    int ust[K1_UST_MAKS + 1];
    czeka = 0;
    memcpy(ust, s.ust, sizeof ust);
    if (n > 0 && odczytaj(z, n, ust) != 0)
        n = 0;
    zdjecie_wczytaj();
    ustawienie(K1_UST_ZDJECIE, s.ust[K1_UST_ZDJECIE]);
    if (ust[K1_UST_TAPETA] == K1_TAPETA_ZDJECIE && !s.ust[K1_UST_ZDJECIE])
        ust[K1_UST_TAPETA] = s.ust[K1_UST_TAPETA];
    for (int k = 1; k <= K1_UST_MAKS; k++)
        if (k != K1_UST_ROZDZIELCZOSC && k != K1_UST_ZDJECIE && k != K1_UST_OPIS && ust[k] != s.ust[k])
            ustawienie(k, ust[k]);
    if (n > 0)
        ustawienia_po_starcie();
}

/* After the first frame: the saved resolution, if this card offers it. */
void ustawienia_po_starcie(void)
{
    int i, n = s.n_trybow < K1_TRYBY_MAKS ? s.n_trybow : K1_TRYBY_MAKS;
    if (zapisana_szer == 0 || (zapisana_szer == s.W && zapisana_wys == s.H))
        return;
    for (i = 0; i < n; i++)
        if ((int32_t)s.tryby[i].hdisplay == zapisana_szer && (int32_t)s.tryby[i].vdisplay == zapisana_wys) {
            s.nowy_tryb = i + 1;
            return;
        }
    k1_log(TAG, "zapisana rozdzielczość %dx%d niedostępna — zostaje %dx%d", (int)zapisana_szer, (int)zapisana_wys,
           (int)s.W, (int)s.H);
}

void ustawienia_zapisz(void)
{
    zapisz(s.W, s.H, NULL);
}

int ustawienia_fd(void)
{
    return skarb;
}

/* What skarbiec sends: results of our saves (only errors and the first success are logged) and — once — the
 * settings asked for at start, when they come late. */
void ustawienia_odbierz(void)
{
    static int zalogowane;
    for (int i = 0; i < 16 && skarb >= 0; i++) {
        struct k1_komunikat k;
        struct zapis z;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(skarb, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "skarbiec zamknął kanał — dalsze zmiany ustawień bez zapisu");
            close(skarb);
            skarb = -1;
            return;
        }
        if (w == K1_ODB_JEST && czeka && k.obiekt == K1_OB_USTAWIENIA && k.typ == K1_KOM_SKARB_DANE)
            spoznione(&z, (size_t)k.a <= sizeof z && k1_memfd_czytaj(fd, &z, (size_t)k.a) == 0 ? k.a : 0);
        else if (w == K1_ODB_JEST && czeka && k.obiekt == K1_OB_USTAWIENIA && k.typ == K1_KOM_SKARB_WYNIK &&
                 k.a == K1_KOM_SKARB_CZYTAJ)
            spoznione(&z, 0); /* nothing saved: the defaults stay, the photo may still be there */
        else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_ZAPISZ && k.b != 0)
            k1_log(TAG, "zapis ustawień: błąd %d", k.b);
        else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_ZAPISZ && !zalogowane++)
            k1_log(TAG, "ustawienia zapisane (%s)", k.c == K1_SKARB_TRWALY ? "na dysku" : "w pamięci");
        if (fd >= 0)
            close(fd);
    }
}

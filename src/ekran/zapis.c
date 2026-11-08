/* K1 ekran — the settings saved in skarbiec (M5d, ADR 0015).
 *
 * fd 5 (from nadzorca, optional): a channel to skarbiec. At start ekran reads object
 * K1_OB_USTAWIENIA (waiting at most 1.5 s — without the channel or the object: defaults) and after
 * every change it saves a new version. Every value read back is checked like a USTAW message
 * (k1_ustawienie_poprawne): a bad value leaves the default (fail safe). The resolution is saved as
 * width × height (mode numbers differ between computers) and restored after the first frame. */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_skarb_klient.h"

#define FD_SKARBIEC 5
#define MAGIA "K1U1"

struct zapis {
    char magia[4];
    int32_t motyw, sekundy, mysz, tapeta, szer, wys;
};
_Static_assert(sizeof(struct zapis) == 28, "zapis ustawień: unexpected size");

static int skarb = -1;
static int32_t zapisana_szer, zapisana_wys;

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
    if (n == -2) {
        k1_log(TAG, "skarbiec nie odpowiedział — ustawienia domyślne");
        return;
    }
    if (n < 0) {
        k1_log(TAG, "brak zapisanych ustawień — domyślne (%s)", tryb == K1_SKARB_RAM ? "zapis tylko w pamięci" : "na dysku");
        return;
    }
    if (n != (int)sizeof z || memcmp(z.magia, MAGIA, 4) != 0) {
        k1_log(TAG, "zapisane ustawienia w nieznanym formacie — domyślne");
        return;
    }
    if (k1_ustawienie_poprawne(K1_UST_MOTYW, z.motyw))
        s.ust[K1_UST_MOTYW] = z.motyw;
    if (k1_ustawienie_poprawne(K1_UST_SEKUNDY, z.sekundy))
        s.ust[K1_UST_SEKUNDY] = z.sekundy;
    if (k1_ustawienie_poprawne(K1_UST_MYSZ, z.mysz))
        s.ust[K1_UST_MYSZ] = z.mysz;
    if (k1_ustawienie_poprawne(K1_UST_TAPETA, z.tapeta))
        s.ust[K1_UST_TAPETA] = z.tapeta;
    if (z.szer >= K1_TRYB_MIN_SZER && z.szer <= K1_POW_MAKS_BOK && z.wys >= K1_TRYB_MIN_WYS && z.wys <= K1_POW_MAKS_BOK) {
        zapisana_szer = z.szer;
        zapisana_wys = z.wys;
    }
    k1_log(TAG, "ustawienia wczytane ze skarbca (tapeta %d, motyw %d, mysz %d, rozdzielczość %dx%d)",
           (int)s.ust[K1_UST_TAPETA], (int)s.ust[K1_UST_MOTYW], (int)s.ust[K1_UST_MYSZ], (int)zapisana_szer,
           (int)zapisana_wys);
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
    struct zapis z;
    if (skarb < 0)
        return;
    memcpy(z.magia, MAGIA, 4);
    z.motyw = s.ust[K1_UST_MOTYW];
    z.sekundy = s.ust[K1_UST_SEKUNDY];
    z.mysz = s.ust[K1_UST_MYSZ];
    z.tapeta = s.ust[K1_UST_TAPETA];
    z.szer = s.W;
    z.wys = s.H;
    if (k1_skarb_zapisz(skarb, K1_OB_USTAWIENIA, &z, sizeof z) != 0)
        k1_log(TAG, "zapis ustawień nieudany");
}

int ustawienia_fd(void)
{
    return skarb;
}

/* Results of our saves (only errors and the first success are logged). */
void ustawienia_odbierz(void)
{
    static int zalogowane;
    struct k1_komunikat k;
    int r;
    while (skarb >= 0 && (r = k1_skarb_wynik(skarb, &k)) != 0) {
        if (r < 0) {
            k1_log(TAG, "skarbiec zamknął kanał — dalsze zmiany ustawień bez zapisu");
            close(skarb);
            skarb = -1;
            return;
        }
        if (k.b != 0)
            k1_log(TAG, "zapis ustawień: błąd %d", k.b);
        else if (!zalogowane++)
            k1_log(TAG, "ustawienia zapisane (%s)", k.c == K1_SKARB_TRWALY ? "na dysku" : "w pamięci");
    }
}

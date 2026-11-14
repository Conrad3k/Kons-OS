/* K1 siec — the remembered Wi-Fi networks in skarbiec (0.9.1 step 2, D4; W12). Part of siec (usluga.h).
 *
 * fd 4 is siec's channel to skarbiec (KANAL_SKARBCA: profile 18 = the owner of the object). At start siec asks for its
 * object (K1_OB_WIFI, pamiec.h); while the vault waits for its password it asks again every PONOW s — Wi-Fi joins by
 * itself only after the person logged in. Without skarbiec (no fd 4) the networks are remembered until power off.
 * Every change is written at once (a new network, a forgotten one, the radio switch). The keys never go to the log. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_sha1.h"
#include "k1_skarb_klient.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "pamiec.h"
#include "usluga.h"

#define SKARB 4
#define PONOW 5.0

static struct k1_wifi_pamiec pamiec;
static int jest = -1, wczytana, radio_czeka = -1; /* jest: fd 4 is open (-1: not checked yet) */
static double ponow;

static void czytaj(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_CZYTAJ, K1_OB_WIFI, 0, 0, 0, 0, 0);
    if (k1_kanal_wyslij(SKARB, &k, sizeof k, -1) != 0)
        jest = 0;
}

static void zapisz(void)
{
    uint8_t b[K1_WIFI_PAMIEC_DL];
    size_t n = k1_wifi_pamiec_buduj(&pamiec, b);
    if (jest > 0 && k1_skarb_zapisz(SKARB, K1_OB_WIFI, b, n) != 0)
        k1_log(TAG, "wifi: zapis zapamiętanych sieci do skarbca nie wysłany");
    k1_wymaz(b, sizeof b);
}

/* What was read (or nothing to read): the radio as the person left it, then the strongest remembered network. */
static void wczytano(void)
{
    wczytana = 1;
    k1_log(TAG, "wifi: zapamiętane sieci: %d%s", pamiec.n, pamiec.radio_wylaczone ? ", radio wyłączone" : "");
    if (radio_czeka >= 0) { /* switched before the vault opened: that counts */
        pamiec.radio_wylaczone = !radio_czeka;
        zapisz();
    } else if (pamiec.radio_wylaczone) {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_RADIO_USTAW, 0, 0, 0, 0, 0, 0);
        if (k1_kanal_wyslij(NADZORCA, &k, sizeof k, -1) != 0)
            k1_koniec(TAG, "kanał do nadzorcy: %s", strerror(errno));
    }
    lista_wyslij();
    wybor_auto();
}

const struct k1_wifi_pamiec *skarb_pamiec(void)
{
    return wczytana ? &pamiec : NULL;
}

void skarb_zapamietaj(const struct k1_wifi_wpis *w)
{
    k1_wifi_pamiec_dodaj(&pamiec, w);
    zapisz();
    k1_log(TAG, "wifi: sieć zapamiętana (razem %d)", pamiec.n);
}

void skarb_zapomnij(int i)
{
    k1_wifi_pamiec_usun(&pamiec, i);
    zapisz();
    k1_log(TAG, "wifi: sieć zapomniana (zostało %d)", pamiec.n);
}

void skarb_radio(int wlacz)
{
    if (!wczytana) {
        radio_czeka = wlacz;
        return;
    }
    pamiec.radio_wylaczone = !wlacz;
    zapisz();
}

int skarb_poll(struct pollfd *p)
{
    if (jest < 0) { /* the first time: is there a channel to skarbiec? */
        jest = fcntl(SKARB, F_GETFD) >= 0;
        if (jest)
            czytaj();
        else
            wczytano();
    }
    p->fd = jest > 0 ? SKARB : -1;
    p->events = POLLIN;
    p->revents = 0;
    return 1;
}

static void dane(const struct k1_komunikat *k, int fd)
{
    uint8_t b[K1_WIFI_PAMIEC_DL];
    if (k->a < 0 || (size_t)k->a > sizeof b || k1_memfd_czytaj(fd, b, (size_t)k->a) != 0 ||
        k1_wifi_pamiec_czytaj(b, (size_t)k->a, &pamiec) != 0)
        k1_log(TAG, "wifi: zapamiętane sieci w złym formacie — zaczynam od zera");
    k1_wymaz(b, sizeof b);
    wczytano();
}

void skarb_odbierz(const struct pollfd *p)
{
    int i;
    for (i = 0; i < 8 && jest > 0 && p->revents; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(SKARB, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            jest = 0;
            if (!wczytana)
                wczytano();
            return;
        }
        if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_DANE && k.obiekt == K1_OB_WIFI && !wczytana && fd >= 0) {
            dane(&k, fd);
        } else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_CZYTAJ && !wczytana) {
            if (k.c == K1_SKARB_ZABLOKOWANY)
                ponow = k1_teraz() + PONOW; /* the vault waits for its password */
            else
                wczytano(); /* nothing remembered yet */
        } else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_ZAPISZ && k.b < 0) {
            k1_log(TAG, "wifi: skarbiec nie zapisał zapamiętanych sieci (%d)", k.b);
        }
        if (fd >= 0)
            close(fd);
    }
}

double skarb_czas(double t)
{
    if (wczytana || ponow <= 0)
        return 0;
    if (t >= ponow) {
        ponow = 0;
        czytaj();
        return 0;
    }
    return ponow;
}

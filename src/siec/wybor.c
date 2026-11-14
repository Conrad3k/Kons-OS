/* K1 siec — which Wi-Fi network to join (0.9.1 step 2, D2, D4). Part of siec (usluga.h).
 *
 * The person's choice comes from Ustawienia on a channel of its own (KANAL_DO_SIECI: Ustawienia → siec, never through
 * nadzorca; its end comes in SIEC_USTAWIENIA, the sender's UID is checked): the name of the network as shown, its
 * security, the password once (WIFI_HASLO, WIFI_WYBRANA, WIFI_POLACZ) — or "forget it" (WIFI_ZAPOMNIJ). The network is
 * found by name and security in the list siec made (lista.c); its key is computed here (PBKDF2) and the password wiped.
 * By itself (wybor_auto: a new list, the vault read, the connection lost): the strongest remembered network in range,
 * not right after a failed attempt (polacz.c). */
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_sha1.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "pamiec.h"
#include "usluga.h"
#include "wpa.h"

static int ust = -1;
static char haslo[K1_WIFI_HASLO_MAKS];
static uint8_t wybrana[K1_WIFI_NAZWA_MAKS];
static int haslo_ma, wybrana_ma; /* bits: the parts in */

static void wyczysc(void)
{
    k1_wymaz(haslo, sizeof haslo);
    memset(wybrana, 0, sizeof wybrana);
    haslo_ma = wybrana_ma = 0;
}

void ustawienia_kanal(int fd)
{
    if (ust >= 0)
        close(ust);
    ust = fd;
    wyczysc();
}

/* WIFI_POLACZ: the network of the name parts; with a password (c > 0), remembered, or open. */
static void polacz_z_ustawien(const struct k1_komunikat *k)
{
    const struct k1_wifi_siec *s;
    const struct k1_wifi_cel *cel;
    const struct k1_wifi_pamiec *p = skarb_pamiec();
    uint8_t pmk[32];
    int karta, i = wybrana_ma == 3 ? lista_znajdz(wybrana, k->b, k->a, &karta, &s, &cel) : -1, j;
    if (i < 0 || (k->c > 0 && haslo_ma != 15)) {
        k1_log(TAG, "wifi: wybranej sieci nie ma już na liście");
        return;
    }
    if (!k1_wifi_mozna_polaczyc(s->zabezpieczenie, cel->grupa)) {
        k1_log(TAG, "wifi: z tą siecią K1 jeszcze się nie łączy");
    } else if (s->zabezpieczenie == K1_WIFI_OTWARTA) {
        polacz_start(karta, s, cel, NULL, 1, 1);
    } else if (k->c > 0) {
        if (k1_wpa_klucz_sieci(haslo, (size_t)k->c, cel->ssid, (size_t)cel->dl_ssid, pmk) == 0)
            polacz_start(karta, s, cel, pmk, 1, 1);
        else
            k1_log(TAG, "wifi: hasło w złym formacie");
    } else if (p != NULL && (j = k1_wifi_pamiec_znajdz(p, cel->ssid, cel->dl_ssid, 1)) >= 0) {
        polacz_start(karta, s, cel, p->w[j].pmk, 0, 1);
    }
    k1_wymaz(pmk, sizeof pmk);
}

/* WIFI_ZAPOMNIJ: the network of the name parts is forgotten (and left, when K1 is connected to it). */
static void zapomnij(const struct k1_komunikat *k)
{
    const struct k1_wifi_siec *s;
    const struct k1_wifi_cel *cel;
    const struct k1_wifi_pamiec *p = skarb_pamiec();
    uint8_t nazwa[K1_WIFI_NAZWA_MAKS];
    int karta, i = wybrana_ma == 3 ? lista_znajdz(wybrana, k->b, k->a, &karta, &s, &cel) : -1, j, ks, zs, dl, st;
    if (i < 0 || p == NULL ||
        (j = k1_wifi_pamiec_znajdz(p, cel->ssid, cel->dl_ssid, s->zabezpieczenie != K1_WIFI_OTWARTA)) < 0)
        return;
    skarb_zapomnij(j);
    st = polacz_stan(&ks, &zs, nazwa, &dl);
    if (st != K1_WIFI_P_BRAK && zs == k->a && dl == k->b && memcmp(nazwa, wybrana, (size_t)dl) == 0)
        polacz_rozlacz();
    else
        lista_wyslij();
}

int ustawienia_poll(struct pollfd *p)
{
    p->fd = ust;
    p->events = POLLIN;
    p->revents = 0;
    return 1;
}

void ustawienia_odbierz(const struct pollfd *p)
{
    int i;
    for (i = 0; i < 8 && ust >= 0 && p->revents; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(ust, K1_UID_USTAWIENIA, K1_KIER_DO_SIECI, &k, NULL);
        const int32_t pola[5] = { k.a, k.b, k.c, k.d, k.e };
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            ustawienia_kanal(-1);
            return;
        }
        if (w != K1_ODB_JEST) {
            k1_log(TAG, "wifi: odrzucono prośbę Ustawień");
        } else if (k.typ == K1_KOM_WIFI_HASLO) {
            k1_wifi_haslo_z_pol(pola, (int)k.obiekt, haslo);
            haslo_ma |= 1 << k.obiekt;
        } else if (k.typ == K1_KOM_WIFI_WYBRANA) {
            k1_wifi_nazwa_z_pol(pola, (int)k.obiekt, wybrana);
            wybrana_ma |= 1 << k.obiekt;
        } else if (k.typ == K1_KOM_WIFI_POLACZ) {
            polacz_z_ustawien(&k);
            wyczysc();
        } else if (k.typ == K1_KOM_WIFI_ZAPOMNIJ) {
            zapomnij(&k);
            wyczysc();
        }
        k1_wymaz(&k, sizeof k);
    }
}

void wybor_auto(void)
{
    const struct k1_wifi_pamiec *p = skarb_pamiec();
    const struct k1_wifi_wynik *w;
    int karta, i;
    if (p == NULL || p->n == 0 || !polacz_mozna_samemu(k1_teraz()) || (w = lista_teraz(&karta)) == NULL)
        return;
    for (i = 0; i < w->n; i++) { /* the strongest first */
        const struct k1_wifi_siec *s = &w->sieci[i];
        const struct k1_wifi_cel *cel = &w->cele[i];
        int haslo_ = s->zabezpieczenie != K1_WIFI_OTWARTA, j;
        if (!k1_wifi_mozna_polaczyc(s->zabezpieczenie, cel->grupa) ||
            (j = k1_wifi_pamiec_znajdz(p, cel->ssid, cel->dl_ssid, haslo_)) < 0)
            continue;
        k1_log(TAG, "wifi: zapamiętana sieć w zasięgu — łączę sam");
        polacz_start(karta, s, cel, haslo_ ? p->w[j].pmk : NULL, 0, 0);
        return;
    }
}

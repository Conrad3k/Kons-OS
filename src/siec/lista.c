/* K1 siec — the list of Wi-Fi networks (0.9.1 step 1, ADR 0041): nadzorca gives what the card heard (SIEC_WIFI_WYNIKI
 * with a sealed memfd), this reads it (wifi.c — the only place where the descriptions from the air are read) and sends
 * back the list: SIEC_WIFI_SIEC and two SIEC_WIFI_NAZWA per network, then SIEC_WIFI_LISTA — since step 2 with the state
 * of the connection, sent again whenever it changes. No name ever goes to the log (the networks around are the
 * neighbours'), only how many. Part of siec (usluga.h). */
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "pamiec.h"
#include "usluga.h"
#include "wifi.h"

static uint8_t bufor[K1_WIFI_WYNIKI_MAKS];
static struct k1_wifi_wynik wynik;
static int karta_listy; /* the card of the last result, 0: none yet */
static int zapisane_n = -1, zapisane_odrzucone = -1, zapisane_ukryte = -1;

static void wyslij(enum k1_komunikat_typ typ, uint32_t obiekt, const int32_t p[5])
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, obiekt, p[0], p[1], p[2], p[3], p[4]);
    if (k1_kanal_wyslij(NADZORCA, &k, sizeof k, -1) != 0)
        k1_koniec(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

const struct k1_wifi_wynik *lista_teraz(int *karta)
{
    *karta = karta_listy;
    return karta_listy != 0 ? &wynik : NULL;
}

int lista_znajdz(const uint8_t nazwa[K1_WIFI_NAZWA_MAKS], int dl, int zabezpieczenie, int *karta,
                 const struct k1_wifi_siec **s, const struct k1_wifi_cel **cel)
{
    int i;
    *karta = karta_listy;
    for (i = 0; i < wynik.n && karta_listy != 0; i++)
        if (wynik.sieci[i].dl == dl && wynik.sieci[i].zabezpieczenie == zabezpieczenie &&
            memcmp(wynik.sieci[i].nazwa, nazwa, (size_t)dl) == 0) {
            *s = &wynik.sieci[i];
            *cel = &wynik.cele[i];
            return i;
        }
    return -1;
}

/* The list to nadzorca: each network (SIEC_WIFI_SIEC, two SIEC_WIFI_NAZWA), then SIEC_WIFI_LISTA with the connection
 * (0.9.1 step 2: its state, its network in this list, the remembered networks of the list). */
void lista_wyslij(void)
{
    const struct k1_wifi_pamiec *pam = skarb_pamiec();
    uint8_t nazwa[K1_WIFI_NAZWA_MAKS];
    int i, ks, zs, dl, st = polacz_stan(&ks, &zs, nazwa, &dl), indeks = -1, maska = 0;
    if (karta_listy == 0)
        return;
    for (i = 0; i < wynik.n; i++) {
        const struct k1_wifi_siec *s = &wynik.sieci[i];
        int32_t p[5] = { s->poziom, s->zabezpieczenie, s->dbm, s->mhz, s->dl };
        int czesc;
        wyslij(K1_KOM_SIEC_WIFI_SIEC, (uint32_t)i, p);
        for (czesc = 0; czesc < 2; czesc++) {
            k1_wifi_nazwa_do_pol(s->nazwa, czesc, p);
            wyslij(K1_KOM_SIEC_WIFI_NAZWA, (uint32_t)(2 * i + czesc), p);
        }
        if (st != K1_WIFI_P_BRAK && ks == karta_listy && zs == s->zabezpieczenie && dl == s->dl &&
            memcmp(nazwa, s->nazwa, (size_t)dl) == 0)
            indeks = i;
        if (pam != NULL && k1_wifi_pamiec_znajdz(pam, wynik.cele[i].ssid, wynik.cele[i].dl_ssid,
                                                 s->zabezpieczenie != K1_WIFI_OTWARTA) >= 0)
            maska |= 1 << i;
    }
    {
        int32_t p[5] = { wynik.n, wynik.odrzucone, ks == karta_listy ? st : K1_WIFI_P_BRAK, indeks, maska };
        if (p[2] == K1_WIFI_P_BRAK)
            p[3] = -1;
        wyslij(K1_KOM_SIEC_WIFI_LISTA, (uint32_t)karta_listy, p);
    }
}

void wifi_wyniki(const struct k1_komunikat *k, int fd)
{
    int r = fd >= 0 ? k1_memfd_czytaj(fd, bufor, (size_t)k->b) : -1;
    if (fd >= 0)
        close(fd);
    if (r != 0 || k1_wifi_wyniki_czytaj(bufor, (size_t)k->b, k->a, &wynik) != 0) {
        k1_log(TAG, "wyniki szukania sieci Wi-Fi w złym formacie — pominięte");
        return;
    }
    karta_listy = (int)k->obiekt;
    lista_wyslij();
    if (wynik.n != zapisane_n || wynik.odrzucone != zapisane_odrzucone || wynik.ukryte != zapisane_ukryte) {
        k1_log(TAG, "sieci Wi-Fi w pobliżu: %d (punktów dostępu %d, ukryte %d, odrzucone opisy %d)", wynik.n, k->a,
               wynik.ukryte, wynik.odrzucone);
        zapisane_n = wynik.n;
        zapisane_odrzucone = wynik.odrzucone;
        zapisane_ukryte = wynik.ukryte;
    }
    wybor_auto(); /* 0.9.1 step 2: the strongest remembered network, when not connected */
}

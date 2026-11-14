/* K1 siec — the list of Wi-Fi networks (0.9.1 step 1, ADR 0041): nadzorca gives what the card heard (SIEC_WIFI_WYNIKI
 * with a sealed memfd), this reads it (wifi.c — the only place where the descriptions from the air are read) and sends
 * back the list: SIEC_WIFI_SIEC and two SIEC_WIFI_NAZWA per network, then SIEC_WIFI_LISTA. No name ever goes to the log
 * (the networks around are the neighbours'), only how many. Part of siec (usluga.h). */
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "usluga.h"
#include "wifi.h"

static uint8_t bufor[K1_WIFI_WYNIKI_MAKS];
static struct k1_wifi_wynik wynik;
static int zapisane_n = -1, zapisane_odrzucone = -1, zapisane_ukryte = -1;

static void wyslij(enum k1_komunikat_typ typ, uint32_t obiekt, const int32_t p[5])
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, obiekt, p[0], p[1], p[2], p[3], p[4]);
    if (k1_kanal_wyslij(NADZORCA, &k, sizeof k, -1) != 0)
        k1_koniec(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

void wifi_wyniki(const struct k1_komunikat *k, int fd)
{
    int i, r = fd >= 0 ? k1_memfd_czytaj(fd, bufor, (size_t)k->b) : -1;
    if (fd >= 0)
        close(fd);
    if (r != 0 || k1_wifi_wyniki_czytaj(bufor, (size_t)k->b, k->a, &wynik) != 0) {
        k1_log(TAG, "wyniki szukania sieci Wi-Fi w złym formacie — pominięte");
        return;
    }
    for (i = 0; i < wynik.n; i++) {
        const struct k1_wifi_siec *s = &wynik.sieci[i];
        int32_t p[5] = { s->poziom, s->zabezpieczenie, s->dbm, s->mhz, s->dl };
        int czesc;
        wyslij(K1_KOM_SIEC_WIFI_SIEC, (uint32_t)i, p);
        for (czesc = 0; czesc < 2; czesc++) {
            k1_wifi_nazwa_do_pol(s->nazwa, czesc, p);
            wyslij(K1_KOM_SIEC_WIFI_NAZWA, (uint32_t)(2 * i + czesc), p);
        }
    }
    {
        int32_t p[5] = { wynik.n, wynik.odrzucone, 0, 0, 0 };
        wyslij(K1_KOM_SIEC_WIFI_LISTA, k->obiekt, p);
    }
    if (wynik.n != zapisane_n || wynik.odrzucone != zapisane_odrzucone || wynik.ukryte != zapisane_ukryte) {
        k1_log(TAG, "sieci Wi-Fi w pobliżu: %d (punktów dostępu %d, ukryte %d, odrzucone opisy %d)", wynik.n, k->a,
               wynik.ukryte, wynik.odrzucone);
        zapisane_n = wynik.n;
        zapisane_odrzucone = wynik.odrzucone;
        zapisane_ukryte = wynik.ukryte;
    }
}

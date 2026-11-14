/* K1 — checking the messages between nadzorca and the service siec (0.9.0 step 6, ADR 0039; the split: k1_kom_wew.h). */
#include "k1_kom_wew.h"

#include "k1_siec.h"

#include <string.h>

static int karta(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_SIEC_KARTA_MAKS;
}

/* nadzorca ↔ siec; 0 also for a type of another group. */
int k1_kom_sprawdz_siec(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    uint8_t mac[6];
    switch (k->typ) {
    case K1_KOM_SIEC_KABEL:
        k1_siec_mac_z_pol(k->a, k->b, mac);
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) && w(k->b, 0, 0xFFFF) && k1_siec_mac_dobry(mac) &&
               zera(k, 2);
    case K1_KOM_SIEC_ROZLACZONO:
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) && w(k->a, K1_SIEC_KABEL_WYJETY, K1_SIEC_WIFI_ROZLACZONE) &&
               zera(k, 1);
    case K1_KOM_SIEC_ADRES:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) &&
               k1_siec_adres_dobry((uint32_t)k->a, k->b, (uint32_t)k->c) &&
               w(k->d, K1_SIEC_DZIERZAWA_MIN, K1_SIEC_DZIERZAWA_MAKS) && k->e == 0;
    case K1_KOM_SIEC_ZWOLNIJ:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && zera(k, 0);
    case K1_KOM_SIEC_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) &&
               (k->a == K1_KOM_SIEC_ADRES || k->a == K1_KOM_SIEC_ZWOLNIJ || k->a == K1_KOM_SIEC_ZEGAR) &&
               w(k->b, K1_SIEC_BLAD_MIN, 0) && zera(k, 2);
    case K1_KOM_SIEC_CZAS_GNIAZDO:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && k1_siec_serwer_dobry((uint32_t)k->a) &&
               w(k->b, 0, K1_SIEC_ZRODLA_MAKS - 1) && zera(k, 2);
    case K1_KOM_SIEC_GNIAZDO:
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) && k1_siec_serwer_dobry((uint32_t)k->a) &&
               w(k->b, 0, K1_SIEC_ZRODLA_MAKS - 1) && w(k->c, K1_SIEC_BLAD_MIN, 0) && zera(k, 3);
    case K1_KOM_SIEC_ZEGAR: {
        int64_t s = (int64_t)k1_kom_u64(k->a, k->b);
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && s >= K1_SIEC_CZAS_MIN && s <= K1_SIEC_CZAS_MAKS &&
               w(k->c, 0, 999999999) && w(k->d, 2, K1_SIEC_ZRODLA_MAKS) && k->e == 0;
    }
    case K1_KOM_SIEC_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_SIEC_BRAK_KARTY, K1_SIEC_STAN_MAKS) &&
               w(k->b, 0, K1_SIEC_LISTA_MAKS) && w(k->c, 0, 0x7fffffff) &&
               (k->c == 0 || k->a == K1_SIEC_POLACZONO || k->a == K1_SIEC_WIFI_POLACZONO) && w(k->d, 0, 0x7fffffff) &&
               (k->a == K1_SIEC_WIFI_POLACZONO ? w(k->e, 1, 4) : k->e == 0);
    case K1_KOM_SIEC_KARTA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_SIEC_LISTA_MAKS &&
               w(k->b, K1_SIEC_PRZEWODOWA, K1_SIEC_INNA) && w(k->c, 0, K1_SIEC_KF_WSZYSTKIE) &&
               (!(k->c & K1_SIEC_KF_KABEL) || (k->b == K1_SIEC_PRZEWODOWA && (k->c & K1_SIEC_KF_STEROWNIK))) &&
               (!(k->c & K1_SIEC_KF_AKTYWNA) || (k->b == K1_SIEC_PRZEWODOWA && (k->c & K1_SIEC_KF_KABEL)) ||
                (k->b == K1_SIEC_WIFI && (k->c & K1_SIEC_KF_STEROWNIK))) && zera(k, 3);
    case K1_KOM_SIEC_SZCZEGOLY:
        k1_siec_mac_z_pol(k->c, k->d, mac);
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_SIEC_LISTA_MAKS && w(k->a, 0, K1_SIEC_PREDKOSC_MAKS) &&
               w(k->b, K1_SIEC_DUPLEKS_NIEZNANY, K1_SIEC_DUPLEKS_PELNY) && w(k->d, 0, 0xFFFF) && k1_siec_mac_dobry(mac) &&
               k->e == 0;
    case K1_KOM_SIEC_STEROWNIK: {
        char t[K1_SIEC_STEROWNIK_MAKS];
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
        memcpy(t, p, sizeof t);
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_SIEC_LISTA_MAKS && k1_siec_sterownik_dobry(t);
    }
    case K1_KOM_SIEC_ADRESY:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_SIEC_LISTA_MAKS &&
               k1_siec_adres_dobry((uint32_t)k->a, k->b, (uint32_t)k->c) && zera(k, 3);
    default:
        return 0;
    }
}

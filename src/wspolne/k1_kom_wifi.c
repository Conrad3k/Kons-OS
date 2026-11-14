/* K1 — checking the Wi-Fi messages (0.9.1 step 1, ADR 0041): nadzorca ↔ siec and nadzorca ↔ Ustawienia. */
#include "k1_kom_wew.h"

#include "k1_siec.h"
#include "k1_wifi.h"

/* SIEC_WIFI_SIEC / WIFI_SIEC: one network of the list. */
static int siec(const struct k1_komunikat *k)
{
    return k->obiekt < K1_WIFI_LISTA_MAKS && k1_wifi_pola_dobre(k->a, k->b, k->c, k->d, k->e);
}

/* SIEC_WIFI_NAZWA / WIFI_NAZWA: a part of a name; the whole name is checked when both parts are in. */
static int nazwa(const struct k1_komunikat *k)
{
    return k->obiekt < 2u * K1_WIFI_LISTA_MAKS && ((k->obiekt & 1u) == 0 || zera(k, 3));
}

/* Wi-Fi; 0 also for a type of another group. */
int k1_kom_sprawdz_wifi(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_SIEC_WIFI_WYNIKI:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_SIEC_KARTA_MAKS &&
               w(k->a, 0, K1_WIFI_REKORDY_MAKS) && w(k->b, 0, (int32_t)K1_WIFI_WYNIKI_MAKS) &&
               (k->a > 0 || k->b == 0) && zera(k, 2);
    case K1_KOM_SIEC_WIFI_SIEC:
        return kier == K1_KIER_DO_NADZORCY && siec(k);
    case K1_KOM_WIFI_SIEC:
        return kier == K1_KIER_OD_NADZORCY && siec(k);
    case K1_KOM_SIEC_WIFI_NAZWA:
        return kier == K1_KIER_DO_NADZORCY && nazwa(k);
    case K1_KOM_WIFI_NAZWA:
        return kier == K1_KIER_OD_NADZORCY && nazwa(k);
    case K1_KOM_SIEC_WIFI_LISTA:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_SIEC_KARTA_MAKS &&
               w(k->a, 0, K1_WIFI_LISTA_MAKS) && w(k->b, 0, K1_WIFI_REKORDY_MAKS) && zera(k, 2);
    case K1_KOM_WIFI_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_WIFI_BRAK, K1_WIFI_STAN_MAKS) &&
               w(k->b, 0, K1_WIFI_LISTA_MAKS) && (k->b == 0 || k->a == K1_WIFI_WLACZONE) && w(k->c, 0, 1) && zera(k, 3);
    case K1_KOM_WIFI_WLACZ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_WIFI_SZUKAJ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    default:
        return 0;
    }
}

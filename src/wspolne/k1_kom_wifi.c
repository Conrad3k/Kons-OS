/* K1 — checking the Wi-Fi messages (0.9.1, ADR 0041): nadzorca ↔ siec, nadzorca ↔ Ustawienia, Ustawienia → siec. */
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

static int karta(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_SIEC_KARTA_MAKS;
}

/* SSID, KLUCZ_CZESC: a part of 20 + 12 bytes (part 1: d, e = 0). */
static int czesc(const struct k1_komunikat *k)
{
    return k->obiekt <= 1 && (k->obiekt == 0 || zera(k, 3));
}

/* 0.9.1 step 2: joining a network (siec ↔ nadzorca, Ustawienia → siec). */
static int polaczenie(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
    uint8_t mac[6];
    switch (k->typ) {
    case K1_KOM_SIEC_WIFI_SSID:
    case K1_KOM_SIEC_WIFI_KLUCZ_CZESC:
        return kier == K1_KIER_DO_NADZORCY && czesc(k);
    case K1_KOM_SIEC_WIFI_POLACZ:
        k1_siec_mac_z_pol(k->a, k->b, mac);
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && w(k->b, 0, 0xFFFF) && k1_siec_mac_dobry(mac) &&
               w(k->c, K1_WIFI_MHZ_MIN, K1_WIFI_MHZ_MAKS) && w(k->d, 1, K1_WIFI_NAZWA_MAKS) &&
               w(k->e, K1_WIFI_TRYB_OTWARTA, K1_WIFI_TRYB_MAKS);
    case K1_KOM_SIEC_WIFI_ROZLACZ:
    case K1_KOM_SIEC_WIFI_OTWORZ:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && zera(k, 0);
    case K1_KOM_SIEC_WIFI_KLUCZ:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) &&
               ((k->a == 1 && k->b == 0 && k->c == K1_WIFI_SZYFR_CCMP && zera(k, 3)) ||
                (k->a == 2 && w(k->b, 1, 3) && w(k->c, K1_WIFI_SZYFR_CCMP, K1_WIFI_SZYFR_TKIP) && w(k->e, 0, 0xFFFF)));
    case K1_KOM_SIEC_WIFI_RADIO_USTAW:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && k->a == 0 && zera(k, 1);
    case K1_KOM_SIEC_WIFI_ZDARZENIE:
        if (kier != K1_KIER_OD_NADZORCY || !karta(k->obiekt))
            return 0;
        k1_siec_mac_z_pol(k->c, k->d, mac);
        switch (k->a) {
        case K1_WIFI_Z_LACZE:
            return k->b == 0 && w(k->d, 0, 0xFFFF) && k1_siec_mac_dobry(mac) && k->e == 0;
        case K1_WIFI_Z_NIEUDANE:
            return (k->b == -1 || w(k->b, 1, 0xFFFF)) && zera(k, 2);
        case K1_WIFI_Z_ROZLACZONO:
            return w(k->b, 0, 0xFFFF) && zera(k, 2);
        case K1_WIFI_Z_KLUCZ:
        case K1_WIFI_Z_OTWARTE:
            return w(k->b, K1_SIEC_BLAD_MIN, 0) && zera(k, 2);
        default:
            return k->a == K1_WIFI_Z_SKOJARZONO && zera(k, 1);
        }
    case K1_KOM_SIEC_WIFI_RADIO:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_SIEC_WIFI_EAPOL:
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) && zera(k, 0);
    case K1_KOM_SIEC_USTAWIENIA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_WIFI_HASLO:
        return kier == K1_KIER_DO_SIECI && k->obiekt <= 3 && k1_wifi_haslo_pola_dobre((int)k->obiekt, p);
    case K1_KOM_WIFI_WYBRANA:
        return kier == K1_KIER_DO_SIECI && k->obiekt <= 1 && (k->obiekt == 0 || zera(k, 3));
    case K1_KOM_WIFI_POLACZ:
        return kier == K1_KIER_DO_SIECI && k->obiekt == 0 && w(k->a, K1_WIFI_OTWARTA, K1_WIFI_ZABEZP_MAKS) &&
               w(k->b, 1, K1_WIFI_NAZWA_MAKS) &&
               (k->c == 0 || w(k->c, K1_WIFI_HASLO_MIN, K1_WIFI_HASLO_MAKS)) && zera(k, 3);
    case K1_KOM_WIFI_ZAPOMNIJ:
        return kier == K1_KIER_DO_SIECI && k->obiekt == 0 && w(k->a, K1_WIFI_OTWARTA, K1_WIFI_ZABEZP_MAKS) &&
               w(k->b, 1, K1_WIFI_NAZWA_MAKS) && zera(k, 2);
    case K1_KOM_WIFI_POLACZENIE:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_WIFI_P_BRAK, K1_WIFI_P_MAKS) &&
               w(k->b, -1, K1_WIFI_LISTA_MAKS - 1) && (k->b < 0 || k->a != K1_WIFI_P_BRAK) &&
               w(k->c, 0, 0xFFFF) && zera(k, 3);
    default:
        return 0;
    }
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
    case K1_KOM_SIEC_WIFI_LISTA: /* 0.9.1 step 2: c=K1_WIFI_P_*, d=its network (-1: none), e=remembered (bit per network) */
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && w(k->a, 0, K1_WIFI_LISTA_MAKS) &&
               w(k->b, 0, K1_WIFI_REKORDY_MAKS) && w(k->c, K1_WIFI_P_BRAK, K1_WIFI_P_MAKS) && w(k->d, -1, k->a - 1) &&
               (k->d < 0 || k->c != K1_WIFI_P_BRAK) && w(k->e, 0, (1 << k->a) - 1);
    case K1_KOM_WIFI_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_WIFI_BRAK, K1_WIFI_STAN_MAKS) &&
               w(k->b, 0, K1_WIFI_LISTA_MAKS) && (k->b == 0 || k->a == K1_WIFI_WLACZONE) && w(k->c, 0, 1) && zera(k, 3);
    case K1_KOM_WIFI_WLACZ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_WIFI_SZUKAJ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    default:
        return polaczenie(k, kier);
    }
}

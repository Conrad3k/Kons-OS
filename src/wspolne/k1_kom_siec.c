/* K1 — checking the messages between nadzorca and the service siec (0.9.0 step 6, ADR 0039; the split: k1_kom_wew.h). */
#include "k1_kom_wew.h"

#include "k1_siec.h"

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
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) && w(k->a, K1_SIEC_KABEL_WYJETY, K1_SIEC_KARTA_ZNIKNELA) &&
               zera(k, 1);
    case K1_KOM_SIEC_ADRES:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) &&
               k1_siec_adres_dobry((uint32_t)k->a, k->b, (uint32_t)k->c) &&
               w(k->d, K1_SIEC_DZIERZAWA_MIN, K1_SIEC_DZIERZAWA_MAKS) && k->e == 0;
    case K1_KOM_SIEC_ZWOLNIJ:
        return kier == K1_KIER_DO_NADZORCY && karta(k->obiekt) && zera(k, 0);
    case K1_KOM_SIEC_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && karta(k->obiekt) &&
               (k->a == K1_KOM_SIEC_ADRES || k->a == K1_KOM_SIEC_ZWOLNIJ) && w(k->b, K1_SIEC_BLAD_MIN, 0) && zera(k, 2);
    default:
        return 0;
    }
}

/* K1 — checking the messages between nadzorca and the VMM of a zone (0.10.0 step 2, ADR 0043 T9: the browser's
 * connections; the split: k1_kom_wew.h). The VMM runs hostile code's requests through it: every field is checked here,
 * the address once more by nadzorca against the network it has (k1_siec_dla_strefy). */
#include "k1_kom_wew.h"

#include "k1_siec.h"

static int polaczenie(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_STREFA_POLACZENIA_MAKS;
}

/* nadzorca ↔ a zone's VMM; 0 also for a type of another group. */
int k1_kom_sprawdz_strefa(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_STREFA_POLACZ:
        return kier == K1_KIER_DO_NADZORCY && polaczenie(k->obiekt) &&
               ((k->c == K1_STREFA_TCP && k1_siec_serwer_dobry((uint32_t)k->a) && w(k->b, 1, 65535)) ||
                (k->c == K1_STREFA_DNS && k->a == 0 && k->b == 0)) &&
               zera(k, 3);
    case K1_KOM_STREFA_GNIAZDO:
        return kier == K1_KIER_OD_NADZORCY && polaczenie(k->obiekt) && w(k->a, K1_STREFA_TCP, K1_STREFA_DNS) &&
               zera(k, 1);
    case K1_KOM_STREFA_ODMOWA:
        return kier == K1_KIER_OD_NADZORCY && polaczenie(k->obiekt) && w(k->a, 1, K1_STREFA_ODM_MAKS) && zera(k, 1);
    default:
        return 0;
    }
}

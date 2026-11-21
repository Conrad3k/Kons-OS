/* K1 — checking the messages of a file from a zone into Pliki (0.11.0 step 2, ADR 0052): a zone's VMM ↔ nadzorca, nadzorca
 * ↔ gui (the window that asks the user). The VMM speaks for a hostile zone: every field here, the memfd (sealed, its
 * size, a name of K1) at nadzorca, the content at skarbiec. */
#include "k1_kom_wew.h"

static int plik(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_PS_NR_MAKS;
}

static int rozmiar(int32_t niskie, int32_t wysokie)
{
    return k1_kom_u64(niskie, wysokie) <= K1_PS_ROZMIAR_MAKS;
}

/* 1 for a valid message of this group, 0 for an invalid one and for a type of another group. */
int k1_kom_sprawdz_pobrane(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_STREFA_PLIK:
        return kier == K1_KIER_DO_NADZORCY && plik(k->obiekt) && rozmiar(k->a, k->b) && w(k->c, 1, K1_DOK_NAZWA - 1) &&
               zera(k, 3);
    case K1_KOM_STREFA_PLIK_KAWALEK:
        return kier == K1_KIER_DO_NADZORCY && plik(k->obiekt) && w(k->a, 1, (int32_t)K1_DOK_KAWALEK) &&
               w(k->b, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) && w(k->c, 0, 1) && (k->c == 1 || k->a == (int32_t)K1_DOK_KAWALEK) &&
               zera(k, 3);
    case K1_KOM_STREFA_PLIK_KONIEC:
        return kier == K1_KIER_DO_NADZORCY && plik(k->obiekt) && (k->a == K1_PS_W_BLAD || k->a == K1_PS_W_ZOSTAWIONY) &&
               zera(k, 1);
    case K1_KOM_STREFA_PLIK_ODP:
        return kier == K1_KIER_OD_NADZORCY && plik(k->obiekt) && w(k->a, K1_PLIK_WYSLIJ, K1_PLIK_GOTOWE) &&
               w(k->b, 0, k->a == K1_PLIK_GOTOWE ? K1_PS_W_MAKS : 0) && zera(k, 2);
    case K1_KOM_POBRANIE:
        return kier == K1_KIER_OD_NADZORCY && w((int32_t)k->obiekt, 1, K1_STREFY_MAKS) && plik((uint32_t)k->a) &&
               k->a > 0 && rozmiar(k->b, k->c) && w(k->d, 1, K1_DOK_NAZWA - 1) && k->e == 0;
    case K1_KOM_POBRANIE_DECYZJA:
        return kier == K1_KIER_DO_NADZORCY && w((int32_t)k->obiekt, 1, K1_STREFY_MAKS) && k->a > 0 && w(k->b, 0, 1) &&
               zera(k, 2);
    case K1_KOM_POBRANIE_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && w((int32_t)k->obiekt, 1, K1_STREFY_MAKS) && k->a > 0 &&
               w(k->b, 0, K1_PS_W_MAKS) && zera(k, 2);
    default:
        return 0;
    }
}

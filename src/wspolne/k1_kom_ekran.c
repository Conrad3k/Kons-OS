/* K1 — checking messages of ekran and its clients: windows, input, settings (ADR 0008; the split: k1_kom_wew.h). */
#include "k1_kom_wew.h"

#include "k1_uslugi.h"

static int powierzchnia(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_POW_MAKS;
}

int k1_ustawienie_poprawne(int32_t klucz, int32_t wartosc)
{
    switch (klucz) {
    case K1_UST_MOTYW:
    case K1_UST_SEKUNDY:
        return w(wartosc, 0, 1);
    case K1_UST_MYSZ:
        return w(wartosc, 1, 3);
    case K1_UST_TAPETA:
        return w(wartosc, 0, K1_TAPETY - 1) || wartosc == K1_TAPETA_ZDJECIE;
    case K1_UST_ZDJECIE:
        return w(wartosc, 0, 1);
    case K1_UST_ROZDZIELCZOSC:
        return w(wartosc, 1, K1_TRYBY_MAKS);
    default: /* the default application of a file type: one that opens it */
        return w(klucz, K1_UST_OTWIERANIE, K1_UST_MAKS) && k1_dok_otwiera(wartosc, klucz - K1_UST_OTWIERANIE + 1);
    }
}

/* Messages of ekran and its clients (ADR 0008); 0 also for a type of another group. */
int k1_kom_sprawdz_ekran(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_POWIERZCHNIA:
        if (kier != K1_KIER_DO_EKRANU || !powierzchnia(k->obiekt) || !w(k->a, 1, K1_POW_MAKS_BOK) ||
            !w(k->b, 1, K1_POW_MAKS_BOK))
            return 0;
        if (k->c == K1_POW_OKNO || k->c == K1_POW_PULPIT)
            return k->d == 0 && k->e == 0;
        return k->c == K1_POW_PANEL && w(k->d, K1_POZ_MIN, K1_POZ_MAKS) && w(k->e, K1_POZ_MIN, K1_POZ_MAKS);
    case K1_KOM_ZATWIERDZ:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && w(k->b, 0, K1_POW_MAKS_BOK) &&
               w(k->c, 0, K1_POW_MAKS_BOK) && w(k->d, 0, K1_POW_MAKS_BOK) && w(k->e, 0, K1_POW_MAKS_BOK);
    case K1_KOM_POKAZ:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_PRZESUN:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && w(k->a, K1_POZ_MIN, K1_POZ_MAKS) &&
               w(k->b, K1_POZ_MIN, K1_POZ_MAKS) && zera(k, 2);
    case K1_KOM_PRZECIAGNIJ:
    case K1_KOM_MINIMALIZUJ:
    case K1_KOM_USUN:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && zera(k, 0);
    case K1_KOM_AKTYWUJ_OKNO:
    case K1_KOM_MINIMALIZUJ_OKNO:
    case K1_KOM_ZAMKNIJ_OKNO:
        return kier == K1_KIER_DO_EKRANU && k->obiekt != 0 && zera(k, 0);
    case K1_KOM_USTAW: /* whether ekran keeps a photo is not a setting anyone sets */
        return kier == K1_KIER_DO_EKRANU && k->obiekt == 0 && k->a != K1_UST_ZDJECIE && k1_ustawienie_poprawne(k->a, k->b) &&
               zera(k, 2);
    case K1_KOM_TAPETA_ZDJECIE:
        return kier == K1_KIER_DO_EKRANU && k->obiekt == 0 && w(k->a, 1, K1_DEK_MAKS_W) && w(k->b, 1, K1_DEK_MAKS_H) &&
               zera(k, 2);

    case K1_KOM_WITAJ:
        return kier == K1_KIER_OD_EKRANU && k->obiekt == 0 && w(k->a, 1, K1_POZ_MAKS) && w(k->b, 1, K1_POZ_MAKS) &&
               w(k->c, 1, K1_ROLA_MAKS) && w(k->d, 0, K1_KLIENT_NR_MAKS) && k->e == 0;
    case K1_KOM_WSKAZNIK:
        return kier == K1_KIER_OD_EKRANU && k->obiekt <= K1_POW_MAKS && w(k->a, -K1_POZ_MAKS, K1_POZ_MAKS) &&
               w(k->b, -K1_POZ_MAKS, K1_POZ_MAKS) && w(k->c, K1_WSK_RUCH, K1_WSK_WYJSCIE) &&
               w(k->d, K1_PRZYCISK_BRAK, K1_PRZYCISK_SRODKOWY) && w(k->e, 0, K1_MOD_MASKA);
    case K1_KOM_KLAWISZ:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && w(k->a, 1, K1_KLAWISZ_MAKS) && w(k->b, 0, 2) &&
               w(k->c, 0, K1_MOD_MASKA) && zera(k, 3);
    case K1_KOM_FOKUS:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_RAMKA:
    case K1_KOM_ZAMKNIJ:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && zera(k, 0);
    case K1_KOM_OKNO:
        return kier == K1_KIER_OD_EKRANU && k->obiekt != 0 && w(k->b, 1, K1_OKNO_STAN_MAKS) &&
               ((w(k->a, 1, K1_APL_MAKS) && k->c == 0) || (k->a == 0 && w(k->c, 1, K1_STREFY_MAKS))) && zera(k, 3);
    case K1_KOM_USTAWIENIE:
        return kier == K1_KIER_OD_EKRANU && k->obiekt == 0 && k1_ustawienie_poprawne(k->a, k->b) && zera(k, 2);
    case K1_KOM_TRYB:
        return kier == K1_KIER_OD_EKRANU && k->obiekt >= 1 && k->obiekt <= K1_TRYBY_MAKS &&
               w(k->a, K1_TRYB_MIN_SZER, K1_POW_MAKS_BOK) && w(k->b, K1_TRYB_MIN_WYS, K1_POW_MAKS_BOK) &&
               w(k->c, 0, 1000) && w(k->d, 0, 1) && k->e == 0;
    default:
        return 0;
    }
}

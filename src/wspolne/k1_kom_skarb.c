/* K1 — checking messages of skarbiec and its clients: the vault, the user's files, the file picker (ADR 0015, 0029,
 * 0030; the split: k1_kom_wew.h). */
#include "k1_kom_wew.h"

#include "k1_uslugi.h"

/* Messages of skarbiec and its clients; 0 also for a type of another group. */
int k1_kom_sprawdz_skarb(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_SKARB_CZYTAJ:
    case K1_KOM_SKARB_USUN:
        return kier == K1_KIER_DO_SKARBCA && k1_ob_poprawny(k->obiekt) && zera(k, 0);
    case K1_KOM_SKARB_ZAPISZ: /* a piece of a zone's disk: never more than one piece */
        return kier == K1_KIER_DO_SKARBCA && k1_ob_poprawny(k->obiekt) &&
               w(k->a, 0, k->obiekt >= K1_OB_DYSK ? (int32_t)K1_STREFA_DYSK_KAWALEK : K1_SKARB_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_ODBLOKUJ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && w(k->a, 1, K1_HASLO_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_HASLO:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && w(k->a, 2, 2 * K1_HASLO_MAKS + 2) && zera(k, 1);
    case K1_KOM_SKARB_INFO:
    case K1_KOM_SKARB_POWIEKSZ:
    case K1_KOM_SKARB_PODZIAL:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && zera(k, 0);
    case K1_KOM_SKARB_CZESC:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_CZESCI_MAKS &&
               k->a >= 0 && w(k->b, 0, k->obiekt >= K1_CZESC_STREFY ? 0 : K1_DOK_MAKS) &&
               zera(k, 2);
    case K1_KOM_SKARB_ZWOLNIONO: /* 0.10.10 step 1 */
    case K1_KOM_POWIEKSZ_DANE:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_SKARB_MIEJSCE:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt == 1 && w(k->b, 0, K1_PAMIEC_MAKS_MIB) && w(k->a, 0, k->b) &&
               w(k->c, 0, K1_DOK_MAKS) && w(k->d, 0, K1_PAMIEC_MAKS_MIB) && w(k->e, 0, 1);
    case K1_KOM_SKARB_STAN:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, K1_SKARB_TRWALY, K1_SKARB_ZABLOKOWANY) &&
               w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_SKARB_URUCHOM:
        return kier == K1_KIER_DO_NADZORCY && (k->obiekt == 0 || (k->obiekt >= K1_DOK_PIERWSZY && k->obiekt <= K1_DOK_ID_MAKS)) &&
               w(k->a, 1, K1_APL_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_DANE:
        return kier == K1_KIER_OD_SKARBCA && k1_ob_poprawny(k->obiekt) &&
               w(k->a, 0, k->obiekt >= K1_OB_DYSK ? (int32_t)K1_STREFA_DYSK_KAWALEK : K1_SKARB_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_WYNIK:
        return kier == K1_KIER_OD_SKARBCA && k1_ob_poprawny(k->obiekt) &&
               (w(k->a, K1_KOM_SKARB_CZYTAJ, K1_KOM_SKARB_INFO) || k->a == K1_KOM_SKARB_POWIEKSZ ||
                k->a == K1_KOM_SKARB_PODZIAL) &&
               w(k->b, K1_MAG_WYNIK_MIN, 0) &&
               w(k->c, K1_SKARB_TRWALY, K1_SKARB_ZABLOKOWANY) && w(k->d, 0, 3) && w(k->e, 0, K1_HASLO_PRZERWA);
    default:
        return 0;
    }
}

int k1_kom_dok_prosba(int typ)
{
    return (typ >= K1_KOM_DOK_SPIS && typ <= K1_KOM_DOK_OPROZNIJ) || (typ >= K1_KOM_DOK_OTWORZ && typ <= K1_KOM_DOK_WYBRANO) ||
           typ == K1_KOM_DOK_KAWALEK || typ == K1_KOM_DOK_KONIEC || typ == K1_KOM_DOK_CZAS ||
           typ == K1_KOM_DOK_DO_STREFY; /* 0.11.0 step 3 */
}

/* Opening a file for an application, the file picker (0.8.0 step 2, ADR 0030); 0 also for another type. */
static int sprawdz_wybor(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_DOK_OTWORZ:
        return kier == K1_KIER_DO_SKARBCA && wpis_uzytkownika(k->obiekt) && w(k->a, 1, K1_APL_MAKS) &&
               k->a != K1_APL_PLIKI && zera(k, 1);
    case K1_KOM_DOK_WYBIERZ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 0 && w(k->a, 0, K1_TYPY_MAKS) && w(k->b, 0, K1_RODZINY_MAKS) &&
               (k->a == 0 || k->b == 0) && zera(k, 2);
    case K1_KOM_DOK_WYBRANO:
    case K1_KOM_DOK_WYBRANY:
        return kier == (k->typ == K1_KOM_DOK_WYBRANO ? K1_KIER_DO_SKARBCA : K1_KIER_OD_SKARBCA) &&
               (k->obiekt == 0 || wpis_uzytkownika(k->obiekt)) && zera(k, 0);
    case K1_KOM_DOK_PROSBA:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) && w(k->b, 0, K1_TYPY_MAKS) &&
               w(k->c, 0, K1_RODZINY_MAKS) && (k->b == 0 || k->c == 0) && zera(k, 3);
    default:
        return 0;
    }
}

/* "More pieces follow" (0.8.1 step 4): 0, or 1 with a whole piece. */
static int dalej(int32_t flaga, int32_t dlugosc)
{
    return flaga == 0 || (flaga == 1 && dlugosc == (int32_t)K1_DOK_KAWALEK);
}

/* Messages about the user's files (0.8.0, ADR 0029); 0 also for a type of another group. */
int k1_kom_sprawdz_dok(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    const int32_t maks = (int32_t)K1_DOK_KAWALEK; /* content travels in pieces (0.8.1 step 4) */
    if (k->typ == K1_KOM_DOK_KAWALEK)
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 0 && w(k->a, 1, maks) && zera(k, 1);
    if (k->typ == K1_KOM_DOK_KONIEC)
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    if (k->typ == K1_KOM_DOK_CZAS)
        return kier == K1_KIER_DO_SKARBCA && wpis_uzytkownika(k->obiekt) && k1_kom_u64(k->a, k->b) >= 1 &&
               k1_kom_u64(k->a, k->b) <= K1_DOK_CZAS_MAKS && zera(k, 2);
    if (k->typ < K1_KOM_DOK_SPIS || k->typ > K1_KOM_DOK_ZMIANA)
        return sprawdz_wybor(k, kier);
    if (kier != (k->typ < K1_KOM_DOK_LISTA ? K1_KIER_DO_SKARBCA : K1_KIER_OD_SKARBCA))
        return 0;
    switch (k->typ) {
    case K1_KOM_DOK_SPIS:
    case K1_KOM_DOK_OPROZNIJ:
    case K1_KOM_DOK_ZMIANA:
        return k->obiekt == 0 && zera(k, 0);
    case K1_KOM_DOK_CZYTAJ:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 1, (int32_t)K1_DOK_KAWALEK) &&
               w(k->b, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) && zera(k, 2);
    case K1_KOM_DOK_NOWY:
        return wpis(k->obiekt) && w(k->a, K1_DOK_PLIK, K1_DOK_FOLDER) && w(k->b, 1, K1_DOK_NAZWA - 1) &&
               w(k->c, 0, k->a == K1_DOK_FOLDER ? 0 : maks) && (k->d == 0 || (k->a == K1_DOK_PLIK && k1_poch_dobre(k->d))) &&
               (k->a == K1_DOK_FOLDER ? k->e == 0 : dalej(k->e, k->c));
    case K1_KOM_DOK_ZAPISZ:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 0, maks) && dalej(k->b, k->a) && zera(k, 2);
    case K1_KOM_DOK_NAZWA:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 1, K1_DOK_NAZWA - 1) && w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_DOK_PRZENIES:
        return wpis_uzytkownika(k->obiekt) && k->a >= 1 && wpis((uint32_t)k->a) && w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_DOK_PRZYWROC:
        return wpis_uzytkownika(k->obiekt) && (k->a == 0 || wpis((uint32_t)k->a)) && zera(k, 1);
    case K1_KOM_DOK_KOSZ:
    case K1_KOM_DOK_USUN:
        return wpis_uzytkownika(k->obiekt) && zera(k, 0);
    case K1_KOM_DOK_LISTA:
        return k->obiekt == 0 && w(k->a, K1_DOK_LISTA_NAGL, K1_DOK_LISTA_MAKS) && w(k->b, 0, K1_DOK_MAKS) &&
               k->a == K1_DOK_LISTA_NAGL + k->b * K1_DOK_WPIS_BAJTY && zera(k, 2);
    case K1_KOM_DOK_TRESC:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 0, (int32_t)K1_DOK_KAWALEK) &&
               w(k->b, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) && rozmiar_pliku(k->c, k->d, (uint64_t)k->b * K1_DOK_KAWALEK + (uint64_t)k->a) && k->e == 0;
    case K1_KOM_DOK_WYNIK:
        return (k->obiekt == 0 || wpis(k->obiekt)) && k1_kom_dok_prosba(k->a) &&
               (w(k->b, K1_MAG_WYNIK_MIN, 0) || w(k->b, K1_DOK_BLAD_MIN, K1_DOK_BLAD_NAZWA)) &&
               (k->c == 0 || ((k->a == K1_KOM_DOK_NOWY || k->a == K1_KOM_DOK_KONIEC) && k->b == 0 && k->c >= K1_DOK_PIERWSZY)) &&
               zera(k, 3);
    default:
        return 0;
    }
}

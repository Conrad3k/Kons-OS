/* K1 — checking messages of the helpers of Pliki and Podgląd: the USB stick (czytnik, ADR 0031) and the picture
 * decoder (dekoder, ADR 0032) (the split: k1_kom_wew.h). */
#include "k1_kom_wew.h"

/* The USB stick: nadzorca ↔ Pliki, Pliki ↔ czytnik (0.8.0 step 3, ADR 0031); 0 also for a type of another group. */
int k1_kom_sprawdz_pendrive(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    const int32_t maks = (int32_t)K1_DOK_KAWALEK;
    int pen_wpis = k->obiekt <= K1_PEN_ID_MAKS;
    switch (k->typ) {
    case K1_KOM_PENDRIVE:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_PENDRIVE_OTWORZ:
    case K1_KOM_PENDRIVE_KANAL:
        return kier == (k->typ == K1_KOM_PENDRIVE_OTWORZ ? K1_KIER_DO_NADZORCY : K1_KIER_OD_NADZORCY) && k->obiekt == 0 &&
               zera(k, 0);
    case K1_KOM_PEN_SPIS:
        return kier == K1_KIER_DO_CZYTNIKA && pen_wpis && w(k->a, 0, K1_PEN_FOLDER_MAKS - 1) && zera(k, 1);
    case K1_KOM_PEN_CZYTAJ:
        return kier == K1_KIER_DO_CZYTNIKA && pen_wpis && k->obiekt != 0 && w(k->a, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) &&
               zera(k, 1);
    case K1_KOM_PEN_STAN:
        return kier == K1_KIER_OD_CZYTNIKA && k->obiekt == 0 &&
               ((k->a == 12 || k->a == 16 || k->a == 32) ? k->b == 0 : k->a == 0 && w(k->b, K1_PEN_BLAD_MIN, -1)) &&
               zera(k, 2);
    case K1_KOM_PEN_LISTA:
        return kier == K1_KIER_OD_CZYTNIKA && pen_wpis && w(k->b, 0, K1_PEN_WPISOW) &&
               k->a == K1_PEN_LISTA_NAGL + k->b * K1_PEN_WPIS_BAJTY && w(k->c, 0, 1) &&
               w(k->d, 0, K1_PEN_FOLDER_MAKS - 1) && k->e == 0;
    case K1_KOM_PEN_PLIK:
        return kier == K1_KIER_OD_CZYTNIKA && pen_wpis && k->obiekt != 0 &&
               (k->c == 0 ? w(k->a, 1, K1_DOK_NAZWA - 1) : k->a == 0) && w(k->b, 0, maks) &&
               w(k->c, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) &&
               rozmiar_pliku(k->d, k->e, (uint64_t)k->c * K1_DOK_KAWALEK + (uint64_t)k->b);
    case K1_KOM_PEN_WYNIK:
        return kier == K1_KIER_OD_CZYTNIKA && pen_wpis && (k->a == K1_KOM_PEN_SPIS || k->a == K1_KOM_PEN_CZYTAJ) &&
               w(k->b, K1_PEN_BLAD_MIN, -1) && zera(k, 2);
    default:
        return 0;
    }
}

/* The picture decoder: nadzorca ↔ Pliki, Pliki ↔ dekoder (0.8.0 step 4, ADR 0032); 0 also for a type of another group. */
int k1_kom_sprawdz_dekoder(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_DEKODER:
    case K1_KOM_DEKODER_KANAL:
        return kier == (k->typ == K1_KOM_DEKODER ? K1_KIER_DO_NADZORCY : K1_KIER_OD_NADZORCY) && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_DEK_OBRAZ:
        return kier == K1_KIER_DO_DEKODERA && w(k->a, 1, (int32_t)K1_DEK_MAKS_BAJTY) && w(k->b, 1, K1_DEK_MAKS_W) &&
               w(k->c, 1, K1_DEK_MAKS_H) && zera(k, 3);
    case K1_KOM_DEK_KAWALEK:
        return kier == K1_KIER_DO_DEKODERA && w(k->a, 0, (int32_t)(K1_DEK_MAKS_BAJTY / K1_DOK_KAWALEK) - 1) &&
               w(k->b, 1, (int32_t)K1_DOK_KAWALEK) && zera(k, 2);
    case K1_KOM_DEK_PIKSELE:
        return kier == K1_KIER_OD_DEKODERA && w(k->a, 1, K1_DEK_MAKS_W) && w(k->b, 1, K1_DEK_MAKS_H) &&
               w(k->c, k->a, (int32_t)K1_DEK_MAKS_PIKSELI) && w(k->d, k->b, (int32_t)K1_DEK_MAKS_PIKSELI) &&
               (uint64_t)k->c * (uint64_t)k->d <= K1_DEK_MAKS_PIKSELI && zera(k, 4);
    case K1_KOM_DEK_BLAD:
        return kier == K1_KIER_OD_DEKODERA && w(k->a, K1_DEK_BLAD_MIN, K1_DEK_BLAD_FORMAT) &&
               (k->a == K1_DEK_BLAD_DUZY ? w(k->b, 1, K1_DEK_MPX_MAKS) : k->b == 0) && zera(k, 2);
    default:
        return 0;
    }
}

/* K1 — checking the messages of the film decoder (0.10.9 step 2, ADR 0049 pkt 3; the split: k1_kom_wew.h): its client ↔
 * filmy, what nadzorca tells Ustawienia about the card, the test program's request for a decoder. The ranges here are
 * those of every row of K1_KODEKI together; the row's own and the film's state the decoder checks (k1_filmy.h). */
#include "k1_kom_wew.h"

#include "k1_filmy.h"
#include "k1_uslugi.h"

static int film(uint32_t o)
{
    return o >= 1 && o <= K1_FILMY_FILMOW_MAKS;
}

static int bity_kodekow(int32_t a)
{
    return a != 0 && ((uint32_t)a >> K1_KODEKI_MAKS) == 0;
}

/* FILMY_UMIE and FILMY_KARTA: the bits only with UMIE, the code only with NIC (and BLAD for KARTA). */
static int karta(const struct k1_komunikat *k, int z_nadzorcy)
{
    int stan = k->b;
    if (!w(stan, z_nadzorcy ? K1_FILMY_KARTA_SPRAWDZA : K1_FILMY_KARTA_BRAK, K1_FILMY_KARTA_MAKS) ||
        (!z_nadzorcy && stan == K1_FILMY_KARTA_BLAD))
        return 0;
    if (stan == K1_FILMY_KARTA_UMIE ? !bity_kodekow(k->a) : k->a != 0)
        return 0;
    if (stan == K1_FILMY_KARTA_NIC || stan == K1_FILMY_KARTA_BLAD ? !w(k->c, 0, 255) : k->c != 0)
        return 0;
    if (z_nadzorcy)
        return k->d == 0 && k->e == 0;
    return stan == K1_FILMY_KARTA_UMIE ? w(k->d, K1_FILMY_MIN_WH, K1_FILMY_MAKS_W) && w(k->e, K1_FILMY_MIN_WH, K1_FILMY_MAKS_H)
                                       : k->d == 0 && k->e == 0;
}

static int do_filmow(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_FILMY_OKNO: /* 0.13.2 step 3: with a film — a frame into the zone's buffer */
        return k->obiekt == 0 ? zera(k, 0) : film(k->obiekt) && w(k->a, 0, K1_FILMY_KLATEK_MAKS - 1) && zera(k, 1);
    case K1_KOM_FILMY_OTWORZ:
        return film(k->obiekt) && w(k->a, 1, K1_KODEKI_MAKS) && w(k->b, K1_FILMY_MIN_WH, K1_FILMY_MAKS_W) &&
               w(k->c, K1_FILMY_MIN_WH, K1_FILMY_MAKS_H) && w(k->d, 1, K1_FILMY_KLATEK_MAKS) && k->e == 0;
    case K1_KOM_FILMY_KLATKA:
        return film(k->obiekt) && w(k->a, 0, K1_FILMY_KLATEK_MAKS - 1) &&
               w(k->b, (int32_t)sizeof(struct k1_filmy_naglowek), (int32_t)K1_FILMY_PROSBA_MAKS) && zera(k, 2);
    case K1_KOM_FILMY_POBIERZ:
        return film(k->obiekt) && w(k->a, 0, K1_FILMY_KLATEK_MAKS - 1) && zera(k, 1);
    case K1_KOM_FILMY_ZAMKNIJ:
        return film(k->obiekt) && zera(k, 0);
    default:
        return 0;
    }
}

static int od_filmow(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_FILMY_UMIE:
        return k->obiekt == 0 && karta(k, 0);
    case K1_KOM_FILMY_WYNIK:
        return k->obiekt <= K1_FILMY_FILMOW_MAKS && w(k->a, K1_KOM_FILMY_OKNO, K1_KOM_FILMY_ZAMKNIJ) &&
               (k->a == K1_KOM_FILMY_OKNO || k->obiekt != 0) && w(k->b, K1_FILMY_BLAD_MIN, 0) && zera(k, 2);
    case K1_KOM_FILMY_OBRAZ:
        return film(k->obiekt) && w(k->a, 0, K1_FILMY_KLATEK_MAKS - 1) && w(k->b, K1_FILMY_MIN_WH, K1_FILMY_MAKS_W) &&
               w(k->c, K1_FILMY_MIN_WH, K1_FILMY_MAKS_H) && k->d == (int32_t)k1_filmy_rzad((uint32_t)k->b) && k->e == 0;
    default:
        return 0;
    }
}

int k1_kom_sprawdz_filmy(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_FILMY_OKNO:
    case K1_KOM_FILMY_OTWORZ:
    case K1_KOM_FILMY_KLATKA:
    case K1_KOM_FILMY_POBIERZ:
    case K1_KOM_FILMY_ZAMKNIJ:
        return kier == K1_KIER_DO_FILMOW && do_filmow(k);
    case K1_KOM_FILMY_UMIE:
    case K1_KOM_FILMY_WYNIK:
    case K1_KOM_FILMY_OBRAZ:
        return kier == K1_KIER_OD_FILMOW && od_filmow(k);
    case K1_KOM_FILMY_KARTA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && karta(k, 1);
    case K1_KOM_TEST_FILMY:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_STREFY_MAKS) && w(k->b, 0, 1) && zera(k, 2);
    default:
        return 0;
    }
}

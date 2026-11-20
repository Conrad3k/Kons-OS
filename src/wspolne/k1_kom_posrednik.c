/* K1 — checking the messages of the graphics broker (0.10.9 step 5, ADR 0049 pkt 7; the split: k1_kom_wew.h): its client
 * ↔ posrednik, what nadzorca tells Ustawienia about the card, the test program's request for a broker. The requests in
 * the window are checked by the broker itself (k1_posrednik.c), after it copied them. */
#include "k1_kom_wew.h"

#include "k1_posrednik.h"
#include "k1_uslugi.h"

static int zasob(uint32_t o)
{
    return o >= 1 && o < K1_POSR_ZASOBY_MAKS;
}

static int rozmiar(int32_t lo, int32_t hi)
{
    uint64_t r = k1_kom_u64(lo, hi);
    return r >= 1 && r <= K1_POSR_BUFOR_MAKS;
}

static int do_posrednika(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_POSR_OKNO:
        return k->obiekt == 0 && zera(k, 0);
    case K1_KOM_POSR_WSPOLNA:
        return k->obiekt == 0 && w(k->a, (int32_t)K1_POSR_WSPOLNA_MIN, (int32_t)K1_POSR_WSPOLNA_MAKS) && k->a % 4096 == 0 &&
               zera(k, 1);
    case K1_KOM_POSR_PROSBY:
        return k->obiekt == 0 && w(k->a, (int32_t)sizeof(struct k1_posr_naglowek), (int32_t)K1_POSR_PROSBY_BAJTY) &&
               k->a % 4 == 0 && zera(k, 1);
    case K1_KOM_POSR_BLOB:
        return zasob(k->obiekt) && k->a >= 1 && rozmiar(k->b, k->c) && zera(k, 3);
    case K1_KOM_POSR_BUFOR:
    case K1_KOM_POSR_ZWOLNIJ:
        return zasob(k->obiekt) && zera(k, 0);
    case K1_KOM_POSR_PLOT:
        return k->obiekt < K1_POSR_PIERSCIENIE && zera(k, 2);
    default:
        return 0;
    }
}

/* POSR_UMIE: the card's numbers only with UMIE, the reason only with NIE. */
static int umie(const struct k1_komunikat *k)
{
    if (k->obiekt != 0 || !w(k->a, K1_POSR_KARTA_BRAK, K1_POSR_KARTA_UMIE))
        return 0;
    if (k->a == K1_POSR_KARTA_NIE ? !w(k->b, 1, K1_POSR_NIE_MAKS) : k->b != 0)
        return 0;
    if (k->a != K1_POSR_KARTA_UMIE)
        return zera(k, 2);
    return w(k->c, 1, 0xffff) && w(k->d, 0, 255) && w(k->e, 0, 0xffffff);
}

static int od_posrednika(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_POSR_UMIE:
        return umie(k);
    case K1_KOM_POSR_WYNIK:
        return (k->obiekt == 0 || zasob(k->obiekt)) && w(k->a, K1_KOM_POSR_OKNO, K1_KOM_POSR_ZWOLNIJ) &&
               w(k->b, K1_POSR_BLAD_MIN, 0) &&
               (k->a == K1_KOM_POSR_PROSBY ? w(k->c, 0, (int32_t)(K1_POSR_PROSBY_BAJTY / 16)) : k->c == 0) && zera(k, 3);
    case K1_KOM_POSR_DMABUF:
        return zasob(k->obiekt) && w(k->a, K1_POSR_MAPA_CACHED, K1_POSR_MAPA_WC) && rozmiar(k->b, k->c) && zera(k, 3);
    case K1_KOM_POSR_PLOT_GOTOWY:
        return k->obiekt < K1_POSR_PIERSCIENIE && zera(k, 2);
    default:
        return 0;
    }
}

/* POSR_KARTA (nadzorca → Ustawienia): the code only with NIE (its reason) and BLAD (an exit code). */
static int karta(const struct k1_komunikat *k)
{
    if (k->obiekt != 0 || !w(k->a, K1_POSR_KARTA_SPRAWDZA, K1_POSR_KARTA_MAKS))
        return 0;
    if (k->a == K1_POSR_KARTA_NIE ? !w(k->b, 1, K1_POSR_NIE_MAKS) : k->a == K1_POSR_KARTA_BLAD ? !w(k->b, 0, 255) : k->b != 0)
        return 0;
    return zera(k, 2);
}

int k1_kom_sprawdz_posrednik(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_POSR_OKNO:
    case K1_KOM_POSR_WSPOLNA:
    case K1_KOM_POSR_PROSBY:
    case K1_KOM_POSR_BLOB:
    case K1_KOM_POSR_BUFOR:
    case K1_KOM_POSR_ZWOLNIJ:
    case K1_KOM_POSR_PLOT:
        return kier == K1_KIER_DO_POSREDNIKA && do_posrednika(k);
    case K1_KOM_POSR_UMIE:
    case K1_KOM_POSR_WYNIK:
    case K1_KOM_POSR_DMABUF:
    case K1_KOM_POSR_PLOT_GOTOWY:
        return kier == K1_KIER_OD_POSREDNIKA && od_posrednika(k);
    case K1_KOM_POSR_KARTA:
        return kier == K1_KIER_OD_NADZORCY && karta(k);
    case K1_KOM_TEST_POSREDNIK:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_STREFY_MAKS) && w(k->b, 0, 1) && zera(k, 2);
    default:
        return 0;
    }
}

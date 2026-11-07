#include "k1_komunikat.h"

#include <string.h>

#include "k1_uslugi.h"

struct k1_komunikat k1_komunikat_nowy(enum k1_komunikat_typ typ, uint32_t obiekt, int32_t a, int32_t b, int32_t c,
                                      int32_t d, int32_t e)
{
    struct k1_komunikat k;
    memset(&k, 0, sizeof k);
    k.wersja = K1_KOMUNIKAT_WERSJA;
    k.typ = (uint8_t)typ;
    k.dlugosc = sizeof k;
    k.obiekt = obiekt;
    k.a = a;
    k.b = b;
    k.c = c;
    k.d = d;
    k.e = e;
    return k;
}

static int w(int32_t v, int32_t lo, int32_t hi)
{
    return v >= lo && v <= hi;
}

static int powierzchnia(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_POW_MAKS;
}

/* Parameters from index `od` on must all be zero (a=0, b=1, ...). */
static int zera(const struct k1_komunikat *k, int od)
{
    const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
    int i;
    for (i = od; i < 5; i++)
        if (p[i] != 0)
            return 0;
    return 1;
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
        return w(wartosc, 0, 3);
    case K1_UST_ROZDZIELCZOSC:
        return w(wartosc, 1, K1_TRYBY_MAKS);
    default:
        return 0;
    }
}

/* K1_KOM_BATERIA_HIST: at least one sample, each 0..100 (+ mains bit), 0xFF only after the last one,
 * none beyond the end of the history. */
static int probki_baterii(const struct k1_komunikat *k)
{
    int i, koniec = 0;
    for (i = 0; i < K1_BAT_HIST_W_KOM; i++) {
        unsigned v = k1_bat_probka(k, i);
        if (v == K1_BAT_BRAK_PROBKI) {
            if (i == 0)
                return 0;
            koniec = 1;
            continue;
        }
        if (koniec || (v & ~(unsigned)K1_BAT_ZASILACZ) > 100 || k->obiekt + (uint32_t)i >= K1_BAT_HIST_MAKS)
            return 0;
    }
    return 1;
}

/* rola + aplikacja + uid, as in KLIENT and TEST_POLACZ. */
static int klient(const struct k1_komunikat *k)
{
    if (k->obiekt != 0 || !w(k->a, 1, K1_ROLA_MAKS) || !w(k->c, 0, 65535) || !zera(k, 3))
        return 0;
    if (k->a == K1_ROLA_APLIKACJA)
        return w(k->b, 1, K1_APL_MAKS);
    return k->b == 0;
}

static int sprawdz_typ(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_KLIENT:
        return kier == K1_KIER_STEROWANIE && klient(k);
    case K1_KOM_SKARB_KLIENT:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 &&
               (w(k->a, 1, K1_PROFIL_MAKS) || k->a == K1_WL_SYSTEM) && w(k->b, 0, 65535) && zera(k, 2);
    case K1_KOM_SKARB_CZYTAJ:
    case K1_KOM_SKARB_USUN:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS && zera(k, 0);
    case K1_KOM_SKARB_ZAPISZ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS && w(k->a, 0, K1_SKARB_MAKS) &&
               zera(k, 1);
    case K1_KOM_SKARB_ODBLOKUJ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && w(k->a, 1, K1_HASLO_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_HASLO:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && w(k->a, 2, 2 * K1_HASLO_MAKS + 2) && zera(k, 1);
    case K1_KOM_SKARB_INFO:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && zera(k, 0);
    case K1_KOM_SKARB_STAN:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, K1_SKARB_TRWALY, K1_SKARB_ZABLOKOWANY) &&
               w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_SKARB_DANE:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS && w(k->a, 0, K1_SKARB_MAKS) &&
               zera(k, 1);
    case K1_KOM_SKARB_WYNIK:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS &&
               w(k->a, K1_KOM_SKARB_CZYTAJ, K1_KOM_SKARB_INFO) && w(k->b, K1_MAG_WYNIK_MIN, 0) &&
               w(k->c, K1_SKARB_TRWALY, K1_SKARB_ZABLOKOWANY) && w(k->d, 0, 1) && w(k->e, 0, K1_HASLO_PRZERWA);

    case K1_KOM_POWIERZCHNIA:
        if (kier != K1_KIER_DO_EKRANU || !powierzchnia(k->obiekt) || !w(k->a, 1, K1_POW_MAKS_BOK) ||
            !w(k->b, 1, K1_POW_MAKS_BOK))
            return 0;
        if (k->c == K1_POW_OKNO)
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
        return kier == K1_KIER_DO_EKRANU && k->obiekt != 0 && zera(k, 0);
    case K1_KOM_USTAW:
        return kier == K1_KIER_DO_EKRANU && k->obiekt == 0 && k1_ustawienie_poprawne(k->a, k->b) && zera(k, 2);

    case K1_KOM_WITAJ:
        return kier == K1_KIER_OD_EKRANU && k->obiekt == 0 && w(k->a, 1, K1_POZ_MAKS) && w(k->b, 1, K1_POZ_MAKS) &&
               w(k->c, 1, K1_ROLA_MAKS) && w(k->d, 0, K1_APL_MAKS) && k->e == 0;
    case K1_KOM_WSKAZNIK:
        return kier == K1_KIER_OD_EKRANU && k->obiekt <= K1_POW_MAKS && w(k->a, -K1_POZ_MAKS, K1_POZ_MAKS) &&
               w(k->b, -K1_POZ_MAKS, K1_POZ_MAKS) && w(k->c, K1_WSK_RUCH, K1_WSK_WYJSCIE) &&
               w(k->d, K1_PRZYCISK_BRAK, K1_PRZYCISK_SRODKOWY) && k->e == 0;
    case K1_KOM_KLAWISZ:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && w(k->a, 1, K1_KLAWISZ_MAKS) && w(k->b, 0, 2) &&
               w(k->c, 0, K1_MOD_MASKA) && zera(k, 3);
    case K1_KOM_FOKUS:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_RAMKA:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && zera(k, 0);
    case K1_KOM_OKNO:
        return kier == K1_KIER_OD_EKRANU && k->obiekt != 0 && w(k->a, 1, K1_APL_MAKS) &&
               w(k->b, 1, K1_OKNO_STAN_MAKS) && zera(k, 2);
    case K1_KOM_USTAWIENIE:
        return kier == K1_KIER_OD_EKRANU && k->obiekt == 0 && k1_ustawienie_poprawne(k->a, k->b) && zera(k, 2);
    case K1_KOM_TRYB:
        return kier == K1_KIER_OD_EKRANU && k->obiekt >= 1 && k->obiekt <= K1_TRYBY_MAKS &&
               w(k->a, K1_TRYB_MIN_SZER, K1_POW_MAKS_BOK) && w(k->b, K1_TRYB_MIN_WYS, K1_POW_MAKS_BOK) &&
               w(k->c, 0, 1000) && w(k->d, 0, 1) && k->e == 0;

    case K1_KOM_URUCHOM:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) && zera(k, 1);
    case K1_KOM_WYLACZ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_TEST_POLACZ:
        return kier == K1_KIER_DO_NADZORCY && klient(k);
    case K1_KOM_TEST_PROBA:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_PROFIL_MAKS) && zera(k, 1);

    case K1_KOM_TEST_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, -1, 255) && zera(k, 1);
    case K1_KOM_TEST_GNIAZDO:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_ZASOBY_SYSTEM:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1000) && w(k->b, 0, K1_PAMIEC_MAKS_MIB) &&
               w(k->c, 1, K1_PAMIEC_MAKS_MIB) && k->b <= k->c && w(k->d, 1, 4096) && k->e >= 0;
    case K1_KOM_ZASOBY_USLUGA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_PROFIL_MAKS && w(k->a, 0, 1000) &&
               k->b >= 0 && w(k->c, 0, 1) && zera(k, 3);
    case K1_KOM_BATERIA:
        if (kier != K1_KIER_OD_NADZORCY || k->obiekt != 0 || !w(k->a, 0, 1) || !w(k->d, 0, 1) || k->e != 0)
            return 0;
        if (k->a == 0)
            return k->b == -1 && k->c == K1_BAT_NIEZNANY;
        return w(k->b, -1, 100) && w(k->c, K1_BAT_NIEZNANY, K1_BAT_STAN_MAKS);
    case K1_KOM_BATERIA_HIST:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_BAT_HIST_MAKS && w(k->a, 1, 86400) && probki_baterii(k);
    default:
        return 0;
    }
}

int k1_komunikat_sprawdz(const void *buf, size_t len, enum k1_kanal_kierunek kierunek, struct k1_komunikat *out)
{
    struct k1_komunikat k;

    memset(out, 0, sizeof *out);
    if (buf == NULL || len != sizeof k)
        return -1;
    memcpy(&k, buf, sizeof k);
    if (k.wersja != K1_KOMUNIKAT_WERSJA || k.dlugosc != sizeof k || k.zarezerwowane != 0 || !sprawdz_typ(&k, kierunek))
        return -1;
    *out = k;
    return 0;
}

int k1_komunikat_z_deskryptorem(uint8_t typ, enum k1_kanal_kierunek kierunek)
{
    return (typ == K1_KOM_KLIENT && kierunek == K1_KIER_STEROWANIE) ||
           (typ == K1_KOM_POWIERZCHNIA && kierunek == K1_KIER_DO_EKRANU) ||
           (typ == K1_KOM_TEST_GNIAZDO && kierunek == K1_KIER_OD_NADZORCY) ||
           (typ == K1_KOM_SKARB_KLIENT && kierunek == K1_KIER_STEROWANIE) ||
           (typ == K1_KOM_SKARB_ZAPISZ && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_ODBLOKUJ && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_HASLO && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_DANE && kierunek == K1_KIER_OD_SKARBCA);
}

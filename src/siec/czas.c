/* K1 siec — the clock from the internet: a round of questions, the agreement of the answers. See czas.h. */
#include "czas.h"

#include <string.h>

#include "sntp.h"

void k1_czas_polaczono(struct k1_czas *c, double teraz)
{
    memset(c, 0, sizeof *c);
    c->nastepna = teraz;
    c->odstep = K1_CZAS_PONOWNIE_MIN;
}

void k1_czas_stop(struct k1_czas *c)
{
    memset(c, 0, sizeof *c);
}

static int jest(const struct k1_czas *c, uint32_t a)
{
    int i;
    for (i = 0; i < c->n; i++)
        if (c->z[i].serwer == a)
            return 1;
    return 0;
}

static void dodaj(struct k1_czas *c, uint32_t a, const uint64_t *los)
{
    if (c->n >= K1_SIEC_ZRODLA_MAKS || !k1_siec_serwer_dobry(a) || jest(c, a) || los[c->n] == 0)
        return;
    c->z[c->n].serwer = a;
    c->z[c->n].los = los[c->n];
    c->z[c->n].stan = K1_CZAS_GNIAZDO;
    c->n++;
}

int k1_czas_start(struct k1_czas *c, const uint32_t *od_routera, int n_r, const uint64_t los[K1_SIEC_ZRODLA_MAKS],
                  double teraz)
{
#define X_SERWER(a, b, cc, d, operator) (uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(cc) << 8 | (uint32_t)(d),
    static const uint32_t stale[] = { K1_SERWERY_CZASU(X_SERWER) };
#undef X_SERWER
    int i;
    c->n = 0;
    memset(c->z, 0, sizeof c->z);
    for (i = 0; i < n_r; i++)
        dodaj(c, od_routera[i], los);
    for (i = 0; i < (int)(sizeof stale / sizeof stale[0]); i++)
        dodaj(c, stale[i], los);
    c->koniec = teraz + K1_CZAS_RUNDA;
    return c->n;
}

void k1_czas_wyslano(struct k1_czas *c, int i, double teraz)
{
    if (i >= 0 && i < c->n && c->z[i].stan == K1_CZAS_GNIAZDO) {
        c->z[i].stan = K1_CZAS_WYSLANO;
        c->z[i].wyslano = teraz;
    }
}

int k1_czas_odpowiedz(struct k1_czas *c, int i, const uint8_t *b, size_t n, double teraz)
{
    double czas, droga;
    int r;
    if (i < 0 || i >= c->n || c->z[i].stan != K1_CZAS_WYSLANO || c->koniec == 0)
        return K1_CZAS_ZA_POZNO;
    droga = teraz - c->z[i].wyslano;
    if (droga < 0 || droga > K1_CZAS_DROGA_MAKS)
        return K1_CZAS_ZA_POZNO;
    r = k1_sntp_czytaj(b, n, c->z[i].los, &czas);
    if (r < 0)
        return r;
    c->z[i].stan = K1_CZAS_ODPOWIEDZ;
    c->z[i].przesuniecie = czas + droga / 2 - teraz; /* the server read its clock about half way */
    return 0;
}

int k1_czas_koniec_rundy(const struct k1_czas *c, double teraz)
{
    int i;
    if (c->koniec == 0)
        return 0;
    if (teraz >= c->koniec)
        return 1;
    for (i = 0; i < c->n; i++)
        if (c->z[i].stan != K1_CZAS_ODPOWIEDZ)
            return 0;
    return 1;
}

int k1_czas_wynik(struct k1_czas *c, double teraz, double *przesuniecie, int *odpowiedzi, int *zgodne)
{
    double t[K1_SIEC_ZRODLA_MAKS];
    int n = 0, i, j, od = 0, ile = 0, dobrze;
    for (i = 0; i < c->n; i++)
        if (c->z[i].stan == K1_CZAS_ODPOWIEDZ) {
            double v = c->z[i].przesuniecie;
            for (j = n; j > 0 && t[j - 1] > v; j--) /* sorted */
                t[j] = t[j - 1];
            t[j] = v;
            n++;
        }
    for (i = 0; i < n; i++) { /* the most answers within K1_CZAS_ZGODNOSC of each other */
        for (j = i; j < n && t[j] - t[i] <= K1_CZAS_ZGODNOSC; j++)
            ;
        if (j - i > ile) {
            ile = j - i;
            od = i;
        }
    }
    *odpowiedzi = n;
    *zgodne = ile;
    dobrze = ile >= 2 && 2 * ile > n;
    *przesuniecie = dobrze ? t[od + ile / 2] : 0;
    c->koniec = 0;
    if (dobrze) {
        c->nastepna = teraz + K1_CZAS_CO;
        c->odstep = K1_CZAS_PONOWNIE_MIN;
    } else {
        k1_czas_odrzucony(c, teraz);
    }
    return dobrze;
}

void k1_czas_odrzucony(struct k1_czas *c, double teraz)
{
    if (c->odstep < K1_CZAS_PONOWNIE_MIN)
        c->odstep = K1_CZAS_PONOWNIE_MIN;
    c->nastepna = teraz + c->odstep;
    c->odstep = c->odstep * 2 > K1_CZAS_PONOWNIE_MAKS ? K1_CZAS_PONOWNIE_MAKS : c->odstep * 2;
}

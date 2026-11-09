/* K1 gui — the desktop arrangement: pinned applications and icon cells (rozmieszczenie.h). */
#include "rozmieszczenie.h"

#include <string.h>

static int poprawna(int apl)
{
    return apl >= 1 && apl <= K1_APL_MAKS;
}

void rozm_domyslne(struct rozmieszczenie *r, const int *domyslne, int wierszy)
{
    int a;
    memset(r, 0, sizeof *r);
    for (a = 1; a <= K1_APL_MAKS; a++)
        if (domyslne[a]) {
            r->pasek[r->n_pasek++] = (uint8_t)a;
            r->ikony[r->n_ikon++].apl = (uint8_t)a;
        }
    rozm_uloz(r, wierszy);
}

int rozm_na_pasku(const struct rozmieszczenie *r, int apl)
{
    int i;
    for (i = 0; i < r->n_pasek; i++)
        if (r->pasek[i] == apl)
            return i;
    return -1;
}

void rozm_przypnij(struct rozmieszczenie *r, int apl)
{
    if (poprawna(apl) && rozm_na_pasku(r, apl) < 0 && r->n_pasek < K1_APL_MAKS)
        r->pasek[r->n_pasek++] = (uint8_t)apl;
}

void rozm_odepnij(struct rozmieszczenie *r, int apl)
{
    int i = rozm_na_pasku(r, apl);
    if (i < 0)
        return;
    memmove(&r->pasek[i], &r->pasek[i + 1], (size_t)(r->n_pasek - i - 1));
    r->n_pasek--;
}

void rozm_przesun_na_pasku(struct rozmieszczenie *r, int apl, int nowa)
{
    int i = rozm_na_pasku(r, apl);
    if (i < 0)
        return;
    nowa = nowa < 0 ? 0 : (nowa >= r->n_pasek ? r->n_pasek - 1 : nowa);
    rozm_odepnij(r, apl);
    memmove(&r->pasek[nowa + 1], &r->pasek[nowa], (size_t)(r->n_pasek - nowa));
    r->pasek[nowa] = (uint8_t)apl;
    r->n_pasek++;
}

int rozm_ikona(const struct rozmieszczenie *r, int apl)
{
    int i;
    for (i = 0; i < r->n_ikon; i++)
        if (r->ikony[i].apl == apl)
            return i;
    return -1;
}

int rozm_w_komorce(const struct rozmieszczenie *r, int kol, int wiersz)
{
    int i;
    for (i = 0; i < r->n_ikon; i++)
        if (r->ikony[i].kol == kol && r->ikony[i].wiersz == wiersz)
            return r->ikony[i].apl;
    return 0;
}

/* The first free cell column by column; 0, or -1 when the grid is full. */
static int wolna(const struct rozmieszczenie *r, int kolumn, int wierszy, int *kol, int *wiersz)
{
    int k, w;
    for (k = 0; k < kolumn && k <= ROZM_KOMORKA_MAKS; k++)
        for (w = 0; w < wierszy && w <= ROZM_KOMORKA_MAKS; w++)
            if (rozm_w_komorce(r, k, w) == 0) {
                *kol = k;
                *wiersz = w;
                return 0;
            }
    return -1;
}

int rozm_dodaj_ikone(struct rozmieszczenie *r, int apl, int kolumn, int wierszy)
{
    int k, w;
    if (!poprawna(apl) || rozm_ikona(r, apl) >= 0)
        return 0;
    if (r->n_ikon >= K1_APL_MAKS || wolna(r, kolumn, wierszy, &k, &w) != 0)
        return -1;
    r->ikony[r->n_ikon++] = (struct rozm_ikona){ (uint8_t)apl, (uint8_t)k, (uint8_t)w };
    return 0;
}

void rozm_usun_ikone(struct rozmieszczenie *r, int apl)
{
    int i = rozm_ikona(r, apl);
    if (i < 0)
        return;
    r->ikony[i] = r->ikony[--r->n_ikon];
}

void rozm_przenies_ikone(struct rozmieszczenie *r, int apl, int kol, int wiersz)
{
    int i = rozm_ikona(r, apl), inna, j;
    if (i < 0 || kol < 0 || wiersz < 0 || kol > ROZM_KOMORKA_MAKS || wiersz > ROZM_KOMORKA_MAKS)
        return;
    inna = rozm_w_komorce(r, kol, wiersz);
    if (inna != 0 && inna != apl) {
        j = rozm_ikona(r, inna);
        r->ikony[j].kol = r->ikony[i].kol;
        r->ikony[j].wiersz = r->ikony[i].wiersz;
    }
    r->ikony[i].kol = (uint8_t)kol;
    r->ikony[i].wiersz = (uint8_t)wiersz;
}

void rozm_uloz(struct rozmieszczenie *r, int wierszy)
{
    int a, n = 0;
    if (wierszy < 1)
        wierszy = 1;
    for (a = 1; a <= K1_APL_MAKS; a++) {
        int i = rozm_ikona(r, a);
        if (i < 0)
            continue;
        r->ikony[i].kol = (uint8_t)(n / wierszy);
        r->ikony[i].wiersz = (uint8_t)(n % wierszy);
        n++;
    }
}

int rozm_dopasuj(struct rozmieszczenie *r, int kolumn, int wierszy)
{
    int i, ile = 0;
    for (i = 0; i < r->n_ikon; i++) {
        struct rozm_ikona *ik = &r->ikony[i];
        int k, w;
        if (ik->kol < kolumn && ik->wiersz < wierszy)
            continue;
        ik->kol = ik->wiersz = ROZM_KOMORKA_MAKS + 1; /* out of the way while looking for a free cell */
        if (wolna(r, kolumn, wierszy, &k, &w) != 0) {
            k = 0;
            w = 0; /* a full grid: on top of the first icon (still reachable from the Start menu) */
        }
        ik->kol = (uint8_t)k;
        ik->wiersz = (uint8_t)w;
        ile++;
    }
    return ile;
}

size_t rozm_zapisz(const struct rozmieszczenie *r, uint8_t *buf, size_t n)
{
    size_t p = 0;
    int i;
    if (n < ROZM_BAJTY_MAKS)
        return 0;
    memcpy(buf, ROZM_ZNAK, 4);
    p = 4;
    buf[p++] = (uint8_t)r->n_pasek;
    for (i = 0; i < r->n_pasek; i++)
        buf[p++] = r->pasek[i];
    buf[p++] = (uint8_t)r->n_ikon;
    for (i = 0; i < r->n_ikon; i++) {
        buf[p++] = r->ikony[i].apl;
        buf[p++] = r->ikony[i].kol;
        buf[p++] = r->ikony[i].wiersz;
    }
    return p;
}

int rozm_czytaj(const uint8_t *buf, size_t n, struct rozmieszczenie *r)
{
    struct rozmieszczenie t;
    size_t p = 5;
    int i;
    memset(&t, 0, sizeof t);
    if (n < 6 || memcmp(buf, ROZM_ZNAK, 4) != 0 || buf[4] > K1_APL_MAKS || n < 6u + buf[4])
        return -1;
    for (i = buf[4]; i > 0; i--, p++) {
        if (!poprawna(buf[p]) || rozm_na_pasku(&t, buf[p]) >= 0)
            return -1;
        t.pasek[t.n_pasek++] = buf[p];
    }
    if (buf[p] > K1_APL_MAKS || n != p + 1 + 3u * buf[p])
        return -1;
    for (i = buf[p++]; i > 0; i--, p += 3) {
        struct rozm_ikona ik = { buf[p], buf[p + 1], buf[p + 2] };
        if (!poprawna(ik.apl) || ik.kol > ROZM_KOMORKA_MAKS || ik.wiersz > ROZM_KOMORKA_MAKS || rozm_ikona(&t, ik.apl) >= 0 ||
            rozm_w_komorce(&t, ik.kol, ik.wiersz) != 0)
            return -1;
        t.ikony[t.n_ikon++] = ik;
    }
    *r = t;
    return 0;
}

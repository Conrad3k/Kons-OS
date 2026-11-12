/* K1 gui — the desktop arrangement: pinned applications and icon cells (rozmieszczenie.h). */
#include "rozmieszczenie.h"

#include <string.h>

#include "k1_bajty.h"

static int poprawna(int apl)
{
    return apl >= 1 && apl <= K1_APL_MAKS;
}

int rozm_aplikacja(uint32_t klucz)
{
    return klucz >= 1 && klucz <= K1_APL_MAKS ? (int)klucz : 0;
}

int rozm_plik(uint32_t klucz)
{
    return klucz >= ROZM_PLIK + K1_DOK_PIERWSZY && klucz - ROZM_PLIK <= K1_DOK_ID_MAKS ? (int)(klucz - ROZM_PLIK) : 0;
}

static int klucz_dobry(uint32_t klucz)
{
    return rozm_aplikacja(klucz) || klucz == ROZM_KOSZ || rozm_plik(klucz);
}

void rozm_domyslne(struct rozmieszczenie *r, const int *domyslne, int wierszy)
{
    int a;
    memset(r, 0, sizeof *r);
    r->ikony[r->n_ikon++].klucz = ROZM_KOSZ;
    for (a = 1; a <= K1_APL_MAKS; a++)
        if (domyslne[a] > 0) { /* -1: a window of K1 itself (never on the bar or the desktop) */
            r->pasek[r->n_pasek++] = (uint8_t)a;
            r->ikony[r->n_ikon++].klucz = (uint32_t)a;
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

int rozm_ikona(const struct rozmieszczenie *r, uint32_t klucz)
{
    int i;
    for (i = 0; i < r->n_ikon; i++)
        if (r->ikony[i].klucz == klucz)
            return i;
    return -1;
}

uint32_t rozm_w_komorce(const struct rozmieszczenie *r, int kol, int wiersz)
{
    int i;
    for (i = 0; i < r->n_ikon; i++)
        if (r->ikony[i].kol == kol && r->ikony[i].wiersz == wiersz)
            return r->ikony[i].klucz;
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

int rozm_dodaj_ikone(struct rozmieszczenie *r, uint32_t klucz, int kolumn, int wierszy)
{
    int k, w;
    if (!klucz_dobry(klucz) || rozm_ikona(r, klucz) >= 0)
        return 0;
    if (r->n_ikon >= ROZM_IKON_MAKS || wolna(r, kolumn, wierszy, &k, &w) != 0)
        return -1;
    r->ikony[r->n_ikon++] = (struct rozm_ikona){ klucz, (uint8_t)k, (uint8_t)w };
    return 0;
}

int rozm_ostatnia_wolna(const struct rozmieszczenie *r, int kolumn, int wierszy, int *kol, int *wiersz)
{
    for (int k = (kolumn <= ROZM_KOMORKA_MAKS ? kolumn : ROZM_KOMORKA_MAKS + 1) - 1; k >= 0; k--)
        for (int w = (wierszy <= ROZM_KOMORKA_MAKS ? wierszy : ROZM_KOMORKA_MAKS + 1) - 1; w >= 0; w--)
            if (rozm_w_komorce(r, k, w) == 0) {
                *kol = k;
                *wiersz = w;
                return 0;
            }
    return -1;
}

/* Cells an icon can still take: free cells of the grid, at most ROZM_IKON_MAKS icons in all. */
static int miejsc(const struct rozmieszczenie *r, int kolumn, int wierszy)
{
    int wolnych = 0;
    for (int k = 0; k < kolumn && k <= ROZM_KOMORKA_MAKS; k++)
        for (int w = 0; w < wierszy && w <= ROZM_KOMORKA_MAKS; w++)
            wolnych += rozm_w_komorce(r, k, w) == 0;
    return wolnych < ROZM_IKON_MAKS - r->n_ikon ? wolnych : ROZM_IKON_MAKS - r->n_ikon;
}

/* No room at all: the file icon in the last cell (column by column) leaves it; 1 when one did. */
static int zwolnij(struct rozmieszczenie *r, int kolumn, int wierszy)
{
    for (int k = (kolumn <= ROZM_KOMORKA_MAKS ? kolumn : ROZM_KOMORKA_MAKS + 1) - 1; k >= 0; k--)
        for (int w = (wierszy <= ROZM_KOMORKA_MAKS ? wierszy : ROZM_KOMORKA_MAKS + 1) - 1; w >= 0; w--) {
            uint32_t kl = rozm_w_komorce(r, k, w);
            if (rozm_plik(kl)) {
                rozm_usun_ikone(r, kl);
                return 1;
            }
        }
    return 0;
}

int rozm_pliki(struct rozmieszczenie *r, const uint32_t *klucze, int n, int kolumn, int wierszy)
{
    int bez = 0, wolne = miejsc(r, kolumn, wierszy), i;
    for (i = 0; i < n; i++)
        bez += rozm_ikona(r, klucze[i]) < 0;
    if (bez > wolne && wolne == 0 && zwolnij(r, kolumn, wierszy)) { /* the kept cell must be somewhere */
        bez++;
        wolne = miejsc(r, kolumn, wierszy);
    }
    if (bez > wolne) /* one cell stays for "Jeszcze n" */
        wolne--;
    for (i = 0; i < n && wolne > 0; i++)
        if (rozm_ikona(r, klucze[i]) < 0 && rozm_dodaj_ikone(r, klucze[i], kolumn, wierszy) == 0) {
            bez--;
            wolne--;
        }
    return bez;
}

void rozm_usun_ikone(struct rozmieszczenie *r, uint32_t klucz)
{
    int i = rozm_ikona(r, klucz);
    if (i < 0)
        return;
    r->ikony[i] = r->ikony[--r->n_ikon];
}

void rozm_przenies_ikone(struct rozmieszczenie *r, uint32_t klucz, int kol, int wiersz)
{
    int i = rozm_ikona(r, klucz), j;
    uint32_t inna;
    if (i < 0 || kol < 0 || wiersz < 0 || kol > ROZM_KOMORKA_MAKS || wiersz > ROZM_KOMORKA_MAKS)
        return;
    inna = rozm_w_komorce(r, kol, wiersz);
    if (inna != 0 && inna != klucz) {
        j = rozm_ikona(r, inna);
        r->ikony[j].kol = r->ikony[i].kol;
        r->ikony[j].wiersz = r->ikony[i].wiersz;
    }
    r->ikony[i].kol = (uint8_t)kol;
    r->ikony[i].wiersz = (uint8_t)wiersz;
}

/* Order of "Ułóż ikony": Kosz, applications by number, files by number (the oldest first). */
static uint64_t porzadek(uint32_t klucz)
{
    return klucz == ROZM_KOSZ ? 0 : rozm_aplikacja(klucz) ? klucz : (1ull << 32) + klucz;
}

void rozm_uloz(struct rozmieszczenie *r, int wierszy)
{
    int i, j, poz[ROZM_IKON_MAKS];
    if (wierszy < 1)
        wierszy = 1;
    for (i = 0; i < r->n_ikon; i++) { /* the place of each icon in the order */
        poz[i] = 0;
        for (j = 0; j < r->n_ikon; j++)
            poz[i] += porzadek(r->ikony[j].klucz) < porzadek(r->ikony[i].klucz);
    }
    for (i = 0; i < r->n_ikon; i++) {
        r->ikony[i].kol = (uint8_t)(poz[i] / wierszy > ROZM_KOMORKA_MAKS ? ROZM_KOMORKA_MAKS : poz[i] / wierszy);
        r->ikony[i].wiersz = (uint8_t)(poz[i] % wierszy > ROZM_KOMORKA_MAKS ? ROZM_KOMORKA_MAKS : poz[i] % wierszy);
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
        k1_z32(buf + p, r->ikony[i].klucz);
        buf[p + 4] = r->ikony[i].kol;
        buf[p + 5] = r->ikony[i].wiersz;
        p += 6;
    }
    return p;
}

int rozm_czytaj(const uint8_t *buf, size_t n, struct rozmieszczenie *r)
{
    struct rozmieszczenie t;
    size_t p = 5, dl;
    int i, stary;
    memset(&t, 0, sizeof t);
    if (n < 6 || buf[4] > K1_APL_MAKS || n < 6u + buf[4])
        return -1;
    stary = memcmp(buf, ROZM_ZNAK_STARY, 4) == 0; /* 0.7.5: one byte per key, applications only */
    if (!stary && memcmp(buf, ROZM_ZNAK, 4) != 0)
        return -1;
    dl = stary ? 3 : 6;
    for (i = buf[4]; i > 0; i--, p++) {
        if (!poprawna(buf[p]) || rozm_na_pasku(&t, buf[p]) >= 0)
            return -1;
        t.pasek[t.n_pasek++] = buf[p];
    }
    if (buf[p] > (stary ? K1_APL_MAKS : ROZM_IKON_MAKS) || n != p + 1 + dl * buf[p])
        return -1;
    for (i = buf[p++]; i > 0; i--, p += dl) {
        struct rozm_ikona ik = { stary ? buf[p] : k1_c32(buf + p), buf[p + dl - 2], buf[p + dl - 1] };
        if ((stary ? !poprawna((int)ik.klucz) : !klucz_dobry(ik.klucz)) || ik.kol > ROZM_KOMORKA_MAKS ||
            ik.wiersz > ROZM_KOMORKA_MAKS || rozm_ikona(&t, ik.klucz) >= 0 || rozm_w_komorce(&t, ik.kol, ik.wiersz) != 0)
            return -1;
        t.ikony[t.n_ikon].klucz = ik.klucz; /* field by field: the padding of `t` stays zero (compared in fuzzing) */
        t.ikony[t.n_ikon].kol = ik.kol;
        t.ikony[t.n_ikon++].wiersz = ik.wiersz;
    }
    *r = t;
    return 0;
}

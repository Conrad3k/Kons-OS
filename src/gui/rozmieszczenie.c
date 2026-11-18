/* K1 gui — the desktop arrangement: pinned applications and icon cells (rozmieszczenie.h). */
#include "rozmieszczenie.h"

#include <string.h>

#include "k1_bajty.h"

int rozm_aplikacja(uint32_t klucz)
{
    return klucz >= 1 && klucz <= K1_APL_MAKS ? (int)klucz : 0;
}

/* the zones whose row has a Start-menu line (K1_STREFY: its `menu` column is not "") */
#define X_MENU(n, nr, profil, uid, obraz, tytul, sesja, pamiec, procesory, kolor, trwalosc, ikona, log, menu, ...) \
    [nr] = sizeof(menu) > 1,
static const int8_t w_menu[K1_STREFY_MAKS + 1] = { K1_STREFY(X_MENU) };
#undef X_MENU

int rozm_strefa(uint32_t klucz)
{
    uint32_t nr = klucz - ROZM_STREFA;
    return klucz > ROZM_STREFA && nr <= K1_STREFY_MAKS && w_menu[nr] ? (int)nr : 0;
}

/* what the bar holds */
static int na_pasek(uint32_t klucz)
{
    return rozm_aplikacja(klucz) || rozm_strefa(klucz);
}

int rozm_plik(uint32_t klucz)
{
    return klucz >= ROZM_PLIK + K1_DOK_PIERWSZY && klucz - ROZM_PLIK <= K1_DOK_ID_MAKS ? (int)(klucz - ROZM_PLIK) : 0;
}

static int klucz_dobry(uint32_t klucz)
{
    return na_pasek(klucz) || klucz == ROZM_KOSZ || rozm_plik(klucz);
}

void rozm_domyslne(struct rozmieszczenie *r, const int *domyslne, int wierszy)
{
    memset(r, 0, sizeof *r);
    r->ikony[r->n_ikon++].klucz = ROZM_KOSZ;
    for (uint32_t k = 1; k <= ROZM_STREFA + K1_STREFY_MAKS; k = k == K1_APL_MAKS ? ROZM_STREFA + 1 : k + 1)
        if (rozm_aplikacja(k) ? domyslne[k] > 0 : rozm_strefa(k) != 0) { /* -1: a window of K1 itself (never there) */
            r->pasek[r->n_pasek++] = k;
            r->ikony[r->n_ikon++].klucz = k;
        }
    rozm_uloz(r, wierszy);
}

int rozm_na_pasku(const struct rozmieszczenie *r, uint32_t klucz)
{
    int i;
    for (i = 0; i < r->n_pasek; i++)
        if (r->pasek[i] == klucz)
            return i;
    return -1;
}

void rozm_przypnij(struct rozmieszczenie *r, uint32_t klucz)
{
    if (na_pasek(klucz) && rozm_na_pasku(r, klucz) < 0 && r->n_pasek < ROZM_PASEK_MAKS)
        r->pasek[r->n_pasek++] = klucz;
}

void rozm_odepnij(struct rozmieszczenie *r, uint32_t klucz)
{
    int i = rozm_na_pasku(r, klucz);
    if (i < 0)
        return;
    memmove(&r->pasek[i], &r->pasek[i + 1], (size_t)(r->n_pasek - i - 1) * sizeof r->pasek[0]);
    r->n_pasek--;
}

void rozm_przesun_na_pasku(struct rozmieszczenie *r, uint32_t klucz, int nowa)
{
    int i = rozm_na_pasku(r, klucz);
    if (i < 0)
        return;
    nowa = nowa < 0 ? 0 : (nowa >= r->n_pasek ? r->n_pasek - 1 : nowa);
    rozm_odepnij(r, klucz);
    memmove(&r->pasek[nowa + 1], &r->pasek[nowa], (size_t)(r->n_pasek - nowa) * sizeof r->pasek[0]);
    r->pasek[nowa] = klucz;
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

/* Order of "Ułóż ikony": Kosz, applications by number, the zones (ROZM_STREFA + n: above every application), files by
 * number (the oldest first). */
static uint64_t porzadek(uint32_t klucz)
{
    return klucz == ROZM_KOSZ ? 0 : na_pasek(klucz) ? klucz : (1ull << 32) + klucz;
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
    for (i = 0; i < r->n_pasek; i++, p += 4)
        k1_z32(buf + p, r->pasek[i]);
    buf[p++] = (uint8_t)r->n_ikon;
    for (i = 0; i < r->n_ikon; i++) {
        k1_z32(buf + p, r->ikony[i].klucz);
        buf[p + 4] = r->ikony[i].kol;
        buf[p + 5] = r->ikony[i].wiersz;
        p += 6;
    }
    return p;
}

/* The format of a saved arrangement: 3 ("K1R3"), 2 ("K1R2": one byte per application on the bar), 1 ("K1R1": one byte
 * per key everywhere, applications only), 0 unknown. */
static int wersja(const uint8_t *buf)
{
    return memcmp(buf, ROZM_ZNAK, 4) == 0 ? 3 : memcmp(buf, ROZM_ZNAK_K1R2, 4) == 0 ? 2
           : memcmp(buf, ROZM_ZNAK_STARY, 4) == 0                              ? 1
                                                                                : 0;
}

int rozm_czytaj(const uint8_t *buf, size_t n, struct rozmieszczenie *r)
{
    struct rozmieszczenie t;
    size_t p = 5, dl, kp;
    int i, v = n >= 6 ? wersja(buf) : 0;
    memset(&t, 0, sizeof t);
    kp = v == 3 ? 4 : 1; /* bytes per key on the bar */
    if (v == 0 || buf[4] > (v == 3 ? ROZM_PASEK_MAKS : K1_APL_MAKS) || n < 6u + kp * buf[4])
        return -1;
    dl = v == 1 ? 3 : 6;
    for (i = buf[4]; i > 0; i--, p += kp) {
        uint32_t kl = v == 3 ? k1_c32(buf + p) : buf[p];
        if (!(v == 3 ? na_pasek(kl) : rozm_aplikacja(kl) != 0) || rozm_na_pasku(&t, kl) >= 0)
            return -1;
        t.pasek[t.n_pasek++] = kl;
    }
    if (buf[p] > (v == 1 ? K1_APL_MAKS : ROZM_IKON_MAKS) || n != p + 1 + dl * buf[p])
        return -1;
    for (i = buf[p++]; i > 0; i--, p += dl) {
        struct rozm_ikona ik = { v == 1 ? buf[p] : k1_c32(buf + p), buf[p + dl - 2], buf[p + dl - 1] };
        if ((v == 1 ? !rozm_aplikacja(ik.klucz) : !klucz_dobry(ik.klucz)) || ik.kol > ROZM_KOMORKA_MAKS ||
            ik.wiersz > ROZM_KOMORKA_MAKS || rozm_ikona(&t, ik.klucz) >= 0 || rozm_w_komorce(&t, ik.kol, ik.wiersz) != 0)
            return -1;
        t.ikony[t.n_ikon].klucz = ik.klucz; /* field by field: the padding of `t` stays zero (compared in fuzzing) */
        t.ikony[t.n_ikon].kol = ik.kol;
        t.ikony[t.n_ikon++].wiersz = ik.wiersz;
    }
    *r = t;
    return 0;
}

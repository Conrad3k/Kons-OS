/* K1 — the frames of a zone's files port (0.11.0 steps 2–5, ADR 0052): reading checked field by field, writing. */
#include "k1_pliki_strefy.h"

#include <string.h>

#include "k1_bajty.h"
#include "k1_schowek.h"

/* Every type of each direction and its layout (the readability rule 5: a new frame is a row). */
static const struct {
    uint16_t typ;
    int od_strefy, uklad;
    uint32_t wynik_maks;           /* K1_PS_U_WYNIK: the largest result */
    uint32_t nr_maks, x_min, x_maks, y_maks; /* K1_PS_U_TRZY: the ranges of nr (from 1), x, y (from 0) */
} ramki[] = {
    { K1_PS_ZGLOS, 1, K1_PS_U_NAZWA, 0, 0, 0, 0, 0 },    { K1_PS_DANE, 1, K1_PS_U_BAJTY, 0, 0, 0, 0, 0 },
    { K1_PS_BRAK, 1, K1_PS_U_NR, 0, 0, 0, 0, 0 },        { K1_PS_ODEBRANY, 1, K1_PS_U_WYNIK, 1, 0, 0, 0, 0 },
    { K1_PS_WYSLIJ, 0, K1_PS_U_NR, 0, 0, 0, 0, 0 },      { K1_PS_WYNIK, 0, K1_PS_U_WYNIK, K1_PS_W_MAKS, 0, 0, 0, 0 },
    { K1_PS_PLIK, 0, K1_PS_U_NAZWA, 0, 0, 0, 0, 0 },     { K1_PS_BAJTY, 0, K1_PS_U_BAJTY, 0, 0, 0, 0, 0 },
    { K1_PS_UPUSC, 0, K1_PS_U_TRZY, 0, K1_PRZ_DO_STREFY_MAKS, 0, K1_PS_POZ_MAKS, K1_PS_POZ_MAKS }, /* step 4 */
    /* step 5: the clipboard — x = the text's length, y = where it was copied (0 K1, a zone's number) */
    { K1_PS_KOPIA, 1, K1_PS_U_TRZY, 0, K1_PS_NR_MAKS, 1, K1_SCHOWEK_MAKS, 0 },
    { K1_PS_KOPIA_TEKST, 1, K1_PS_U_BAJTY, 0, 0, 0, 0, 0 }, { K1_PS_WKLEJONE, 1, K1_PS_U_NR, 0, 0, 0, 0, 0 },
    { K1_PS_WKLEJ, 0, K1_PS_U_TRZY, 0, K1_PS_NR_MAKS, 1, K1_SCHOWEK_MAKS, K1_STREFY_MAKS },
    { K1_PS_WKLEJ_TEKST, 0, K1_PS_U_BAJTY, 0, 0, 0, 0, 0 },
};

/* The three numbers of a K1_PS_U_TRZY frame of row w within its ranges. */
static int trzy_dobre(int w, uint32_t nr, uint32_t x, uint32_t y)
{
    return nr <= ramki[w].nr_maks && x >= ramki[w].x_min && x <= ramki[w].x_maks && y <= ramki[w].y_maks;
}

static int wiersz(uint16_t typ, int od_strefy)
{
    for (size_t i = 0; i < sizeof ramki / sizeof ramki[0]; i++)
        if (ramki[i].typ == typ && ramki[i].od_strefy == (od_strefy != 0))
            return (int)i;
    return -1;
}

int k1_ps_uklad(uint16_t typ, int od_strefy)
{
    int w = wiersz(typ, od_strefy);
    return w < 0 ? 0 : ramki[w].uklad;
}

/* The length of what follows the header for a layout: exact (*min == *maks) or a range. */
static void dlugosc(int uklad, uint32_t *min, uint32_t *maks)
{
    static const uint32_t m[][2] = { [K1_PS_U_NAZWA] = { 17 + 1, 17 + K1_PS_NAZWA_MAKS },
                                     [K1_PS_U_BAJTY] = { 4 + 1, 4 + K1_PS_DANE_MAKS },
                                     [K1_PS_U_NR] = { 4, 4 }, [K1_PS_U_WYNIK] = { 8, 8 }, [K1_PS_U_TRZY] = { 12, 12 } };
    *min = m[uklad][0], *maks = m[uklad][1];
}

/* The fields of the whole frame in c->b: 0, or -1 against the rules. */
static int pola(const struct k1_ps_czytnik *c, uint32_t dl, struct k1_ps_ramka *r)
{
    const uint8_t *p = c->b + K1_PS_NAGLOWEK;
    int w;
    memset(r, 0, sizeof *r);
    r->typ = k1_c16(c->b);
    r->nr = k1_c32(p);
    w = wiersz(r->typ, c->od_strefy);
    if (r->nr < 1 || r->nr > K1_PS_NR_MAKS || w < 0)
        return -1;
    if (ramki[w].uklad == K1_PS_U_NAZWA) {
        r->rozmiar = k1_c64(p + 4);
        r->dodatek = k1_c32(p + 12);
        r->nl = p[16];
        if (r->nl < 1 || dl != 17u + r->nl || memchr(p + 17, 0, r->nl) != NULL || r->dodatek > K1_PS_NR_MAKS ||
            (r->typ == K1_PS_PLIK && r->dodatek > K1_PS_TRYB_MAKS))
            return -1;
        memcpy(r->nazwa, p + 17, r->nl);
    } else if (ramki[w].uklad == K1_PS_U_BAJTY) {
        r->dane = p + 4;
        r->dl = dl - 4;
    } else if (ramki[w].uklad == K1_PS_U_WYNIK) {
        r->wynik = k1_c32(p + 4);
        if (r->wynik > ramki[w].wynik_maks)
            return -1;
    } else if (ramki[w].uklad == K1_PS_U_TRZY) {
        r->x = k1_c32(p + 4);
        r->y = k1_c32(p + 8);
        if (!trzy_dobre(w, r->nr, r->x, r->y))
            return -1;
    }
    return 0;
}

int k1_ps_czytaj(struct k1_ps_czytnik *c, const uint8_t *p, size_t n, size_t *uzyte, struct k1_ps_ramka *r)
{
    uint32_t dl = 0, min, maks, chce;
    size_t ile;
    int uklad;
    *uzyte = 0;
    memset(r, 0, sizeof *r);
    for (;;) {
        chce = K1_PS_NAGLOWEK;
        if (c->n >= K1_PS_NAGLOWEK) {
            dl = k1_c32(c->b + 4);
            if ((uklad = k1_ps_uklad(k1_c16(c->b), c->od_strefy)) == 0 || k1_c16(c->b + 2) != 0)
                return -1;
            dlugosc(uklad, &min, &maks);
            if (dl < min || dl > maks)
                return -1;
            chce = K1_PS_NAGLOWEK + dl;
        }
        if (c->n == chce)
            break;
        if (*uzyte == n)
            return 0;
        ile = chce - c->n < n - *uzyte ? chce - c->n : n - *uzyte;
        memcpy(c->b + c->n, p + *uzyte, ile);
        c->n += (uint32_t)ile;
        *uzyte += ile;
    }
    c->n = 0; /* the next call starts a new frame; r->dane stays valid until then (it is not overwritten before) */
    return pola(c, dl, r) == 0 ? 1 : -1;
}

size_t k1_ps_zapisz(const struct k1_ps_ramka *r, int od_strefy, uint8_t *b, size_t maks)
{
    int w = wiersz(r->typ, od_strefy), u;
    uint32_t min, mx, dl;
    if (w < 0 || r->nr < 1 || r->nr > K1_PS_NR_MAKS)
        return 0;
    u = ramki[w].uklad;
    dlugosc(u, &min, &mx);
    dl = u == K1_PS_U_NAZWA ? 17u + r->nl : u == K1_PS_U_BAJTY ? 4u + r->dl : min;
    if (dl < min || dl > mx || K1_PS_NAGLOWEK + (size_t)dl > maks || (u == K1_PS_U_WYNIK && r->wynik > ramki[w].wynik_maks) ||
        (u == K1_PS_U_NAZWA && (memchr(r->nazwa, 0, r->nl) != NULL ||
                                r->dodatek > (r->typ == K1_PS_PLIK ? (uint32_t)K1_PS_TRYB_MAKS : K1_PS_NR_MAKS))) ||
        (u == K1_PS_U_TRZY && !trzy_dobre(w, r->nr, r->x, r->y)) ||
        (u == K1_PS_U_BAJTY && r->dane == NULL))
        return 0;
    k1_z16(b, r->typ);
    k1_z16(b + 2, 0);
    k1_z32(b + 4, dl);
    k1_z32(b + 8, r->nr);
    if (u == K1_PS_U_NAZWA) {
        k1_z64(b + 12, r->rozmiar);
        k1_z32(b + 20, r->dodatek);
        b[24] = r->nl;
        memcpy(b + 25, r->nazwa, r->nl);
    } else if (u == K1_PS_U_BAJTY) {
        memmove(b + 12, r->dane, r->dl);
    } else if (u == K1_PS_U_WYNIK) {
        k1_z32(b + 12, r->wynik);
    } else if (u == K1_PS_U_TRZY) {
        k1_z32(b + 12, r->x);
        k1_z32(b + 16, r->y);
    }
    return K1_PS_NAGLOWEK + (size_t)dl;
}

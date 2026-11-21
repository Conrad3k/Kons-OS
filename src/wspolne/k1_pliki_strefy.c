/* K1 — the frames of a zone's files port (0.11.0 step 2, ADR 0052): reading checked field by field, writing. */
#include "k1_pliki_strefy.h"

#include <string.h>

#include "k1_bajty.h"

/* The length of what follows the header of a frame of type `typ` in that direction: exact (*min == *maks) or a range;
 * 0 for a type that does not exist there. */
static int dlugosc(uint16_t typ, int od_strefy, uint32_t *min, uint32_t *maks)
{
    if (od_strefy && typ == K1_PS_ZGLOS)
        *min = 13 + 1, *maks = 13 + K1_PS_NAZWA_MAKS;
    else if (od_strefy && typ == K1_PS_DANE)
        *min = 4 + 1, *maks = 4 + K1_PS_DANE_MAKS;
    else if ((od_strefy && typ == K1_PS_BRAK) || (!od_strefy && typ == K1_PS_WYSLIJ))
        *min = *maks = 4;
    else if (!od_strefy && typ == K1_PS_WYNIK)
        *min = *maks = 8;
    else
        return 0;
    return 1;
}

/* The fields of the whole frame in c->b: 0, or -1 against the rules. */
static int pola(const struct k1_ps_czytnik *c, uint32_t dl, struct k1_ps_ramka *r)
{
    const uint8_t *p = c->b + K1_PS_NAGLOWEK;
    memset(r, 0, sizeof *r);
    r->typ = k1_c16(c->b);
    r->nr = k1_c32(p);
    if (r->nr < 1 || r->nr > K1_PS_NR_MAKS)
        return -1;
    if (r->typ == K1_PS_ZGLOS && c->od_strefy) {
        r->rozmiar = k1_c64(p + 4);
        r->nl = p[12];
        if (r->nl < 1 || dl != 13u + r->nl || memchr(p + 13, 0, r->nl) != NULL)
            return -1;
        memcpy(r->nazwa, p + 13, r->nl);
    } else if (r->typ == K1_PS_DANE && c->od_strefy) {
        r->dane = p + 4;
        r->dl = dl - 4;
    } else if (r->typ == K1_PS_WYNIK && !c->od_strefy) {
        r->wynik = k1_c32(p + 4);
        if (r->wynik > K1_PS_W_MAKS)
            return -1;
    }
    return 0;
}

int k1_ps_czytaj(struct k1_ps_czytnik *c, const uint8_t *p, size_t n, size_t *uzyte, struct k1_ps_ramka *r)
{
    uint32_t dl = 0, min, maks, chce;
    size_t ile;
    *uzyte = 0;
    memset(r, 0, sizeof *r);
    for (;;) {
        chce = K1_PS_NAGLOWEK;
        if (c->n >= K1_PS_NAGLOWEK) {
            dl = k1_c32(c->b + 4);
            if (!dlugosc(k1_c16(c->b), c->od_strefy, &min, &maks) || k1_c16(c->b + 2) != 0 || dl < min || dl > maks)
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
    int zglos = od_strefy && r->typ == K1_PS_ZGLOS, bajty = od_strefy && r->typ == K1_PS_DANE,
        wynik = !od_strefy && r->typ == K1_PS_WYNIK; /* the numbers of the types repeat in the other direction */
    uint32_t min, mx, dl;
    if (!dlugosc(r->typ, od_strefy, &min, &mx) || r->nr < 1 || r->nr > K1_PS_NR_MAKS)
        return 0;
    dl = zglos ? 13u + r->nl : bajty ? 4u + r->dl : min;
    if (dl < min || dl > mx || K1_PS_NAGLOWEK + (size_t)dl > maks || (wynik && r->wynik > K1_PS_W_MAKS) ||
        (zglos && memchr(r->nazwa, 0, r->nl) != NULL) || (bajty && r->dane == NULL))
        return 0;
    k1_z16(b, r->typ);
    k1_z16(b + 2, 0);
    k1_z32(b + 4, dl);
    k1_z32(b + 8, r->nr);
    if (zglos) {
        k1_z64(b + 12, r->rozmiar);
        b[20] = r->nl;
        memcpy(b + 21, r->nazwa, r->nl);
    } else if (bajty) {
        memmove(b + 12, r->dane, r->dl);
    } else if (wynik) {
        k1_z32(b + 12, r->wynik);
    }
    return K1_PS_NAGLOWEK + (size_t)dl;
}

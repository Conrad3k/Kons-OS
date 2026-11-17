/* K1 — the release announcement. See k1_ogloszenie.h. */
#include "k1_ogloszenie.h"
#include "k1_bajty.h"

#include <string.h>

/* The characters a description may hold besides ASCII letters and digits: those the fonts of K1 draw
 * (narzedzia/czcionki.c) and a text needs — the Polish letters and plain punctuation. */
static const uint32_t ZNAKI[] = { ' ', '.', ',', ':', ';', '!', '?', '(', ')', '-', '"', '\'', '/', '%', '+',
                                  0x104, 0x105, 0x106, 0x107, 0x118, 0x119, 0x141, 0x142, 0x143, 0x144, 0xd3, 0xf3,
                                  0x15a, 0x15b, 0x179, 0x17a, 0x17b, 0x17c, 0xb7, 0x2013, 0x2014, 0x2026, 0x201e,
                                  0x201d };

static int znak_dobry(uint32_t z)
{
    size_t i;
    if ((z >= 'a' && z <= 'z') || (z >= 'A' && z <= 'Z') || (z >= '0' && z <= '9'))
        return 1;
    for (i = 0; i < sizeof ZNAKI / sizeof ZNAKI[0]; i++)
        if (ZNAKI[i] == z)
            return 1;
    return 0;
}

/* The next character of UTF-8 at t[*i] (shortest form only), or 0xFFFFFFFF. */
static uint32_t utf8(const unsigned char *t, size_t n, size_t *i)
{
    uint32_t z, min;
    int dalej, k;
    unsigned char c = t[*i];
    if (c < 0x80)
        return (*i)++, c;
    if ((c & 0xE0) == 0xC0)
        z = c & 0x1F, dalej = 1, min = 0x80;
    else if ((c & 0xF0) == 0xE0)
        z = c & 0x0F, dalej = 2, min = 0x800;
    else
        return 0xFFFFFFFFu; /* four bytes: nothing of the table */
    if (n - *i <= (size_t)dalej)
        return 0xFFFFFFFFu;
    for (k = 1; k <= dalej; k++) {
        if ((t[*i + (size_t)k] & 0xC0) != 0x80)
            return 0xFFFFFFFFu;
        z = z << 6 | (t[*i + (size_t)k] & 0x3F);
    }
    if (z < min || (z >= 0xD800 && z <= 0xDFFF))
        return 0xFFFFFFFFu;
    *i += (size_t)dalej + 1;
    return z;
}

static int zawiera(const char *t, size_t n, const char *wzor)
{
    size_t m = strlen(wzor), i, j;
    for (i = 0; i + m <= n; i++) {
        for (j = 0; j < m && (t[i + j] | 0x20) == wzor[j]; j++)
            ;
        if (j == m)
            return 1;
    }
    return 0;
}

int k1_ogl_tekst_dobry(const char *t, size_t n)
{
    size_t i = 0;
    if (n == 0 || n > K1_OGL_TEKST_MAKS || zawiera(t, n, "://") || zawiera(t, n, "www."))
        return 0;
    while (i < n)
        if (!znak_dobry(utf8((const unsigned char *)t, n, &i)))
            return 0;
    return 1;
}

static int zera(const uint8_t *b, size_t od, size_t do_)
{
    for (; od < do_; od++)
        if (b[od] != 0)
            return 0;
    return 1;
}

/* The rules of the descriptions (both ways): kinds, texts, versions newest first and not newer than the release. */
static int opisy_dobre(const struct k1_ogloszenie *o)
{
    uint32_t i;
    if (o->n > K1_OGL_OPISOW_MAKS)
        return 0;
    for (i = 0; i < o->n; i++) {
        const struct k1_ogl_opis *d = &o->opis[i];
        if (d->wersja == 0 || d->wersja > (i == 0 ? o->wersja : o->opis[i - 1].wersja - 1))
            return 0;
        if (d->rodzaj == K1_OGL_POPRAWKI ? d->dl != 0
                                         : d->rodzaj != K1_OGL_OPIS || !k1_ogl_tekst_dobry(d->tekst, d->dl))
            return 0;
    }
    return 1;
}

int k1_ogloszenie_czytaj(const uint8_t *b, size_t n, struct k1_ogloszenie *o)
{
    size_t p = K1_OGL_NAGLOWEK;
    uint32_t i;
    memset(o, 0, sizeof *o);
    if (n < K1_OGL_NAGLOWEK + 64 || n > K1_OGL_MAKS || memcmp(b, "K1O1", 4) != 0 || k1_c32(b + 4) != 1)
        goto zle;
    o->wersja = k1_c32(b + 8);
    o->flagi = k1_c32(b + 12);
    memcpy(o->wydanie, b + 16, 16);
    o->paczka_bajty = k1_c64(b + 32);
    o->czas = k1_c64(b + 40);
    memcpy(o->sha256, b + 48, 32);
    memcpy(o->klucz_id, b + 80, 8);
    o->n = k1_c32(b + 88);
    o->dl = k1_c32(b + 92);
    if (o->wersja == 0 || o->wersja > 0xffffffu || (o->flagi & ~K1_OGL_NOWA_LINIA) != 0 || o->paczka_bajty == 0 ||
        o->paczka_bajty > K1_OGL_PACZKA_MAKS || o->n > K1_OGL_OPISOW_MAKS || (size_t)o->dl + 64 != n ||
        !zera(b, 96, K1_OGL_NAGLOWEK))
        goto zle;
    for (i = 0; i < o->n; i++) {
        struct k1_ogl_opis *d = &o->opis[i];
        size_t z;
        if (o->dl - p < 12)
            goto zle;
        d->wersja = k1_c32(b + p);
        d->rodzaj = k1_c32(b + p + 4);
        d->dl = k1_c32(b + p + 8);
        p += 12;
        if (d->dl > K1_OGL_TEKST_MAKS || o->dl - p < d->dl)
            goto zle;
        memcpy(d->tekst, b + p, d->dl);
        d->tekst[d->dl] = '\0';
        z = (d->dl + 3u) & ~3u;
        if (o->dl - p < z || !zera(b, p + d->dl, p + z))
            goto zle;
        p += z;
    }
    if (p != o->dl || !opisy_dobre(o))
        goto zle;
    return 0;
zle:
    memset(o, 0, sizeof *o);
    return -1;
}

size_t k1_ogloszenie_zbuduj(struct k1_ogloszenie *o, uint8_t *b, size_t maks)
{
    size_t p = K1_OGL_NAGLOWEK;
    uint32_t i;
    if (o->wersja == 0 || o->wersja > 0xffffffu || (o->flagi & ~K1_OGL_NOWA_LINIA) != 0 || o->paczka_bajty == 0 ||
        o->paczka_bajty > K1_OGL_PACZKA_MAKS || maks < K1_OGL_NAGLOWEK + 64 || !opisy_dobre(o))
        return 0;
    memset(b, 0, K1_OGL_NAGLOWEK);
    for (i = 0; i < o->n; i++) {
        const struct k1_ogl_opis *d = &o->opis[i];
        size_t z = (d->dl + 3u) & ~3u;
        if (p + 12 + z + 64 > maks || p + 12 + z + 64 > K1_OGL_MAKS)
            return 0;
        k1_z32(b + p, d->wersja);
        k1_z32(b + p + 4, d->rodzaj);
        k1_z32(b + p + 8, d->dl);
        memset(b + p + 12, 0, z);
        memcpy(b + p + 12, d->tekst, d->dl);
        p += 12 + z;
    }
    o->dl = (uint32_t)p;
    memcpy(b, "K1O1", 4);
    k1_z32(b + 4, 1);
    k1_z32(b + 8, o->wersja);
    k1_z32(b + 12, o->flagi);
    memcpy(b + 16, o->wydanie, 16);
    k1_z64(b + 32, o->paczka_bajty);
    k1_z64(b + 40, o->czas);
    memcpy(b + 48, o->sha256, 32);
    memcpy(b + 80, o->klucz_id, 8);
    k1_z32(b + 88, o->n);
    k1_z32(b + 92, o->dl);
    return p;
}

void k1_ogl_wybierz(const struct k1_ogloszenie *o, uint32_t dziala, struct k1_ogl_wybor *w)
{
    uint32_t i;
    memset(w, 0, sizeof *w);
    for (i = 0; i < o->n && i < K1_OGL_OPISOW_MAKS && o->opis[i].wersja > dziala; i++) {
        const struct k1_ogl_opis *d = &o->opis[i];
        if (d->rodzaj == K1_OGL_POPRAWKI && w->n > 0 && w->w[w->n - 1].rodzaj == K1_OGL_POPRAWKI)
            continue; /* fixes one after another: one line, under the newest of them */
        w->w[w->n].wersja = d->wersja;
        w->w[w->n].rodzaj = d->rodzaj == K1_OGL_OPIS ? K1_OGL_OPIS : K1_OGL_POPRAWKI;
        w->w[w->n].tekst = d->rodzaj == K1_OGL_OPIS ? d->tekst : K1_OGL_ZDANIE_POPRAWEK;
        w->n++;
    }
    w->wczesniejsze = o->n == K1_OGL_OPISOW_MAKS && i == o->n;
}

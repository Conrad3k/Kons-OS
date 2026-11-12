/* K1 — names of files and folders (0.8.0, ADR 0029): what a name may be, repairing one, comparing two, a name from
 * a text. See k1_dokument.h. Split from k1_dokument.c before it grows (0.8.2 step 2). */
#include <string.h>

#include "k1_dokument.h"

/* One code point of UTF-8 at s[0..n): its length, 0 for anything invalid (overlong, surrogate, > U+10FFFF,
 * cut off). */
size_t k1_dok_utf8(const unsigned char *s, size_t n, uint32_t *kod)
{
    size_t dl, i;
    uint32_t k;
    if (n == 0)
        return 0;
    if (s[0] < 0x80) {
        *kod = s[0];
        return 1;
    }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) {
        dl = 2;
        k = s[0] & 0x1fu;
    } else if (s[0] >= 0xe0 && s[0] <= 0xef) {
        dl = 3;
        k = s[0] & 0x0fu;
    } else if (s[0] >= 0xf0 && s[0] <= 0xf4) {
        dl = 4;
        k = s[0] & 0x07u;
    } else {
        return 0;
    }
    if (n < dl)
        return 0;
    for (i = 1; i < dl; i++) {
        if ((s[i] & 0xc0) != 0x80)
            return 0;
        k = (k << 6) | (s[i] & 0x3fu);
    }
    if ((dl == 3 && k < 0x800) || (dl == 4 && (k < 0x10000 || k > 0x10ffff)) || (k >= 0xd800 && k <= 0xdfff))
        return 0;
    *kod = k;
    return dl;
}

/* A character a name may have: no control characters, none of \ / : * ? " < > |, no text-direction tricks. */
static int znak_dozwolony(uint32_t k)
{
    if (k < 0x20 || (k >= 0x7f && k <= 0x9f) || k == 0x2028 || k == 0x2029 || k == 0xfeff ||
        (k >= 0x202a && k <= 0x202e) || (k >= 0x2066 && k <= 0x2069))
        return 0;
    return k >= 0x80 || strchr("\\/:*?\"<>|", (int)k) == NULL;
}

int k1_dok_nazwa_poprawna(const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    if (s == NULL || n < 1 || n >= K1_DOK_NAZWA || u[0] == ' ' || u[n - 1] == ' ' || u[n - 1] == '.')
        return 0;
    while (i < n) {
        uint32_t k;
        size_t d = k1_dok_utf8(u + i, n - i, &k);
        if (d == 0 || !znak_dozwolony(k))
            return 0;
        i += d;
    }
    return 1;
}

void k1_dok_nazwa_popraw(const char *s, size_t n, char out[K1_DOK_NAZWA])
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0, o = 0;
    while (i < n) {
        uint32_t k;
        size_t d = k1_dok_utf8(u + i, n - i, &k), dz = d;
        const unsigned char *z = u + i; /* what goes out: the letter, or "_" for a bad one */
        if (d == 0 || !znak_dozwolony(k)) {
            z = (const unsigned char *)"_";
            dz = 1;
            d = d == 0 ? 1 : d;
        } else if (k == ' ' && o == 0) { /* no space at the start */
            i += d;
            continue;
        }
        if (o + dz > K1_DOK_NAZWA - 1)
            break; /* whole letters only */
        memcpy(out + o, z, dz);
        o += dz;
        i += d;
    }
    while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '.'))
        o--;
    out[o] = '\0';
}

/* Small letter for a capital: A-Z and the Polish ones. */
static uint32_t mala(uint32_t k)
{
    static const uint32_t duze[] = { 0x104, 0x106, 0x118, 0x141, 0x143, 0xd3, 0x15a, 0x179, 0x17b };
    size_t i;
    if (k >= 'A' && k <= 'Z')
        return k + 32;
    for (i = 0; i < sizeof duze / sizeof duze[0]; i++)
        if (k == duze[i])
            return k == 0xd3 ? 0xf3 : k + 1;
    return k;
}

int k1_dok_nazwy_rowne(const char *a, const char *b)
{
    size_t na = strnlen(a, K1_DOK_NAZWA), nb = strnlen(b, K1_DOK_NAZWA), i = 0, j = 0;
    while (i < na && j < nb) {
        uint32_t ka, kb;
        size_t da = k1_dok_utf8((const unsigned char *)a + i, na - i, &ka), db = k1_dok_utf8((const unsigned char *)b + j, nb - j, &kb);
        if (da == 0 || db == 0)
            return na == nb && memcmp(a, b, na) == 0;
        if (mala(ka) != mala(kb))
            return 0;
        i += da;
        j += db;
    }
    return i == na && j == nb;
}

/* ---- a name from a text (Notatki) ---- */

#define NAZWA_Z_TEKSTU 48

static int zakazany(unsigned char c)
{
    return c < 0x20 || c == 0x7f || (c != 0 && strchr("\\/:*?\"<>|", c) != NULL);
}

void k1_dok_nazwa_z_tekstu(const char *t, size_t dl, char out[K1_DOK_NAZWA])
{
    size_t i = 0, n = 0;
    while (i < dl && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r'))
        i++; /* the first line with something in it */
    for (; i < dl && t[i] != '\n' && n < NAZWA_Z_TEKSTU; i++) {
        unsigned char c = (unsigned char)t[i];
        if (zakazany(c) || (c == ' ' && (n == 0 || out[n - 1] == ' ')))
            continue; /* characters a name cannot have; one space at a time */
        out[n++] = (char)c;
    }
    if (n > 0 && i < dl && ((unsigned char)t[i] & 0xc0) == 0x80) /* cut inside a letter: without it */
        while (n > 0 && ((unsigned char)out[--n] & 0xc0) == 0x80)
            ;
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.'))
        n--;
    out[n] = '\0';
    if (n == 0 || !k1_dok_nazwa_poprawna(out, n))
        memcpy(out, "Notatka", sizeof "Notatka");
}

int k1_dok_nazwa_pasuje(const char *nazwa, const char *baza)
{
    size_t n = strlen(baza), i;
    if (strncmp(nazwa, baza, n) != 0)
        return 0;
    if (nazwa[n] == '\0')
        return 1;
    if (nazwa[n] != ' ' || nazwa[n + 1] != '(' || nazwa[n + 2] < '0' || nazwa[n + 2] > '9')
        return 0;
    for (i = n + 2; nazwa[i] >= '0' && nazwa[i] <= '9'; i++)
        ;
    return nazwa[i] == ')' && nazwa[i + 1] == '\0';
}

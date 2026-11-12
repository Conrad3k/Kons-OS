/* K1 — names of files and folders (0.8.0, ADR 0029; 0.8.2 step 2: names that cannot pretend to be others). See
 * k1_dokument.h. One table says which characters a name may not have (and where); one table says which letters
 * written as a letter + a mark (NFD, as macOS writes them) are one letter in K1 (NFC): "a" + U+0328 is "ą", so two
 * names that look the same are the same name. Comparing names: A-Z, Latin-1 and Latin Extended-A capitals equal to
 * small letters, as Windows does. */
#include <string.h>

#include "k1_dokument.h"

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

static size_t na_utf8(uint32_t k, char *out)
{
    if (k < 0x80) {
        out[0] = (char)k;
        return 1;
    }
    if (k < 0x800) {
        out[0] = (char)(0xc0 | (k >> 6));
        out[1] = (char)(0x80 | (k & 0x3f));
        return 2;
    }
    if (k < 0x10000) {
        out[0] = (char)(0xe0 | (k >> 12));
        out[1] = (char)(0x80 | ((k >> 6) & 0x3f));
        out[2] = (char)(0x80 | (k & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (k >> 18));
    out[1] = (char)(0x80 | ((k >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((k >> 6) & 0x3f));
    out[3] = (char)(0x80 | (k & 0x3f));
    return 4;
}

/* ---- characters a name may not have: one row = a range, where it is not allowed, and why ---- */

static const struct zakres {
    uint32_t od, do_;
    int gdzie; /* K1_ZNAK_* */
} zakazane[] = {
    { 0x0000, 0x001f, K1_ZNAK_NIGDZIE },     /* control characters */
    { 0x007f, 0x009f, K1_ZNAK_NIGDZIE },     /* control characters */
    { 0x00ad, 0x00ad, K1_ZNAK_NIGDZIE },     /* soft hyphen: invisible */
    { 0x061c, 0x061c, K1_ZNAK_NIGDZIE },     /* Arabic letter mark: changes the direction of the text */
    { 0x115f, 0x1160, K1_ZNAK_NIGDZIE },     /* Hangul fillers: an empty-looking letter */
    { 0x180e, 0x180e, K1_ZNAK_NIGDZIE },     /* Mongolian vowel separator: invisible */
    { 0x200b, 0x200f, K1_ZNAK_NIGDZIE },     /* zero-width space, joiners, direction marks */
    { 0x2028, 0x202e, K1_ZNAK_NIGDZIE },     /* line and paragraph separators, direction embeddings */
    { 0x2060, 0x2064, K1_ZNAK_NIGDZIE },     /* word joiner, invisible operators */
    { 0x2066, 0x206f, K1_ZNAK_NIGDZIE },     /* direction isolates, deprecated format characters */
    { 0x3164, 0x3164, K1_ZNAK_NIGDZIE },     /* Hangul filler */
    { 0xd800, 0xdfff, K1_ZNAK_NIGDZIE },     /* surrogates (not characters in UTF-8) */
    { 0xe000, 0xf8ff, K1_ZNAK_NIGDZIE },     /* private use: no meaning, any look */
    { 0xfdd0, 0xfdef, K1_ZNAK_NIGDZIE },     /* noncharacters */
    { 0xfeff, 0xfeff, K1_ZNAK_NIGDZIE },     /* byte order mark / zero-width no-break space */
    { 0xffa0, 0xffa0, K1_ZNAK_NIGDZIE },     /* halfwidth Hangul filler */
    { 0xfff9, 0xfffb, K1_ZNAK_NIGDZIE },     /* interlinear annotation marks */
    { 0xe0000, 0xe007f, K1_ZNAK_NIGDZIE },   /* tags: invisible */
    { 0xf0000, 0x10ffff, K1_ZNAK_NIGDZIE },  /* private use planes */
    { 0x0020, 0x0020, K1_ZNAK_NA_BRZEGU },   /* a space at the start or the end: "a.txt " looks like "a.txt" */
    { 0x00a0, 0x00a0, K1_ZNAK_NA_BRZEGU },   /* no-break space */
    { 0x2000, 0x200a, K1_ZNAK_NA_BRZEGU },   /* spaces of other widths */
    { 0x202f, 0x202f, K1_ZNAK_NA_BRZEGU },   /* narrow no-break space */
    { 0x205f, 0x205f, K1_ZNAK_NA_BRZEGU },   /* medium mathematical space */
    { 0x3000, 0x3000, K1_ZNAK_NA_BRZEGU },   /* ideographic space */
    { 0x0300, 0x036f, K1_ZNAK_NA_POCZATKU }, /* combining marks: on nothing at the start */
    { 0x1ab0, 0x1aff, K1_ZNAK_NA_POCZATKU },
    { 0x1dc0, 0x1dff, K1_ZNAK_NA_POCZATKU },
    { 0x20d0, 0x20ff, K1_ZNAK_NA_POCZATKU },
    { 0xfe20, 0xfe2f, K1_ZNAK_NA_POCZATKU },
};

int k1_dok_znak(uint32_t k)
{
    if (k > 0x10ffff || (k & 0xfffe) == 0xfffe || (k < 0x80 && k != 0 && strchr("\\/:*?\"<>|", (int)k) != NULL))
        return K1_ZNAK_NIGDZIE; /* U+xFFFE, U+xFFFF: noncharacters in every plane; what Windows does not allow */
    for (size_t i = 0; i < sizeof zakazane / sizeof zakazane[0]; i++)
        if (k >= zakazane[i].od && k <= zakazane[i].do_)
            return zakazane[i].gdzie;
    return K1_ZNAK_WSZEDZIE;
}

/* ---- a letter and a mark that are one letter (NFC) ---- */

static const struct {
    uint32_t znak;               /* the combining mark */
    const char *litery;          /* after these letters */
    const uint_least16_t *jedna; /* …are these letters (the same order) */
} skladanie[] = {
    { 0x300, "AEIOUaeiou", u"ÀÈÌÒÙàèìòù" },                                 /* grave accent */
    { 0x301, "AEIOUYaeiouyCcLlNnRrSsZz", u"ÁÉÍÓÚÝáéíóúýĆćĹĺŃńŔŕŚśŹź" },     /* acute accent */
    { 0x302, "AEIOUaeiouCcGgHhJjSsWwYy", u"ÂÊÎÔÛâêîôûĈĉĜĝĤĥĴĵŜŝŴŵŶŷ" },     /* circumflex */
    { 0x303, "ANOanoIiUu", u"ÃÑÕãñõĨĩŨũ" },                                 /* tilde */
    { 0x304, "AaEeIiOoUu", u"ĀāĒēĪīŌōŪū" },                                 /* macron */
    { 0x306, "AaEeGgIiOoUu", u"ĂăĔĕĞğĬĭŎŏŬŭ" },                             /* breve */
    { 0x307, "CcEeGgIZz", u"ĊċĖėĠġİŻż" },                                   /* dot above */
    { 0x308, "AEIOUaeiouyY", u"ÄËÏÖÜäëïöüÿŸ" },                             /* diaeresis */
    { 0x30a, "AaUu", u"ÅåŮů" },                                             /* ring above */
    { 0x30b, "OoUu", u"ŐőŰű" },                                             /* double acute */
    { 0x30c, "CcDdEeLlNnRrSsTtZz", u"ČčĎďĚěĽľŇňŘřŠšŤťŽž" },                 /* caron */
    { 0x327, "CcGgKkLlNnRrSsTt", u"ÇçĢģĶķĻļŅņŖŗŞşŢţ" },                     /* cedilla */
    { 0x328, "AaEeIiUu", u"ĄąĘęĮįŲų" },                                     /* ogonek */
};

uint32_t k1_dok_jedna_litera(uint32_t litera, uint32_t znak)
{
    if (litera == 0 || litera >= 0x80)
        return 0;
    for (size_t i = 0; i < sizeof skladanie / sizeof skladanie[0]; i++) {
        const char *p = skladanie[i].znak == znak ? strchr(skladanie[i].litery, (int)litera) : NULL;
        if (p != NULL)
            return skladanie[i].jedna[p - skladanie[i].litery];
    }
    return 0;
}

int k1_dok_nazwa_poprawna(const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    uint32_t poprzedni = 0;
    if (s == NULL || n < 1 || n >= K1_DOK_NAZWA || u[n - 1] == '.')
        return 0;
    while (i < n) {
        uint32_t k;
        size_t d = k1_dok_utf8(u + i, n - i, &k);
        int z = d == 0 ? K1_ZNAK_NIGDZIE : k1_dok_znak(k);
        if (z == K1_ZNAK_NIGDZIE || (z == K1_ZNAK_NA_BRZEGU && (i == 0 || i + d == n)) || (z == K1_ZNAK_NA_POCZATKU && i == 0) ||
            k1_dok_jedna_litera(poprzedni, k) != 0)
            return 0; /* a letter + a mark that is one letter: written as one (NFC) */
        poprzedni = k;
        i += d;
    }
    return 1;
}

/* The letters of a name being repaired: code points and the bytes they take. */
struct litery {
    uint32_t k[K1_DOK_TEKST_MAKS];
    size_t n, bajtow, maks;
};

static size_t bajty(uint32_t k)
{
    return k < 0x80 ? 1 : k < 0x800 ? 2 : k < 0x10000 ? 3 : 4;
}

/* One more letter: 0, or -1 when it does not fit (whole letters only). */
static int dodaj(struct litery *l, uint32_t k)
{
    uint32_t jedna = l->n > 0 ? k1_dok_jedna_litera(l->k[l->n - 1], k) : 0;
    int z = k1_dok_znak(k);
    if (jedna != 0) { /* "a" + U+0328 → "ą" */
        if (l->bajtow - 1 + bajty(jedna) > l->maks)
            return -1;
        l->bajtow += bajty(jedna) - 1;
        l->k[l->n - 1] = jedna;
        return 0;
    }
    if (z == K1_ZNAK_NIGDZIE)
        k = '_';
    else if (l->n == 0 && z == K1_ZNAK_NA_BRZEGU)
        return 0; /* no space at the start */
    else if (l->n == 0 && z == K1_ZNAK_NA_POCZATKU)
        k = '_';
    if (l->n == K1_DOK_TEKST_MAKS || l->bajtow + bajty(k) > l->maks)
        return -1;
    l->k[l->n++] = k;
    l->bajtow += bajty(k);
    return 0;
}

size_t k1_dok_tekst_nazwy(const char *s, size_t n, char *out, size_t maks)
{
    struct litery l = { { 0 }, 0, 0, maks > 0 ? maks - 1 : 0 };
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0, o = 0;
    if (maks == 0)
        return 0;
    while (i < n) {
        uint32_t k;
        size_t d = k1_dok_utf8(u + i, n - i, &k);
        if (d == 0)
            k = '_', d = 1; /* a bad byte */
        if (dodaj(&l, k) != 0)
            break;
        i += d;
    }
    while (l.n > 0 && (l.k[l.n - 1] == '.' || k1_dok_znak(l.k[l.n - 1]) == K1_ZNAK_NA_BRZEGU))
        l.n--; /* no space or dot at the end */
    for (size_t j = 0; j < l.n; j++)
        o += na_utf8(l.k[j], out + o);
    out[o] = '\0';
    return o;
}

void k1_dok_nazwa_popraw(const char *s, size_t n, char out[K1_DOK_NAZWA])
{
    k1_dok_tekst_nazwy(s, n, out, K1_DOK_NAZWA);
}

/* ---- comparing ---- */

/* Small letter for a capital: A-Z, Latin-1 (À-Þ) and Latin Extended-A (Ą, Ł, Ś, Č, Ő… — all Polish ones). */
static uint32_t mala(uint32_t k)
{
    if ((k >= 'A' && k <= 'Z') || (k >= 0xc0 && k <= 0xde && k != 0xd7))
        return k + 32;
    if ((k >= 0x100 && k <= 0x137 && k % 2 == 0 && k != 0x130) || (k >= 0x139 && k <= 0x148 && k % 2 == 1) ||
        (k >= 0x14a && k <= 0x177 && k % 2 == 0) || (k >= 0x179 && k <= 0x17e && k % 2 == 1))
        return k + 1;
    return k == 0x178 ? 0xff : k; /* Ÿ */
}

int k1_dok_nazwy_rowne(const char *a, const char *b)
{
    size_t na = strnlen(a, K1_DOK_NAZWA), nb = strnlen(b, K1_DOK_NAZWA), i = 0, j = 0;
    while (i < na && j < nb) {
        uint32_t ka, kb;
        size_t da = k1_dok_utf8((const unsigned char *)a + i, na - i, &ka);
        size_t db = k1_dok_utf8((const unsigned char *)b + j, nb - j, &kb);
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

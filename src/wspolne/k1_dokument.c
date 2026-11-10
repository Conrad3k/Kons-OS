/* K1 — files of the user: names, types, the entry layout (0.8.0, ADR 0029). See k1_dokument.h. */
#include "k1_dokument.h"

#include <string.h>

#include "k1_bajty.h"
#include "k1_uslugi.h"

/* One code point of UTF-8 at s[0..n): its length, 0 for anything invalid (overlong, surrogate, > U+10FFFF,
 * cut off). */
static size_t utf8(const unsigned char *s, size_t n, uint32_t *kod)
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

int k1_dok_nazwa_poprawna(const char *s, size_t n)
{
    const unsigned char *u = (const unsigned char *)s;
    size_t i = 0;
    if (s == NULL || n < 1 || n >= K1_DOK_NAZWA || u[0] == ' ' || u[n - 1] == ' ' || u[n - 1] == '.')
        return 0;
    while (i < n) {
        uint32_t k;
        size_t d = utf8(u + i, n - i, &k);
        if (d == 0 || k < 0x20 || (k >= 0x7f && k <= 0x9f) || k == 0x2028 || k == 0x2029 || k == 0xfeff ||
            (k >= 0x202a && k <= 0x202e) || (k >= 0x2066 && k <= 0x2069)) /* also no text-direction tricks */
            return 0;
        if (k < 0x80 && strchr("\\/:*?\"<>|", (int)k) != NULL)
            return 0;
        i += d;
    }
    return 1;
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
        size_t da = utf8((const unsigned char *)a + i, na - i, &ka), db = utf8((const unsigned char *)b + j, nb - j, &kb);
        if (da == 0 || db == 0)
            return na == nb && memcmp(a, b, na) == 0;
        if (mala(ka) != mala(kb))
            return 0;
        i += da;
        j += db;
    }
    return i == na && j == nb;
}

/* Text: UTF-8 without control characters other than tab, line breaks and form feed. A character cut by the
 * end of the sample (when there is more content) does not count against it. */
static int tekst(const uint8_t *p, size_t n, uint64_t calosc)
{
    size_t i = 0;
    while (i < n) {
        uint32_t k;
        size_t d = utf8(p + i, n - i, &k);
        if (d == 0)
            return calosc > n && n - i < 4 && (p[i] & 0xc0) == 0xc0;
        if ((k < 0x20 && k != '\t' && k != '\n' && k != '\r' && k != '\f') || k == 0x7f)
            return 0;
        i += d;
    }
    return 1;
}

int k1_dok_typ(const uint8_t *p, size_t n, uint64_t calosc)
{
    static const uint8_t png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (calosc == 0 || n == 0)
        return K1_TYP_TEKST; /* a new, empty text file (as "Nowy dokument tekstowy" in Windows) */
    if (n >= 8 && memcmp(p, png, 8) == 0)
        return K1_TYP_PNG;
    if (n >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff)
        return K1_TYP_JPEG;
    if (n >= 5 && memcmp(p, "%PDF-", 5) == 0)
        return K1_TYP_PDF;
    if (n >= 4 && memcmp(p, "PK\x03\x04", 4) == 0)
        return K1_TYP_ZIP;
    return tekst(p, n, calosc) ? K1_TYP_TEKST : K1_TYP_INNY;
}

void k1_dok_wpis_zapisz(const struct k1_dok_wpis *w, uint8_t b[K1_DOK_WPIS_BAJTY])
{
    memset(b, 0, K1_DOK_WPIS_BAJTY);
    k1_z32(b, w->id);
    k1_z32(b + 4, w->rodzic);
    b[8] = w->rodzaj;
    b[9] = w->typ;
    b[10] = w->pochodzenie;
    b[11] = w->tworca;
    k1_z32(b + 12, w->generacja);
    k1_z64(b + 16, w->rozmiar);
    k1_z64(b + 24, (uint64_t)w->utworzono);
    k1_z64(b + 32, (uint64_t)w->zmieniono);
    k1_z64(b + 40, (uint64_t)w->kosz);
    memcpy(b + 48, w->nazwa, strnlen(w->nazwa, K1_DOK_NAZWA - 1));
}

int k1_dok_wpis_czytaj(const uint8_t b[K1_DOK_WPIS_BAJTY], struct k1_dok_wpis *w)
{
    size_t n = strnlen((const char *)b + 48, K1_DOK_NAZWA), i;
    memset(w, 0, sizeof *w);
    w->id = k1_c32(b);
    w->rodzic = k1_c32(b + 4);
    w->rodzaj = b[8];
    w->typ = b[9];
    w->pochodzenie = b[10];
    w->tworca = b[11];
    w->generacja = k1_c32(b + 12);
    w->rozmiar = k1_c64(b + 16);
    w->utworzono = (int64_t)k1_c64(b + 24);
    w->zmieniono = (int64_t)k1_c64(b + 32);
    w->kosz = (int64_t)k1_c64(b + 40);
    for (i = n; i < K1_DOK_NAZWA; i++)
        if (b[48 + i] != 0)
            n = K1_DOK_NAZWA; /* bytes after the NUL: refused below */
    if (w->id < K1_DOK_PIERWSZY || w->id > K1_DOK_ID_MAKS || w->rodzic < 1 || w->rodzic > K1_DOK_ID_MAKS ||
        w->rodzic == w->id || (w->rodzic < K1_DOK_PIERWSZY && w->rodzic > K1_FOLDERY_MAKS) ||
        (w->rodzaj != K1_DOK_PLIK && w->rodzaj != K1_DOK_FOLDER) || w->pochodzenie < 1 || w->pochodzenie > K1_POCH_MAKS ||
        w->tworca < 1 || w->tworca > K1_PROFIL_MAKS || w->rozmiar > K1_DOK_MAKS_BAJTY || w->utworzono < 0 ||
        w->zmieniono < 0 || w->kosz < 0 || n >= K1_DOK_NAZWA || !k1_dok_nazwa_poprawna((const char *)b + 48, n) ||
        (w->rodzaj == K1_DOK_PLIK ? w->typ < 1 || w->typ > K1_TYPY_MAKS
                                  : w->typ != 0 || w->rozmiar != 0 || w->generacja != 0)) {
        memset(w, 0, sizeof *w);
        return -1;
    }
    memcpy(w->nazwa, b + 48, n);
    return 0;
}

size_t k1_dok_lista_zapisz(const struct k1_dok_wpis *w, int n, uint32_t w_koszu, uint8_t *buf)
{
    int i;
    memcpy(buf, "K1D1", 4);
    k1_z32(buf + 4, (uint32_t)n);
    k1_z32(buf + 8, w_koszu);
    k1_z32(buf + 12, 0);
    for (i = 0; i < n; i++)
        k1_dok_wpis_zapisz(&w[i], buf + K1_DOK_LISTA_NAGL + (size_t)i * K1_DOK_WPIS_BAJTY);
    return K1_DOK_LISTA_NAGL + (size_t)n * K1_DOK_WPIS_BAJTY;
}

int k1_dok_lista_czytaj(const uint8_t *buf, size_t dl, struct k1_dok_wpis *w, int maks, uint32_t *w_koszu)
{
    uint32_t n;
    int i, j;
    if (dl < K1_DOK_LISTA_NAGL || memcmp(buf, "K1D1", 4) != 0 || k1_c32(buf + 12) != 0)
        return -1;
    n = k1_c32(buf + 4);
    if (n > K1_DOK_MAKS || (int)n > maks || dl != K1_DOK_LISTA_NAGL + (size_t)n * K1_DOK_WPIS_BAJTY ||
        k1_c32(buf + 8) > K1_DOK_MAKS)
        return -1;
    for (i = 0; i < (int)n; i++) {
        if (k1_dok_wpis_czytaj(buf + K1_DOK_LISTA_NAGL + (size_t)i * K1_DOK_WPIS_BAJTY, &w[i]) != 0)
            return -1;
        for (j = 0; j < i; j++)
            if (w[j].id == w[i].id)
                return -1;
    }
    *w_koszu = k1_c32(buf + 8);
    return (int)n;
}

const char *k1_folder_nazwa(uint32_t nr)
{
#define X_NAZWA(n, nr_, nazwa, ...) case nr_: return nazwa;
    switch (nr) {
        K1_FOLDERY(X_NAZWA)
    default:
        return NULL;
    }
#undef X_NAZWA
}

const char *k1_typ_nazwa(int typ)
{
#define X_NAZWA(n, nr_, nazwa, ...) case nr_: return nazwa;
    switch (typ) {
        K1_TYPY(X_NAZWA)
    default:
        return "Plik";
    }
#undef X_NAZWA
}

const char *k1_poch_nazwa(int poch)
{
#define X_NAZWA(n, nr_, nazwa) case nr_: return nazwa;
    switch (poch) {
        K1_POCHODZENIA(X_NAZWA)
    default:
        return "";
    }
#undef X_NAZWA
}

#define X_PARA(apl_, typ_) { K1_APL_##apl_, K1_TYP_##typ_ },
static const struct {
    int apl, typ;
} otwieranie[] = { K1_OTWIERANIE(X_PARA) };
#undef X_PARA

int k1_dok_otwiera(int apl, int typ)
{
    if (typ < 1 || typ > K1_TYPY_MAKS)
        return 0;
    if (apl == K1_APL_PLIKI)
        return 1;
    for (size_t i = 0; i < sizeof otwieranie / sizeof otwieranie[0]; i++)
        if (otwieranie[i].apl == apl && otwieranie[i].typ == typ)
            return 1;
    return 0;
}

int k1_dok_domyslna(int typ)
{
    for (size_t i = 0; i < sizeof otwieranie / sizeof otwieranie[0]; i++)
        if (otwieranie[i].typ == typ)
            return otwieranie[i].apl;
    return K1_APL_PLIKI;
}

int k1_dok_aplikacje(int typ, int *apl)
{
    int n = 0;
    for (size_t i = 0; i < sizeof otwieranie / sizeof otwieranie[0]; i++)
        if (otwieranie[i].typ == typ)
            apl[n++] = otwieranie[i].apl;
    if (typ >= 1 && typ <= K1_TYPY_MAKS)
        apl[n++] = K1_APL_PLIKI;
    return n;
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

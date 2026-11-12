/* K1 czytnik — names from the stick into valid K1 names (0.8.0 step 3, ADR 0031): a short name 8.3 (with the
 * small-letter flags Windows writes) or a long name in UTF-16. The rules of names are K1's own (k1_dok_tekst_nazwy,
 * the same table skarbiec checks, 0.8.2 step 2): what a name cannot hold becomes "_" (control and invisible
 * characters, \ / : * ? " < > |, text-direction marks, broken UTF-16), a letter + a mark becomes one letter, no spaces
 * at the edges; a name over 127 bytes keeps its extension; a name with nothing before its extension ("    .jpg")
 * becomes "Plik.jpg" — not a hidden ".jpg". Part of czytnik (fat.h). */
#include <stdio.h>
#include <string.h>

#include "fat.h"

#define ZAMIENNIK '_'

static size_t utf8(uint32_t k, char *out)
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

/* Where a UTF-8 text may be cut at or before byte `n` (never inside a letter). */
static size_t granica(const char *t, size_t n)
{
    while (n > 0 && ((unsigned char)t[n] & 0xc0) == 0x80)
        n--;
    return n;
}

/* `surowa` (UTF-8 as on the stick, `ukryta`: it started with a dot) into a K1 name: K1's rules first, then at most
 * 127 bytes — a longer one is cut before its extension (up to K1_DOK_KONCOWKA bytes), as Windows shortens. */
static void na_nazwe(const char *surowa, size_t dl, int ukryta, char out[K1_DOK_NAZWA])
{
    static char t[K1_DOK_TEKST_MAKS];
    char nazwa[K1_DOK_NAZWA];
    size_t n = k1_dok_tekst_nazwy(surowa, dl, t, sizeof t), kropka = n, ext = 0, baza;
    for (size_t i = 0; i < n; i++)
        if (t[i] == '.')
            kropka = i;
    if (kropka < n && n - kropka <= K1_DOK_KONCOWKA)
        ext = n - kropka;
    baza = n - ext;
    if (n >= K1_DOK_NAZWA) /* too long: the start of the name, then the extension */
        baza = granica(t, K1_DOK_NAZWA - 1 - ext);
    while (baza > 0 && (t[baza - 1] == ' ' || (ext == 0 && t[baza - 1] == '.')))
        baza--;
    if (baza == 0 && !ukryta) /* nothing before the extension (L8): not a hidden ".jpg" */
        snprintf(nazwa, sizeof nazwa, "Plik%.*s", (int)ext, t + n - ext);
    else
        snprintf(nazwa, sizeof nazwa, "%.*s%.*s", (int)baza, t, (int)ext, t + n - ext);
    k1_dok_nazwa_popraw(nazwa, strlen(nazwa), out);
    if (out[0] == '\0')
        memcpy(out, "Plik", sizeof "Plik");
}

void fat_nazwa_krotka(const uint8_t n[11], uint8_t flagi, char out[K1_DOK_NAZWA])
{
    char t[16];
    size_t d = 0;
    int b, e;
    for (b = 8; b > 0 && n[b - 1] == ' '; b--)
        ;
    for (e = 11; e > 8 && n[e - 1] == ' '; e--)
        ;
    for (int i = 0; i < e; i++) {
        unsigned c = i == 0 && n[0] == 0x05 ? 0xe5 : n[i]; /* 0x05 stands for 0xE5 in the first byte */
        if (i >= b && i < 8)
            continue;
        if (i == 8)
            t[d++] = '.';
        if (c >= 'A' && c <= 'Z' && (flagi & (i < 8 ? 0x08 : 0x10)))
            c += 32; /* NT: small letters in the base (0x08) / the extension (0x10) */
        t[d++] = c >= 0x80 ? ZAMIENNIK : (char)c; /* a code page we do not know: "_" */
    }
    na_nazwe(t, d, 0, out);
}

void fat_nazwa_dluga(const uint16_t *z, int n, char out[K1_DOK_NAZWA])
{
    char t[20 * 13 * 3 + 4];
    size_t d = 0;
    for (int i = 0; i < n && z[i] != 0 && z[i] != 0xffff && d + 4 < sizeof t; i++) { /* 0, then 0xFFFF: the end */
        uint32_t k = z[i];
        if (k >= 0xd800 && k <= 0xdbff && i + 1 < n && z[i + 1] >= 0xdc00 && z[i + 1] <= 0xdfff) {
            k = 0x10000 + ((k - 0xd800) << 10) + (uint32_t)(z[i + 1] - 0xdc00);
            i++;
        }
        d += k >= 0xd800 && k <= 0xdfff ? utf8(ZAMIENNIK, t + d) : utf8(k, t + d); /* half of a pair: "_" */
    }
    na_nazwe(t, d, n > 0 && z[0] == '.', out);
}

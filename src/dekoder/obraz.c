/* K1 dekoder — what the file is, how the camera held it (EXIF) and the picture at the size asked for (0.8.0 step 4,
 * ADR 0032). Decoding itself: wuffs.c. Everything read from the file is checked before use: the EXIF reader looks only
 * at the markers before the image data and at the first list of tags, every offset within the bytes it has. */
#include "obraz.h"

#include <stdlib.h>
#include <string.h>

#include "k1_dekoder.h"
#include "k1_skaluj.h"

int obraz_rodzaj(const uint8_t *d, size_t n)
{
    static const uint8_t png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (n >= sizeof png && memcmp(d, png, sizeof png) == 0)
        return OBRAZ_PNG;
    if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff)
        return OBRAZ_JPEG;
    return 0;
}

/* ---- EXIF orientation ---- */

static uint32_t u16(const uint8_t *p, int le)
{
    return le ? (uint32_t)p[0] | (uint32_t)p[1] << 8 : (uint32_t)p[0] << 8 | p[1];
}

static uint32_t u32(const uint8_t *p, int le)
{
    return le ? u16(p, 1) | u16(p + 2, 1) << 16 : u16(p, 0) << 16 | u16(p + 2, 0);
}

/* The TIFF block of an APP1 "Exif" segment (n bytes): tag 0x0112 (one SHORT) in IFD0. */
static int exif(const uint8_t *t, size_t n)
{
    int le;
    uint32_t ifd, ile;
    if (n < 8 || !((t[0] == 'I' && t[1] == 'I') || (t[0] == 'M' && t[1] == 'M')))
        return 1;
    le = t[0] == 'I';
    ifd = u32(t + 4, le);
    if (u16(t + 2, le) != 42 || ifd < 8 || ifd > n - 2)
        return 1;
    ile = u16(t + ifd, le);
    for (uint32_t k = 0; k < ile; k++) {
        size_t e = (size_t)ifd + 2 + (size_t)k * 12;
        uint32_t v;
        if (e + 12 > n)
            return 1;
        if (u16(t + e, le) != 0x0112)
            continue;
        v = u16(t + e + 8, le);
        return u16(t + e + 2, le) == 3 && u32(t + e + 4, le) == 1 && v >= 1 && v <= 8 ? (int)v : 1;
    }
    return 1;
}

int obraz_orientacja(const uint8_t *d, size_t n)
{
    size_t i = 2;
    if (obraz_rodzaj(d, n) != OBRAZ_JPEG)
        return 1;
    while (i + 4 <= n) {
        uint8_t m = d[i + 1];
        size_t dl;
        if (d[i] != 0xff)
            return 1;
        if (m == 0xff) { /* fill byte */
            i++;
            continue;
        }
        if (m == 0xda || m == 0xd9) /* the image data or the end: no EXIF before it */
            return 1;
        dl = (size_t)d[i + 2] << 8 | d[i + 3];
        if (dl < 2 || dl > n - i - 2)
            return 1;
        if (m == 0xe1 && dl >= 8 && memcmp(d + i + 4, "Exif\0\0", 6) == 0)
            return exif(d + i + 10, dl - 8);
        i += 2 + dl;
    }
    return 1;
}

/* ---- the size asked for ---- */

/* Where target pixel (x, y) of the turned picture comes from in the stored one (w × h): EXIF orientations 1..8. */
static size_t skad(int o, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    switch (o) {
    case 2: return (size_t)y * w + (w - 1 - x);           /* mirrored */
    case 3: return (size_t)(h - 1 - y) * w + (w - 1 - x); /* upside down */
    case 4: return (size_t)(h - 1 - y) * w + x;           /* mirrored upside down */
    case 5: return (size_t)x * w + y;                     /* the sides swapped (5..8) */
    case 6: return (size_t)(h - 1 - x) * w + y;           /* turned right */
    case 7: return (size_t)(h - 1 - x) * w + (w - 1 - y);
    case 8: return (size_t)x * w + (w - 1 - y);           /* turned left */
    default: return (size_t)y * w + x;
    }
}

int obraz_przygotuj(const uint8_t *dane, size_t n, uint32_t mw, uint32_t mh, struct obraz *wynik)
{
    struct obraz o, m;
    int orient = obraz_orientacja(dane, n), r = obraz_dekoduj(dane, n, &o), boki = orient >= 5;
    uint32_t tw, th; /* the target, turned as it will be seen */
    memset(wynik, 0, sizeof *wynik);
    if (r != 0)
        return r;
    k1_skaluj_zmiesc(boki ? o.h : o.w, boki ? o.w : o.h, mw, mh, &tw, &th);
    m.w = boki ? th : tw; /* the shrunk picture, still as stored */
    m.h = boki ? tw : th;
    m.px = malloc((size_t)m.w * m.h * 4);
    wynik->px = malloc((size_t)tw * th * 4);
    if (m.px == NULL || wynik->px == NULL ||
        k1_skaluj(o.px, o.w, o.h, o.w, (struct k1_wycinek){ 0, 0, o.w, o.h }, m.px, m.w, m.h, m.w) != 0) {
        free(o.px);
        free(m.px);
        free(wynik->px);
        wynik->px = NULL;
        return K1_DEK_BLAD_PAMIEC;
    }
    free(o.px);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++)
            wynik->px[(size_t)y * tw + x] = m.px[skad(orient, x, y, m.w, m.h)];
    free(m.px);
    wynik->w = tw;
    wynik->h = th;
    return 0;
}

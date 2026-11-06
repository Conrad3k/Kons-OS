/* K1 — bitmap fonts for the native GUI (ADR 0009).
 *
 * Glyphs are rasterised at BUILD time on the host (narzedzia/czcionki.c, FreeType, IBM Plex Sans
 * and Serif from Ubuntu's fonts-ibm-plex, OFL-1.1) into 8-bit coverage bitmaps compiled into the
 * programs. Nothing parses font files at run time: the GUI processes only index fixed tables. */
#ifndef K1_CZCIONKA_H
#define K1_CZCIONKA_H

#include <stdint.h>

struct k1_glif {
    uint32_t kod;         /* Unicode code point */
    int16_t lewo, gora;   /* bitmap offset from the pen position: x, and y above the baseline */
    uint16_t szer, wys;   /* bitmap size */
    uint16_t krok;        /* advance in 1/64 px */
    uint32_t dane;        /* offset into k1_czcionka.piksele */
};

struct k1_czcionka {
    const struct k1_glif *glify; /* sorted by kod */
    uint16_t n;
    int16_t wznos, opad, linia;  /* ascender (px, > 0), descender (px, < 0), line height (px) */
    const uint8_t *piksele;
};

enum k1_cz {
    K1_CZ_MALA = 0,        /* Plex Sans 13 */
    K1_CZ_MALA_MOCNA = 1,  /* Plex Sans Medium 13 */
    K1_CZ_TEKST = 2,       /* Plex Sans 15 */
    K1_CZ_TEKST_MOCNA = 3, /* Plex Sans Medium 15 */
    K1_CZ_TYTUL = 4,       /* Plex Serif 24 */
    K1_CZ_ZEGAR = 5,       /* Plex Serif Light 64, digits and ':' only */
    K1_CZ_LICZBA = 6
};

extern const struct k1_czcionka k1_czcionki[K1_CZ_LICZBA];

/* The glyph for `kod`, or NULL. Binary search, no allocation. */
const struct k1_glif *k1_glif(const struct k1_czcionka *c, uint32_t kod);

#endif

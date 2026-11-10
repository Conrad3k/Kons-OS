/* K1 dekoder — one picture: PNG or JPEG file → pixels (0.8.0 step 4, ADR 0032). Pure functions on memory, also
 * built on the host: test_dekoder.c (pictures made by Pillow and damaged ones) and fuzz_dekoder.c. */
#ifndef K1_DEKODER_OBRAZ_H
#define K1_DEKODER_OBRAZ_H

#include <stddef.h>
#include <stdint.h>

struct obraz {
    uint32_t *px; /* premultiplied 0xAARRGGBB, w per row; free() it */
    uint32_t w, h;
};

enum { OBRAZ_PNG = 1, OBRAZ_JPEG = 2 };

/* What the file is by its first bytes: OBRAZ_PNG, OBRAZ_JPEG or 0. */
int obraz_rodzaj(const uint8_t *dane, size_t n);

/* EXIF orientation of a JPEG, 1..8 (1: as stored; also when there is none or it cannot be read). */
int obraz_orientacja(const uint8_t *dane, size_t n);

/* Decodes the whole file into *o at its own size: 0 or K1_DEK_BLAD_* (then *o is empty). */
int obraz_dekoduj(const uint8_t *dane, size_t n, struct obraz *o);

/* The whole job of dekoder: decode, turn as EXIF says and shrink (never enlarge) to fit in mw × mh with the
 * photo's proportions. 0 or K1_DEK_BLAD_*. */
int obraz_przygotuj(const uint8_t *dane, size_t n, uint32_t mw, uint32_t mh, struct obraz *wynik);

#endif

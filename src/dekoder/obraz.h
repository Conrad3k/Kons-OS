/* K1 dekoder — one picture: a PNG, JPEG, GIF, BMP or WebP file → pixels (0.8.0 step 4, ADR 0032; 0.8.2 step 3). Pure functions on memory, also
 * built on the host: test_dekoder.c (pictures made by Pillow and damaged ones) and fuzz_dekoder.c. */
#ifndef K1_DEKODER_OBRAZ_H
#define K1_DEKODER_OBRAZ_H

#include <stddef.h>
#include <stdint.h>

struct obraz {
    uint32_t *px; /* premultiplied 0xAARRGGBB, w per row; free() it */
    uint32_t w, h;
};

/* What the file is by its first bytes (k1_dok_typ): a type of family "obrazy" dekoder reads (K1_TYP_*), or 0. */
int obraz_rodzaj(const uint8_t *dane, size_t n);

/* 1 when dekoder has a decoder for file type `typ` (wuffs.c). */
int obraz_czytelny(int typ);

/* EXIF orientation of a JPEG, 1..8 (1: as stored; also when there is none or it cannot be read). */
int obraz_orientacja(const uint8_t *dane, size_t n);

/* Decodes the whole file into *o at its own size: 0 or K1_DEK_BLAD_* (then o->px is NULL; after
 * K1_DEK_BLAD_DUZY and K1_DEK_BLAD_PAMIEC o->w × o->h is the size in the file). GIF: the first frame. */
int obraz_dekoduj(const uint8_t *dane, size_t n, struct obraz *o);

/* The whole job of dekoder: decode, turn as EXIF says and shrink (never enlarge) to fit in mw × mh with the
 * photo's proportions. 0 or K1_DEK_BLAD_* (then wynik->w × wynik->h as after obraz_dekoduj). */
int obraz_przygotuj(const uint8_t *dane, size_t n, uint32_t mw, uint32_t mh, struct obraz *wynik);

#endif

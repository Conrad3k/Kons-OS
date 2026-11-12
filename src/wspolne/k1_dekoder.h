/* K1 — the picture decoder (0.8.0 step 4, ADR 0032): limits and results shared by dekoder, nadzorca, Pliki and ekran.
 *
 * dekoder turns one picture file (PNG, JPEG, GIF — its first frame, BMP, WebP; the types of family "obrazy" in
 * K1_TYPY) into pixels and ends (a new process for every picture). The file comes in pieces of K1_DOK_KAWALEK, as
 * skarbiec gives them (0.8.2 step 3). It gives at most K1_DEK_MAKS_W × K1_DEK_MAKS_H pixels (the wallpaper), shrunk
 * with the photo's proportions and turned as the camera says (EXIF orientation), as 32-bit premultiplied 0xAARRGGBB,
 * row after row, in a sealed memfd (K1_KOM_DEK_PIKSELE). */
#ifndef K1_DEKODER_H
#define K1_DEKODER_H

#define K1_DEK_MAKS_W 2560 /* the largest picture dekoder gives (the wallpaper photo) */
#define K1_DEK_MAKS_H 1600

/* The limits of dekoder — one table (0.8.2 step 3; measured: docs/komponenty/dekoder.md). Memory, not safety, sets
 * them: the file, its pixels (4 bytes each) and the decoder's work area (a progressive JPEG keeps all its
 * coefficients) must fit in K1_DEK_PAMIEC together, or dekoder refuses before taking any of it. nadzorca makes the
 * sandbox (profile.c) from the same numbers and lowers them on a computer with little memory (dekoder.c). */
#define K1_DEK_MAKS_BAJTY (128u << 20)      /* the file (a photo of 100 Mpx is 20–60 MB) */
#define K1_DEK_MAKS_MPX 100                 /* a photo larger than this (in the file) is not decoded */
#define K1_DEK_MAKS_PIKSELI (K1_DEK_MAKS_MPX * 1000000u)
#define K1_DEK_PAMIEC (1024u << 20)         /* the file + pixels + work area (100 Mpx progressive: 0.85 GiB) */
#define K1_DEK_ZAPAS (128u << 20)           /* code, stacks, the decoder itself and the picture it gives */
#define K1_DEK_PAMIEC_RAM 6                 /* never more than 1/6 of the computer's memory (8 GB: the full limit) … */
#define K1_DEK_PAMIEC_MIN (384u << 20)      /* … but never less than this (the limit of 0.8.0–0.8.1: 24 Mpx) */

/* Results of dekoder (K1_KOM_DEK_BLAD a) */
#define K1_DEK_BLAD_FORMAT (-40)     /* not a picture K1 reads */
#define K1_DEK_BLAD_DUZY (-41)       /* more than K1_DEK_MAKS_MPX (b = the photo's Mpx, rounded up) */
#define K1_DEK_BLAD_USZKODZONY (-42) /* the file is damaged or cut off */
#define K1_DEK_BLAD_PAMIEC (-43)     /* it does not fit in the memory dekoder has */
#define K1_DEK_BLAD_MIN (-43)
#define K1_DEK_MPX_MAKS 100000       /* the largest b of K1_DEK_BLAD_DUZY (more is said as this) */

#endif

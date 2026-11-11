/* K1 — the picture decoder (0.8.0 step 4, ADR 0032): limits and results shared by dekoder, Pliki and ekran.
 *
 * dekoder turns one PNG or JPEG file into pixels and ends (a new process for every picture). It gives at most
 * K1_DEK_MAKS_W × K1_DEK_MAKS_H pixels (the wallpaper), shrunk with the photo's proportions and turned as the camera
 * says (EXIF orientation), as 32-bit premultiplied 0xAARRGGBB, row after row, in a sealed memfd (K1_KOM_DEK_PIKSELE). */
#ifndef K1_DEKODER_H
#define K1_DEKODER_H

#define K1_DEK_MAKS_W 2560             /* the largest picture dekoder gives (the wallpaper photo) */
#define K1_DEK_MAKS_H 1600
#define K1_DEK_MAKS_BAJTY (16u << 20)  /* the file dekoder takes (whole, in one memfd; Pliki gather it piece by piece) */
#define K1_DEK_MAKS_PIKSELI (24u << 20) /* a photo larger than this (in the file) is not decoded: 6000 × 4000 fits */

/* Results of dekoder (K1_KOM_DEK_BLAD a) */
#define K1_DEK_BLAD_FORMAT (-40)     /* neither PNG nor JPEG */
#define K1_DEK_BLAD_DUZY (-41)       /* more than K1_DEK_MAKS_PIKSELI pixels */
#define K1_DEK_BLAD_USZKODZONY (-42) /* the file is damaged or cut off */
#define K1_DEK_BLAD_PAMIEC (-43)
#define K1_DEK_BLAD_MIN (-43)

#endif

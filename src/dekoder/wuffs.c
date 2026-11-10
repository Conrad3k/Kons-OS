/* K1 dekoder — decoding with Wuffs (0.8.0 step 4, ADR 0032; docs/ZALEZNOSCI.md).
 *
 * Wuffs is a library of decoders written in a language that proves at compile time that no read or write goes out
 * of bounds and no integer overflows; the C file is generated from it (src/zewnetrzne/wuffs/, unchanged). Only the
 * modules for PNG (with zlib/deflate and checksums) and JPEG are built, all of it static in this one file. Wuffs
 * allocates nothing itself except the decoder: the pixels and its work area come from us, with sizes checked
 * against K1_DEK_MAKS_PIKSELI first. */
#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__STATIC_FUNCTIONS
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__JPEG
#include "wuffs-v0.4.c"

#include <stdlib.h>
#include <string.h>

#include "k1_dekoder.h"
#include "obraz.h"

#define PRACA_MAKS (512u << 20) /* the decoder's work area: never more (progressive JPEG of the largest photo) */

static int stan_dobry(wuffs_base__status s)
{
    return wuffs_base__status__is_ok(&s);
}

/* The decoder of one kind, called directly (not through Wuffs' table of functions: those calls go through a
 * pointer of another type, which UBSan rightly reports). */
struct dekoder {
    int rodzaj;
    wuffs_png__decoder *png;
    wuffs_jpeg__decoder *jpeg;
};

static wuffs_base__status konfiguracja(struct dekoder *d, wuffs_base__image_config *ic, wuffs_base__io_buffer *zr)
{
    return d->rodzaj == OBRAZ_PNG ? wuffs_png__decoder__decode_image_config(d->png, ic, zr)
                                  : wuffs_jpeg__decoder__decode_image_config(d->jpeg, ic, zr);
}

static uint64_t praca_dlugosc(struct dekoder *d)
{
    return d->rodzaj == OBRAZ_PNG ? wuffs_png__decoder__workbuf_len(d->png).max_incl
                                  : wuffs_jpeg__decoder__workbuf_len(d->jpeg).max_incl;
}

static wuffs_base__status ramka(struct dekoder *d, wuffs_base__pixel_buffer *pb, wuffs_base__io_buffer *zr,
                                wuffs_base__slice_u8 praca)
{
    return d->rodzaj == OBRAZ_PNG
               ? wuffs_png__decoder__decode_frame(d->png, pb, zr, WUFFS_BASE__PIXEL_BLEND__SRC, praca, NULL)
               : wuffs_jpeg__decoder__decode_frame(d->jpeg, pb, zr, WUFFS_BASE__PIXEL_BLEND__SRC, praca, NULL);
}

/* Decodes the frame into a new buffer of w × h pixels (transparent black where the file gives nothing). */
static int klatka(struct dekoder *dek, wuffs_base__image_config *ic, wuffs_base__io_buffer *zr, struct obraz *o)
{
    uint32_t w = wuffs_base__pixel_config__width(&ic->pixcfg), h = wuffs_base__pixel_config__height(&ic->pixcfg);
    uint64_t praca_dl;
    uint8_t *praca;
    wuffs_base__pixel_buffer pb;
    int wynik = K1_DEK_BLAD_USZKODZONY;
    if (w == 0 || h == 0)
        return K1_DEK_BLAD_USZKODZONY;
    if ((uint64_t)w * h > K1_DEK_MAKS_PIKSELI)
        return K1_DEK_BLAD_DUZY;
    wuffs_base__pixel_config__set(&ic->pixcfg, WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE,
                                  w, h);
    praca_dl = praca_dlugosc(dek);
    if (praca_dl > PRACA_MAKS)
        return K1_DEK_BLAD_DUZY;
    praca = malloc(praca_dl > 0 ? (size_t)praca_dl : 1);
    o->px = calloc((size_t)w * h, 4);
    if (praca == NULL || o->px == NULL) {
        free(praca);
        free(o->px);
        o->px = NULL;
        return K1_DEK_BLAD_PAMIEC;
    }
    o->w = w;
    o->h = h;
    if (stan_dobry(wuffs_base__pixel_buffer__set_from_slice(&pb, &ic->pixcfg,
                                                            wuffs_base__make_slice_u8((uint8_t *)o->px, (size_t)w * h * 4))) &&
        stan_dobry(ramka(dek, &pb, zr, wuffs_base__make_slice_u8(praca, (size_t)praca_dl))))
        wynik = 0;
    free(praca);
    if (wynik != 0) {
        free(o->px);
        memset(o, 0, sizeof *o);
    }
    return wynik;
}

int obraz_dekoduj(const uint8_t *dane, size_t n, struct obraz *o)
{
    struct dekoder dek = { obraz_rodzaj(dane, n), NULL, NULL };
    wuffs_base__image_config ic;
    /* Wuffs only reads from the buffer; its type is not const */
    wuffs_base__io_buffer zr = wuffs_base__ptr_u8__reader((uint8_t *)(uintptr_t)dane, n, true);
    int wynik = K1_DEK_BLAD_USZKODZONY;
    memset(o, 0, sizeof *o);
    if (dek.rodzaj == OBRAZ_PNG)
        dek.png = wuffs_png__decoder__alloc();
    else if (dek.rodzaj == OBRAZ_JPEG)
        dek.jpeg = wuffs_jpeg__decoder__alloc();
    else
        return K1_DEK_BLAD_FORMAT;
    if (dek.png == NULL && dek.jpeg == NULL)
        return K1_DEK_BLAD_PAMIEC;
    memset(&ic, 0, sizeof ic);
    if (stan_dobry(konfiguracja(&dek, &ic, &zr)))
        wynik = klatka(&dek, &ic, &zr, o);
    free(dek.png);
    free(dek.jpeg);
    return wynik;
}

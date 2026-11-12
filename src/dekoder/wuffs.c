/* K1 dekoder — decoding with Wuffs (0.8.0 step 4, ADR 0032; docs/ZALEZNOSCI.md).
 *
 * Wuffs is a library of decoders written in a language that proves at compile time that no read or write goes out
 * of bounds and no integer overflows; the C file is generated from it (src/zewnetrzne/wuffs/, unchanged). Only the
 * modules of the formats in the table below are built (with what they need: zlib/deflate and checksums for PNG, LZW
 * for GIF, VP8 for lossy WebP), all of it static in this one file. Wuffs allocates nothing itself except the
 * decoder: the pixels and its work area come from us, after the check that they fit (k1_dekoder.h). */
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
#define WUFFS_CONFIG__MODULE__LZW
#define WUFFS_CONFIG__MODULE__GIF
#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__VP8
#define WUFFS_CONFIG__MODULE__WEBP
#include "wuffs-v0.4.c"

#include <stdlib.h>
#include <string.h>

#include "k1_dekoder.h"
#include "k1_dokument.h"
#include "obraz.h"

/* The decoders: X(file type K1_TYP_*, Wuffs module). A new format: one line here, one in K1_TYPY (family "obrazy")
 * and its test in k1_dok_typ — and its WUFFS_CONFIG__MODULE__ above. */
#define K1_DEKODERY(X) X(PNG, png) X(JPEG, jpeg) X(GIF, gif) X(BMP, bmp) X(WEBP, webp)

/* Each decoder called through functions of one type (not through Wuffs' own table of functions: those calls go
 * through a pointer of another type, which UBSan rightly reports). */
#define X_FUNKCJE(T, f)                                                                                               \
    static void *f##_nowy(void) { return wuffs_##f##__decoder__alloc(); }                                            \
    static wuffs_base__status f##_konfiguracja(void *d, wuffs_base__image_config *ic, wuffs_base__io_buffer *zr)      \
    {                                                                                                                 \
        return wuffs_##f##__decoder__decode_image_config(d, ic, zr);                                                  \
    }                                                                                                                 \
    static uint64_t f##_praca(void *d) { return wuffs_##f##__decoder__workbuf_len(d).max_incl; }                     \
    static wuffs_base__status f##_ramka(void *d, wuffs_base__pixel_buffer *pb, wuffs_base__io_buffer *zr,            \
                                        wuffs_base__slice_u8 praca)                                                   \
    {                                                                                                                 \
        return wuffs_##f##__decoder__decode_frame(d, pb, zr, WUFFS_BASE__PIXEL_BLEND__SRC, praca, NULL);              \
    }
K1_DEKODERY(X_FUNKCJE)
#undef X_FUNKCJE

struct dekoder {
    int typ;
    void *(*nowy)(void);
    wuffs_base__status (*konfiguracja)(void *d, wuffs_base__image_config *ic, wuffs_base__io_buffer *zr);
    uint64_t (*praca)(void *d);
    wuffs_base__status (*ramka)(void *d, wuffs_base__pixel_buffer *pb, wuffs_base__io_buffer *zr,
                                wuffs_base__slice_u8 praca);
};

#define X_WIERSZ(T, f) { K1_TYP_##T, f##_nowy, f##_konfiguracja, f##_praca, f##_ramka },
static const struct dekoder dekodery[] = { K1_DEKODERY(X_WIERSZ) };
#undef X_WIERSZ

static const struct dekoder *dekoder_typu(int typ)
{
    for (size_t i = 0; i < sizeof dekodery / sizeof dekodery[0]; i++)
        if (dekodery[i].typ == typ)
            return &dekodery[i];
    return NULL;
}

int obraz_czytelny(int typ)
{
    return dekoder_typu(typ) != NULL;
}

static int stan_dobry(wuffs_base__status s)
{
    return wuffs_base__status__is_ok(&s);
}

/* Whether the file (n bytes), its pixels and the work area fit in what dekoder has: 0 or K1_DEK_BLAD_*. */
static int miesci_sie(uint64_t n, uint32_t w, uint32_t h, uint64_t praca)
{
    if ((uint64_t)w * h > K1_DEK_MAKS_PIKSELI)
        return K1_DEK_BLAD_DUZY;
    return n + (uint64_t)w * h * 4 + praca > K1_DEK_PAMIEC ? K1_DEK_BLAD_PAMIEC : 0;
}

/* Decodes the frame into a new buffer of w × h pixels (transparent black where the file gives nothing). */
static int klatka(const struct dekoder *dek, void *d, wuffs_base__image_config *ic, wuffs_base__io_buffer *zr,
                  struct obraz *o)
{
    uint32_t w = wuffs_base__pixel_config__width(&ic->pixcfg), h = wuffs_base__pixel_config__height(&ic->pixcfg);
    uint64_t praca_dl = dek->praca(d);
    uint8_t *praca;
    wuffs_base__pixel_buffer pb;
    int wynik;
    if (w == 0 || h == 0)
        return K1_DEK_BLAD_USZKODZONY;
    o->w = w; /* also when it is too large: dekoder says how large */
    o->h = h;
    wynik = miesci_sie(zr->meta.wi, w, h, praca_dl);
    if (wynik != 0)
        return wynik;
    wuffs_base__pixel_config__set(&ic->pixcfg, WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL, WUFFS_BASE__PIXEL_SUBSAMPLING__NONE,
                                  w, h);
    praca = malloc(praca_dl > 0 ? (size_t)praca_dl : 1);
    o->px = calloc((size_t)w * h, 4);
    if (praca == NULL || o->px == NULL) {
        free(praca);
        free(o->px);
        o->px = NULL;
        return K1_DEK_BLAD_PAMIEC;
    }
    wynik = stan_dobry(wuffs_base__pixel_buffer__set_from_slice(&pb, &ic->pixcfg,
                                                               wuffs_base__make_slice_u8((uint8_t *)o->px, (size_t)w * h * 4))) &&
                    stan_dobry(dek->ramka(d, &pb, zr, wuffs_base__make_slice_u8(praca, (size_t)praca_dl)))
                ? 0
                : K1_DEK_BLAD_USZKODZONY;
    free(praca);
    if (wynik != 0) {
        free(o->px);
        o->px = NULL;
    }
    return wynik;
}

int obraz_dekoduj(const uint8_t *dane, size_t n, struct obraz *o)
{
    const struct dekoder *dek = dekoder_typu(obraz_rodzaj(dane, n));
    wuffs_base__image_config ic;
    /* Wuffs only reads from the buffer; its type is not const */
    wuffs_base__io_buffer zr = wuffs_base__ptr_u8__reader((uint8_t *)(uintptr_t)dane, n, true);
    int wynik = K1_DEK_BLAD_USZKODZONY;
    void *d;
    memset(o, 0, sizeof *o);
    if (dek == NULL)
        return K1_DEK_BLAD_FORMAT;
    d = dek->nowy();
    if (d == NULL)
        return K1_DEK_BLAD_PAMIEC;
    memset(&ic, 0, sizeof ic);
    if (stan_dobry(dek->konfiguracja(d, &ic, &zr)))
        wynik = klatka(dek, d, &ic, &zr, o);
    free(d);
    return wynik;
}

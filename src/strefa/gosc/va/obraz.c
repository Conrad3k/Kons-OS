/* K1 — the VA driver of the zone: EndPicture (0.13.2 step 4, ADR 0063 pkt 1, 3). The buffers VA rendered since
 * BeginPicture become one request (h264.c) straight in the film context's shared memory, then K1_KF_KLATKA into the
 * target's surface number (klatki.c) and K1_KF_POBIERZ into the target's buffer of the card. A slice parameter buffer
 * goes with the slice data buffer of the same place among them (as in every VA driver: FFmpeg renders them in pairs). */
#include <string.h>

#include "sterownik.h"

struct zebrane {
    const VAPictureParameterBufferH264 *obraz;
    const VAIQMatrixBufferH264 *macierz;
    VASliceParameterBufferH264 plastry[K1_FILMY_PLASTROW_MAKS];
    uint32_t plaster_dane[K1_FILMY_PLASTROW_MAKS], n_plastrow;
    struct va_dane dane[K1_FILMY_PLASTROW_MAKS * 2];
    uint32_t n_danych, n_param;
};
static struct zebrane z; /* under the display's lock */

static int jeden(const struct va_bufor *b, size_t rozmiar)
{
    return b->rozmiar >= rozmiar && b->ile >= 1;
}

/* One rendered buffer into `z`: 0, or -1 (a buffer of a kind or size H.264 does not have, too many slices) */
static int zbierz(const struct va_bufor *b)
{
    switch (b->typ) {
    case VAPictureParameterBufferType:
        if (!jeden(b, sizeof *z.obraz))
            return -1;
        z.obraz = (const VAPictureParameterBufferH264 *)(const void *)b->dane;
        return 0;
    case VAIQMatrixBufferType:
        if (!jeden(b, sizeof *z.macierz))
            return -1;
        z.macierz = (const VAIQMatrixBufferH264 *)(const void *)b->dane;
        return 0;
    case VASliceParameterBufferType:
        if (b->rozmiar != sizeof z.plastry[0] || b->ile > K1_FILMY_PLASTROW_MAKS - z.n_plastrow)
            return -1;
        memcpy(&z.plastry[z.n_plastrow], b->dane, (size_t)b->ile * sizeof z.plastry[0]);
        for (uint32_t i = 0; i < b->ile; i++)
            z.plaster_dane[z.n_plastrow++] = z.n_param;
        z.n_param++;
        return 0;
    case VASliceDataBufferType:
        if (z.n_danych == sizeof z.dane / sizeof z.dane[0] || (uint64_t)b->rozmiar * b->ile > K1_FILMY_DANE_MAKS)
            return -1;
        z.dane[z.n_danych++] = (struct va_dane){ b->dane, b->rozmiar * b->ile };
        return 0;
    default:
        return 0; /* nothing else changes an H.264 picture (FFmpeg sends none) */
    }
}

static int numer(void *ja, VASurfaceID s)
{
    return va_klatki_numer(ja, s);
}

/* Every surface the picture refers to: they keep their numbers (klatki.c) */
static size_t uzywane(VASurfaceID *u)
{
    size_t n = 0;
    for (int i = 0; i < 16; i++)
        u[n++] = z.obraz->ReferenceFrames[i].picture_id;
    for (uint32_t s = 0; s < z.n_plastrow; s++)
        for (int i = 0; i < 32; i++) {
            u[n++] = z.plastry[s].RefPicList0[i].picture_id;
            u[n++] = z.plastry[s].RefPicList1[i].picture_id;
        }
    return n;
}

VAStatus va_obraz_koniec(struct va_dane_sterownika *d, uint32_t film, struct va_kontekst *k)
{
    static VASurfaceID u[16 + K1_FILMY_PLASTROW_MAKS * 64];
    struct va_powierzchnia *p = va_pow(d, k->cel);
    struct va_obraz w;
    size_t dl;
    int cel, r;
    memset(&z, 0, sizeof z);
    for (uint32_t i = 0; i < k->n_buforow; i++) {
        const struct va_bufor *b = va_buf(d, k->bufory[i]);
        if (b == NULL || zbierz(b) != 0)
            return VA_STATUS_ERROR_INVALID_BUFFER;
    }
    if (p == NULL || z.obraz == NULL || z.n_plastrow == 0 || z.n_param != z.n_danych)
        return VA_STATUS_ERROR_INVALID_BUFFER;
    w = (struct va_obraz){ k->kodek, z.obraz, z.macierz, z.plastry, z.plaster_dane, z.n_plastrow, z.dane, z.n_danych };
    cel = va_klatki_cel(&k->klatki, k->cel, u, uzywane(u));
    if (cel < 0)
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    dl = va_h264_prosba(d->l.wsp + K1_KF_PROSBA, VA_WSPOLNA - K1_KF_PROSBA, &w, (uint32_t)cel, numer, &k->klatki);
    if (dl == 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    r = va_lacze_polecenie(&d->l, K1_KF_KLATKA, film, (uint32_t)cel, (uint32_t)dl, 0, 0);
    va_klatki_gotowa(&k->klatki, cel, r == 0);
    if (r == 0)
        r = va_lacze_polecenie(&d->l, K1_KF_POBIERZ, film, (uint32_t)cel, p->zasob, 0, 0);
    return va_status(r);
}

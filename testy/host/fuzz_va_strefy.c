/* libFuzzer harness: the zone's VA driver (src/strefa/gosc/va/; 0.13.2 step 4) — what FFmpeg (and through it a web
 * page's film) puts into VA's buffers, turned into one request of the film decoder (h264.c), and what K1 sends back (the
 * capset and the VMM's answer — sprawdz.c). The input: byte 0 — slices (1..4) and data buffers; then the picture
 * parameters, the slices' parameters, the data — as many bytes as there are, the rest zero. Nothing may be read beyond
 * a buffer or written beyond the request's place; a request the driver builds must have exactly the length it said
 * (va_h264_dlugosc), and its header and slices must lie where k1_filmy_czesci finds them. */
#include <stdlib.h>
#include <string.h>

#include "sterownik.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static VAPictureParameterBufferH264 pp;
static VASliceParameterBufferH264 pl[4];
static uint8_t prosba[1u << 16];

static int numer(void *ja, VASurfaceID s)
{
    (void)ja;
    return s % 23u < VA_KLATKI ? (int)(s % 23u) : -1; /* some surfaces decoded, some not */
}

static size_t wez(void *cel, size_t n, const uint8_t *data, size_t size, size_t *o)
{
    size_t k = size - *o < n ? size - *o : n;
    memset(cel, 0, n);
    memcpy(cel, data + *o, k);
    *o += k;
    return k;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct va_dane d[4];
    uint32_t zd[4];
    struct va_obraz w;
    struct k1_filmy_czesci c;
    size_t o = 1, n, dl;
    uint8_t *bufor;
    if (size < 1)
        return 0;
    if (size >= sizeof(struct k1_filmy_zbior)) { /* the capset as the VMM wrote it (any 16 bytes) */
        struct k1_filmy_zbior z;
        memcpy(&z, data, sizeof z);
        if (va_zbior_dobry(&z, sizeof z) && (z.maks_w > K1_FILMY_MAKS_W || z.kodeki == 0))
            abort();
    }
    n = (data[0] & 3u) + 1u;
    wez(&pp, sizeof pp, data, size, &o);
    for (size_t i = 0; i < n; i++)
        wez(&pl[i], sizeof pl[0], data, size, &o), zd[i] = (uint32_t)(i % ((data[0] >> 2 & 1u) + 1u));
    dl = size - o;
    bufor = malloc(dl ? dl : 1); /* exactly the data: ASan sees a read beyond it */
    memcpy(bufor, data + o, dl);
    d[0] = (struct va_dane){ bufor, (uint32_t)(dl / 2) };
    d[1] = (struct va_dane){ bufor + dl / 2, (uint32_t)(dl - dl / 2) };
    w = (struct va_obraz){ 1 + (data[0] >> 4) % K1_KODEKI_MAKS, &pp, (data[0] & 8u) ? NULL : (const void *)bufor,
                           pl, zd, (uint32_t)n, d, (data[0] >> 2 & 1u) + 1u };
    if (w.macierz != NULL && dl < sizeof(VAIQMatrixBufferH264))
        w.macierz = NULL;
    n = va_h264_prosba(prosba, sizeof prosba, &w, data[0] % VA_KLATKI, numer, NULL);
    if (n != 0) {
        if (n != va_h264_dlugosc(&w))
            abort();
        k1_filmy_czesci(prosba, &c);
        if (c.dane + c.n_danych != n || c.n_plastrow != w.n_plastrow)
            abort();
    }
    if (size >= sizeof(struct k1_kf_odpowiedz)) {
        int r = va_odpowiedz(data, data[0]);
        if (r != VA_BRAK_ODPOWIEDZI && (r > 0 || r < K1_KF_BLAD_DEKODER))
            abort();
    }
    free(bufor);
    return 0;
}

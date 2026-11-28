/* K1 — the VA driver of the zone: surfaces and buffers (0.13.2 step 4, ADR 0063 pkt 3). A surface is a buffer of the
 * card of the zone, linear NV12 in the decoder's layout (k1_filmy_bufor_*: rows of k1_filmy_rzad bytes, the colours from
 * the first whole page after the brightness) — exported to Firefox as one dma-buf with two layers (R8 + GR88, as it asks:
 * READ_ONLY | SEPARATE_LAYERS) or one NV12 layer. Buffers of VA are plain memory: the driver reads them at EndPicture.
 * The tables grow by doubling; an ID is a tag (VA_ID_*) + its place. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm/drm_fourcc.h>
#include <va/va_drmcommon.h>

#include "sterownik.h"

#define POW_MAKS 256u  /* surfaces of one display (FFmpeg's pool: ~20 per film, Firefox holds a few more) */
#define BUF_MAKS 4096u /* buffers alive at once (one picture: its parameters + 2 per slice) */

struct va_powierzchnia *va_pow(struct va_dane_sterownika *d, VASurfaceID id)
{
    uint32_t i = id - VA_ID_POW;
    return id >= VA_ID_POW && i < d->n_pow && d->pow[i].jest ? &d->pow[i] : NULL;
}

struct va_bufor *va_buf(struct va_dane_sterownika *d, VABufferID id)
{
    uint32_t i = id - VA_ID_BUF;
    return id >= VA_ID_BUF && i < d->n_buf && d->buf[i].jest ? &d->buf[i] : NULL;
}

/* A free place in a table of `n` elements of `rozmiar` bytes (grown up to `maks`): its index, or -1. */
static int miejsce(void **tab, uint32_t *n, size_t rozmiar, uint32_t maks, size_t jest_od)
{
    uint32_t stare = *n, nowe = stare ? stare * 2 : 32;
    uint8_t *t = *tab;
    int jest;
    for (uint32_t i = 0; i < stare; i++) {
        memcpy(&jest, t + i * rozmiar + jest_od, sizeof jest);
        if (!jest)
            return (int)i;
    }
    nowe = nowe > maks ? maks : nowe;
    if (stare >= maks || (t = realloc(*tab, (size_t)nowe * rozmiar)) == NULL)
        return -1;
    memset(t + (size_t)stare * rozmiar, 0, (size_t)(nowe - stare) * rozmiar);
    *tab = t, *n = nowe;
    return (int)stare;
}

static VAStatus nowe_powierzchnie(struct va_dane_sterownika *d, uint32_t w, uint32_t h, uint32_t ile, VASurfaceID *s)
{
    uint64_t dl = (k1_filmy_bufor_bajty(w, h) + 4095u) & ~(uint64_t)4095u;
    for (uint32_t k = 0; k < ile; k++) {
        void *tab = d->pow;
        int i = miejsce(&tab, &d->n_pow, sizeof *d->pow, POW_MAKS, offsetof(struct va_powierzchnia, jest));
        struct va_powierzchnia *p;
        d->pow = tab;
        if (i < 0)
            return VA_STATUS_ERROR_ALLOCATION_FAILED;
        p = &d->pow[i];
        *p = (struct va_powierzchnia){ 1, w, h, 0, 0, dl };
        p->uchwyt = va_lacze_bufor(&d->l, dl, &p->zasob);
        if (p->uchwyt == 0) {
            p->jest = 0;
            return VA_STATUS_ERROR_ALLOCATION_FAILED; /* the zone's card memory (K1_POSR_PAMIEC), or no broker */
        }
        s[k] = VA_ID_POW + (uint32_t)i;
    }
    return VA_STATUS_SUCCESS;
}

static VAStatus usun_powierzchnie(VADriverContextP ctx, VASurfaceID *s, int n);

static VAStatus k_create_surfaces2(VADriverContextP ctx, unsigned int format, unsigned int w, unsigned int h,
                                   VASurfaceID *s, unsigned int ile, VASurfaceAttrib *a, unsigned int n_a)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    VAStatus r;
    if (format != VA_RT_FORMAT_YUV420)
        return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
    if (w < K1_FILMY_MIN_WH || h < K1_FILMY_MIN_WH || w > d->l.zbior.maks_w || h > d->l.zbior.maks_h || ile < 1)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    for (unsigned int i = 0; i < n_a; i++) {
        if (a[i].type == VASurfaceAttribPixelFormat && a[i].value.value.i != (int)VA_FOURCC_NV12)
            return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
        if (a[i].type == VASurfaceAttribMemoryType && a[i].value.value.i != VA_SURFACE_ATTRIB_MEM_TYPE_VA)
            return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE; /* the buffers are K1's, never imported */
    }
    pthread_mutex_lock(&d->zamek);
    for (unsigned int i = 0; i < ile; i++)
        s[i] = VA_INVALID_SURFACE;
    r = nowe_powierzchnie(d, w, h, ile, s);
    pthread_mutex_unlock(&d->zamek);
    if (r != VA_STATUS_SUCCESS)
        usun_powierzchnie(ctx, s, (int)ile);
    return r;
}

static VAStatus k_create_surfaces(VADriverContextP ctx, int w, int h, int format, int ile, VASurfaceID *s)
{
    if (w < 0 || h < 0 || format < 0 || ile < 0)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    return k_create_surfaces2(ctx, (unsigned)format, (unsigned)w, (unsigned)h, s, (unsigned)ile, NULL, 0);
}

static VAStatus usun_powierzchnie(VADriverContextP ctx, VASurfaceID *s, int n)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    pthread_mutex_lock(&d->zamek);
    for (int i = 0; i < n; i++) {
        struct va_powierzchnia *p = va_pow(d, s[i]);
        if (p == NULL)
            continue;
        va_lacze_bufor_koniec(&d->l, p->uchwyt);
        p->jest = 0;
        for (int k = 0; k < VA_KONTEKSTOW; k++)
            va_klatki_zapomnij(&d->kont[k].klatki, s[i]);
    }
    pthread_mutex_unlock(&d->zamek);
    return VA_STATUS_SUCCESS;
}

static VAStatus k_sync(VADriverContextP ctx, VASurfaceID s)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    int jest;
    pthread_mutex_lock(&d->zamek);
    jest = va_pow(d, s) != NULL; /* EndPicture waited for both commands */
    pthread_mutex_unlock(&d->zamek);
    return jest ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_SURFACE;
}

static VAStatus k_status(VADriverContextP ctx, VASurfaceID s, VASurfaceStatus *st)
{
    VAStatus r = k_sync(ctx, s);
    *st = VASurfaceReady;
    return r;
}

/* The buffer's layout as the decoder writes it (k1_filmy_bufor_uv) */
static void opis(VADRMPRIMESurfaceDescriptor *o, const struct va_powierzchnia *p, int fd, uint32_t flagi)
{
    uint32_t rzad = k1_filmy_rzad(p->w), uv = (uint32_t)k1_filmy_bufor_uv(p->w, p->h);
    memset(o, 0, sizeof *o);
    o->fourcc = VA_FOURCC_NV12;
    o->width = p->w, o->height = p->h;
    o->num_objects = 1;
    o->objects[0].fd = fd;
    o->objects[0].size = (uint32_t)p->dl;
    o->objects[0].drm_format_modifier = DRM_FORMAT_MOD_LINEAR;
    if (flagi & VA_EXPORT_SURFACE_SEPARATE_LAYERS) {
        o->num_layers = 2;
        o->layers[0] = (__typeof__(o->layers[0])){ DRM_FORMAT_R8, 1, { 0 }, { 0 }, { rzad } };
        o->layers[1] = (__typeof__(o->layers[1])){ DRM_FORMAT_GR88, 1, { 0 }, { uv }, { rzad } };
    } else {
        o->num_layers = 1;
        o->layers[0] = (__typeof__(o->layers[0])){ DRM_FORMAT_NV12, 2, { 0, 0 }, { 0, uv }, { rzad, rzad } };
    }
}

static VAStatus k_export(VADriverContextP ctx, VASurfaceID s, uint32_t typ, uint32_t flagi, void *wynik)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_powierzchnia *p;
    VAStatus r = VA_STATUS_ERROR_INVALID_SURFACE;
    int fd;
    if (typ != VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2)
        return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
    pthread_mutex_lock(&d->zamek);
    if ((p = va_pow(d, s)) != NULL) {
        fd = va_lacze_dmabuf(&d->l, p->uchwyt);
        r = fd >= 0 ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_OPERATION_FAILED;
        if (fd >= 0)
            opis(wynik, p, fd, flagi);
    }
    pthread_mutex_unlock(&d->zamek);
    return r;
}

static VAStatus k_create_buffer(VADriverContextP ctx, VAContextID kont, VABufferType typ, unsigned int rozmiar,
                                unsigned int ile, void *dane, VABufferID *id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    uint64_t n = (uint64_t)rozmiar * ile;
    VAStatus r = VA_STATUS_ERROR_ALLOCATION_FAILED;
    void *tab;
    int i;
    (void)kont;
    if (rozmiar == 0 || ile == 0 || n > K1_FILMY_DANE_MAKS)
        return VA_STATUS_ERROR_INVALID_PARAMETER;
    pthread_mutex_lock(&d->zamek);
    tab = d->buf;
    i = miejsce(&tab, &d->n_buf, sizeof *d->buf, BUF_MAKS, offsetof(struct va_bufor, jest));
    d->buf = tab;
    if (i >= 0 && (d->buf[i].dane = calloc(1, (size_t)n)) != NULL) {
        d->buf[i] = (struct va_bufor){ 1, typ, rozmiar, ile, d->buf[i].dane };
        if (dane != NULL)
            memcpy(d->buf[i].dane, dane, (size_t)n);
        *id = VA_ID_BUF + (uint32_t)i;
        r = VA_STATUS_SUCCESS;
    }
    pthread_mutex_unlock(&d->zamek);
    return r;
}

static VAStatus k_destroy_buffer(VADriverContextP ctx, VABufferID id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_bufor *b;
    pthread_mutex_lock(&d->zamek);
    if ((b = va_buf(d, id)) != NULL) {
        free(b->dane);
        b->dane = NULL, b->jest = 0;
    }
    pthread_mutex_unlock(&d->zamek);
    return b != NULL ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_BUFFER;
}

static VAStatus k_map_buffer(VADriverContextP ctx, VABufferID id, void **p)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_bufor *b;
    pthread_mutex_lock(&d->zamek);
    b = va_buf(d, id);
    *p = b != NULL ? b->dane : NULL;
    pthread_mutex_unlock(&d->zamek);
    return b != NULL ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_BUFFER;
}

static VAStatus k_unmap_buffer(VADriverContextP ctx, VABufferID id)
{
    void *p;
    return k_map_buffer(ctx, id, &p);
}

static VAStatus k_set_num(VADriverContextP ctx, VABufferID id, unsigned int ile)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_bufor *b;
    VAStatus r = VA_STATUS_ERROR_INVALID_BUFFER;
    pthread_mutex_lock(&d->zamek);
    if ((b = va_buf(d, id)) != NULL) {
        r = ile >= 1 && ile <= b->ile ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_PARAMETER; /* never more memory */
        if (r == VA_STATUS_SUCCESS)
            b->ile = ile;
    }
    pthread_mutex_unlock(&d->zamek);
    return r;
}

void va_vtable_powierzchnie(struct VADriverVTable *v)
{
    v->vaCreateSurfaces = k_create_surfaces;
    v->vaCreateSurfaces2 = k_create_surfaces2;
    v->vaDestroySurfaces = usun_powierzchnie;
    v->vaSyncSurface = k_sync;
    v->vaQuerySurfaceStatus = k_status;
    v->vaExportSurfaceHandle = k_export;
    v->vaCreateBuffer = k_create_buffer;
    v->vaDestroyBuffer = k_destroy_buffer;
    v->vaMapBuffer = k_map_buffer;
    v->vaUnmapBuffer = k_unmap_buffer;
    v->vaBufferSetNumElements = k_set_num;
}

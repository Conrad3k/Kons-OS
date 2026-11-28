/* K1 — the VA driver of the zone: what it can do (0.13.2 step 4). Firefox's vaapitest asks the profiles and entry points
 * (and makes and destroys a configuration); FFmpeg asks the image formats (one is needed), the surface attributes
 * (NV12, the sizes from the capset) and the configuration's RT format. libva wants every other function of its table
 * too (va.c, CHECK_VTABLE): images, subpictures, display attributes and PutSurface — none of them is offered here
 * (Firefox takes every frame as a dma-buf; ADR 0063 L1), so they answer "not implemented". */
#include <string.h>

#include <va/va_drmcommon.h>

#include "sterownik.h"

#define X_PROF(n, nr, nazwa, rodz, prof, ...) (VAProfile)prof,
static const VAProfile PROFILE[] = { K1_KODEKI(X_PROF) };

static VAStatus k_profiles(VADriverContextP ctx, VAProfile *lista, int *n)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    *n = 0;
    for (size_t i = 0; i < sizeof PROFILE / sizeof PROFILE[0]; i++)
        if (va_kodek(d, PROFILE[i]) != 0)
            lista[(*n)++] = PROFILE[i];
    return VA_STATUS_SUCCESS;
}

static VAStatus k_entrypoints(VADriverContextP ctx, VAProfile p, VAEntrypoint *lista, int *n)
{
    *n = 0;
    if (va_kodek(VA_DANE(ctx), p) == 0)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    lista[(*n)++] = VAEntrypointVLD;
    return VA_STATUS_SUCCESS;
}

static VAStatus k_get_attributes(VADriverContextP ctx, VAProfile p, VAEntrypoint e, VAConfigAttrib *a, int n)
{
    if (va_kodek(VA_DANE(ctx), p) == 0)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    if (e != VAEntrypointVLD)
        return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
    for (int i = 0; i < n; i++)
        a[i].value = a[i].type == VAConfigAttribRTFormat ? VA_RT_FORMAT_YUV420 : VA_ATTRIB_NOT_SUPPORTED;
    return VA_STATUS_SUCCESS;
}

static VAStatus k_query_config(VADriverContextP ctx, VAConfigID id, VAProfile *p, VAEntrypoint *e, VAConfigAttrib *a,
                               int *n)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    uint32_t i = id - VA_ID_KONF;
    if (id < VA_ID_KONF || i >= VA_KONFIGURACJI || !d->konf[i].jest)
        return VA_STATUS_ERROR_INVALID_CONFIG;
    *p = d->konf[i].profil;
    *e = VAEntrypointVLD;
    a[0] = (VAConfigAttrib){ VAConfigAttribRTFormat, VA_RT_FORMAT_YUV420 };
    *n = 1;
    return VA_STATUS_SUCCESS;
}

#define ATRYBUTY 6
static VAStatus k_surface_attributes(VADriverContextP ctx, VAConfigID id, VASurfaceAttrib *a, unsigned int *n)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    const VASurfaceAttrib t[ATRYBUTY] = {
        { VASurfaceAttribPixelFormat, VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE,
          { VAGenericValueTypeInteger, { .i = (int)VA_FOURCC_NV12 } } },
        { VASurfaceAttribMinWidth, VA_SURFACE_ATTRIB_GETTABLE, { VAGenericValueTypeInteger, { .i = K1_FILMY_MIN_WH } } },
        { VASurfaceAttribMinHeight, VA_SURFACE_ATTRIB_GETTABLE, { VAGenericValueTypeInteger, { .i = K1_FILMY_MIN_WH } } },
        { VASurfaceAttribMaxWidth, VA_SURFACE_ATTRIB_GETTABLE, { VAGenericValueTypeInteger, { .i = (int)d->l.zbior.maks_w } } },
        { VASurfaceAttribMaxHeight, VA_SURFACE_ATTRIB_GETTABLE, { VAGenericValueTypeInteger, { .i = (int)d->l.zbior.maks_h } } },
        { VASurfaceAttribMemoryType, VA_SURFACE_ATTRIB_GETTABLE | VA_SURFACE_ATTRIB_SETTABLE,
          { VAGenericValueTypeInteger,
            { .i = VA_SURFACE_ATTRIB_MEM_TYPE_VA | (int)VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 } } },
    };
    uint32_t i = id - VA_ID_KONF;
    if (id < VA_ID_KONF || i >= VA_KONFIGURACJI || !d->konf[i].jest)
        return VA_STATUS_ERROR_INVALID_CONFIG;
    if (a == NULL) { /* the count first (FFmpeg asks twice) */
        *n = ATRYBUTY;
        return VA_STATUS_SUCCESS;
    }
    if (*n < ATRYBUTY) {
        *n = ATRYBUTY;
        return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    }
    memcpy(a, t, sizeof t);
    *n = ATRYBUTY;
    return VA_STATUS_SUCCESS;
}

static VAStatus k_image_formats(VADriverContextP ctx, VAImageFormat *lista, int *n)
{
    (void)ctx;
    lista[0] = (VAImageFormat){ .fourcc = VA_FOURCC_NV12, .byte_order = VA_LSB_FIRST, .bits_per_pixel = 12 };
    *n = 1;
    return VA_STATUS_SUCCESS;
}

static VAStatus k_subpicture_formats(VADriverContextP ctx, VAImageFormat *lista, unsigned int *flagi, unsigned int *n)
{
    (void)ctx, (void)lista, (void)flagi;
    *n = 0;
    return VA_STATUS_SUCCESS;
}

static VAStatus k_display_attributes(VADriverContextP ctx, VADisplayAttribute *lista, int *n)
{
    (void)ctx, (void)lista;
    *n = 0;
    return VA_STATUS_SUCCESS;
}

/* The parts of VA this driver has nothing for. Every function of the table has its own signature; each is one line. */
#define NIE (void)ctx; return VA_STATUS_ERROR_UNIMPLEMENTED
static VAStatus n_create_image(VADriverContextP ctx, VAImageFormat *f, int w, int h, VAImage *i)
{ (void)f, (void)w, (void)h, (void)i; NIE; }
static VAStatus n_derive_image(VADriverContextP ctx, VASurfaceID s, VAImage *i)
{ (void)s, (void)i; NIE; }
static VAStatus n_destroy_image(VADriverContextP ctx, VAImageID i)
{ (void)i; NIE; }
static VAStatus n_palette(VADriverContextP ctx, VAImageID i, unsigned char *p)
{ (void)i, (void)p; NIE; }
static VAStatus n_get_image(VADriverContextP ctx, VASurfaceID s, int x, int y, unsigned int w, unsigned int h, VAImageID i)
{ (void)s, (void)x, (void)y, (void)w, (void)h, (void)i; NIE; }
static VAStatus n_put_image(VADriverContextP ctx, VASurfaceID s, VAImageID i, int sx, int sy, unsigned int sw,
                            unsigned int sh, int dx, int dy, unsigned int dw, unsigned int dh)
{ (void)s, (void)i, (void)sx, (void)sy, (void)sw, (void)sh, (void)dx, (void)dy, (void)dw, (void)dh; NIE; }
static VAStatus n_create_sub(VADriverContextP ctx, VAImageID i, VASubpictureID *s)
{ (void)i, (void)s; NIE; }
static VAStatus n_destroy_sub(VADriverContextP ctx, VASubpictureID s)
{ (void)s; NIE; }
static VAStatus n_sub_image(VADriverContextP ctx, VASubpictureID s, VAImageID i)
{ (void)s, (void)i; NIE; }
static VAStatus n_chromakey(VADriverContextP ctx, VASubpictureID s, unsigned int a, unsigned int b, unsigned int c)
{ (void)s, (void)a, (void)b, (void)c; NIE; }
static VAStatus n_alpha(VADriverContextP ctx, VASubpictureID s, float a)
{ (void)s, (void)a; NIE; }
static VAStatus n_associate(VADriverContextP ctx, VASubpictureID s, VASurfaceID *t, int n, short sx, short sy,
                            unsigned short sw, unsigned short sh, short dx, short dy, unsigned short dw,
                            unsigned short dh, unsigned int f)
{ (void)s, (void)t, (void)n, (void)sx, (void)sy, (void)sw, (void)sh, (void)dx, (void)dy, (void)dw, (void)dh, (void)f; NIE; }
static VAStatus n_deassociate(VADriverContextP ctx, VASubpictureID s, VASurfaceID *t, int n)
{ (void)s, (void)t, (void)n; NIE; }
static VAStatus n_get_display(VADriverContextP ctx, VADisplayAttribute *a, int n)
{ (void)a, (void)n; NIE; }
static VAStatus n_set_display(VADriverContextP ctx, VADisplayAttribute *a, int n)
{ (void)a, (void)n; NIE; }
static VAStatus n_put_surface(VADriverContextP ctx, VASurfaceID s, void *d, short sx, short sy, unsigned short sw,
                              unsigned short sh, short dx, short dy, unsigned short dw, unsigned short dh,
                              VARectangle *r, unsigned int nr, unsigned int f)
{ (void)s, (void)d, (void)sx, (void)sy, (void)sw, (void)sh, (void)dx, (void)dy, (void)dw, (void)dh, (void)r, (void)nr, (void)f; NIE; }

void va_vtable_zapytania(struct VADriverVTable *v)
{
    v->vaQueryConfigProfiles = k_profiles;
    v->vaQueryConfigEntrypoints = k_entrypoints;
    v->vaGetConfigAttributes = k_get_attributes;
    v->vaQueryConfigAttributes = k_query_config;
    v->vaQuerySurfaceAttributes = k_surface_attributes;
    v->vaQueryImageFormats = k_image_formats;
    v->vaQuerySubpictureFormats = k_subpicture_formats;
    v->vaQueryDisplayAttributes = k_display_attributes;
    v->vaCreateImage = n_create_image;
    v->vaDeriveImage = n_derive_image;
    v->vaDestroyImage = n_destroy_image;
    v->vaSetImagePalette = n_palette;
    v->vaGetImage = n_get_image;
    v->vaPutImage = n_put_image;
    v->vaCreateSubpicture = n_create_sub;
    v->vaDestroySubpicture = n_destroy_sub;
    v->vaSetSubpictureImage = n_sub_image;
    v->vaSetSubpictureChromakey = n_chromakey;
    v->vaSetSubpictureGlobalAlpha = n_alpha;
    v->vaAssociateSubpicture = n_associate;
    v->vaDeassociateSubpicture = n_deassociate;
    v->vaGetDisplayAttributes = n_get_display;
    v->vaSetDisplayAttributes = n_set_display;
    v->vaPutSurface = n_put_surface;
}

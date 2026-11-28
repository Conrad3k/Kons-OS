/* K1 filmy — the card through VA-API (0.10.9 step 2, ADR 0049 pkt 3; T2): libva with the i965 driver, on the render
 * node from nadzorca (fd 4). What the card decodes (start), a film = a VA config, its surfaces and a context (otworz),
 * a picture back in NV12 (pobierz: the card copies the surface into a linear image first — T9 (1): the processor reads
 * that, not the card's tiled buffer). The H.264 request itself: karta_h264.c. A failure of the card fails the request
 * (K1_FILMY_BLAD_KARTA; out of memory: K1_FILMY_BLAD_PAMIEC) — the film plays on the processor in the zone. */
#include <string.h>
#include <sys/ioctl.h>

#include <drm/drm.h>
#include <va/va.h>
#include <va/va_drm.h>

#include "karta.h"
#include "k1_log.h"

VADisplay karta_dpy;
struct karta_film karta_filmy[K1_FILMY_FILMOW_MAKS + 1];

#define X_PROFIL(n, nr, nazwa, rodz, prof, ...) _Static_assert(prof >= VAProfileNone, #n ": profil VA");
K1_KODEKI(X_PROFIL)
_Static_assert(VAProfileH264ConstrainedBaseline == 13 && VAProfileH264Main == 6 && VAProfileH264High == 7,
               "K1_KODEKI: numery profili VA (va.h)");

int karta_blad(VAStatus s)
{
    return s == VA_STATUS_ERROR_ALLOCATION_FAILED ? K1_FILMY_BLAD_PAMIEC : K1_FILMY_BLAD_KARTA;
}

static void komunikat_va(void *kontekst, const char *napis)
{
    static unsigned ile;
    (void)kontekst;
    (void)napis; /* libva's text names paths and functions; to the log only that it spoke (numbers only) */
    if (ile++ < 4)
        k1_log(TAG, "libva zgłasza błąd (%u)", ile);
}

/* The render node is the i915 driver's (nadzorca opens only that one; /dev/null: no card). */
static int to_i915(void)
{
    char nazwa[8] = "";
    struct drm_version v;
    memset(&v, 0, sizeof v);
    v.name = nazwa;
    v.name_len = sizeof nazwa - 1;
    return ioctl(FD_KARTA, (int)DRM_IOCTL_VERSION, &v) == 0 && strcmp(nazwa, "i915") == 0;
}

static int dekoduje(VAProfile p)
{
    VAEntrypoint e[16];
    int n = 0;
    if (vaMaxNumEntrypoints(karta_dpy) > 16 || vaQueryConfigEntrypoints(karta_dpy, p, e, &n) != VA_STATUS_SUCCESS)
        return 0;
    for (int i = 0; i < n; i++)
        if (e[i] == VAEntrypointVLD)
            return 1;
    return 0;
}

/* The largest surface the card makes for profile p (0 when it does not say). */
static void najwiekszy(VAProfile p, uint32_t *w, uint32_t *h)
{
    VAConfigID cfg;
    VASurfaceAttrib a[32];
    unsigned n = 32;
    *w = *h = 0;
    if (vaCreateConfig(karta_dpy, p, VAEntrypointVLD, NULL, 0, &cfg) != VA_STATUS_SUCCESS)
        return;
    if (vaQuerySurfaceAttributes(karta_dpy, cfg, a, &n) == VA_STATUS_SUCCESS && n <= 32)
        for (unsigned i = 0; i < n; i++) {
            if (a[i].type == VASurfaceAttribMaxWidth && a[i].value.type == VAGenericValueTypeInteger)
                *w = (uint32_t)a[i].value.value.i;
            if (a[i].type == VASurfaceAttribMaxHeight && a[i].value.type == VAGenericValueTypeInteger)
                *h = (uint32_t)a[i].value.value.i;
        }
    vaDestroyConfig(karta_dpy, cfg);
}

static int start(int32_t *bity, uint32_t *w, uint32_t *h, int *kod)
{
    VAProfile profile[64];
    int maj, min, n = 0;
    VAStatus s;
    *bity = 0, *w = *h = 0, *kod = 0;
    if (!to_i915() || (karta_dpy = vaGetDisplayDRM(FD_KARTA)) == NULL)
        return K1_FILMY_KARTA_BRAK;
    vaSetErrorCallback(karta_dpy, komunikat_va, NULL);
    vaSetInfoCallback(karta_dpy, NULL, NULL);
    s = vaInitialize(karta_dpy, &maj, &min);
    if (s != VA_STATUS_SUCCESS || vaMaxNumProfiles(karta_dpy) > 64 ||
        (s = vaQueryConfigProfiles(karta_dpy, profile, &n)) != VA_STATUS_SUCCESS) {
        *kod = (int)s;
        return K1_FILMY_KARTA_NIC;
    }
    for (int k = 1; k <= K1_KODEKI_MAKS; k++)
        for (int i = 0; i < n; i++)
            if ((int)profile[i] == k1_kodek(k)->profil_va && dekoduje(profile[i])) {
                uint32_t kw, kh;
                najwiekszy(profile[i], &kw, &kh);
                kw = kw == 0 || kw > k1_kodek(k)->maks_w ? k1_kodek(k)->maks_w : kw;
                kh = kh == 0 || kh > k1_kodek(k)->maks_h ? k1_kodek(k)->maks_h : kh;
                *w = *w > kw || *w == 0 ? kw : *w; /* one size for every codec: the smallest */
                *h = *h > kh || *h == 0 ? kh : *h;
                *bity |= (int32_t)(1u << (k - 1));
            }
    if (*bity == 0 || *w < K1_FILMY_MIN_WH || *h < K1_FILMY_MIN_WH)
        return *bity = 0, *w = *h = 0, K1_FILMY_KARTA_NIC;
    return K1_FILMY_KARTA_UMIE;
}

static void zamknij(int film)
{
    struct karta_film *f = &karta_filmy[film];
    if (f->obraz.image_id != VA_INVALID_ID)
        vaDestroyImage(karta_dpy, f->obraz.image_id);
    if (f->kontekst != VA_INVALID_ID)
        vaDestroyContext(karta_dpy, f->kontekst);
    if (f->n > 0)
        vaDestroySurfaces(karta_dpy, f->pow, (int)f->n);
    if (f->cfg != VA_INVALID_ID)
        vaDestroyConfig(karta_dpy, f->cfg);
    memset(f, 0, sizeof *f);
}

static int otworz(int film, const struct k1_filmy_film *ff)
{
    struct karta_film *f = &karta_filmy[film];
    VAConfigAttrib a = { VAConfigAttribRTFormat, VA_RT_FORMAT_YUV420 };
    VAImageFormat nv12;
    VAStatus s;
    memset(f, 0, sizeof *f);
    f->cfg = f->kontekst = f->obraz.image_id = VA_INVALID_ID;
    memset(&nv12, 0, sizeof nv12);
    nv12.fourcc = VA_FOURCC_NV12, nv12.byte_order = VA_LSB_FIRST, nv12.bits_per_pixel = 12;
    s = vaCreateConfig(karta_dpy, (VAProfile)k1_kodek(ff->kodek)->profil_va, VAEntrypointVLD, &a, 1, &f->cfg);
    if (s == VA_STATUS_SUCCESS) {
        s = vaCreateSurfaces(karta_dpy, VA_RT_FORMAT_YUV420, ff->w, ff->h, f->pow, ff->klatki, NULL, 0);
        f->n = s == VA_STATUS_SUCCESS ? ff->klatki : 0;
    }
    if (s == VA_STATUS_SUCCESS)
        s = vaCreateContext(karta_dpy, f->cfg, (int)ff->w, (int)ff->h, VA_PROGRESSIVE, f->pow, (int)ff->klatki,
                            &f->kontekst);
    if (s == VA_STATUS_SUCCESS)
        s = vaCreateImage(karta_dpy, &nv12, (int)ff->w, (int)ff->h, &f->obraz);
    if (s != VA_STATUS_SUCCESS) {
        k1_log(TAG, "film %d: karta odmówiła (VA %d)", film, (int)s);
        zamknij(film);
        return karta_blad(s);
    }
    return 0;
}

/* The image of NV12 the card copied into: both planes inside it, rows long enough (checked — the driver's numbers). */
static int obraz_dobry(const VAImage *o, uint32_t w, uint32_t h)
{
    uint64_t uv = (uint64_t)o->offsets[1] + (uint64_t)o->pitches[1] * ((h + 1) / 2);
    return o->format.fourcc == VA_FOURCC_NV12 && o->num_planes == 2 && o->width >= w && o->height >= h &&
           o->pitches[0] >= w && o->pitches[1] >= ((w + 1) & ~1u) &&
           (uint64_t)o->offsets[0] + (uint64_t)o->pitches[0] * h <= o->data_size && uv <= o->data_size;
}

static int pobierz(int film, const struct k1_filmy_film *ff, uint32_t cel, uint8_t *y, uint8_t *uv)
{
    struct karta_film *f = &karta_filmy[film];
    uint32_t rzad = k1_filmy_rzad(ff->w), uv_w = (ff->w + 1) & ~1u;
    uint8_t *m = NULL;
    VAStatus s = vaSyncSurface(karta_dpy, f->pow[cel]);
    if (s == VA_STATUS_SUCCESS)
        s = vaGetImage(karta_dpy, f->pow[cel], 0, 0, ff->w, ff->h, f->obraz.image_id);
    if (s == VA_STATUS_SUCCESS && !obraz_dobry(&f->obraz, ff->w, ff->h))
        s = VA_STATUS_ERROR_INVALID_IMAGE;
    if (s == VA_STATUS_SUCCESS)
        s = vaMapBuffer(karta_dpy, f->obraz.buf, (void **)&m);
    if (s != VA_STATUS_SUCCESS)
        return karta_blad(s);
    for (uint32_t r = 0; r < ff->h; r++)
        memcpy(y + (size_t)r * rzad, m + f->obraz.offsets[0] + (size_t)r * f->obraz.pitches[0], ff->w);
    for (uint32_t r = 0; r < (ff->h + 1) / 2; r++)
        memcpy(uv + (size_t)r * rzad, m + f->obraz.offsets[1] + (size_t)r * f->obraz.pitches[1], uv_w);
    vaUnmapBuffer(karta_dpy, f->obraz.buf);
    return 0;
}

const struct silnik silnik_karty = { "karta (VA-API, i965)", K1_FILMY_PAMIEC, start, otworz, karta_klatka_h264, pobierz, zamknij };

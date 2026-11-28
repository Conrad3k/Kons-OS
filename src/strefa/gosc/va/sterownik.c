/* K1 — the VA driver of the zone: libva's entry point, configurations, contexts and pictures (0.13.2 step 4, ADR 0063
 * pkt 2, 3). A VA context = one film of the film context (K1_KF_OTWORZ, the film number = the context's place + 1);
 * EndPicture = the request in the shared memory and two commands: K1_KF_KLATKA (the decoder decodes it with the card)
 * and K1_KF_POBIERZ (it writes the frame into the target surface's buffer). Both are waited for, so a surface is ready
 * once EndPicture returns (vaSyncSurface has nothing left to wait for). Any error from K1 is an error of VA: FFmpeg
 * gives up the picture, Firefox plays the film with the processor. One lock per display — FFmpeg's decoding thread and
 * Firefox's export may meet. */
#include <stdlib.h>
#include <string.h>

#include <va/va_drmcommon.h>

#include "sterownik.h"

#define X_PROFIL(n, nr, nazwa, rodz, prof, ...) { nr, prof },
static const struct { int kodek, profil; } PROFILE[] = { K1_KODEKI(X_PROFIL) };

int va_kodek(const struct va_dane_sterownika *d, VAProfile p)
{
    for (size_t i = 0; i < sizeof PROFILE / sizeof PROFILE[0]; i++)
        if (PROFILE[i].profil == (int)p && (d->l.zbior.kodeki & (1u << (PROFILE[i].kodek - 1))))
            return PROFILE[i].kodek;
    return 0;
}

VAStatus va_status(int wynik)
{
    switch (wynik) {
    case 0:
        return VA_STATUS_SUCCESS;
    case K1_FILMY_BLAD_KODEK:
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    case K1_FILMY_BLAD_ROZMIAR:
        return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
    case K1_FILMY_BLAD_PAMIEC:
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    case K1_FILMY_BLAD_PROSBA:
    case K1_FILMY_BLAD_ODNIES:
    case K1_FILMY_BLAD_KARTA:
        return VA_STATUS_ERROR_DECODING_ERROR;
    default:
        return VA_STATUS_ERROR_OPERATION_FAILED; /* no decoder (K1_KF_BLAD_DEKODER), no answer, the window */
    }
}

static struct va_kontekst *kont(struct va_dane_sterownika *d, VAContextID id, uint32_t *film)
{
    uint32_t i = id - VA_ID_KONT;
    if (id < VA_ID_KONT || i >= VA_KONTEKSTOW || !d->kont[i].jest)
        return NULL;
    *film = i + 1;
    return &d->kont[i];
}

static VAStatus k_terminate(VADriverContextP ctx)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    for (uint32_t i = 0; i < d->n_pow; i++)
        if (d->pow[i].jest)
            va_lacze_bufor_koniec(&d->l, d->pow[i].uchwyt);
    for (uint32_t i = 0; i < d->n_buf; i++)
        free(d->buf[i].dane);
    for (int i = 0; i < VA_KONTEKSTOW; i++)
        free(d->kont[i].bufory);
    va_lacze_koniec(&d->l);
    pthread_mutex_destroy(&d->zamek);
    free(d->pow);
    free(d->buf);
    free(d);
    ctx->pDriverData = NULL;
    return VA_STATUS_SUCCESS;
}

static VAStatus k_create_config(VADriverContextP ctx, VAProfile profil, VAEntrypoint wejscie, VAConfigAttrib *a, int n,
                                VAConfigID *id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    VAStatus s = VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
    if (va_kodek(d, profil) == 0)
        return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
    if (wejscie != VAEntrypointVLD)
        return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
    for (int i = 0; i < n; i++)
        if (a[i].type == VAConfigAttribRTFormat && !(a[i].value & VA_RT_FORMAT_YUV420))
            return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
    pthread_mutex_lock(&d->zamek);
    for (uint32_t i = 0; i < VA_KONFIGURACJI; i++)
        if (!d->konf[i].jest) {
            d->konf[i] = (struct va_konfiguracja){ 1, profil };
            *id = VA_ID_KONF + i;
            s = VA_STATUS_SUCCESS;
            break;
        }
    pthread_mutex_unlock(&d->zamek);
    return s;
}

static VAStatus k_destroy_config(VADriverContextP ctx, VAConfigID id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    uint32_t i = id - VA_ID_KONF;
    if (id < VA_ID_KONF || i >= VA_KONFIGURACJI || !d->konf[i].jest)
        return VA_STATUS_ERROR_INVALID_CONFIG;
    pthread_mutex_lock(&d->zamek);
    d->konf[i].jest = 0;
    pthread_mutex_unlock(&d->zamek);
    return VA_STATUS_SUCCESS;
}

/* A film: in the first free place, opened in the decoder with every surface number of the row (the pool's size is not
 * known — klatki.c). The film context opens with the first film. */
static VAStatus nowy_film(struct va_dane_sterownika *d, int kodek, uint32_t w, uint32_t h, VAContextID *id)
{
    int r;
    for (uint32_t i = 0; i < VA_KONTEKSTOW; i++) {
        struct va_kontekst *k = &d->kont[i];
        if (k->jest)
            continue;
        if (va_lacze_filmy(&d->l) != 0)
            return VA_STATUS_ERROR_OPERATION_FAILED;
        r = va_lacze_polecenie(&d->l, K1_KF_OTWORZ, i + 1, (uint32_t)kodek, w, h, VA_KLATKI);
        if (r != 0)
            return va_status(r);
        *k = (struct va_kontekst){ .jest = 1, .kodek = kodek, .w = w, .h = h, .cel = VA_INVALID_SURFACE,
                                   .bufory = k->bufory, .miejsce_buforow = k->miejsce_buforow };
        va_klatki_zacznij(&k->klatki);
        *id = VA_ID_KONT + i;
        return VA_STATUS_SUCCESS;
    }
    return VA_STATUS_ERROR_MAX_NUM_EXCEEDED;
}

static int filmow(const struct va_dane_sterownika *d)
{
    int n = 0;
    for (int i = 0; i < VA_KONTEKSTOW; i++)
        n += d->kont[i].jest;
    return n;
}

static VAStatus k_create_context(VADriverContextP ctx, VAConfigID konf, int w, int h, int flagi, VASurfaceID *cele,
                                 int n_celow, VAContextID *id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    uint32_t i = konf - VA_ID_KONF;
    VAStatus s;
    (void)flagi, (void)cele, (void)n_celow; /* the surfaces get their numbers as targets (klatki.c) */
    if (konf < VA_ID_KONF || i >= VA_KONFIGURACJI)
        return VA_STATUS_ERROR_INVALID_CONFIG;
    if (w < K1_FILMY_MIN_WH || h < K1_FILMY_MIN_WH || (uint32_t)w > d->l.zbior.maks_w || (uint32_t)h > d->l.zbior.maks_h)
        return VA_STATUS_ERROR_RESOLUTION_NOT_SUPPORTED;
    pthread_mutex_lock(&d->zamek);
    s = !d->konf[i].jest ? VA_STATUS_ERROR_INVALID_CONFIG
                         : nowy_film(d, va_kodek(d, d->konf[i].profil), (uint32_t)w, (uint32_t)h, id);
    if (s != VA_STATUS_SUCCESS && filmow(d) == 0)
        va_lacze_filmy_koniec(&d->l);
    pthread_mutex_unlock(&d->zamek);
    return s;
}

static VAStatus k_destroy_context(VADriverContextP ctx, VAContextID id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_kontekst *k;
    uint32_t film = 0;
    pthread_mutex_lock(&d->zamek);
    k = kont(d, id, &film);
    if (k != NULL) {
        va_lacze_polecenie(&d->l, K1_KF_ZAMKNIJ, film, 0, 0, 0, 0);
        k->jest = 0;
        if (filmow(d) == 0)
            va_lacze_filmy_koniec(&d->l); /* no film left: the decoder ends and gives its memory back */
    }
    pthread_mutex_unlock(&d->zamek);
    return k != NULL ? VA_STATUS_SUCCESS : VA_STATUS_ERROR_INVALID_CONTEXT;
}

static VAStatus k_begin(VADriverContextP ctx, VAContextID id, VASurfaceID cel)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_kontekst *k;
    struct va_powierzchnia *p;
    uint32_t film = 0;
    VAStatus s = VA_STATUS_SUCCESS;
    pthread_mutex_lock(&d->zamek);
    k = kont(d, id, &film);
    p = va_pow(d, cel);
    if (k == NULL)
        s = VA_STATUS_ERROR_INVALID_CONTEXT;
    else if (p == NULL)
        s = VA_STATUS_ERROR_INVALID_SURFACE;
    else if (p->dl < k1_filmy_bufor_bajty(k->w, k->h))
        s = VA_STATUS_ERROR_INVALID_SURFACE; /* smaller than the film: the decoder would refuse the frame */
    else
        p->w = k->w, p->h = k->h, k->cel = cel, k->n_buforow = 0, k->zepsuty = 0; /* the film's layout */
    pthread_mutex_unlock(&d->zamek);
    return s;
}

static VAStatus k_render(VADriverContextP ctx, VAContextID id, VABufferID *b, int n)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_kontekst *k;
    uint32_t film = 0;
    VAStatus s = VA_STATUS_SUCCESS;
    pthread_mutex_lock(&d->zamek);
    k = kont(d, id, &film);
    if (k == NULL || k->cel == VA_INVALID_SURFACE)
        s = VA_STATUS_ERROR_INVALID_CONTEXT;
    for (int i = 0; s == VA_STATUS_SUCCESS && i < n; i++) {
        if (va_buf(d, b[i]) == NULL)
            s = VA_STATUS_ERROR_INVALID_BUFFER;
        else if (k->n_buforow == k->miejsce_buforow) {
            uint32_t m = k->miejsce_buforow ? k->miejsce_buforow * 2 : 64;
            VABufferID *nowe = m <= 4096 ? realloc(k->bufory, m * sizeof *nowe) : NULL;
            if (nowe == NULL)
                s = VA_STATUS_ERROR_ALLOCATION_FAILED;
            else
                k->bufory = nowe, k->miejsce_buforow = m;
        }
        if (s == VA_STATUS_SUCCESS)
            k->bufory[k->n_buforow++] = b[i]; /* read at EndPicture: FFmpeg destroys them only after it */
    }
    if (k != NULL && s != VA_STATUS_SUCCESS)
        k->zepsuty = 1;
    pthread_mutex_unlock(&d->zamek);
    return s;
}

static VAStatus k_end(VADriverContextP ctx, VAContextID id)
{
    struct va_dane_sterownika *d = VA_DANE(ctx);
    struct va_kontekst *k;
    uint32_t film = 0;
    VAStatus s;
    pthread_mutex_lock(&d->zamek);
    k = kont(d, id, &film);
    s = k == NULL || k->cel == VA_INVALID_SURFACE ? VA_STATUS_ERROR_INVALID_CONTEXT
        : k->zepsuty                              ? VA_STATUS_ERROR_INVALID_BUFFER
                                                  : va_obraz_koniec(d, film, k);
    if (k != NULL)
        k->cel = VA_INVALID_SURFACE, k->n_buforow = 0;
    pthread_mutex_unlock(&d->zamek);
    return s;
}

VAStatus __attribute__((visibility("default"))) __vaDriverInit_1_0(VADriverContextP ctx)
{
    struct va_dane_sterownika *d;
    struct VADriverVTable *v = ctx->vtable;
    const struct drm_state *drm = ctx->drm_state;
    if (ctx->display_type >> 4 != VA_DISPLAY_DRM >> 4 || drm == NULL) /* DRM and its render nodes */
        return VA_STATUS_ERROR_UNIMPLEMENTED;
    if ((d = calloc(1, sizeof *d)) == NULL)
        return VA_STATUS_ERROR_ALLOCATION_FAILED;
    if (va_lacze_zacznij(&d->l, drm->fd) != 0) { /* no film context in this session (or not K1's device) */
        free(d);
        return VA_STATUS_ERROR_UNIMPLEMENTED;
    }
    pthread_mutex_init(&d->zamek, NULL);
    ctx->pDriverData = d;
    ctx->max_profiles = K1_KODEKI_MAKS;
    ctx->max_entrypoints = 1;
    ctx->max_attributes = 1;
    ctx->max_image_formats = 1;
    ctx->max_subpic_formats = 1; /* libva wants more than 0; none is offered */
    ctx->max_display_attributes = 1;
    ctx->str_vendor = "K1 VA strefy (dekoder filmow K1)"; /* ASCII: FFmpeg and Firefox print it */
    v->vaTerminate = k_terminate;
    v->vaCreateConfig = k_create_config;
    v->vaDestroyConfig = k_destroy_config;
    v->vaCreateContext = k_create_context;
    v->vaDestroyContext = k_destroy_context;
    v->vaBeginPicture = k_begin;
    v->vaRenderPicture = k_render;
    v->vaEndPicture = k_end;
    va_vtable_powierzchnie(v);
    va_vtable_zapytania(v);
    return VA_STATUS_SUCCESS;
}

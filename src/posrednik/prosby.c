/* K1 posrednik — the requests of the bridge (0.10.9 step 5; the rules: k1_posrednik.h, the checks: k1_posrednik.c).
 * A stream from the window is copied first, then each request is checked and only then done — as in virglrenderer's
 * i915 bridge (src/drm/i915/i915_ccmd.c, the pattern), but every argument the kernel reads is built here from checked
 * fields: the guest's pointers are never used, its resource numbers become the card's handles, its extensions are linked
 * anew. A refusal of the kernel goes back to the guest in the reply (ret = errno), as there. A reply is built in our
 * memory and copied into the shared memory at its place, at most the size the guest gave it there (vdrm_ccmd_rsp). */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm/i915_drm.h>

#include "k1_log.h"
#include "posrednik.h"

/* The kernel's layouts are those of the protocol (k1_posrednik.h) — the checks read one, the card gets the other. */
_Static_assert(sizeof(struct drm_i915_gem_exec_object2) == sizeof(struct k1_posr_obiekt_exec), "exec_object2");
_Static_assert(sizeof(struct drm_i915_gem_context_param) == sizeof(struct k1_posr_param), "context_param");
_Static_assert(sizeof(struct i915_engine_class_instance) == sizeof(struct k1_posr_silnik), "engine_class_instance");
_Static_assert(sizeof(struct drm_i915_gem_memory_class_instance) == sizeof(struct k1_posr_region), "memory_class");
#define X_IOC(n, nr, kier, rozmiar, uchwyt) _Static_assert(K1_POSR_IOCTL_##n == DRM_IOCTL_I915_##n, #n);
K1_POSR_IOCTLE(X_IOC)
_Static_assert(I915_PARAM_HAS_CONTEXT_FREQ_HINT == K1_POSR_PARAM_MAKS, "K1_POSR_PARAM_MAKS");
_Static_assert(I915_CONTEXT_PARAM_ENGINES == K1_POSR_PARAM_ENGINES && I915_CONTEXT_PARAM_VM == K1_POSR_PARAM_VM &&
                   I915_CONTEXT_PARAM_PRIORITY == K1_POSR_PARAM_PRIORITY && I915_EXEC_FENCE_OUT == K1_POSR_EXEC_FENCE_OUT &&
                   I915_EXEC_RING_MASK == K1_POSR_EXEC_PIERSCIEN && I915_MMAP_OFFSET_FIXED == K1_POSR_MMAP_MAKS &&
                   I915_CONTEXT_MIN_USER_PRIORITY == K1_POSR_PRIORYTET_MIN,
               "k1_posrednik.h: wartości z i915_drm.h");

static uint8_t *strumien;                     /* our copy of the stream */
static uint8_t odp[16 + K1_POSR_QUERY_MAKS];  /* the reply being built */
static struct drm_i915_gem_exec_object2 bufory[K1_POSR_EXEC_BUFORY_MAKS];
static uint32_t konteksty, przestrzenie;      /* T15: a zone's contexts and address spaces are limited */
static uint32_t biezacy;                      /* the guest context of the stream being done (step 6) */
/* whose (the guest context) is each context and address space of the card: destroyed when it ends */
static uint8_t wlasciciel_ctx[K1_POSR_ID_MAKS], wlasciciel_vm[K1_POSR_ID_MAKS];

static void zapisz32(volatile uint8_t *gdzie, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        gdzie[i] = (uint8_t)(v >> (8 * i)); /* native order: little-endian (x86-64) */
}

static uint32_t czytaj32(const volatile uint8_t *skad)
{
    return (uint32_t)skad[0] | (uint32_t)skad[1] << 8 | (uint32_t)skad[2] << 16 | (uint32_t)skad[3] << 24;
}

/* The reply into the shared memory at its place: at most what the guest's `len` there allows (it may be smaller). */
static void odpowiedz(const struct k1_posr_prosba_ok *ok)
{
    volatile uint8_t *cel = wspolna[biezacy] + sizeof(struct k1_posr_wspolna) + ok->rsp_off;
    uint32_t n = czytaj32(cel);
    n = n < ok->odp ? n : ok->odp;
    zapisz32(odp, n);
    for (uint32_t i = 4; i < n; i++)
        cel[i] = odp[i];
    zapisz32(cel, n);
}

/* Bit ctx of the banned mask in the shared memory of the guest context that made ctx: set (ban) or cleared. */
static void maska(uint32_t ctx, int ban)
{
    volatile uint8_t *w = ctx < 64 ? wspolna[wlasciciel_ctx[ctx]] : NULL;
    uint64_t m = 0;
    if (w == NULL)
        return;
    for (int i = 0; i < 8; i++)
        m |= (uint64_t)w[8 + i] << (8 * i);
    m = ban ? m | 1ull << ctx : m & ~(1ull << ctx);
    for (int i = 0; i < 8; i++)
        w[8 + i] = (uint8_t)(m >> (8 * i));
}

void posr_ban(uint32_t ctx)
{
    maska(ctx, 1);
}

/* A new context or address space `id` of the current guest context: 0, or ENOSPC when K1 cannot follow its number (it
 * is destroyed at once — the card gives the lowest free number, so this never happens with honest limits). */
static int32_t nowy_id(uint8_t *wlasciciele, uint32_t id, unsigned long zniszcz)
{
    if (id < K1_POSR_ID_MAKS) {
        wlasciciele[id] = (uint8_t)biezacy;
        return 0;
    }
    if (zniszcz == DRM_IOCTL_I915_GEM_CONTEXT_DESTROY) {
        struct drm_i915_gem_context_destroy d = { .ctx_id = id };
        silnik->ioctl(zniszcz, &d);
    } else {
        struct drm_i915_gem_vm_control v = { .vm_id = id };
        silnik->ioctl(zniszcz, &v);
    }
    return ENOSPC;
}

void posr_gosc_koniec(uint32_t g)
{
    uint32_t u;
    for (uint32_t id = 0; id < K1_POSR_ID_MAKS; id++) {
        if (wlasciciel_ctx[id] == g) {
            struct drm_i915_gem_context_destroy d = { .ctx_id = id };
            maska(id, 0);
            silnik->ioctl(DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &d);
            wlasciciel_ctx[id] = 0;
            konteksty -= konteksty > 0;
        }
        if (wlasciciel_vm[id] == g) {
            struct drm_i915_gem_vm_control v = { .vm_id = id };
            silnik->ioctl(DRM_IOCTL_I915_GEM_VM_DESTROY, &v);
            wlasciciel_vm[id] = 0;
            przestrzenie -= przestrzenie > 0;
        }
    }
    while ((u = k1_posr_ob_usun_czekajacy(&obiekty, g)) != 0) {
        struct drm_gem_close z = { .handle = u };
        silnik->ioctl(DRM_IOCTL_GEM_CLOSE, &z);
    }
}

static int32_t ioctl_prosty(const struct k1_posr_prosba_ok *ok, const uint8_t *p)
{
    uint8_t arg[K1_POSR_IOCTL_ARG_MAKS];
    uint32_t n = k1_posr_ioctl_rozmiar(ok->ioctl), res = 0;
    int32_t ret;
    memcpy(arg, p + sizeof(struct k1_posr_ioctl), n);
    if (ok->uchwyt) { /* the guest's resource → the card's handle */
        struct k1_posr_obiekt *o;
        memcpy(&res, arg, 4);
        o = k1_posr_ob(&obiekty, res);
        if (o == NULL)
            return EINVAL;
        memcpy(arg, &o->uchwyt, 4);
    }
    if ((ok->ioctl == K1_POSR_IOCTL_GEM_CONTEXT_CREATE && konteksty >= K1_POSR_KONTEKSTY_MAKS) ||
        (ok->ioctl == K1_POSR_IOCTL_GEM_VM_CREATE && przestrzenie >= K1_POSR_VM_MAKS))
        return ENOSPC;
    ret = silnik->ioctl(ok->ioctl, arg);
    if (ret == 0) {
        uint32_t id;
        memcpy(&id, ok->ioctl == K1_POSR_IOCTL_GEM_VM_CREATE || ok->ioctl == K1_POSR_IOCTL_GEM_VM_DESTROY ? arg + 12 : arg,
               4);
        if (ok->ioctl == K1_POSR_IOCTL_GEM_CONTEXT_CREATE && (ret = nowy_id(wlasciciel_ctx, id,
                                                                            DRM_IOCTL_I915_GEM_CONTEXT_DESTROY)) == 0)
            konteksty++;
        if (ok->ioctl == K1_POSR_IOCTL_GEM_VM_CREATE &&
            (ret = nowy_id(wlasciciel_vm, id, DRM_IOCTL_I915_GEM_VM_DESTROY)) == 0)
            przestrzenie++;
        if (ok->ioctl == K1_POSR_IOCTL_GEM_CONTEXT_DESTROY) {
            konteksty -= konteksty > 0;
            maska(id, 0);
            if (id < K1_POSR_ID_MAKS)
                wlasciciel_ctx[id] = 0;
        }
        if (ok->ioctl == K1_POSR_IOCTL_GEM_VM_DESTROY) {
            przestrzenie -= przestrzenie > 0;
            if (id < K1_POSR_ID_MAKS)
                wlasciciel_vm[id] = 0;
        }
    }
    if (ok->uchwyt)
        memcpy(arg, &res, 4); /* the card's handle never goes back to the guest */
    if (ok->odp > 8)
        memcpy(odp + 8, arg, n);
    return ret;
}

static int32_t getparam(const uint8_t *p)
{
    struct k1_posr_getparam g;
    int wartosc = -1;
    struct drm_i915_getparam gp = { 0 };
    int32_t ret;
    memcpy(&g, p, sizeof g);
    gp.param = (int)g.param, gp.value = &wartosc;
    ret = silnik->ioctl(DRM_IOCTL_I915_GETPARAM, &gp);
    if (g.param == I915_PARAM_MMAP_GTT_VERSION && wartosc >= 5)
        wartosc = 4; /* virtio-gpu maps no part of a buffer (virglrenderer: the same) */
    memcpy(odp + 12, &wartosc, 4);
    return ret;
}

static int32_t query(const uint8_t *p)
{
    struct k1_posr_query q;
    struct drm_i915_query_item item = { 0 };
    struct drm_i915_query zapytanie = { 0 };
    int32_t ret;
    memcpy(&q, p, sizeof q);
    item.query_id = q.query_id, item.length = (int32_t)q.length, item.flags = q.flags;
    item.data_ptr = q.length ? (uintptr_t)(odp + 16) : 0;
    zapytanie.num_items = 1, zapytanie.items_ptr = (uintptr_t)&item;
    ret = silnik->ioctl(DRM_IOCTL_I915_QUERY, &zapytanie);
    memcpy(odp + 12, &item.length, 4);
    return ret;
}

/* GEM_CREATE and GEM_CREATE_EXT: T14 — refused before the card is asked when the zone's buffers would take more. */
static int nowy_bufor(const uint8_t *p, int ext)
{
    struct k1_posr_gem_create_ext c = { 0 };
    struct k1_posr_regiony r = { 0 };
    struct drm_i915_gem_memory_class_instance regiony[K1_POSR_REGIONY_MAKS];
    struct drm_i915_gem_create_ext_memory_regions mr = { 0 };
    struct drm_i915_gem_create_ext n = { 0 };
    uint8_t mapa = K1_POSR_MAPA_CACHED;
    int w;
    memcpy(&c, p, ext ? sizeof c : sizeof(struct k1_posr_gem_create));
    if (!ext)
        c.gem_flags = 0, c.ext_size = 0;
    w = k1_posr_ob_moze(&obiekty, biezacy, c.blob_id, c.size);
    if (w != 0)
        return w;
    n.size = c.size, n.flags = c.gem_flags;
    if (c.ext_size != 0) {
        memcpy(&r, p + sizeof c, sizeof r);
        memcpy(regiony, p + sizeof c + sizeof r, r.num_regions * sizeof regiony[0]);
        mr.base.name = I915_GEM_CREATE_EXT_MEMORY_REGIONS;
        mr.num_regions = r.num_regions, mr.regions = (uintptr_t)regiony;
        n.extensions = (uintptr_t)&mr;
        for (uint32_t i = 0; i < r.num_regions; i++)
            if (regiony[i].memory_class == I915_MEMORY_CLASS_DEVICE)
                mapa = K1_POSR_MAPA_WC;
    }
    if (silnik->ioctl(DRM_IOCTL_I915_GEM_CREATE_EXT, &n) != 0)
        return K1_POSR_BLAD_KARTA; /* the bridge has no reply for it: the stream stops (virglrenderer: the same) */
    w = k1_posr_ob_dodaj(&obiekty, biezacy, c.blob_id, n.handle, c.size, mapa);
    if (w != 0) {
        struct drm_gem_close z = { .handle = n.handle };
        silnik->ioctl(DRM_IOCTL_GEM_CLOSE, &z);
    }
    return w;
}

/* KONTEKST: the parameters linked anew; ENGINES' engines out of our copy. */
static int32_t kontekst(const uint8_t *p, uint32_t len)
{
    static struct drm_i915_gem_context_create_ext_setparam param[K1_POSR_PARAMY_MAKS];
    static uint8_t silniki[K1_POSR_PARAMY_MAKS][8 + K1_POSR_SILNIKI_MAKS * 4];
    struct k1_posr_kontekst k;
    struct drm_i915_gem_context_create_ext c = { 0 };
    uint32_t o = sizeof k, i = 0;
    int32_t ret;
    memcpy(&k, p, sizeof k);
    memset(param, 0, sizeof param);
    for (; o < len; i++) {
        struct k1_posr_setparam s;
        memcpy(&s, p + o, sizeof s);
        o += sizeof s;
        param[i].base.name = I915_CONTEXT_CREATE_EXT_SETPARAM;
        param[i].param.param = s.param.param, param[i].param.size = s.param.size, param[i].param.value = s.param.value;
        if (s.param.size != 0) { /* ENGINES (checked: the only one with a size) */
            memcpy(silniki[i], p + o, s.param.size);
            param[i].param.value = (uintptr_t)silniki[i];
            o += s.param.size;
        }
        if (i > 0)
            param[i - 1].base.next_extension = (uintptr_t)&param[i];
    }
    if (konteksty >= K1_POSR_KONTEKSTY_MAKS)
        return ENOSPC;
    c.flags = k.flags, c.extensions = i > 0 ? (uintptr_t)&param[0] : 0;
    ret = silnik->ioctl(DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT, &c);
    if (ret == 0 && (ret = nowy_id(wlasciciel_ctx, c.ctx_id, DRM_IOCTL_I915_GEM_CONTEXT_DESTROY)) == 0)
        konteksty++;
    memcpy(odp + 12, &c.ctx_id, 4);
    return ret;
}

static int32_t execbuffer(const uint8_t *p)
{
    struct k1_posr_execbuffer e;
    struct drm_i915_gem_execbuffer2 x = { 0 };
    int32_t ret;
    memcpy(&e, p, sizeof e);
    memcpy(bufory, p + sizeof e, e.buffer_count * sizeof bufory[0]);
    for (uint32_t i = 0; i < e.buffer_count; i++) {
        struct k1_posr_obiekt *o = k1_posr_ob(&obiekty, bufory[i].handle);
        if (o == NULL)
            return EINVAL;
        bufory[i].handle = o->uchwyt, bufory[i].relocs_ptr = 0;
    }
    x.buffers_ptr = (uintptr_t)bufory, x.buffer_count = e.buffer_count;
    x.batch_start_offset = e.batch_start_offset, x.batch_len = e.batch_len;
    x.flags = e.flags | I915_EXEC_FENCE_OUT, x.rsvd1 = e.context_id;
    ret = silnik->ioctl(DRM_IOCTL_I915_GEM_EXECBUFFER2_WR, &x);
    if (ret == 0)
        plot_nowy(biezacy, 1 + (uint32_t)(e.flags & I915_EXEC_RING_MASK), (int)(int32_t)(x.rsvd2 >> 32),
                  (uint32_t)e.context_id);
    if (ret == EIO) { /* T15, S39: the context was banned after it hung the card — fast graphics of this zone ends */
        posr_ban((uint32_t)e.context_id);
        zawieszona = 1;
    }
    return ret;
}

static void mmap_mode(const uint8_t *p)
{
    struct k1_posr_mmap_mode m;
    struct k1_posr_obiekt *o;
    memcpy(&m, p, sizeof m);
    o = k1_posr_ob(&obiekty, m.res_id);
    if (o != NULL) /* unknown: nothing (virglrenderer: the same) */
        o->mapa = m.flags == I915_MMAP_OFFSET_WB ? K1_POSR_MAPA_CACHED
                  : m.flags == I915_MMAP_OFFSET_UC ? K1_POSR_MAPA_UNCACHED
                                                   : K1_POSR_MAPA_WC;
}

static int32_t busy(const uint8_t *p)
{
    struct k1_posr_busy b;
    struct drm_i915_gem_busy x = { 0 };
    struct k1_posr_obiekt *o;
    int32_t ret;
    memcpy(&b, p, sizeof b);
    o = k1_posr_ob(&obiekty, b.res_id);
    if (o == NULL)
        return EINVAL;
    x.handle = o->uchwyt;
    ret = silnik->ioctl(DRM_IOCTL_I915_GEM_BUSY, &x);
    memcpy(odp + 12, &x.busy, 4);
    return ret;
}

/* One checked request: 0 (its reply, if any, written) or K1_POSR_BLAD_*. */
static int wykonaj(const struct k1_posr_prosba_ok *ok, const uint8_t *p)
{
    int32_t ret = 0;
    memset(odp, 0, ok->odp);
    switch (ok->cmd) {
    case K1_POSR_IOCTL:
        ret = ioctl_prosty(ok, p);
        break;
    case K1_POSR_GETPARAM:
        ret = getparam(p);
        break;
    case K1_POSR_QUERY:
        ret = query(p);
        break;
    case K1_POSR_GEM_CREATE:
    case K1_POSR_GEM_CREATE_EXT:
        return nowy_bufor(p, ok->cmd == K1_POSR_GEM_CREATE_EXT);
    case K1_POSR_KONTEKST:
        ret = kontekst(p, ok->len);
        break;
    case K1_POSR_EXECBUFFER:
        ret = execbuffer(p);
        break;
    case K1_POSR_MMAP_MODE:
        mmap_mode(p);
        return 0;
    default: /* BUSY */
        ret = busy(p);
        break;
    }
    /* ret: after the header (4 bytes) — at 4 for IOCTL and EXECBUFFER, after a pad at 8 for the others */
    memcpy(odp + (ok->cmd == K1_POSR_IOCTL || ok->cmd == K1_POSR_EXECBUFFER ? 4 : 8), &ret, 4);
    odpowiedz(ok);
    return 0;
}

int posr_prosby(const volatile uint8_t *okno, uint32_t dl, uint32_t gosc, int32_t *zrobione)
{
    uint32_t o = 0;
    biezacy = gosc; /* checked: 1..K1_POSR_GOSCIE */
    if (strumien == NULL && (strumien = malloc(K1_POSR_PROSBY_BAJTY)) == NULL)
        return K1_POSR_BLAD_PAMIEC;
    for (uint32_t i = 0; i < dl; i++)
        strumien[i] = okno[i]; /* first a copy: the client may change the window while we look */
    while (o < dl) {
        struct k1_posr_prosba_ok ok;
        uint32_t miejsce = wspolna[gosc] != NULL ? wspolna_rozmiar[gosc] - (uint32_t)sizeof(struct k1_posr_wspolna) : 0;
        int r = k1_posr_sprawdz(strumien + o, dl - o, miejsce, &ok);
        if (r == 0)
            r = wykonaj(&ok, strumien + o);
        if (r != 0)
            return r;
        if (wspolna[gosc] != NULL)
            zapisz32(wspolna[gosc], ok.seqno); /* the last request done */
        o += ok.len;
        (*zrobione)++;
        if (zawieszona)
            break;
    }
    return 0;
}

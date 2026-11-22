/* K1 strefa — the bridge of fast graphics in virtio-gpu (gpu_most.h). */
#include "gpu_most.h"

#include <string.h>

#include <linux/virtio_gpu.h>

#define STRONA 4096u

static void naglowek(uint8_t *odp, uint32_t typ)
{
    struct virtio_gpu_ctrl_hdr h;
    memset(&h, 0, sizeof h);
    h.type = typ;
    memcpy(odp, &h, sizeof h);
}

static uint32_t blad(struct most *m, uint8_t *odp, uint32_t typ)
{
    m->bledy++;
    naglowek(odp, typ);
    return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
}

void most_start(struct most *m, const struct most_lacze *l, uint32_t pci, uint32_t rewizja, uint32_t wersja)
{
    memset(m, 0, sizeof *m);
    m->l = l;
    m->caps.wire_format_version = 1;
    m->caps.version_major = wersja >> 16, m->caps.version_minor = wersja >> 8 & 0xff;
    m->caps.version_patchlevel = wersja & 0xff;
    m->caps.context_type = MOST_KONTEKST_I915;
    m->okno_baza = MOST_OKNO_BAZA;
    m->caps.pci_device_id = pci, m->caps.pci_revision_id = rewizja;
    for (uint32_t i = 0; i < K1_POSR_ZASOBY_MAKS; i++)
        m->zasoby[i].fd = -1;
}

uint64_t most_okno_baza(uint64_t koniec_pamieci)
{
    uint64_t g = (koniec_pamieci + (1ull << 30) - 1) & ~((1ull << 30) - 1);
    return g > MOST_OKNO_BAZA ? g : MOST_OKNO_BAZA;
}

/* The guest context of the guest's virtio-gpu context ctx (capset DRM), 0: none. */
static uint32_t gosc(const struct most *m, uint32_t ctx)
{
    for (uint32_t g = 1; ctx != 0 && g <= K1_POSR_GOSCIE; g++)
        if (m->ctx[g] == ctx)
            return g;
    return 0;
}

int most_zasob_jest(const struct most *m, uint32_t res)
{
    return res >= 1 && res < K1_POSR_ZASOBY_MAKS && m->zasoby[res].rodzaj != MOST_BRAK;
}

/* The command's structure (all of it in the chain), else -1 — a broken guest. */
#define STRUKTURA(typ, zm)                                                                                            \
    typ zm;                                                                                                          \
    if (n < sizeof zm) {                                                                                             \
        *powod = "virtio-gpu: polecenie 3D krótsze niż jego struktura";                                               \
        return -1;                                                                                                   \
    }                                                                                                                \
    memcpy(&zm, p, sizeof zm);

static uint32_t kontekst(struct most *m, const struct virtio_gpu_ctx_create *c, uint8_t *odp)
{
    uint32_t capset = c->context_init & VIRTIO_GPU_CONTEXT_INIT_CAPSET_ID_MASK, g;
    if (c->hdr.ctx_id == 0 || (c->context_init & ~(uint32_t)VIRTIO_GPU_CONTEXT_INIT_CAPSET_ID_MASK) != 0)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    if (capset != 0 && (capset != VIRTIO_GPU_CAPSET_DRM || gosc(m, c->hdr.ctx_id) != 0 || m->padl))
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    for (g = 1; capset != 0 && g <= K1_POSR_GOSCIE && m->ctx[g] != 0; g++)
        ;
    if (g > K1_POSR_GOSCIE) /* more processes with the card than K1_POSR_GOSCIE */
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
    if (capset != 0) /* without a capset (a context the guest kernel makes for an opener of the card): nothing */
        m->ctx[g] = c->hdr.ctx_id;
    naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA);
    return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
}

static void odmapuj(struct most *m, struct most_zasob *z)
{
    if (z->slot == 0)
        return;
    m->l->odmapuj(m->l->ja, z->slot, z->gpa, z->rozmiar);
    m->sloty[z->slot - MOST_SLOT_PIERWSZY] = 0;
    z->slot = 0;
}

static void usun(struct most *m, uint32_t res)
{
    struct most_zasob *z = &m->zasoby[res];
    odmapuj(m, z);
    if (z->rodzaj == MOST_BUFOR && z->fd >= 0)
        m->l->obraz_koniec(m->l->ja, res); /* 0.10.11 step 2: it was shown — the window forgets it */
    if (z->fd >= 0)
        m->l->zamknij(m->l->ja, z->fd);
    if (z->rodzaj == MOST_BUFOR && !m->padl)
        m->l->zwolnij(m->l->ja, res);
    memset(z, 0, sizeof *z);
    z->fd = -1;
}

static void koniec_goscia(struct most *m, uint32_t g)
{
    m->ctx[g] = 0;
    if (!m->padl)
        m->l->gosc_koniec(m->l->ja, g);
}

/* SUBMIT_3D: the stream into the window, the broker does it; with a fence the reply waits for it. */
static int zglos(struct most *m, const uint8_t *p, uint64_t n, uint8_t *odp, uint32_t *dl, struct most_czeka *c,
                 const char **powod)
{
    STRUKTURA(struct virtio_gpu_cmd_submit, s)
    uint32_t g = gosc(m, s.hdr.ctx_id);
    int r;
    if (n - sizeof s < s.size) {
        *powod = "virtio-gpu: strumień SUBMIT_3D dłuższy niż polecenie";
        return -1;
    }
    if (g == 0 || m->padl || s.size < sizeof(struct k1_posr_naglowek) || s.size > K1_POSR_PROSBY_BAJTY ||
        s.size % 4 != 0) {
        *dl = blad(m, odp, g == 0 ? VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID : VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
        return 0;
    }
    memcpy(m->l->okno, p + sizeof s, s.size);
    r = m->l->prosby(m->l->ja, g, s.size);
    if (r == MOST_PADL)
        most_padl(m);
    *dl = r == 0 ? (naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA), (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr))
                 : blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    if (!(s.hdr.flags & VIRTIO_GPU_FLAG_FENCE) || m->padl)
        return 0;
    c->gosc = g;
    c->pierscien = (s.hdr.flags & VIRTIO_GPU_FLAG_INFO_RING_IDX) ? s.hdr.ring_idx : 0;
    c->plot = s.hdr.fence_id;
    if (c->pierscien >= K1_POSR_PIERSCIENIE) {
        *dl = blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
        return 0;
    }
    m->l->plot(m->l->ja, g, c->pierscien, c->plot);
    return 1;
}

/* RESOURCE_CREATE_BLOB: only the card's memory of a guest context (HOST3D), no pages of the guest; blob 0 is the
 * context's shared memory (the VMM makes it), any other a buffer the context created just before (GEM_CREATE). */
static uint32_t nowy(struct most *m, const struct virtio_gpu_resource_create_blob *b, uint8_t *odp)
{
    uint32_t g = gosc(m, b->hdr.ctx_id), res = b->resource_id;
    struct most_zasob *z;
    int r, fd = -1;
    if (g == 0)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID);
    if (res == 0 || res >= K1_POSR_ZASOBY_MAKS || m->zasoby[res].rodzaj != MOST_BRAK)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    if (m->padl || b->blob_mem != VIRTIO_GPU_BLOB_MEM_HOST3D || b->nr_entries != 0 || b->size == 0 ||
        b->size % STRONA != 0 || b->size > K1_POSR_BUFOR_MAKS ||
        (b->blob_flags & ~(uint32_t)(VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE | VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE)) != 0 ||
        b->blob_id > UINT32_MAX ||
        (b->blob_id == 0 && (b->size < K1_POSR_WSPOLNA_MIN || b->size > K1_POSR_WSPOLNA_MAKS)))
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    r = b->blob_id == 0 ? m->l->wspolna(m->l->ja, g, b->size, &fd)
                        : m->l->blob(m->l->ja, g, res, b->blob_id, b->size);
    if (r == MOST_PADL)
        most_padl(m);
    if (r != 0)
        return blad(m, odp, r == K1_POSR_BLAD_PAMIEC ? VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY
                                                     : VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    z = &m->zasoby[res];
    z->rodzaj = b->blob_id == 0 ? MOST_WSPOLNA : MOST_BUFOR;
    z->gosc = (uint8_t)g, z->fd = fd, z->rozmiar = b->size;
    z->mapowalny = (b->blob_flags & VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE) != 0;
    naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA);
    return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
}

/* MAP_BLOB: the buffer at `offset` of the host-visible region (the guest kernel picks the place) — never over another
 * mapping, never outside the region; the reply says how the guest maps it (cached, write-combined). */
static uint32_t mapuj(struct most *m, const struct virtio_gpu_resource_map_blob *b, uint8_t *odp)
{
    struct most_zasob *z = b->resource_id >= 1 && b->resource_id < K1_POSR_ZASOBY_MAKS ? &m->zasoby[b->resource_id] : NULL;
    struct virtio_gpu_resp_map_info info;
    uint32_t mapa = VIRTIO_GPU_MAP_CACHE_CACHED, slot = 0;
    int fd;
    if (z == NULL || z->rodzaj == MOST_BRAK)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    if (m->padl || !z->mapowalny || z->slot != 0 || b->offset % STRONA != 0 || b->offset > MOST_OKNO_DL ||
        z->rozmiar > MOST_OKNO_DL - b->offset)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    for (uint32_t i = 1; i < K1_POSR_ZASOBY_MAKS; i++) {
        const struct most_zasob *o = &m->zasoby[i];
        if (o->slot != 0 && b->offset < o->gpa - m->okno_baza + o->rozmiar && o->gpa - m->okno_baza < b->offset + z->rozmiar)
            return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    }
    for (uint32_t i = 0; i < MOST_MAPY_MAKS && slot == 0; i++)
        if (!m->sloty[i])
            slot = MOST_SLOT_PIERWSZY + i;
    if (slot == 0)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
    if (z->rodzaj == MOST_WSPOLNA) {
        fd = z->fd;
    } else {
        int r = m->l->bufor(m->l->ja, b->resource_id, &fd, &mapa);
        if (r == MOST_PADL)
            most_padl(m);
        if (r != 0)
            return blad(m, odp, VIRTIO_GPU_RESP_ERR_UNSPEC);
    }
    if (m->l->mapuj(m->l->ja, slot, m->okno_baza + b->offset, fd, z->rozmiar) != 0) {
        if (z->rodzaj == MOST_BUFOR)
            m->l->zamknij(m->l->ja, fd);
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
    }
    if (z->rodzaj == MOST_BUFOR)
        m->l->zamknij(m->l->ja, fd); /* the mapping keeps the buffer */
    m->sloty[slot - MOST_SLOT_PIERWSZY] = 1;
    z->slot = slot, z->gpa = m->okno_baza + b->offset, z->mapa = (uint8_t)mapa;
    memset(&info, 0, sizeof info);
    info.hdr.type = VIRTIO_GPU_RESP_OK_MAP_INFO;
    info.map_info = mapa;
    memcpy(odp, &info, sizeof info);
    return (uint32_t)sizeof info;
}

/* SET_SCANOUT_BLOB (0.10.11 step 2, ADR 0051 T3, T4): the guest's display shows buffer res of the card — XRGB8888 or
 * ARGB8888 (the window is opaque), one plane, the whole picture (no part of it), inside the buffer. Its dma-buf is asked
 * for once (POSR_BUFOR) and kept until the resource goes; the window (okno.c) hands it to ekran. Resource 0: no buffer. */
static uint32_t pokaz_blob(struct most *m, const struct virtio_gpu_set_scanout_blob *b, uint8_t *odp)
{
    struct most_zasob *z = b->resource_id >= 1 && b->resource_id < K1_POSR_ZASOBY_MAKS ? &m->zasoby[b->resource_id] : NULL;
    struct most_obraz o = { b->resource_id, b->width, b->height, b->strides[0], b->offsets[0] };
    uint32_t mapa;
    if (b->scanout_id != 0)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID);
    if (b->resource_id == 0) {
        m->l->obraz(m->l->ja, NULL, -1);
        naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA);
        return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
    }
    if (z == NULL || z->rodzaj != MOST_BUFOR)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    if ((b->format != VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM && b->format != VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM) ||
        b->width == 0 || b->height == 0 || b->width > K1_POW_MAKS_BOK || b->height > K1_POW_MAKS_BOK ||
        b->r.x != 0 || b->r.y != 0 || b->r.width != b->width || b->r.height != b->height || b->strides[0] % 4 != 0 ||
        b->strides[0] < 4 * b->width || b->strides[0] > K1_STREFA_KROK_MAKS || b->offsets[0] % 4 != 0 ||
        b->offsets[0] > K1_STREFA_PRZES_MAKS ||
        (uint64_t)b->offsets[0] + (uint64_t)b->strides[0] * (b->height - 1) + 4u * (uint64_t)b->width > z->rozmiar)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    for (int i = 1; i < 4; i++)
        if (b->strides[i] != 0 || b->offsets[i] != 0)
            return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    if (z->fd < 0) {
        int r = m->padl ? MOST_PADL : m->l->bufor(m->l->ja, b->resource_id, &z->fd, &mapa);
        if (r == MOST_PADL)
            most_padl(m);
        if (r != 0) {
            z->fd = -1;
            return blad(m, odp, VIRTIO_GPU_RESP_ERR_UNSPEC);
        }
    }
    if (m->l->obraz(m->l->ja, &o, z->fd) != 0)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    m->obrazy++;
    naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA);
    return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
}

static uint32_t capset(struct most *m, const uint8_t *p, uint8_t *odp, uint32_t typ)
{
    struct virtio_gpu_get_capset_info i;
    struct virtio_gpu_get_capset c;
    if (typ == VIRTIO_GPU_CMD_GET_CAPSET_INFO) { /* (the length was checked: most_polecenie) */
        struct virtio_gpu_resp_capset_info r;
        memcpy(&i, p, sizeof i);
        if (i.capset_index != 0)
            return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
        memset(&r, 0, sizeof r);
        r.hdr.type = VIRTIO_GPU_RESP_OK_CAPSET_INFO;
        r.capset_id = VIRTIO_GPU_CAPSET_DRM;
        r.capset_max_version = 0;
        r.capset_max_size = sizeof m->caps;
        memcpy(odp, &r, sizeof r);
        return (uint32_t)sizeof r;
    }
    memcpy(&c, p, sizeof c);
    if (c.capset_id != VIRTIO_GPU_CAPSET_DRM || c.capset_version != 0)
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    naglowek(odp, VIRTIO_GPU_RESP_OK_CAPSET);
    memcpy(odp + sizeof(struct virtio_gpu_ctrl_hdr), &m->caps, sizeof m->caps);
    return (uint32_t)(sizeof(struct virtio_gpu_ctrl_hdr) + sizeof m->caps);
}

/* The commands of one resource: unmap, unref; and the contexts' attach/detach (one card for the zone: nothing to do). */
static uint32_t zasob(struct most *m, uint32_t typ, uint32_t res, uint32_t ctx, uint8_t *odp)
{
    struct most_zasob *z = res >= 1 && res < K1_POSR_ZASOBY_MAKS ? &m->zasoby[res] : NULL;
    if (typ == VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE || typ == VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE) {
        naglowek(odp, ctx != 0 ? VIRTIO_GPU_RESP_OK_NODATA : VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID);
        return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
    }
    if (z == NULL || z->rodzaj == MOST_BRAK || (typ == VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB && z->slot == 0))
        return blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    if (typ == VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB)
        odmapuj(m, z);
    else
        usun(m, res);
    naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA);
    return (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
}

int most_polecenie(struct most *m, const uint8_t *p, uint64_t n, uint8_t *odp, uint32_t *odp_dl, struct most_czeka *c,
                   const char **powod)
{
    STRUKTURA(struct virtio_gpu_ctrl_hdr, h)
    m->polecenia++;
    switch (h.type) {
    case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
    case VIRTIO_GPU_CMD_GET_CAPSET: {
        STRUKTURA(struct virtio_gpu_get_capset, g) /* both: the header and two 32-bit fields */
        (void)g;
        *odp_dl = capset(m, p, odp, h.type);
        return 0;
    }
    case VIRTIO_GPU_CMD_CTX_CREATE: {
        STRUKTURA(struct virtio_gpu_ctx_create, k)
        *odp_dl = kontekst(m, &k, odp);
        return 0;
    }
    case VIRTIO_GPU_CMD_CTX_DESTROY: {
        uint32_t g = gosc(m, h.ctx_id);
        if (g != 0)
            koniec_goscia(m, g);
        naglowek(odp, VIRTIO_GPU_RESP_OK_NODATA); /* a context without a capset: nothing was there */
        *odp_dl = (uint32_t)sizeof(struct virtio_gpu_ctrl_hdr);
        return 0;
    }
    case VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE:
    case VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE:
    case VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB:
    case VIRTIO_GPU_CMD_RESOURCE_UNREF: {
        STRUKTURA(struct virtio_gpu_resource_unref, u) /* every one of them: the header and resource_id */
        *odp_dl = zasob(m, h.type, u.resource_id, h.ctx_id, odp);
        return 0;
    }
    case VIRTIO_GPU_CMD_SUBMIT_3D:
        return zglos(m, p, n, odp, odp_dl, c, powod);
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB: {
        STRUKTURA(struct virtio_gpu_resource_create_blob, b)
        *odp_dl = nowy(m, &b, odp);
        return 0;
    }
    case VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB: {
        STRUKTURA(struct virtio_gpu_resource_map_blob, b)
        *odp_dl = mapuj(m, &b, odp);
        return 0;
    }
    case VIRTIO_GPU_CMD_SET_SCANOUT_BLOB: {
        STRUKTURA(struct virtio_gpu_set_scanout_blob, b)
        *odp_dl = pokaz_blob(m, &b, odp);
        return 0;
    }
    default: /* 3D of virgl (CREATE_3D, TRANSFER_*_3D), EDID, UUID: not offered */
        *odp_dl = blad(m, odp, VIRTIO_GPU_RESP_ERR_UNSPEC);
        return 0;
    }
}

void most_reset(struct most *m)
{
    for (uint32_t g = 1; g <= K1_POSR_GOSCIE; g++)
        if (m->ctx[g] != 0)
            koniec_goscia(m, g);
    for (uint32_t r = 1; r < K1_POSR_ZASOBY_MAKS; r++)
        if (m->zasoby[r].rodzaj != MOST_BRAK)
            usun(m, r);
}

/* What is mapped stays (a mapping holds its buffer even without the broker): the guest's driver only gets errors. */
void most_padl(struct most *m)
{
    m->padl = 1;
}

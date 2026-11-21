/* K1 strefa — virtio-gpu, 2D (virtio 1.2, section 5.7; ADR 0042 T6): the guest draws with its processor into its
 * own memory ("backing" pages), TRANSFER_TO_HOST_2D copies a rectangle into our copy of the resource, RESOURCE_FLUSH of
 * the shown resource tells the window (okno.c) which part changed. No EDID (none offered). The picture always comes this
 * way. With a graphics broker (0.10.9 step 6, fast graphics: gpu_lacze.c) the device also offers 3D — contexts with the
 * capset DRM, blob resources, the host-visible region — and the control queue is done by a thread of its own (the
 * broker answers in milliseconds); the 2D commands it does here, under the VMM's lock, the 3D ones gpu_most.c.
 * Limits: K1_GPU_ZASOBY resources, each at most 4096 × 4096, all together GPU_PAMIEC; every rectangle inside its
 * resource and every byte read inside the backing the guest gave (ERR_INVALID_PARAMETER otherwise); a command shorter
 * than its structure or without room for the answer ends the machine. */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <linux/virtio_gpu.h>
#include <linux/virtio_ids.h>

#include "gpu_most.h"
#include "k1_pomiar.h"
#include "okno.h"
#include "pomiar.h"
#include "strefa.h"
#include "tlo.h"

#define ZASOBY 16
#define BOK_MAKS 4096u
#define GPU_PAMIEC (96u << 20)
#define WPISY_MAKS 16384u /* backing entries of one resource (4 KiB pages: 64 MiB) */

struct zasob {
    uint32_t id, w, h;
    uint32_t *px;
    struct virtio_gpu_mem_entry *tlo; /* the backing: guest memory, checked when attached */
    uint64_t *od;                     /* od[i] = bytes of the backing before entry i */
    uint32_t n_tla;
};

static struct zasob zasoby[ZASOBY];
static uint32_t pokazany;            /* the resource on scanout 0 (0: none) */
static uint64_t zajete;              /* bytes of all resources */
static unsigned long polecenia, bledy;

static struct zasob *znajdz(uint32_t id)
{
    int i;
    for (i = 0; i < ZASOBY && id != 0; i++)
        if (zasoby[i].id == id)
            return &zasoby[i];
    return NULL;
}

static void zwolnij_tlo(struct zasob *z)
{
    free(z->tlo);
    free(z->od);
    z->tlo = NULL;
    z->od = NULL;
    z->n_tla = 0;
}

static void usun(struct zasob *z)
{
    zwolnij_tlo(z);
    free(z->px);
    zajete -= (uint64_t)z->w * z->h * 4;
    if (pokazany == z->id)
        pokazany = 0;
    memset(z, 0, sizeof *z);
}

const uint32_t *gpu_obraz(uint32_t *w, uint32_t *h)
{
    struct zasob *z = znajdz(pokazany);
    if (z == NULL)
        return NULL;
    *w = z->w;
    *h = z->h;
    return z->px;
}

/* Bytes [od, od + n) of the resource's backing into dst: 0, or -1 when they are not all inside it (tlo.c). */
static int z_tla(const struct zasob *z, uint64_t od, uint8_t *dst, uint64_t n)
{
    return tlo_czytaj(&mz.pamiec, z->tlo, z->od, z->n_tla, od, dst, n);
}

static int prostokat_w(const struct virtio_gpu_rect *r, const struct zasob *z)
{
    return r->width > 0 && r->height > 0 && r->x <= z->w && r->width <= z->w - r->x && r->y <= z->h &&
           r->height <= z->h - r->y;
}

static uint32_t utworz(const struct virtio_gpu_resource_create_2d *c)
{
    struct zasob *z = NULL;
    uint64_t bajty = (uint64_t)c->width * c->height * 4;
    int i;
    if (c->resource_id == 0 || znajdz(c->resource_id) != NULL || gpu_lacze_zasob(c->resource_id) || c->width == 0 ||
        c->height == 0 ||
        c->width > BOK_MAKS || c->height > BOK_MAKS ||
        (c->format != VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM && c->format != VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM))
        return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
    for (i = 0; i < ZASOBY && z == NULL; i++)
        if (zasoby[i].id == 0)
            z = &zasoby[i];
    if (z == NULL || zajete + bajty > GPU_PAMIEC)
        return VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
    z->px = calloc((size_t)bajty, 1);
    if (z->px == NULL)
        return VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
    z->id = c->resource_id;
    z->w = c->width;
    z->h = c->height;
    zajete += bajty;
    return VIRTIO_GPU_RESP_OK_NODATA;
}

/* ATTACH_BACKING: the entries follow the structure in the request; each must lie in guest memory. */
static uint32_t dolacz(const struct vq_lancuch *l, const struct virtio_gpu_resource_attach_backing *a)
{
    struct zasob *z = znajdz(a->resource_id);
    uint64_t suma = 0;
    uint32_t i;
    if (z == NULL || z->tlo != NULL || a->nr_entries == 0 || a->nr_entries > WPISY_MAKS ||
        l->dl_odczyt < sizeof *a + (uint64_t)a->nr_entries * sizeof(struct virtio_gpu_mem_entry))
        return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
    z->tlo = malloc(a->nr_entries * sizeof *z->tlo);
    z->od = malloc(a->nr_entries * sizeof *z->od);
    if (z->tlo == NULL || z->od == NULL) {
        zwolnij_tlo(z);
        return VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
    }
    vq_czytaj(l, sizeof *a, z->tlo, (uint64_t)a->nr_entries * sizeof *z->tlo);
    for (i = 0; i < a->nr_entries; i++) {
        if (gp_wsk(&mz.pamiec, z->tlo[i].addr, z->tlo[i].length) == NULL) {
            zwolnij_tlo(z);
            return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
        }
        z->od[i] = suma;
        suma += z->tlo[i].length;
    }
    z->n_tla = a->nr_entries;
    return VIRTIO_GPU_RESP_OK_NODATA;
}

static uint32_t przenies(const struct virtio_gpu_transfer_to_host_2d *t)
{
    struct zasob *z = znajdz(t->resource_id);
    uint64_t krok, t0 = k1_pomiar_teraz_us();
    uint32_t y;
    if (z == NULL || z->tlo == NULL || !prostokat_w(&t->r, z))
        return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
    krok = (uint64_t)z->w * 4;
    for (y = 0; y < t->r.height; y++)
        if (z_tla(z, t->offset + krok * y, (uint8_t *)(z->px + (size_t)(t->r.y + y) * z->w + t->r.x),
                  (uint64_t)t->r.width * 4) != 0)
            return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
    pomiar_przeniesienie(k1_pomiar_teraz_us() - t0); /* 0.10.11 step 1 */
    return VIRTIO_GPU_RESP_OK_NODATA;
}

static uint32_t pokaz(const struct virtio_gpu_set_scanout *s)
{
    struct zasob *z = znajdz(s->resource_id);
    if (s->scanout_id != 0)
        return VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
    if (s->resource_id != 0 && z == NULL)
        return VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
    pokazany = s->resource_id;
    if (z != NULL)
        okno_zmiana(0, 0, z->w, z->h);
    return VIRTIO_GPU_RESP_OK_NODATA;
}

static uint32_t splucz(const struct virtio_gpu_resource_flush *f)
{
    struct zasob *z = znajdz(f->resource_id);
    if (z == NULL || !prostokat_w(&f->r, z))
        return VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
    if (f->resource_id == pokazany) {
        okno_zmiana(f->r.x, f->r.y, f->r.width, f->r.height);
        pomiar_obraz();
    }
    return VIRTIO_GPU_RESP_OK_NODATA;
}

/* The structure of a command (its whole size read from the chain) or -1 when the chain is shorter. */
#define POLECENIE(typ, zm)                                                                                            \
    typ zm;                                                                                                          \
    if (vq_czytaj(l, 0, &zm, sizeof zm) != sizeof zm)                                                               \
        return -1;

int gpu_2d(const struct vq_lancuch *l, uint32_t typ, uint32_t *wynik, struct virtio_gpu_resp_display_info *info)
{
    uint32_t w, h;
    switch (typ) {
    case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
        okno_rozmiar(&w, &h);
        info->pmodes[0].r.width = w;
        info->pmodes[0].r.height = h;
        info->pmodes[0].enabled = 1;
        *wynik = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
        return 0;
    case VIRTIO_GPU_CMD_RESOURCE_CREATE_2D: {
        POLECENIE(struct virtio_gpu_resource_create_2d, c)
        *wynik = utworz(&c);
        return 0;
    }
    case VIRTIO_GPU_CMD_RESOURCE_UNREF: {
        POLECENIE(struct virtio_gpu_resource_unref, u)
        if (znajdz(u.resource_id) == NULL)
            *wynik = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        else
            usun(znajdz(u.resource_id));
        return 0;
    }
    case VIRTIO_GPU_CMD_SET_SCANOUT: {
        POLECENIE(struct virtio_gpu_set_scanout, s)
        *wynik = pokaz(&s);
        return 0;
    }
    case VIRTIO_GPU_CMD_RESOURCE_FLUSH: {
        POLECENIE(struct virtio_gpu_resource_flush, f)
        *wynik = splucz(&f);
        return 0;
    }
    case VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D: {
        POLECENIE(struct virtio_gpu_transfer_to_host_2d, t)
        *wynik = przenies(&t);
        return 0;
    }
    case VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING: {
        POLECENIE(struct virtio_gpu_resource_attach_backing, a)
        *wynik = dolacz(l, &a);
        return 0;
    }
    case VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING: {
        POLECENIE(struct virtio_gpu_resource_detach_backing, d)
        if (znajdz(d.resource_id) == NULL)
            *wynik = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
        else
            zwolnij_tlo(znajdz(d.resource_id));
        return 0;
    }
    case VIRTIO_GPU_CMD_UPDATE_CURSOR:
    case VIRTIO_GPU_CMD_MOVE_CURSOR:
        return 0; /* K1 draws its own cursor */
    default:
        *wynik = VIRTIO_GPU_RESP_ERR_UNSPEC;
        return 0;
    }
}

/* Is the command of type `typ` (resource `id`) one of 2D (this file) — else 3D (gpu_most.c)? Under mz.zamek. */
int gpu_jest_2d(uint32_t typ, uint32_t id)
{
    return (typ >= VIRTIO_GPU_CMD_GET_DISPLAY_INFO && typ <= VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING &&
            typ != VIRTIO_GPU_CMD_RESOURCE_UNREF) ||
           (typ == VIRTIO_GPU_CMD_RESOURCE_UNREF && znajdz(id) != NULL) || typ == VIRTIO_GPU_CMD_UPDATE_CURSOR ||
           typ == VIRTIO_GPU_CMD_MOVE_CURSOR;
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w, bylo = 0;
    if (k == 0 && gpu_lacze_jest()) { /* fast graphics: the control queue is the bridge's thread's */
        gpu_lacze_obudz(u);
        return 0;
    }
    while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
        struct virtio_gpu_ctrl_hdr h;
        struct virtio_gpu_resp_display_info odp;
        uint32_t wynik = VIRTIO_GPU_RESP_OK_NODATA;
        uint64_t dl;
        memset(&odp, 0, sizeof odp);
        if (vq_czytaj(&l, 0, &h, sizeof h) != sizeof h || gpu_2d(&l, h.type, &wynik, &odp) != 0) {
            *powod = "virtio-gpu: polecenie krótsze niż jego struktura";
            return -1;
        }
        polecenia++;
        bylo = 1;
        if (k == 1) { /* the cursor queue: no answers */
            vq_oddaj(&u->q[k], u->pamiec, l.glowa, 0);
            continue;
        }
        bledy += wynik >= VIRTIO_GPU_RESP_ERR_UNSPEC;
        odp.hdr.type = wynik;
        odp.hdr.flags = h.flags & VIRTIO_GPU_FLAG_FENCE;
        odp.hdr.fence_id = h.fence_id;
        odp.hdr.ctx_id = h.ctx_id;
        dl = wynik == VIRTIO_GPU_RESP_OK_DISPLAY_INFO ? sizeof odp : sizeof odp.hdr;
        if (l.dl_zapis < dl) {
            *powod = "virtio-gpu: brak miejsca na odpowiedź";
            return -1;
        }
        vq_zapisz(&l, 0, &odp, dl);
        vq_oddaj(&u->q[k], u->pamiec, l.glowa, (uint32_t)dl);
    }
    if (bylo)
        mmio_uzyte(u);
    return w;
}

/* 0.10.5 step 4: events_read — VIRTIO_GPU_EVENT_DISPLAY after ekran gave the window a new size (gpu_zmiana_ekranu);
 * the guest's kernel asks for the display again and tells its programs (a hotplug event), then clears it. */
static uint32_t zdarzenia;

static void konfig(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    /* by name: Linux 7.x added blob_alignment (it means something only with VIRTIO_GPU_F_BLOB_ALIGNMENT, not offered) */
    struct virtio_gpu_config c = { .events_read = zdarzenia, .num_scanouts = 1, .num_capsets = gpu_lacze_jest() ? 1u : 0u };
    (void)u;
    memcpy(dst, (const uint8_t *)&c + od, n);
}

static int konfig_pisz(struct mmio_urz *u, uint32_t od, const uint8_t *src, uint32_t n, const char **powod)
{
    uint32_t v = 0;
    (void)u, (void)powod;
    if (od == offsetof(struct virtio_gpu_config, events_clear) && n == sizeof v) {
        memcpy(&v, src, sizeof v);
        zdarzenia &= ~v;
    }
    return 0; /* anything else there is read-only: ignored */
}

void gpu_zmiana_ekranu(void)
{
    zdarzenia |= VIRTIO_GPU_EVENT_DISPLAY;
    mmio_konfig_zmiana(&mz.urz[URZ_EKRAN]);
}

/* The counters for the log (0.10.1 step 1, S13): resources now and their MiB (GPU_PAMIEC at most). */
void gpu_liczniki(unsigned *n, unsigned *mib)
{
    unsigned i;
    *n = 0;
    for (i = 0; i < ZASOBY; i++)
        *n += zasoby[i].id != 0;
    *mib = (unsigned)(zajete >> 20);
}

static void reset(struct mmio_urz *u)
{
    int i;
    for (i = 0; i < ZASOBY; i++)
        if (zasoby[i].id != 0)
            usun(&zasoby[i]);
    pokazany = 0;
    gpu_lacze_reset(u);
}

const struct mmio_ops gpu_ops = {
    .id = VIRTIO_ID_GPU,
    .n_kolejek = 2, /* control, cursor */
    .rozmiar_konfig = sizeof(struct virtio_gpu_config),
    .konfig_czytaj = konfig,
    .konfig_pisz = konfig_pisz,
    .powiadom = powiadom,
    .reset = reset,
    .cechy_dodatkowe = gpu_lacze_cechy,
    .pamiec_dzielona = gpu_lacze_okno,
};

/* K1 strefa — the virtio-mmio transport (mmio.h). */
#include "mmio.h"

#include <string.h>

#include <linux/virtio_config.h>
#include <linux/virtio_mmio.h>

#define MAGIA 0x74726976u        /* "virt" */
#define DOSTAWCA 0x4b31u         /* "K1" */
#define KONFIG VIRTIO_MMIO_CONFIG /* 0x100: the device's configuration space */
#define BRAK_PAMIECI_DZIELONEJ 0xffffffffu

static uint64_t cechy_urzadzenia(const struct mmio_urz *u)
{
    return u->ops->cechy | (u->ops->cechy_dodatkowe != NULL ? u->ops->cechy_dodatkowe(u) : 0) |
           (1ull << VIRTIO_F_VERSION_1);
}

/* SHMLenLow/High, SHMBaseLow/High of the region SHMSel names: all ones when there is none (virtio 1.2, 4.2.2). */
static uint32_t pamiec_dzielona(const struct mmio_urz *u, uint64_t od)
{
    uint64_t baza = 0, dl = 0;
    if (u->ops->pamiec_dzielona == NULL || u->ops->pamiec_dzielona(u, u->shm_sel, &baza, &dl) != 0)
        return BRAK_PAMIECI_DZIELONEJ;
    switch (od) {
    case VIRTIO_MMIO_SHM_LEN_LOW:
        return (uint32_t)dl;
    case VIRTIO_MMIO_SHM_LEN_HIGH:
        return (uint32_t)(dl >> 32);
    case VIRTIO_MMIO_SHM_BASE_LOW:
        return (uint32_t)baza;
    default:
        return (uint32_t)(baza >> 32);
    }
}

static void reset(struct mmio_urz *u)
{
    u->status = u->przerwanie = 0;
    u->cechy_sel = u->cechy_ster_sel = u->kolejka_sel = 0;
    u->cechy_ster = 0;
    memset(u->q, 0, sizeof u->q);
    if (u->ops->reset != NULL)
        u->ops->reset(u);
}

void mmio_start(struct mmio_urz *u, const struct mmio_ops *ops, void *stan, const struct gosc_pamiec *m,
                void (*przerwij)(struct mmio_urz *u))
{
    memset(u, 0, sizeof *u);
    u->ops = ops;
    u->stan = stan;
    u->pamiec = m;
    u->przerwij = przerwij;
    reset(u);
}

int mmio_dziala(const struct mmio_urz *u, int k)
{
    return (u->status & VIRTIO_CONFIG_S_DRIVER_OK) && k >= 0 && k < u->ops->n_kolejek && u->q[k].gotowa;
}

void mmio_uzyte(struct mmio_urz *u)
{
    u->przerwanie |= VIRTIO_MMIO_INT_VRING;
    if (u->przerwij != NULL)
        u->przerwij(u);
}

void mmio_konfig_zmiana(struct mmio_urz *u)
{
    u->konfig_gen++;
    u->przerwanie |= VIRTIO_MMIO_INT_CONFIG;
    if (u->przerwij != NULL)
        u->przerwij(u);
}

static struct virtq *wybrana(struct mmio_urz *u)
{
    return u->kolejka_sel < (uint32_t)u->ops->n_kolejek ? &u->q[u->kolejka_sel] : NULL;
}

static uint32_t rejestr(struct mmio_urz *u, uint64_t od)
{
    struct virtq *q = wybrana(u);
    switch (od) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        return MAGIA;
    case VIRTIO_MMIO_VERSION:
        return 2;
    case VIRTIO_MMIO_DEVICE_ID:
        return u->ops->id;
    case VIRTIO_MMIO_VENDOR_ID:
        return DOSTAWCA;
    case VIRTIO_MMIO_DEVICE_FEATURES:
        return u->cechy_sel < 2 ? (uint32_t)(cechy_urzadzenia(u) >> (32 * u->cechy_sel)) : 0;
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        return q != NULL ? VQ_MAKS : 0;
    case VIRTIO_MMIO_QUEUE_READY:
        return q != NULL ? (uint32_t)q->gotowa : 0;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        return u->przerwanie;
    case VIRTIO_MMIO_STATUS:
        return u->status;
    case VIRTIO_MMIO_CONFIG_GENERATION:
        return u->konfig_gen;
    case VIRTIO_MMIO_SHM_LEN_LOW:
    case VIRTIO_MMIO_SHM_LEN_HIGH:
    case VIRTIO_MMIO_SHM_BASE_LOW:
    case VIRTIO_MMIO_SHM_BASE_HIGH:
        return pamiec_dzielona(u, od);
    default:
        return 0;
    }
}

int mmio_czytaj(struct mmio_urz *u, uint64_t od, uint8_t *dst, uint32_t dl, const char **powod)
{
    if (od >= KONFIG) {
        uint64_t k = od - KONFIG;
        memset(dst, 0, dl);
        if (dl != 1 && dl != 2 && dl != 4 && dl != 8) {
            *powod = "zły rozmiar odczytu konfiguracji virtio";
            return -1;
        }
        if (k < u->ops->rozmiar_konfig && dl <= u->ops->rozmiar_konfig - k && u->ops->konfig_czytaj != NULL)
            u->ops->konfig_czytaj(u, (uint32_t)k, dst, dl);
        return 0;
    }
    if (dl != 4 || (od & 3) != 0) {
        *powod = "odczyt rejestru virtio-mmio nie po 4 bajty";
        return -1;
    }
    {
        uint32_t v = rejestr(u, od);
        memcpy(dst, &v, 4);
    }
    return 0;
}

/* FEATURES_OK only when the driver took VERSION_1 and nothing the device did not offer. */
static uint32_t nowy_status(const struct mmio_urz *u, uint32_t v)
{
    if ((v & VIRTIO_CONFIG_S_FEATURES_OK) && !(u->status & VIRTIO_CONFIG_S_FEATURES_OK) &&
        ((u->cechy_ster & ~cechy_urzadzenia(u)) != 0 || !(u->cechy_ster & (1ull << VIRTIO_F_VERSION_1))))
        v &= ~(uint32_t)VIRTIO_CONFIG_S_FEATURES_OK;
    return v;
}

/* A new status. When the driver sets DRIVER_OK the device looks into every ready queue: a notification sent before it
 * was not handled then (QUEUE_NOTIFY needs DRIVER_OK), and Linux's balloon puts its first statistics buffer in and
 * notifies before virtio_device_ready (drivers/virtio/virtio_balloon.c: init_vqs, then virtballoon_probe's end) — it
 * waits for that buffer back and sends no statistics until then (0.10.1 step 3: the guest's free memory was never
 * known). */
static int status(struct mmio_urz *u, uint32_t v, const char **powod)
{
    int byl = (u->status & VIRTIO_CONFIG_S_DRIVER_OK) != 0;
    u->status = nowy_status(u, v);
    for (int k = 0; !byl && k < u->ops->n_kolejek; k++)
        if (mmio_dziala(u, k) && u->ops->powiadom(u, k, powod) < 0)
            return -1;
    return 0;
}

/* A half of a 64-bit guest address of the selected queue (only while it is not ready). */
static void adres(struct mmio_urz *u, uint64_t od, uint32_t v)
{
    struct virtq *q = wybrana(u);
    uint64_t *cel;
    if (q == NULL || q->gotowa)
        return;
    cel = od < VIRTIO_MMIO_QUEUE_AVAIL_LOW ? &q->desc : od < VIRTIO_MMIO_QUEUE_USED_LOW ? &q->avail : &q->used;
    if (od & 4)
        *cel = (*cel & 0xffffffffull) | ((uint64_t)v << 32);
    else
        *cel = (*cel & ~0xffffffffull) | v;
}

static int gotowosc(struct mmio_urz *u, uint32_t v, const char **powod)
{
    struct virtq *q = wybrana(u);
    if (q == NULL)
        return 0;
    if (v == 0) {
        q->gotowa = 0;
        return 0;
    }
    if (q->gotowa)
        return 0;
    *powod = vq_sprawdz(q, u->pamiec);
    if (*powod != NULL)
        return -1;
    q->ost_avail = q->used_idx = 0;
    q->gotowa = 1;
    return 0;
}

int mmio_powiadomienie(struct mmio_urz *u, uint32_t kolejka, const char **powod)
{
    u->powiadomienia++;
    if (kolejka > INT32_MAX || !mmio_dziala(u, (int)kolejka))
        return 0;
    return u->ops->powiadom(u, (int)kolejka, powod);
}

static int zapisz_rejestr(struct mmio_urz *u, uint64_t od, uint32_t v, const char **powod)
{
    struct virtq *q = wybrana(u);
    switch (od) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        u->cechy_sel = v;
        return 0;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        if (u->cechy_ster_sel < 2 && !(u->status & VIRTIO_CONFIG_S_FEATURES_OK)) {
            unsigned p = 32 * u->cechy_ster_sel;
            u->cechy_ster = (u->cechy_ster & ~(0xffffffffull << p)) | ((uint64_t)v << p);
        }
        return 0;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        u->cechy_ster_sel = v;
        return 0;
    case VIRTIO_MMIO_SHM_SEL:
        u->shm_sel = v;
        return 0;
    case VIRTIO_MMIO_QUEUE_SEL:
        u->kolejka_sel = v;
        return 0;
    case VIRTIO_MMIO_QUEUE_NUM:
        if (q != NULL && !q->gotowa)
            q->rozmiar = v <= VQ_MAKS ? (uint16_t)v : 0; /* checked when it becomes ready */
        return 0;
    case VIRTIO_MMIO_QUEUE_READY:
        return gotowosc(u, v, powod);
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        return mmio_powiadomienie(u, v, powod);
    case VIRTIO_MMIO_INTERRUPT_ACK:
        u->przerwanie &= ~v;
        return 0;
    case VIRTIO_MMIO_STATUS:
        if (v != 0)
            return status(u, v, powod);
        reset(u);
        return 0;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
    case VIRTIO_MMIO_QUEUE_USED_LOW:
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        adres(u, od, v);
        return 0;
    default:
        return 0; /* read-only and unknown registers: the write has no effect */
    }
}

int mmio_pisz(struct mmio_urz *u, uint64_t od, const uint8_t *src, uint32_t dl, const char **powod)
{
    uint32_t v;
    if (od >= KONFIG) {
        uint64_t k = od - KONFIG;
        if (dl != 1 && dl != 2 && dl != 4 && dl != 8) {
            *powod = "zły rozmiar zapisu konfiguracji virtio";
            return -1;
        }
        if (k < u->ops->rozmiar_konfig && dl <= u->ops->rozmiar_konfig - k && u->ops->konfig_pisz != NULL)
            return u->ops->konfig_pisz(u, (uint32_t)k, src, dl, powod);
        return 0;
    }
    if (dl != 4 || (od & 3) != 0) {
        *powod = "zapis rejestru virtio-mmio nie po 4 bajty";
        return -1;
    }
    memcpy(&v, src, 4);
    return zapisz_rejestr(u, od, v, powod);
}

/* K1 strefa — guest memory and the split virtqueue (virtio 1.2, section 2.7), checked field by field (ADR 0042,
 * invariant 6). Everything here comes from the guest: an address or a length outside its memory, a loop of
 * descriptors, a chain longer than the queue, an indirect descriptor (not offered), a readable descriptor after a
 * writable one — the request is refused and the machine ends (vq_wez < 0). Pure functions without system calls, tested
 * on the host (testy/host/test_virtio.c) and fuzzed (fuzz_virtq). */
#ifndef K1_STREFA_VIRTQ_H
#define K1_STREFA_VIRTQ_H

#include <stdint.h>

/* The guest's memory as the VMM sees it: one block in the VMM (baza, rozmiar bytes — one memfd), in the guest's
 * addresses from 0 below the devices (ADR 0042) and, when it is larger than that (0.10.1 step 2, ADR 0044 T5), the
 * rest from GP_GORA: `dol` bytes from 0 (0: all of it), rozmiar − dol from 4 GiB. A range never crosses the two. */
#define GP_GORA (4ull << 30)
struct gosc_pamiec {
    uint8_t *baza;
    uint64_t rozmiar;
    uint64_t dol;
};

/* [adres, adres + dl) of guest memory, or NULL when any byte of it is outside (or in the hole below 4 GiB). */
static inline uint8_t *gp_wsk(const struct gosc_pamiec *m, uint64_t adres, uint64_t dl)
{
    uint64_t dol = m->dol != 0 && m->dol < m->rozmiar ? m->dol : m->rozmiar, gora = m->rozmiar - dol;
    if (adres <= dol && dl <= dol - adres)
        return m->baza + adres;
    if (adres >= GP_GORA && adres - GP_GORA <= gora && dl <= gora - (adres - GP_GORA))
        return m->baza + dol + (adres - GP_GORA);
    return (uint8_t *)0;
}

#define VQ_MAKS 128 /* the queue size every device offers (QueueNumMax); also the longest chain */

struct virtq {
    uint16_t rozmiar;      /* QueueNum the driver set (a power of 2, 1..VQ_MAKS) */
    int gotowa;            /* QueueReady */
    uint64_t desc, avail, used; /* guest addresses of the three parts */
    uint16_t ost_avail;    /* the next entry of the available ring we take */
    uint16_t used_idx;     /* the next entry of the used ring we fill */
};

struct vq_kawalek {
    uint8_t *p;            /* in guest memory (checked) */
    uint32_t dl;
    int zapis;             /* written by the device (VIRTQ_DESC_F_WRITE) */
};

struct vq_lancuch {
    uint16_t glowa;        /* the first descriptor: the id the used ring gets back */
    int n, n_odczyt;       /* pieces; how many of them (the first ones) the device reads */
    uint64_t dl_odczyt, dl_zapis; /* bytes the device may read / write */
    struct vq_kawalek k[VQ_MAKS];
};

/* Whether the queue the driver describes can be used: size a power of 2 up to VQ_MAKS, the three parts aligned
 * (16, 2, 4) and inside guest memory. NULL: good; else why not. */
const char *vq_sprawdz(const struct virtq *q, const struct gosc_pamiec *m);

/* The next chain the driver made available: 1 (*l filled), 0 nothing new, -1 refused (*powod says why). */
int vq_wez(struct virtq *q, const struct gosc_pamiec *m, struct vq_lancuch *l, const char **powod);

/* Gives chain `glowa` back with `zapisane` bytes written by the device. */
void vq_oddaj(struct virtq *q, const struct gosc_pamiec *m, uint16_t glowa, uint32_t zapisane);

/* Copies up to `n` bytes the device may read from the chain, from byte `od` of its readable part; returns how many. */
uint64_t vq_czytaj(const struct vq_lancuch *l, uint64_t od, void *dst, uint64_t n);
/* Writes up to `n` bytes into the writable part of the chain from its byte `od`; returns how many. */
uint64_t vq_zapisz(const struct vq_lancuch *l, uint64_t od, const void *src, uint64_t n);

#endif

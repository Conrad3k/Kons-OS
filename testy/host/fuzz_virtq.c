/* libFuzzer harness for the zones' virtqueues and virtio-mmio registers (0.10.0 step 1, ADR 0042): the guest's memory
 * (descriptor table, available ring, buffers) and its register writes are the input. Every chain taken lies whole in
 * guest memory, is no longer than the queue and has its readable pieces first; copying in and out of it never leaves
 * it; the registers never crash (the device's handler here only takes and gives back chains). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <linux/virtio_mmio.h>

#include "mmio.h"
#include "virtq.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define PAMIEC 0x8000u

static uint8_t pam[PAMIEC];
static const struct gosc_pamiec m = { pam, PAMIEC };

static void sprawdz(const struct virtq *q, const struct vq_lancuch *l)
{
    uint64_t r = 0, z = 0;
    uint8_t b[64];
    int i;
    if (l->n < 1 || l->n > q->rozmiar || l->n_odczyt > l->n)
        abort();
    for (i = 0; i < l->n; i++) {
        const struct vq_kawalek *k = &l->k[i];
        if (k->p < pam || k->p + k->dl > pam + PAMIEC || (i < l->n_odczyt) == k->zapis)
            abort();
        if (k->zapis)
            z += k->dl;
        else
            r += k->dl;
    }
    if (r != l->dl_odczyt || z != l->dl_zapis)
        abort();
    vq_czytaj(l, l->dl_odczyt / 2, b, sizeof b);
    vq_zapisz(l, l->dl_zapis / 2, b, sizeof b);
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w, n = 0;
    while (n++ < 2 * VQ_MAKS && (w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
        sprawdz(&u->q[k], &l);
        vq_oddaj(&u->q[k], u->pamiec, l.glowa, (uint32_t)l.dl_zapis);
    }
    return 0;
}

static const struct mmio_ops ops = { .id = 4, .n_kolejek = 2, .powiadom = powiadom };

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct mmio_urz u;
    const char *p = NULL;
    size_t i;
    /* the first half of the input: guest memory; the second: register writes of 5 bytes (offset/4, value) */
    size_t pol = size / 2 < PAMIEC ? size / 2 : PAMIEC;
    memset(pam, 0, sizeof pam);
    memcpy(pam, data, pol);
    mmio_start(&u, &ops, NULL, &m, NULL);
    for (i = pol; i + 5 <= size; i += 5) {
        uint32_t v;
        uint64_t od = (uint64_t)data[i] * 4;
        memcpy(&v, data + i + 1, 4);
        if (mmio_pisz(&u, od, (const uint8_t *)&v, 4, &p) != 0)
            break; /* the machine would end here */
    }
    return 0;
}

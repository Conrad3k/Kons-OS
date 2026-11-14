/* K1 strefa — the virtio-mmio transport (virtio 1.2, section 4.2.2; version 2, no legacy): the registers of one device
 * at a 4 KiB window of guest addresses. The guest's reads and writes come here from the processor threads (under the
 * VMM's lock, kvm.c); a device gets "notify queue n", "config read/write" and "reset". Pure (no system calls but the
 * interrupt callback), tested on the host (test_virtio.c). */
#ifndef K1_STREFA_MMIO_H
#define K1_STREFA_MMIO_H

#include <stdint.h>

#include "virtq.h"

#define MMIO_ROZMIAR 0x1000u  /* each device's window */
#define MMIO_KOLEJKI_MAKS 4   /* queues of one device (gpu, vsock, input: 2–3) */

struct mmio_urz;

/* What one device type does. Each returns 0, or -1 when the guest did something only a broken or hostile driver does
 * (*powod, a constant text, says what) — the machine ends then. */
struct mmio_ops {
    uint32_t id;                   /* virtio device id (VIRTIO_ID_*) */
    uint64_t cechy;                /* the device's feature bits (VIRTIO_F_VERSION_1 is added by the transport) */
    int n_kolejek;
    uint32_t rozmiar_konfig;       /* bytes of its configuration space */
    void (*konfig_czytaj)(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n);
    int (*konfig_pisz)(struct mmio_urz *u, uint32_t od, const uint8_t *src, uint32_t n, const char **powod);
    int (*powiadom)(struct mmio_urz *u, int kolejka, const char **powod); /* the driver notified queue `kolejka` */
    void (*reset)(struct mmio_urz *u);                                     /* status 0, or the start */
};

struct mmio_urz {
    const struct mmio_ops *ops;
    void *stan;                    /* the device's own state */
    const struct gosc_pamiec *pamiec;
    void (*przerwij)(struct mmio_urz *u); /* raise the device's interrupt line (kvm.c: its irqfd) */
    uint32_t status, przerwanie;   /* Status, InterruptStatus */
    uint32_t cechy_sel, cechy_ster_sel, kolejka_sel, konfig_gen;
    uint64_t cechy_ster;           /* what the driver accepted */
    struct virtq q[MMIO_KOLEJKI_MAKS];
    unsigned long powiadomienia;   /* notifications handled (Zasoby/log) */
};

void mmio_start(struct mmio_urz *u, const struct mmio_ops *ops, void *stan, const struct gosc_pamiec *m,
                void (*przerwij)(struct mmio_urz *u));

/* A read of `dl` bytes (1, 2, 4; registers: 4 only) at `od` in the window into dst: 0, or -1 (the access is invalid —
 * the machine ends, *powod). */
int mmio_czytaj(struct mmio_urz *u, uint64_t od, uint8_t *dst, uint32_t dl, const char **powod);
int mmio_pisz(struct mmio_urz *u, uint64_t od, const uint8_t *src, uint32_t dl, const char **powod);

/* The device used buffers of a queue: InterruptStatus bit 0 and the interrupt. */
void mmio_uzyte(struct mmio_urz *u);
/* Its configuration changed: InterruptStatus bit 1, the generation counter, the interrupt. */
void mmio_konfig_zmiana(struct mmio_urz *u);
/* The driver is ready (DRIVER_OK) and queue `k` too. */
int mmio_dziala(const struct mmio_urz *u, int k);

#endif

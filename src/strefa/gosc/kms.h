/* K1 — the display of a zone's guest (kms.c), shared by gosc and kompozytor. */
#ifndef K1_KMS_H
#define K1_KMS_H

#include <stdint.h>

struct kms {
    uint32_t *px;
    uint32_t szer, wys, krok; /* krok in pixels */
};
int kms_start(struct kms *k);                                  /* 0, or -1 (logged with kms_log) */
/* 0.10.5 step 4: K1 gave the zone's window a new size — the zone kernel says so with a hotplug event (uevent). The
 * descriptor to wait on (-1: none), and after it is readable: 1 and *k with a buffer of the new size (the old one is
 * gone), 0 nothing changed. */
int kms_zmiany_fd(void);
int kms_zmiana(struct kms *k);
void kms_pokaz(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* this rectangle changed */
/* 0.10.11 step 2 (ADR 0051): a buffer of the card (its dma-buf, of the display's size) shown as the display itself — the
 * zone kernel sends SET_SCANOUT_BLOB, the VMM hands it to ekran; 0, or -1 (the card refused it: draw into our own). Back
 * to our own buffer (before kms_pokaz of it): kms_pokaz_swoj. kms_zapomnij: that dma-buf goes. */
int kms_pokaz_bufor(int dmabuf, uint32_t w, uint32_t h, uint32_t krok, uint32_t przes);
int kms_pokaz_swoj(void);
void kms_zapomnij(int dmabuf);
void kms_log(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* each program its own */

#endif

/* K1 — the display of a zone's guest (kms.c), shared by gosc and kompozytor. */
#ifndef K1_KMS_H
#define K1_KMS_H

#include <stdint.h>

struct kms {
    uint32_t *px;
    uint32_t szer, wys, krok; /* krok in pixels */
};
int kms_start(struct kms *k);                                  /* 0, or -1 (logged with kms_log) */
void kms_pokaz(uint32_t x, uint32_t y, uint32_t w, uint32_t h); /* this rectangle changed */
void kms_log(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* each program its own */

#endif

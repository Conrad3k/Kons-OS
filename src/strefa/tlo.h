/* K1 strefa — the backing of a virtio-gpu resource: the guest's pages the guest draws into (ATTACH_BACKING), read when
 * it asks for a TRANSFER_TO_HOST_2D. Every address goes through gp_wsk (virtq.h): above 3 GiB the guest's memory is in
 * two parts (ADR 0044) and the upper one lies in the VMM's memory right after the lower one — not at its guest address
 * (until 0.10.5 the copy did not, and the browser's window was black on an 8 GB X230). Pure: tested on the host
 * (testy/host/test_tlo.c). */
#ifndef K1_STREFA_TLO_H
#define K1_STREFA_TLO_H

#include <stdint.h>

#include <linux/virtio_gpu.h>

#include "virtq.h"

/* Bytes [od, od + n) of the backing `tlo` (n_tla entries; od_wpisu[i] = bytes before entry i) into dst: 0, or -1 when
 * they are not all inside it or an entry is not inside the guest's memory. */
int tlo_czytaj(const struct gosc_pamiec *m, const struct virtio_gpu_mem_entry *tlo, const uint64_t *od_wpisu,
               uint32_t n_tla, uint64_t od, uint8_t *dst, uint64_t n);

#endif

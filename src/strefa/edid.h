/* K1 strefa — the EDID of the zone's display (0.11.0 step 1, T9): one detailed timing of exactly the window's size.
 * Without it the zone's kernel makes its mode with drm_cvt_mode, which rounds the width down to a multiple of 8
 * (drivers/gpu/drm/virtio/virtgpu_display.c, drm_modes.c CVT_H_GRANULARITY) — the browser's picture was up to 7 px
 * narrower than its window (1366 → 1360 on the X230). With VIRTIO_GPU_F_EDID the kernel takes the modes of the EDID
 * only (drm_edid_connector_add_modes), and a detailed timing keeps its width as it is (drm_edid.c, drm_mode_detailed).
 * Pure: tested on the host (testy/host/test_edid.c). */
#ifndef K1_STREFA_EDID_H
#define K1_STREFA_EDID_H

#include <stdint.h>

#define EDID_DL 128u
#define EDID_BOK_MAKS 4095u /* 12 bits of a detailed timing's active pixels */
#define EDID_BOK_MIN 64u    /* drm_mode_detailed ignores smaller ones */

/* EDID 1.4 of one block for a display of w × h (EDID_BOK_MIN..EDID_BOK_MAKS each): 0, or -1 for another size (the zone's
 * kernel then makes its mode from the display info as before). */
int edid_zbuduj(uint8_t e[EDID_DL], uint32_t w, uint32_t h);

#endif

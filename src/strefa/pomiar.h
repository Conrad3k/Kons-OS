/* K1 strefa — the measurement of the picture's way (0.10.11 step 1, plan T1): where the time goes between "the card
 * drew the page" and "the picture is on K1's screen". The guest's compositor measures its links and sends "pomiar …"
 * (vsock.c passes the numbers here); the VMM measures its own: TRANSFER_TO_HOST_2D (the copy from the guest's memory),
 * the copy into the window, the wait for ekran (from a commit to its RAMKA), the bridge's requests to the graphics
 * broker (there and back), and the time from a key, a move or a wheel to the guest's next picture (within 1 s; later
 * the input changed nothing on the screen). Two lines every
 * POMIAR_ODSTEP s while pictures come; numbers only (invariant 8). Every function takes its own lock (any thread). */
#ifndef K1_STREFA_POMIAR_H
#define K1_STREFA_POMIAR_H

#include <stdint.h>

#define POMIAR_GOSC_LICZB 11 /* "pomiar" from the guest: frames, buffers of the card, of memory, 4 × (mean, 95 %) µs */

void pomiar_gosc(const uint32_t *w);    /* the guest's numbers (vsock.c, checked by linia.c) */
void pomiar_przeniesienie(uint64_t us); /* gpu.c: one TRANSFER_TO_HOST_2D */
void pomiar_okno(uint64_t us);          /* okno.c: one copy into the window */
void pomiar_zatwierdzone(void);         /* okno.c: the window's surface committed to ekran */
void pomiar_ramka(void);                /* okno.c: ekran took it (the surface may be drawn again) */
void pomiar_most(uint64_t us);          /* gpu_lacze.c: one POSR_PROSBY to the broker and its answer */
void pomiar_mapa(uint32_t rodzaj);      /* gpu_lacze.c: a buffer of the card mapped (VIRTIO_GPU_MAP_CACHE_*, plan P3) */
void pomiar_wejscie(void);              /* okno.c: input went to the guest */
void pomiar_obraz(void);                /* gpu.c: the guest showed a new picture (RESOURCE_FLUSH of the shown one) */
void pomiar_loguj(void);                /* main.c, once a second: the lines when their time came */

#endif

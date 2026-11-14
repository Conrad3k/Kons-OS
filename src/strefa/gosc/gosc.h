/* K1 gosc — the program of the test zone (0.10.0 step 1, ADR 0042): /init of the zone image, run by the zone kernel
 * INSIDE the micro machine (not in K1). Our code, static with musl and the flags of SPEC §11, but no K1 sandbox: the
 * whole machine is its sandbox. It draws a pattern on the guest's display (virtio-gpu, a "dumb" buffer), counts keys
 * and clicks from the guest's input devices and draws them, says "gotowa" to the VMM on the vsock control channel and
 * ends when the VMM says "zamknij". Its lines on the console (hvc0) go to K1's log in the test zone only — counts,
 * never key codes (invariant 8). With k1.tryb=N on its command line it is a HOSTILE guest instead (zlosliwy.c): it
 * drives a device of the VMM itself and breaks its queue — the VMM must end this machine, K1 must live on. */
#ifndef K1_GOSC_H
#define K1_GOSC_H

#include <stdint.h>

#define GOSC_TAG "K1-GOSC"

void loguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* obraz.c — the display */
int obraz_start(void);                  /* 0, or -1 (logged) */
void obraz_rysuj(unsigned klawisze, unsigned klikniecia, int32_t x, int32_t y); /* pointer x, y in 0..32767 */

/* zlosliwy.c — hostile modes (k1.tryb=1..4, the test image only) */
enum { TRYB_ZWYKLY = 0, TRYB_ZLY_ADRES = 1, TRYB_PETLA = 2, TRYB_DLUGOSC = 3, TRYB_ZAPIS_RO = 4, TRYB_MAKS = 4 };
int zlosliwy(int tryb);                 /* returns only when the VMM did not stop the machine */

#endif

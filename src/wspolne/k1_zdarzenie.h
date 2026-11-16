/* K1 — input event protocol: `wejscie` → `ekran` (SPEC §12).
 *
 * Fixed-size binary messages with a version, an operation type and a length; every field is
 * validated by the receiver (k1_zdarzenie_sprawdz). Transport in M2: a packet-mode pipe
 * (pipe2 O_DIRECT) created by the supervisor — one write = one message, atomic (16 bytes <
 * PIPE_BUF). Both ends run on the same machine, so fields are in native byte order (little-endian
 * on x86_64). Documentation: docs/komponenty/wejscie.md. */
#ifndef K1_ZDARZENIE_H
#define K1_ZDARZENIE_H

#include <stddef.h>
#include <stdint.h>

#define K1_ZDARZENIE_WERSJA 1

enum k1_zdarzenie_typ {
    K1_ZD_KLAWISZ = 1, /* kod = Linux KEY_* (< 0x100 or >= 0x160), wartosc 0 = puszczony, 1 = wciśnięty, 2 = powtórzenie */
    K1_ZD_RUCH = 2,    /* dx, dy = względny ruch myszy w pikselach (|d| <= K1_ZD_RUCH_MAX) */
    K1_ZD_PRZYCISK = 3, /* kod = Linux BTN_LEFT..BTN_TASK (0x110..0x117), wartosc 0/1 */
    K1_ZD_PRZEWIJANIE = 4 /* 0.10.0 step 4: kod = K1_ZD_KOLKO / K1_ZD_TOUCHPAD, wartosc 0; dx, dy = scrolling in
                           * 1/K1_ZD_ZABEK of a wheel notch, dy > 0 = further down (the content goes up), dx > 0 = to the
                           * right; |d| <= K1_ZD_PRZEWIN_MAX, not both 0. The direction is already K1's: the touchpad's
                           * "natural" one (D8) is applied by wejscie. */
};

#define K1_ZD_RUCH_MAX 1024
#define K1_ZD_ZABEK 120          /* one notch of a wheel (REL_WHEEL_HI_RES uses the same unit) */
#define K1_ZD_PRZEWIN_MAX (16 * K1_ZD_ZABEK)
enum { K1_ZD_KOLKO = 1, K1_ZD_TOUCHPAD = 2 }; /* where the scrolling came from */
#define K1_ZD_KOD_MAX 0x2ff /* KEY_MAX in include/uapi/linux/input-event-codes.h */

struct k1_zdarzenie {
    uint8_t wersja;          /* K1_ZDARZENIE_WERSJA */
    uint8_t typ;             /* enum k1_zdarzenie_typ */
    uint16_t dlugosc;        /* sizeof(struct k1_zdarzenie) = 16 */
    uint16_t kod;
    int16_t wartosc;
    int16_t dx;
    int16_t dy;
    uint32_t zarezerwowane;  /* must be 0 */
};

_Static_assert(sizeof(struct k1_zdarzenie) == 16, "k1_zdarzenie: unexpected size/padding");
_Static_assert(offsetof(struct k1_zdarzenie, zarezerwowane) == 12, "k1_zdarzenie: unexpected layout");

/* Validates a received message (`len` bytes at `buf`). On success copies it to `out` and
 * returns 0; otherwise returns -1 and leaves `out` zeroed. Never reads beyond `len`. */
int k1_zdarzenie_sprawdz(const void *buf, size_t len, struct k1_zdarzenie *out);

#endif

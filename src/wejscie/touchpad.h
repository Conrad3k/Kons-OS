/* K1 wejscie — a touchpad in its own (absolute) mode turned into K1's events (0.10.0 step 4, S29): one finger moves the
 * pointer (a little faster when the finger is fast), two fingers scroll — "naturally", the content follows the fingers
 * (the owner's D8) — a short tap is a click (one finger: left, two: right, three: middle) and a press of a "clickpad"
 * (the whole pad is the button, INPUT_PROP_BUTTONPAD) is the right button with two fingers or in the pad's lower right
 * corner. No knowledge of the device beyond evdev's ABS_X/ABS_Y, BTN_TOUCH and BTN_TOOL_* (every PS/2 touchpad driver
 * gives them: Synaptics, Elantech, ALPS). Pure: no system calls — host test test_touchpad. */
#ifndef K1_WEJSCIE_TOUCHPAD_H
#define K1_WEJSCIE_TOUCHPAD_H

#include <stdint.h>

#include <linux/input.h>

#include "k1_zdarzenie.h"

#define TP_STUKNIECIE_MS 180  /* a tap: the fingers down and up within this */
#define TP_STUKNIECIE_MM 2.0f /* … moving at most this far */
#define TP_PISANIE_MS 400     /* no taps this long after a key (the hand resting on the pad while typing) */
#define TP_PROG_MM 1.5f       /* two fingers scroll after moving this far (a two finger tap does not scroll) */
#define TP_ZABEK_MM 8.0f      /* two fingers moved this far = one notch of a wheel */
#define TP_DOL 0.2f           /* the clickpad's lower part (of its height) … */
#define TP_PRAWO 0.5f         /* … right of this (of its width) is the right button */

struct touchpad {
    /* the pad */
    int32_t min_x, max_x, min_y, max_y;
    float na_mm_x, na_mm_y; /* units per millimetre */
    int przyciskowy;        /* a clickpad */
    /* the frame being read (until SYN_REPORT) */
    int32_t x, y;
    int dotyk, narzedzie;   /* BTN_TOUCH; BTN_TOOL_*: fingers, 0 = none */
    int przycisk_nowy[3];   /* BTN_LEFT, BTN_RIGHT, BTN_MIDDLE this frame: -1 none, 0 up, 1 down */
    /* between frames */
    int palce, ma_poz;      /* fingers in the last frame; whether ref_* hold a position for them */
    int32_t ref_x, ref_y;
    float rx, ry;           /* parts of a pixel not sent yet */
    float sx, sy;           /* two fingers: moved before scrolling starts, then parts of a unit */
    int przewija;
    int64_t dotyk_od, klawisz_ms;
    float droga;            /* how far the fingers went since they touched (mm) */
    int stuk_palce, stuk_mozliwy;
    int wcisniety[3];       /* buttons held, as sent (a clickpad: which one its press became) */
};

/* `ax`, `ay`: EVIOCGABS(ABS_X / ABS_Y); `przyciskowy`: INPUT_PROP_BUTTONPAD. */
void tp_start(struct touchpad *t, const struct input_absinfo *ax, const struct input_absinfo *ay, int przyciskowy);
/* One event of the touchpad; whatever K1 events result go to `wyslij`. */
void tp_zdarzenie(struct touchpad *t, const struct input_event *ev, void (*wyslij)(const struct k1_zdarzenie *z));
/* A key went down (any keyboard), at `ms` (the event's time): no taps for TP_PISANIE_MS. */
void tp_klawisz(struct touchpad *t, int64_t ms);

#endif

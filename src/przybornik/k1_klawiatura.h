/* K1 przybornik — keyboard layout: Linux key codes → characters, Polish (programmer's) layout:
 * US QWERTY plus AltGr+a/c/e/l/n/o/s/x/z → ą ć ę ł ń ó ś ź ż (Shift: capitals). Pure function. */
#ifndef K1_KLAWIATURA_H
#define K1_KLAWIATURA_H

#include <stdint.h>

/* Unicode character produced by key `kod` with modifiers `mody` (K1_MOD_*), 0 if none
 * (function keys, Ctrl/Alt combinations). */
uint32_t k1_znak_klawisza(unsigned kod, unsigned mody);

#endif

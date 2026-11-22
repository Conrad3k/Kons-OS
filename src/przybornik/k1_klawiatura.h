/* K1 przybornik — keyboard layout: Linux key codes → characters, Polish (programmer's) layout:
 * US QWERTY plus AltGr+a/c/e/l/n/o/s/x/z → ą ć ę ł ń ó ś ź ż (Shift: capitals). Pure function. */
#ifndef K1_KLAWIATURA_H
#define K1_KLAWIATURA_H

#include <stddef.h>
#include <stdint.h>

/* Unicode character produced by key `kod` with modifiers `mody` (K1_MOD_*), 0 if none
 * (function keys, Ctrl/Alt combinations). */
uint32_t k1_znak_klawisza(unsigned kod, unsigned mody);

/* A one-line text field (passwords, the word that confirms an installation): `t` holds `*dl` bytes of
 * UTF-8, NUL-terminated, at most `maks` bytes (the buffer has maks + 1). Backspace removes the last
 * character, a key with a printable character appends it when it fits. 1 when the key was one of these
 * (the text may stay the same: empty or full), 0 for any other key. */
int k1_pole_klawisz(char *t, size_t *dl, size_t maks, unsigned kod, unsigned mody);

/* 0.11.0 step 5: text from the clipboard (z, n bytes of valid UTF-8) appended to such a field: whole characters while
 * they fit; tabs and new lines become spaces, other control characters are skipped. */
void k1_pole_wklej(char *t, size_t *dl, size_t maks, const char *z, size_t n);

#endif

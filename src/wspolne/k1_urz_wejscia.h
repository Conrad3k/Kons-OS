/* K1 — an input device as nadzorca sees it in sysfs (0.13.0 step 5, ADR 0060): what it is (from the bitmaps of its
 * capabilities, the same rules as wejscie's own), whether it sits on USB, and its mark for "the same device again".
 * Pure: no I/O; nadzorca reads the files (wejscia.c), test_urz_wejscia checks this. */
#ifndef K1_URZ_WEJSCIA_H
#define K1_URZ_WEJSCIA_H

#include <stddef.h>
#include <stdint.h>

/* What a device is — the bits of OCHRONA USB b (k1_komunikat.h) */
#define K1_URZ_KLAWIATURA 1 /* letters and the space bar: it can type */
#define K1_URZ_MYSZ 2       /* a mouse or a touchpad: it can point and click */
#define K1_URZ_INNE 4       /* other keys or buttons (volume keys, a remote) */
#define K1_URZ_MAKS 7

/* The bitmaps of sysfs (capabilities/ev, key, rel, abs, properties): words of hex digits separated by one space,
 * the most significant first, at most 16 digits each. */
struct k1_urz_bity {
    const char *ev, *key, *rel, *abs, *prop; /* NULL: the file was not there (= no bits) */
};

/* K1_URZ_*: one bit — KLAWIATURA wins over MYSZ (a "mouse" that also types is a keyboard for the question). */
int k1_urz_rodzaj(const struct k1_urz_bity *b);

/* Bit `nr` of a sysfs bitmap: 1, 0, or -1 when the text is not such a bitmap. */
int k1_urz_bit(const char *bitmapa, unsigned nr);

/* 1: the device's sysfs path (the target of class/input/eventN) goes through a USB device. */
int k1_urz_na_usb(const char *sciezka);

/* The mark of a device: FNV-1a of "vendor:product:name" (ids as sysfs gives them, the name cut at 128 bytes). Never 0. */
uint32_t k1_urz_znak(const char *vendor, const char *product, const char *nazwa);

#endif

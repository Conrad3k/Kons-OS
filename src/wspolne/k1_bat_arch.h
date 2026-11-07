/* K1 — battery history of earlier boots (0.6.0). nadzorca keeps it in skarbiec (owner K1_WL_SYSTEM,
 * object K1_OB_BATERIA) and gives it to Ustawienia as a sealed memfd (K1_KOM_BATERIA_ARCH).
 * Format "K1B2" (little-endian): magic 4, count of sessions (u16), 2 zero bytes; then per session:
 * start (u32, minutes since 1970), seconds between samples (u16), number of samples (u16), samples
 * (one byte each, as in K1_KOM_BATERIA_HIST: percent 0..100, +128 = on mains).
 * Pure functions (host test + fuzzing): the reader trusts nothing. */
#ifndef K1_BAT_ARCH_H
#define K1_BAT_ARCH_H

#include <stddef.h>
#include <stdint.h>

#define K1_BAT_ARCH_SESJE 16    /* boots kept */
#define K1_BAT_ARCH_PROBKI 4096 /* samples kept in all sessions together */
#define K1_BAT_ARCH_DNI 7       /* sessions older than this are dropped */
#define K1_BAT_ARCH_BAJTY (8 + K1_BAT_ARCH_SESJE * 8 + K1_BAT_ARCH_PROBKI)

struct k1_bat_sesja {
    uint32_t start_min;
    uint16_t interwal, n;
    uint16_t od; /* index of its first sample in k1_bat_arch.p */
};

struct k1_bat_arch {
    struct k1_bat_sesja s[K1_BAT_ARCH_SESJE]; /* oldest first */
    int n;
    uint8_t p[K1_BAT_ARCH_PROBKI];
    int probek;
};

/* Reads `n` bytes. 0, or -1 (then `a` is empty). */
int k1_bat_arch_czytaj(const void *buf, size_t n, struct k1_bat_arch *a);

/* Writes the archive plus `biezaca` (the running boot, may be NULL) into `buf`; drops sessions older
 * than K1_BAT_ARCH_DNI before `teraz_min` and the oldest ones beyond the limits. Returns the length. */
size_t k1_bat_arch_zapisz(const struct k1_bat_arch *a, const struct k1_bat_sesja *biezaca, const uint8_t *probki,
                          uint32_t teraz_min, uint8_t *buf, size_t maks);

#endif

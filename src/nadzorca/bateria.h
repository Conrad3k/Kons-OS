/* K1 nadzorca — battery state from the kernel's power_supply class (sysfs), 0.5.3.
 * Read by nadzorca (root) and sent to gui as k1_komunikat BATERIA; gui reads no files. */
#ifndef K1_BATERIA_H
#define K1_BATERIA_H

#include <stdint.h>

#include "k1_komunikat.h"

struct k1_bateria {
    int obecna;   /* 1: at least one system battery (scope != Device, present != 0) */
    int procent;  /* 0..100, -1 unknown (or no battery) */
    int stan;     /* enum k1_bateria_stan (k1_komunikat.h) */
    int zasilacz; /* 1: a "Mains" supply is online */
};

/* Reads every entry of `katalog` (/sys/class/power_supply). Returns 0 (also when the directory
 * is empty), -1 when it cannot be opened (then `b` = no battery). Values outside sane ranges or
 * not plain decimal numbers are ignored. */
int k1_bateria_odczytaj(const char *katalog, struct k1_bateria *b);

/* Charge history since boot (0.5.5) for the chart in Ustawienia: one byte per sample (percent,
 * + K1_BAT_ZASILACZ on mains). When full, neighbouring samples are merged and the interval doubles,
 * so the history always covers the whole time since boot. */
struct k1_bat_historia {
    uint8_t p[K1_BAT_HIST_MAKS];
    int n;
    int interwal; /* seconds between samples */
};

void k1_bat_hist_init(struct k1_bat_historia *h, int interwal);
/* Adds a sample of `b` (ignored without a battery or with an unknown percent). Returns 1 when the
 * history was compacted (receivers must get it again from index 0), else 0. */
int k1_bat_hist_dodaj(struct k1_bat_historia *h, const struct k1_bateria *b);
/* K1_KOM_BATERIA_HIST with the samples from index `od` (up to K1_BAT_HIST_W_KOM). Returns how many
 * samples it carries (0: nothing from `od` on). */
int k1_bat_hist_komunikat(const struct k1_bat_historia *h, int od, struct k1_komunikat *k);

#endif

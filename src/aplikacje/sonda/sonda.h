/* K1 card probe — 0.10.9 step 1, TRIAL CODE (the trial branch only, never in main): shared declarations. */
#ifndef K1_SONDA_H
#define K1_SONDA_H

#include <stdint.h>

/* descriptors from nadzorca (src/nadzorca/sonda.c) */
#define FD_KARTA 4   /* render node of the card (or /dev/null) */
#define FD_PAMIEC 5  /* memory.current of the probe's own cgroup */
#define FD_MOC 6     /* battery: power_now (µW) */
#define FD_PRAD 7    /* battery: current_now (µA) */
#define FD_NAPIECIE 8 /* battery: voltage_now (µV) */

#define SONDA_IOCTL_MAKS 64
#define SONDA_WYWOLAN_MAKS 32
struct sonda_liczniki {
    struct {
        unsigned nr, ile;
    } ioctl[SONDA_IOCTL_MAKS];
    unsigned n_ioctl;
    unsigned wywolania[SONDA_WYWOLAN_MAKS]; /* system calls the sandbox refused (SIGSYS → ENOSYS) */
    unsigned n_wywolan;
    unsigned dlopen;
};
extern struct sonda_liczniki sonda_licz;
void sonda_lap_wywolania(void);

/* The result lines shown in the window (and in the log). */
#define SONDA_WIERSZE 64
void wynik(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* readings */
long long sonda_odczyt(int fd);          /* the first number in a sysfs/cgroup file read from offset 0, -1 if none */
long long sonda_moc_mw(void);            /* battery power in mW (discharging only), -1 unknown */
double sonda_cpu(void);                  /* processor time of the probe in s */

/* karta.c: the card (S2, S38, S39) */
int karta_opis(void);                    /* the driver, the device, PPGTT; returns the generation (0: no i915) */
void karta_oddzielenie(void);            /* two address spaces: the second must not see the first's buffer */
void karta_zawieszenie(void);            /* a batch without end: only its context is reset, the screen lives */

/* film.c: VA-API and the decode measurement (S4, S9, S26, S27, T9 d) */
int film_va(void);                       /* vaInitialize, profiles, sizes; 0 ok */
void film_pomiar(int tryb);              /* H.264 1080p; tryb 0: processor, 1: card + copy, 2: card without a copy */
void film_koniec(void);

#endif

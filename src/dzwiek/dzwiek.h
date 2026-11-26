/* K1 dzwiek — the sound service: what its parts share (main.c the channels and the streams, karta.c the card's playback
 * node, kontrolki.c the card's controls, korektor.c the system equalizer — 0.12.3 step 2, przestrzen.c Kons Spatial
 * Audio — step 3). */
#ifndef K1_DZWIEK_USLUGA_H
#define K1_DZWIEK_USLUGA_H

#include <stdint.h>

#include "k1_dzwiek.h"
#include "k1_komunikat.h"

#define TAG "K1-DZWIEK"
#define FD_NADZORCA 3 /* from nadzorca, one way (its UID 0 checked): the card's nodes, the clients' channels */

/* karta.c — the playback node (pcmC<k>D<u>p) from nadzorca: 48 kHz, stereo, K1_DZW_OKRESY periods of K1_DZW_OKRES
 * frames; 0.12.3 (ADR 0055, T1): the card's best format of its table — 24 bits (HDA: S32_LE with 24 significant), 20,
 * else 16 as before; plain writes (SNDRV_PCM_IOCTL_WRITEI_FRAMES), no mapped status pages. */
#define KARTA_PROBKA_MAKS 4 /* bytes of one sample of the card at most (S32_LE) */
int karta_przyjmij(int fd, int nr, int urzadzenie); /* 0, or -1: not a playback node we can use (fd closed) */
int karta_jest(void);
int karta_fd(void);                                 /* for poll while it plays (POLLOUT: room for a period), else -1 */
int karta_obudz(void);                              /* parameters and PREPARE before the first period: 0 or -1 */
int karta_bajty(void);                              /* bytes of one sample in the format the card took (2, 3, 4) */
int karta_bity(void);                               /* the bits it really plays (16..24) */
int karta_zapisz(const uint8_t *ramki);             /* one period in that format: 1 written, 0 no room, -1 the card failed */
void karta_usypiaj(void);                           /* nothing plays: stop and free the card's buffer (T8) */
int karta_spi(void);
unsigned long karta_przerwy(void);                  /* underruns since the start (the log: a number only) */

/* kontrolki.c — the card's controls (controlC<k>): the outputs on, their hardware volume at 0 dB (T3: the volume is the
 * service's own); the card's name for the log; step 2: the headphone jacks and their events. */
void kontrolki_przyjmij(int fd);
int kontrolki_fd(void);         /* for poll (POLLIN: an event of a control) while the card has a headphone jack, else -1 */
int kontrolki_zdarzenia(void);  /* the events read, the jacks read again: 1 when the output changed */
int kontrolki_wyjscie(void);    /* K1_DZW_WYJSCIE_GLOSNIKI or _SLUCHAWKI */

/* korektor.c — the system equalizer (0.12.3 step 2, ADR 0055): ten bands of each output, on the sum before K1's volume. */
void korektor_start(void);                         /* both outputs flat */
struct k1_komunikat korektor_stan(int wyjscie);   /* DZWIEK_KOREKTOR_STAN of that output */
int korektor_ustaw(const struct k1_komunikat *m, double t); /* DZWIEK_KOREKTOR: 1 changed, 0 the same, -1 refused */
int korektor_zalegle(double t);                   /* the log's line it owes: ms until it says it, -1 none */
void korektor_licz(float *suma, uint32_t ramki, int wyjscie); /* one period of the sum, the bands of `wyjscie` */
void korektor_minuta(void);                       /* the log once a minute: the mean cost of a period (T6) */

/* przestrzen.c — Kons Spatial Audio (0.12.3 step 3, ADR 0055): the scene of each output, after the equalizer. */
void przestrzen_start(void);                       /* both outputs off */
struct k1_komunikat przestrzen_stan(int wyjscie); /* DZWIEK_SCENA_STAN of that output */
struct k1_komunikat przestrzen_wlasna_stan(int wyjscie); /* step 4: … of its "Własna" (a = wyjscie + SCENA_WLASNA) */
/* DZWIEK_SCENA: 1 changed (the scene of an output), 2 changed (its "Własna", step 4), 0 the same, -1 refused */
int przestrzen_ustaw(const struct k1_komunikat *m, double t);
int przestrzen_zalegle(double t);                 /* the log's line it owes: ms until it says it, -1 none */
void przestrzen_licz(float *suma, uint32_t ramki, int wyjscie); /* one period of the sum, the scene of `wyjscie` */
void przestrzen_minuta(void);                     /* the log once a minute: the mean cost of a period (T6) */

#endif

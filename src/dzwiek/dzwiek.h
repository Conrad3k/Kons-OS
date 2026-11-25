/* K1 dzwiek — the sound service: what its parts share (main.c the channels and the streams, karta.c the card's playback
 * node, kontrolki.c the card's controls). */
#ifndef K1_DZWIEK_USLUGA_H
#define K1_DZWIEK_USLUGA_H

#include <stdint.h>

#include "k1_dzwiek.h"

#define TAG "K1-DZWIEK"
#define FD_NADZORCA 3 /* from nadzorca, one way (its UID 0 checked): the card's nodes, the clients' channels */

/* karta.c — the playback node (pcmC<k>D<u>p) from nadzorca: 48 kHz, S16_LE, stereo, K1_DZW_OKRESY periods of
 * K1_DZW_OKRES frames; plain writes (SNDRV_PCM_IOCTL_WRITEI_FRAMES), no mapped status pages. */
int karta_przyjmij(int fd, int nr, int urzadzenie); /* 0, or -1: not a playback node we can use (fd closed) */
int karta_jest(void);
int karta_fd(void);                                 /* for poll while it plays (POLLOUT: room for a period), else -1 */
int karta_obudz(void);                              /* parameters and PREPARE before the first period: 0 or -1 */
int karta_zapisz(const int16_t *ramki);             /* one period: 1 written, 0 no room now, -1 the card failed */
void karta_usypiaj(void);                           /* nothing plays: stop and free the card's buffer (T8) */
int karta_spi(void);
unsigned long karta_przerwy(void);                  /* underruns since the start (the log: a number only) */

/* kontrolki.c — the card's controls (controlC<k>): the outputs on, their hardware volume at 0 dB (T3: the volume is the
 * service's own); the card's name for the log. */
void kontrolki_przyjmij(int fd);

#endif

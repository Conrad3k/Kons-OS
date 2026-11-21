/* K1 agent — the files of a browser zone (main.c; 0.11.0 steps 2, 3, ADR 0052): what its parts share. */
#ifndef K1_AGENT_H
#define K1_AGENT_H

#include <stdint.h>

#include "k1_pliki_strefy.h"

#define TAG "K1-AGENT"

/* main.c */
extern int vs;                               /* the files port (vsock), -1: not connected */
void loguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ramka(const struct k1_ps_ramka *r);      /* a frame to the VMM; -1: the port broke */
void rozlacz(void);

/* odbior.c — files from K1 into the session's folder "Z K1" (0.11.0 step 3) */
int odbior_start(const char *z_k1, const char *fifo); /* the folder, the compositor's FIFO ("Otwórz w…"); 0 or -1 */
int odbior_katalog(void);                    /* the folder "Z K1" (a descriptor) */
void odbior_ramka(const struct k1_ps_ramka *r); /* PLIK, BAJTY */
void odbior_rozlacz(void);                   /* the port went: a file being received is dropped */
/* K1's number of a file K1 gave, of which `nazwa` is a changed version: in the folder of downloads (z_k1 0) the same
 * name or Firefox's "name(N).ext"; in "Z K1" (z_k1 1) the same name. 0: none. */
uint32_t odbior_dotyczy(const char *nazwa, int z_k1);

#endif

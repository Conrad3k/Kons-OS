/* K1 agent — the files of a browser zone (main.c; 0.11.0 steps 2, 3, ADR 0052): what its parts share. */
#ifndef K1_AGENT_H
#define K1_AGENT_H

#include <stdint.h>

#include "k1_pliki_strefy.h"

#define TAG "K1-AGENT"

/* main.c */
extern int vs;                               /* the files port (vsock), -1: not connected */
uint64_t teraz_ms(void);                     /* CLOCK_MONOTONIC */
void loguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ramka(const struct k1_ps_ramka *r);      /* a frame to the VMM; -1: the port broke */
void rozlacz(void);
/* A file ready to be told to K1 (foldery.c): in folder `kat` (main.c keeps its own copy of the descriptor);
 * przeniesiony: renamed into place (may be empty); dotyczy: K1's number of the file it is a changed version of. */
void plik_gotowy(int kat, const char *nazwa, int przeniesiony, uint32_t dotyczy);

/* foldery.c — the folders watched (0.11.4): the downloads, the browser's home and the user's folders in them, "Z K1" */
#define AGENT_PROFIL "profil" /* the browser's profile in its home (gosc/przegladarka.c): not the user's folder */
int foldery_start(const char *pobrane, const char *z_k1); /* the home is the downloads' parent; 0 or -1 */
int foldery_fd(void);      /* inotify */
void foldery_czytaj(void); /* its events */
int foldery_czekaj(void);  /* ms until foldery_czas has a file ready; -1: none waits */
void foldery_czas(void);   /* files quiet long enough go to plik_gotowy */

/* odbior.c — files from K1 into the session's folder "Z K1" (0.11.0 step 3) */
int odbior_start(const char *z_k1, const char *fifo); /* the folder, the compositor's FIFO ("Otwórz w…"); 0 or -1 */
int odbior_katalog(void);                    /* the folder "Z K1" (a descriptor) */
void odbior_ramka(const struct k1_ps_ramka *r); /* PLIK, BAJTY */
void odbior_rozlacz(void);                   /* the port went: a file being received is dropped */
/* K1's number of a file K1 gave, of which `nazwa` is a changed version: in the folder of downloads (z_k1 0) the same
 * name or Firefox's "name(N).ext"; in "Z K1" (z_k1 1) the same name. 0: none. */
uint32_t odbior_dotyczy(const char *nazwa, int z_k1);
int odbior_tekst(uint32_t nr, const uint8_t *t, size_t n); /* 0.11.0 step 5: K1's text to the compositor; 0 or -1 */

/* schowek.c — the clipboard (0.11.0 step 5, ADR 0052 T8): the compositor's copies to K1, K1's text for a Ctrl+V */
void schowek_start(const char *fifo); /* the compositor's FIFO to us ("kopia", "wklejone") */
int schowek_fd(void);                 /* -1: not open yet */
void schowek_czytaj(void);
void schowek_ramka(const struct k1_ps_ramka *r); /* WKLEJ, WKLEJ_TEKST from the VMM */

#endif

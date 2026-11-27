/* K1 logowanie — the password screen (main.c) and, started with "--ratunek", the rescue screen (ratunek.c; 0.13.0 step 3,
 * ADR 0058). */
#ifndef K1_LOGOWANIE_H
#define K1_LOGOWANIE_H

#define TAG "K1-LOGOWANIE"
#define FD_NADZORCA 4 /* requests to nadzorca */
#define FD_BATERIA 5  /* from nadzorca: the battery; in the rescue mode the rescue mode's state */
#define FD_SKARBIEC 6

int ratunek_main(void);

#endif

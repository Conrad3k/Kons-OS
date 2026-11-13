/* K1 — host test of the service protocol, split into files like the checking itself (src/wspolne/k1_kom_*.c; 0.9.0
 * step 9): test_komunikat.c (header, ekran, Zasoby, skarbiec, main), test_komunikat_pliki.c (nadzorca, the channel
 * helpers, Pliki, opening files, the USB stick, dekoder). What the parts share. */
#ifndef K1_TEST_KOMUNIKAT_H
#define K1_TEST_KOMUNIKAT_H

#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k1_dokument.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_uslugi.h"
#include "k1_test_host.h"

int ok(struct k1_komunikat k, enum k1_kanal_kierunek kier);  /* checked, and the same comes out */
int zle(struct k1_komunikat k, enum k1_kanal_kierunek kier); /* refused, and nothing comes out */
#define K(t, o, a, b, c, d, e) k1_komunikat_nowy(t, o, a, b, c, d, e)

/* test_komunikat_pliki.c */
void do_nadzorcy(void);
void kanal(void);
void pliki(void);
void otwieranie(void);
void pendrive(void);
void dekoder(void);

#endif

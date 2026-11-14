/* K1 siec — what the parts of the service share (0.9.0 step 8): main.c (the channel to nadzorca, DHCP on the packet
 * socket) and zegar.c (the clock from the internet). Not the pure parts (dhcp.c, klient.c, sntp.c, czas.c). */
#ifndef K1_SIEC_USLUGA_H
#define K1_SIEC_USLUGA_H

#include <poll.h>

#include "k1_komunikat.h"
#include "klient.h"

#define TAG "K1-SIEC"
#define NADZORCA 3

extern struct k1_dhcp_klient klient;
/* A request to nadzorca about the card siec has (obiekt = the card); a broken channel ends the service. */
void do_nadzorcy(enum k1_komunikat_typ typ, int32_t a, int32_t b, int32_t c, int32_t d);

/* zegar.c */
void zegar_polaczono(void); /* an address is set: the first round now */
void zegar_stop(void);      /* no address: no rounds, sockets closed */
void zegar_gniazdo(const struct k1_komunikat *k, int fd); /* SIEC_GNIAZDO */
int zegar_poll(struct pollfd *p, int maks);               /* the sockets to watch (fd -1 where none): how many */
void zegar_odbierz(const struct pollfd *p, int n);        /* after poll: the answers */
void zegar_odrzucony(void); /* SIEC_WYNIK of SIEC_ZEGAR with an error: try again later */
double zegar_czas(double t); /* timers; the next time it has something to do (CLOCK_MONOTONIC), 0: never */

/* lista.c (0.9.1): the Wi-Fi networks around — SIEC_WIFI_WYNIKI with its memfd (closed here) */
void wifi_wyniki(const struct k1_komunikat *k, int fd);

#endif

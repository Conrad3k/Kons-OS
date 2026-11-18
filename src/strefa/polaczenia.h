/* K1 strefa — the browser's connections (polaczenia.c; 0.10.0 step 2, ADR 0043 T9): what vsock.c and the main loop
 * call. A connection is numbered 1..K1_STREFA_POLACZENIA_MAKS (also its number towards nadzorca). */
#ifndef K1_POLACZENIA_H
#define K1_POLACZENIA_H

#include <poll.h>
#include <stddef.h>
#include <stdint.h>

#include "k1_komunikat.h"
#include "strefa.h"

#define POL_PORT 2          /* the host port the relay in the zone connects to */
#define POL_BUFOR 65536u    /* each direction: what we announce to the guest (buf_alloc) and what we keep for it */
#define FD_NADZORCA 7       /* the channel to nadzorca (strefy.c, strefy_siec.c) */

/* The vsock credit of one connection (virtio 1.2 §5.10.6.3): the guest's room for us and ours for it. */
struct pol_kredyt {
    uint32_t jego_bufor, jego_fwd; /* as the guest last said */
    uint32_t wyslane;              /* bytes we sent it */
    uint32_t odebrane, przekazane, zgloszone; /* bytes it sent; of them used (our fwd_cnt); what we last told it */
    int koniec_wyslany;            /* SHUTDOWN sent to the guest (the socket's end) */
    int odpowiedz, kredyt_prosba;  /* owed to the guest (vsock.c): its RESPONSE; a credit update it asked for */
};

/* vsock.c, under mz.zamek */
int pol_nowe(uint32_t port_goscia, const struct pol_kredyt *kr); /* a new connection: its number, 0: all taken */
void pol_ten_sam_port(void);                         /* a new connection from a port still in use (counted) */
int pol_znajdz(uint32_t port_goscia);                            /* the connection of the guest's port, 0: none */
unsigned pol_zajete(void);                                       /* connections in use now (the counters' line) */
uint32_t pol_port(int n);                                        /* its guest's port; 0: the connection is free */
struct pol_kredyt *pol_kredyt(int n);
int pol_od_goscia(int n, const void *b, size_t dl);  /* the guest's bytes; -1: more than its credit (hostile) */
void pol_gosc_konczy(int n, int calkiem);            /* SHUTDOWN (it sends no more) / RST (calkiem: forget it) */
size_t pol_do_goscia(int n, uint8_t *b, size_t maks); /* bytes for the guest, at most maks */
int pol_stan_dla_vsock(int n, int *koniec);          /* 1: bytes wait for the guest; *koniec: nothing more will —
                                                      * the SHUTDOWN flags: SEND (the socket's end: the guest may still
                                                      * send, a half-close), or RCV | SEND (an error, refused, closed) */
void vsock_wyslij(void);                             /* vsock.c: what waits, into the guest's receive buffers */

/* the main loop (main.c) */
int pol_deskryptory(struct pollfd *p, int maks);     /* fills p: nadzorca, DNS, one per connection; their number */
void pol_obsluz(const struct pollfd *p, int n);
void pol_podsumowanie(void);                         /* counts in the log, at the end */

#endif

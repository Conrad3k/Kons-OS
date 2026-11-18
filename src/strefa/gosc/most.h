/* K1 gosc — the test of Raptor Bridge from inside a zone (most.c: the connections, most_fazy.c: what is checked;
 * 0.10.5 step 5, T15). */
#ifndef K1_GOSC_MOST_H
#define K1_GOSC_MOST_H

#include <stddef.h>
#include <stdint.h>

#define MOST_MAKS 192        /* tasks in one phase */
#define MOST_PORT 8790       /* the test server on QEMU's router (scripts/test-most.sh) */
#define MOST_PORT_VMM 2      /* the VMM's port of connections (src/strefa/polaczenia.h: POL_PORT) */
#define MOST_ROUTER 0x0A000202u /* 10.0.2.2: QEMU's user network, the computer that runs the test */

enum { MOST_D = 0, MOST_E, MOST_H, MOST_C };              /* the server's line (most.c) */
enum { MOST_EOF = 1, MOST_PRZERWANE, MOST_BLAD, MOST_CZAS }; /* how a task ended */

struct zad {
    int fd, vsock, laczy;
    int rodzaj;                     /* MOST_D … MOST_C */
    uint8_t prosba[300];            /* what goes first: SOCKS5 and the server's line, or raw bytes */
    size_t n_prosba, wyslane_prosby;
    uint8_t odp[12];                /* the replies of SOCKS5 (relay) or the connector's raw reply (vsock) */
    size_t n_odp, odp_dl;
    int socks;                      /* the reply code; 256 + method: no method; -1: none yet */
    uint64_t do_wyslania, wyslane;  /* the pattern up ("E", "H") */
    uint64_t odebrane, przerwij_po; /* the pattern down; the client closes after przerwij_po (0: never) */
    unsigned long zle;              /* bytes not as the pattern */
    int eof, wr_zamkniete, koniec, blad;
    double t_start, t_odp, t_koniec;
};

uint8_t most_wzor(uint64_t i);
double most_teraz(void);
struct zad *most_nowe(int przez_vsock);
void most_socks_ip(struct zad *a, uint32_t ip, uint16_t port, const char *linia);
void most_socks_nazwa(struct zad *a, const char *nazwa, uint16_t port);
void most_surowe(struct zad *a, const void *b, size_t n, size_t odp_dl);
double most_uruchom(int od, double limit_s);
int most_liczba(void);
struct zad *most_zad(int i);
void most_zwolnij(void);

void most(void); /* most_fazy.c: the whole test, then returns (the machine ends) */

#endif

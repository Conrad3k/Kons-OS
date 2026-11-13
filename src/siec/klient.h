/* K1 siec — the DHCP client (RFC 2131, section 4.4): the states, the timers and what to do next.
 *
 * Pure: time comes in as a number (seconds, CLOCK_MONOTONIC in the service, anything in the host test), and every call
 * answers with what the service has to do (K1_DHCP_A_* bits): send `k->zapytanie`, ask nadzorca to set the address
 * from `k->adres` …, or to take it away. The transaction numbers come from a small generator seeded once from the
 * kernel's randomness (the service: getrandom). Host test test_siec (every state, the timers, the negative cases),
 * fuzzing fuzz_dhcp (any packets in any state). */
#ifndef K1_SIEC_KLIENT_H
#define K1_SIEC_KLIENT_H

#include <stdint.h>

#include "dhcp.h"

enum k1_dhcp_stan {
    K1_DHCP_BEZ_KABLA = 0, /* nothing to do (no card with a cable) */
    K1_DHCP_WYBOR,         /* DISCOVER sent, waiting for an OFFER (RFC: SELECTING) */
    K1_DHCP_ZADANIE,       /* REQUEST for the offered address sent, waiting for ACK (REQUESTING) */
    K1_DHCP_ZWIAZANY,      /* the address is ours until the lease ends (BOUND) */
    K1_DHCP_ODNAWIANIE,    /* after T1: REQUEST straight to our router (RENEWING) */
    K1_DHCP_PONOWNIE,      /* after T2: REQUEST to everyone (REBINDING) */
};

/* What the service does after a call (bits). */
#define K1_DHCP_A_WYSLIJ 1  /* send k->zapytanie (k1_dhcp_buduj) — to everyone, or to k->serwer_mac when k->do_serwera */
#define K1_DHCP_A_USTAW 2   /* ask nadzorca to set k->adres, k->prefiks, k->router (SIEC_ADRES) */
#define K1_DHCP_A_ZWOLNIJ 4 /* ask nadzorca to take the address away (SIEC_ZWOLNIJ) */

#define K1_DHCP_ODSTEP_MIN 4.0   /* s: the first retry; then twice as long each time … */
#define K1_DHCP_ODSTEP_MAKS 64.0 /* … up to this (RFC 2131 4.1) */
#define K1_DHCP_PROBY_ZADANIA 4  /* REQUESTs for an offer before asking everyone again */

struct k1_dhcp_klient {
    enum k1_dhcp_stan stan;
    uint8_t mac[6];
    uint64_t los;              /* the generator of transaction numbers */
    uint32_t xid;
    double poczatek;           /* when this round of asking began (the "secs" field) */
    double nastepny;           /* when k1_dhcp_czas has something to do (0: never) */
    double odstep;             /* the current wait before a retry */
    int proby;                 /* retries in this state */
    /* the offer we asked for, then the lease we hold */
    uint32_t adres, serwer, router;
    int prefiks;
    uint32_t dzierzawa;
    double t1, t2, koniec;     /* renew, rebind, the end of the lease (absolute times) */
    uint8_t serwer_mac[6];     /* where the ACK came from (renewing goes there) */
    uint32_t czas[2];          /* the time servers the router named (0.9.0 step 8) */
    int n_czas;
    int do_serwera;            /* the last zapytanie goes to serwer_mac, not to everyone */
    struct k1_dhcp_zapytanie zapytanie;
};

/* A cable is in: start asking (DISCOVER now). `ziarno`: random bits for the transaction numbers. */
int k1_dhcp_start(struct k1_dhcp_klient *k, const uint8_t mac[6], uint64_t ziarno, double teraz);
/* The cable is out: forget everything (nadzorca takes the address away itself). */
void k1_dhcp_stop(struct k1_dhcp_klient *k);
/* A checked reply `o` (k1_dhcp_czytaj > 0) that came from the card `od_mac` at time `teraz`. */
int k1_dhcp_odebrano(struct k1_dhcp_klient *k, const struct k1_dhcp_odpowiedz *o, const uint8_t od_mac[6], double teraz);
/* Time passed: retries, renewing, rebinding, the end of the lease. Call it at k->nastepny (or later). */
int k1_dhcp_czas(struct k1_dhcp_klient *k, double teraz);

#endif

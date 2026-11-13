/* K1 siec — the clock from the internet: one round of questions to several time servers and whether their answers
 * agree (0.9.0 step 8, T5).
 *
 * A plain time protocol is not signed (L17): someone on the way can answer with a wrong time. So K1 asks up to
 * K1_SIEC_ZRODLA_MAKS servers at once — the ones the router names (DHCP option 42) and fixed addresses of two operators
 * (K1_SERWERY_CZASU; no names: K1 has no DNS) — and takes the time only when at least two answers, and more than half of
 * all answers, agree within K1_CZAS_ZGODNOSC. nadzorca sets the clock and keeps its own bounds (not before this K1's
 * build). Pure: the time comes in as numbers (CLOCK_MONOTONIC in the service). Host test test_czas, fuzzing fuzz_sntp. */
#ifndef K1_SIEC_CZAS_H
#define K1_SIEC_CZAS_H

#include <stddef.h>
#include <stdint.h>

#include "k1_siec.h"

/* Fixed time servers: X(a, b, c, d, operator). Addresses of anycast services (the same address everywhere); one of two
 * operators in turn, so that the round has both even with the router's two. */
#define K1_SERWERY_CZASU(X)                     \
    X(216, 239, 35, 0, "time.google.com")       \
    X(162, 159, 200, 1, "time.cloudflare.com")  \
    X(216, 239, 35, 4, "time.google.com")       \
    X(162, 159, 200, 123, "time.cloudflare.com")

#define K1_CZAS_RUNDA 8.0             /* s: how long a round waits for answers */
#define K1_CZAS_ZGODNOSC 2.0          /* s: answers within this agree */
#define K1_CZAS_DROGA_MAKS 4.0        /* s: an answer that took longer is not taken */
#define K1_CZAS_PONOWNIE_MIN 15.0     /* s: after a failed round, then twice as long each time … */
#define K1_CZAS_PONOWNIE_MAKS 3600.0  /* … up to an hour */
#define K1_CZAS_CO 21600.0            /* s: after a good round — every 6 hours while connected */

enum { K1_CZAS_NIC = 0, K1_CZAS_GNIAZDO, K1_CZAS_WYSLANO, K1_CZAS_ODPOWIEDZ };

struct k1_czas {
    int n; /* sources in this round */
    struct {
        uint32_t serwer;     /* host order */
        uint64_t los;        /* the 64 random bits of our question */
        double wyslano;      /* when */
        int stan;            /* K1_CZAS_* */
        double przesuniecie; /* the server's time minus CLOCK_MONOTONIC, at the middle of the way */
    } z[K1_SIEC_ZRODLA_MAKS];
    double koniec;   /* the end of the round now running, 0: none */
    double nastepna; /* the next round, 0: none (no address) */
    double odstep;   /* after a failed round */
};

/* Connected (an address is set): the first round now. `teraz`: CLOCK_MONOTONIC. */
void k1_czas_polaczono(struct k1_czas *c, double teraz);
/* No address any more: no rounds. */
void k1_czas_stop(struct k1_czas *c);
/* A round starts: the sources — the router's (`od_routera`, `n_r` of them; bad and repeated ones skipped) first, then
 * K1_SERWERY_CZASU — each with its random bits `los[i]` (not 0). Returns how many (the service asks nadzorca for a
 * socket to each). */
int k1_czas_start(struct k1_czas *c, const uint32_t *od_routera, int n_r, const uint64_t los[K1_SIEC_ZRODLA_MAKS],
                  double teraz);
/* The question of source i went out at `teraz`. */
void k1_czas_wyslano(struct k1_czas *c, int i, double teraz);
/* Answer `b` (`n` bytes) from source i at `teraz`: 0 taken, or a reason < 0 (K1_SNTP_*; K1_CZAS_ZA_POZNO). */
int k1_czas_odpowiedz(struct k1_czas *c, int i, const uint8_t *b, size_t n, double teraz);
#define K1_CZAS_ZA_POZNO (-20) /* not asked, answered already, or K1_CZAS_DROGA_MAKS passed */
/* 1 when the round is over (every source answered, or K1_CZAS_RUNDA passed). */
int k1_czas_koniec_rundy(const struct k1_czas *c, double teraz);
/* Ends the round: 1 with *przesuniecie (the time = CLOCK_MONOTONIC + it) when the answers agree, else 0. *odpowiedzi and
 * *zgodne: how many answered, how many agree. Plans the next round. */
int k1_czas_wynik(struct k1_czas *c, double teraz, double *przesuniecie, int *odpowiedzi, int *zgodne);
/* nadzorca did not take the time of a good round (its own bounds): the next round as after a failed one. */
void k1_czas_odrzucony(struct k1_czas *c, double teraz);

#endif

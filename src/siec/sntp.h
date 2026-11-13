/* K1 siec — the time from a time server (SNTP, RFC 4330 / RFC 5905 section 7: a client of mode 3): the 48 bytes we
 * send and the checking of what comes back (0.9.0 step 8, T5).
 *
 * The question carries 64 random bits in its transmit timestamp; a server copies them into the origin timestamp of its
 * answer — an answer without them is not an answer to us (a guess from outside the path has 1 in 2^64). The socket
 * nadzorca opens is connected to the server (answers from anyone else never reach us) and has a random port. Every
 * answer is checked field by field like a message between services (niezmiennik 6). Pure functions: host test
 * test_czas, fuzzing fuzz_sntp. */
#ifndef K1_SIEC_SNTP_H
#define K1_SIEC_SNTP_H

#include <stddef.h>
#include <stdint.h>

#define K1_SNTP_DLUGOSC 48         /* the packet without extension fields */
#define K1_SNTP_ODPOWIEDZ_MAKS 1024 /* longer: not taken */
#define K1_NTP_OD_1900 2208988800ULL /* seconds from 1900 (NTP) to 1970 (Unix) */

/* Why an answer was rejected (k1_sntp_czytaj < 0); k1_sntp_powod() names it for the log. */
enum {
    K1_SNTP_ZLA_DLUGOSC = -1, /* shorter than 48 bytes, longer than K1_SNTP_ODPOWIEDZ_MAKS */
    K1_SNTP_ZLY_TRYB = -2,    /* not version 3 or 4, not mode 4 (a server) */
    K1_SNTP_NIE_DO_NAS = -3,  /* the origin timestamp is not our 64 random bits */
    K1_SNTP_BEZ_CZASU = -4,   /* stratum 0 (a "kiss of death"), 16 and more, alarm (leap 3), no receive or transmit time */
    K1_SNTP_NIEPEWNY = -5,    /* the server itself is not sure: root delay / 2 + root dispersion ≥ 1 s */
    K1_SNTP_ZLY_CZAS = -6,    /* the time is outside K1_SIEC_CZAS_MIN..MAKS */
    K1_SNTP_POWOD_MIN = -6,
};

/* The question with `los` (random, not 0) as its transmit timestamp. */
void k1_sntp_zapytanie(uint8_t b[K1_SNTP_DLUGOSC], uint64_t los);

/* Checks answer `b` (`n` bytes) to question `los`: 0 with *czas = the server's transmit time in seconds since 1970
 * (UTC, with the fraction), or a reason < 0. */
int k1_sntp_czytaj(const uint8_t *b, size_t n, uint64_t los, double *czas);

/* The reason in Polish words (for the log). */
const char *k1_sntp_powod(int powod);

#endif

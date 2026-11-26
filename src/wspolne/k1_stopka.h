/* K1 — the footer of the download zone (0.13.0, ADR 0056 point 3; Issue #238 p. 2): 16 bytes the download zone puts after
 * the file it downloaded, before it ends the connection to the writing program — the time the HTTPS server said (its
 * header "Date"), which K1 checks its own clock against (SNTP) and counts the freshness of an announcement and the
 * waiting of a new version by. TLS (curl in the zone, the certificate checked) is what makes that time the server's;
 * K1 trusts it no more than the zone: a bad time can only make K1 refuse an announcement or wait longer.
 *
 *    0 "K1S1"   4 zeros (u32)   8 the time (u64, s since 1970; 0: the server said none)
 *
 * The time is K1_STOPKA_OD … K1_STOPKA_DO or 0; anything else is refused (the whole download with it). Pure functions
 * (host test test_stopka, fuzzing fuzz_stopka): the zone (src/strefa/gosc/pobieranie.c) parses the header and builds the
 * footer, the writing program (src/instalator/siec.c) reads it. */
#ifndef K1_STOPKA_H
#define K1_STOPKA_H

#include <stddef.h>
#include <stdint.h>

#define K1_STOPKA 16u
#define K1_STOPKA_OD 1767225600ull /* 2026-01-01: no K1 release is older */
#define K1_STOPKA_DO 4102444800ull /* 2100-01-01 */

void k1_stopka_zbuduj(uint64_t czas, uint8_t b[K1_STOPKA]); /* a time out of range: 0 */
int k1_stopka_czytaj(const uint8_t b[K1_STOPKA], uint64_t *czas); /* 0, or -1 (and *czas 0) */

/* The value of an HTTP header "Date" in the only form servers must send (RFC 9110 5.6.7, IMF-fixdate:
 * "Sun, 06 Nov 1994 08:49:37 GMT"), `n` bytes, spaces and a line end around it allowed: seconds since 1970, or 0. */
uint64_t k1_data_http(const char *t, size_t n);

#endif

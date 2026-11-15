/* K1 strefa — the connector's protocols (lacznik.c; 0.10.0 step 2, ADR 0043 T9): SOCKS5 (RFC 1928) as the browser in
 * the zone speaks it through the relay, and DNS (RFC 1035, A records only) to the server the router gave. Pure code:
 * bytes in, a decision out — no I/O (polaczenia.c does it), tested on the host (test_lacznik) and fuzzed (fuzz_socks,
 * fuzz_dns). Everything from the zone is hostile; so is every DNS answer (anyone on the home network can send one). */
#ifndef K1_LACZNIK_H
#define K1_LACZNIK_H

#include <stddef.h>
#include <stdint.h>

#define NAZWA_MAKS 253 /* a DNS name without its final dot */

/* SOCKS5 reply codes (RFC 1928 §6) */
enum { SOCKS_OK = 0, SOCKS_BLAD = 1, SOCKS_NIE_WOLNO = 2, SOCKS_BRAK_SIECI = 3, SOCKS_BRAK_HOSTA = 4,
       SOCKS_ODMOWA = 5, SOCKS_POLECENIE = 7, SOCKS_TYP_ADRESU = 8 };

struct socks_cel {
    uint32_t adres;            /* IPv4 (host order); 0: a name to look up */
    uint16_t port;
    char nazwa[NAZWA_MAKS + 1];
};

/* The greeting (VER, NMETHODS, METHODS): 0 more bytes needed, 1 fine (method "no authentication" offered; *zuzyte
 * bytes used), -1 not SOCKS5 or no method we take (reply 05 FF and close). */
int socks_powitanie(const uint8_t *b, size_t n, size_t *zuzyte);
/* The request (VER, CMD, RSV, ATYP, address, port): 0 more needed, 1 fine (*c filled, *zuzyte used), or −(reply code)
 * for what we refuse: −SOCKS_POLECENIE (BIND, UDP), −SOCKS_TYP_ADRESU (IPv6), −SOCKS_BLAD (anything malformed: a wrong
 * version, a reserved byte, port 0, a bad name). */
int socks_prosba(const uint8_t *b, size_t n, size_t *zuzyte, struct socks_cel *c);
/* The 10-byte reply with code `kod` and the bound address 0.0.0.0:0. */
void socks_odpowiedz(uint8_t o[10], int kod);

/* A host name we look up: letters, digits, "-", "_", labels of 1..63 separated by single dots, 1..NAZWA_MAKS bytes. */
int nazwa_dobra(const char *s, size_t n);
/* A dotted IPv4 address ("93.184.216.34"): 1 and *a (host order), else 0. */
int nazwa_ipv4(const char *s, uint32_t *a);

/* The question "A, IN" for `nazwa` with identifier `id`, recursion desired: its length, or 0 (does not fit / bad). */
size_t dns_zapytanie(uint8_t *b, size_t maks, uint16_t id, const char *nazwa);
/* The answer to our question (id, the same name — case does not matter — type A, class IN): 1 and *adres (the first
 * A record in the answer section, host order, k1_siec_serwer_dobry), 0 an answer without one (no such name, an error),
 * −1 not an answer to it or malformed (ignored: the right one may still come). Names in records are skipped with their
 * compression pointers, every pointer backwards and at most a bounded number of them. */
int dns_odpowiedz(const uint8_t *b, size_t n, uint16_t id, const char *nazwa, uint32_t *adres);

#endif

/* K1 siec — DHCP packets (RFC 2131, 2132): what the service sends to the router and the checking of what comes back.
 *
 * The packet socket nadzorca gives siec carries IPv4 datagrams (SOCK_DGRAM: the kernel adds and removes the Ethernet
 * header), so a packet here is a whole IPv4 datagram: IP header, UDP header, the DHCP message. Everything from the
 * network is checked field by field like a message between services (SPEC §3, niezmiennik 6): any packet of any length
 * gives either a checked answer or a reason it was rejected, without reading outside it. Pure functions: host test
 * test_siec, fuzzing fuzz_dhcp. */
#ifndef K1_SIEC_DHCP_H
#define K1_SIEC_DHCP_H

#include <stddef.h>
#include <stdint.h>

#define K1_DHCP_PAKIET_MAKS 576 /* what we send fits in the smallest datagram every router takes (RFC 2131: 576) */

/* DHCP message types (option 53). */
enum { K1_DHCP_DISCOVER = 1, K1_DHCP_OFFER = 2, K1_DHCP_REQUEST = 3, K1_DHCP_ACK = 5, K1_DHCP_NAK = 6 };

/* Why a packet was rejected (k1_dhcp_czytaj < 0); k1_dhcp_powod() names it for the log. */
enum {
    K1_DHCP_ZLY_IP = -1,        /* not a whole IPv4 datagram (version, header length, checksum, fragment, length) */
    K1_DHCP_ZLY_UDP = -2,       /* not UDP from port 67 to port 68, wrong length or checksum */
    K1_DHCP_ZLY_NAGLOWEK = -3,  /* not a DHCP reply (op, hardware type, address length, magic cookie, too short) */
    K1_DHCP_OBCY = -4,          /* not for us: another transaction or another card */
    K1_DHCP_ZLE_OPCJE = -5,     /* an option runs past the end, has a wrong length, no end mark, no type or server */
    K1_DHCP_ZLY_TYP = -6,       /* not OFFER, ACK or NAK */
    K1_DHCP_ZLY_ADRES = -7,     /* the address, mask or router could not be ours (k1_siec_adres_dobry) */
    K1_DHCP_POWOD_MIN = -7,
};

/* What we send: DISCOVER, or REQUEST (selecting: zadany + serwer; renewing / rebinding: ciaddr). */
struct k1_dhcp_zapytanie {
    int typ;            /* K1_DHCP_DISCOVER / K1_DHCP_REQUEST */
    uint32_t xid;       /* the transaction number */
    uint8_t mac[6];     /* our card */
    uint16_t sekundy;   /* seconds since we began asking */
    uint32_t ciaddr;    /* our address when renewing or rebinding (then also the IP source), else 0 */
    uint32_t zadany;    /* option 50, the address we ask for (selecting), else 0 */
    uint32_t serwer;    /* option 54, the server we answer (selecting), else 0 */
    uint32_t cel;       /* the IP destination: 0xffffffff (everyone) or the server (renewing) */
};

/* A checked reply. Addresses in host order; for NAK only `typ` and `serwer`. */
struct k1_dhcp_odpowiedz {
    int typ;            /* K1_DHCP_OFFER / ACK / NAK */
    uint32_t adres;     /* yiaddr: the address for us */
    uint32_t serwer;    /* option 54: who answers */
    int prefiks;        /* option 1 (the mask), /24 when the router gives none */
    uint32_t router;    /* option 3, the first one (0: none) */
    uint32_t dzierzawa; /* option 51, seconds, within K1_SIEC_DZIERZAWA_MIN..MAKS (3600 when none) */
    uint32_t t1, t2;    /* options 58, 59 (0: none — the client takes 1/2 and 7/8 of the lease) */
    uint32_t czas[2];   /* option 42 (0.9.0 step 8): the first two time servers that can be (k1_siec_serwer_dobry) */
    int n_czas;
    uint32_t dns;       /* option 6 (0.10.0 step 2): the first DNS server that can be one (k1_siec_serwer_dobry), 0: none */
};

/* Builds the datagram of `z` into `bufor` (at least K1_DHCP_PAKIET_MAKS bytes): its length, or 0 for a wrong `z`. */
size_t k1_dhcp_buduj(const struct k1_dhcp_zapytanie *z, uint8_t *bufor, size_t maks);

/* Checks datagram `ip` (`n` bytes) as a reply to transaction `xid` of the card `mac`: the type (> 0) with `*o` filled,
 * or a reason < 0 (K1_DHCP_ZLY_*, K1_DHCP_OBCY…). */
int k1_dhcp_czytaj(const uint8_t *ip, size_t n, uint32_t xid, const uint8_t mac[6], struct k1_dhcp_odpowiedz *o);

/* The reason in Polish words (for the log): "zły nagłówek IP", … */
const char *k1_dhcp_powod(int powod);

#endif

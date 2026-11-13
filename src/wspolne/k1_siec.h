/* K1 — the network (0.9.0, ADR 0039): what nadzorca and the service "siec" share about a network card and its address.
 *
 * nadzorca (root) finds the cards, switches them on and opens a packet socket on the card with a cable; siec (in its
 * sandbox) asks the router for an address (DHCP) and asks nadzorca to set it. Addresses travel in messages as numbers
 * in host order (k1_komunikat.h: SIEC_ADRES); a MAC address in two fields (k1_siec_mac_do_pol). Pure functions,
 * tested on the host (test_siec) and fuzzed. */
#ifndef K1_SIEC_H
#define K1_SIEC_H

#include <stdint.h>

#define K1_SIEC_KARTA_MAKS 0x7fffffff     /* a card is its kernel number (ifindex) 1..this */
#define K1_SIEC_DZIERZAWA_MIN 10          /* seconds: a shorter lease from a router is taken as this */
#define K1_SIEC_DZIERZAWA_MAKS 0x7fffffff /* seconds ("for ever" from a router: 0xffffffff, taken as this) */
#define K1_SIEC_PREFIKS_MIN 8             /* the shortest mask taken from a router (255.0.0.0) */
#define K1_SIEC_PREFIKS_MAKS 30           /* the longest (255.255.255.252: two addresses) */

/* Why the cable of the card is gone (K1_KOM_SIEC_ROZLACZONO a). */
enum { K1_SIEC_KABEL_WYJETY = 1, K1_SIEC_KARTA_ZNIKNELA = 2 };
/* nadzorca's answer to SIEC_ADRES / SIEC_ZWOLNIJ (K1_KOM_SIEC_WYNIK b): 0 or one of these. */
enum { K1_SIEC_BLAD_KARTA = -1, K1_SIEC_BLAD_JADRO = -2, K1_SIEC_BLAD_MIN = -2 };

/* 1 when `adres` (host order) with mask /`prefiks` may be the address of this computer and `router` (0: none) its router:
 * prefiks K1_SIEC_PREFIKS_MIN..MAKS; neither of them 0.x, 127.x (loopback), 224.x and above (multicast, reserved,
 * broadcast) nor the first or the last address of the network; the router in the same network and not the address. */
int k1_siec_adres_dobry(uint32_t adres, int prefiks, uint32_t router);

/* The mask of /prefiks in host order (prefiks 0..32). */
uint32_t k1_siec_maska(int prefiks);
/* The prefix length of a mask (host order), or -1 when the mask is not contiguous ones then zeros. */
int k1_siec_prefiks(uint32_t maska);

/* 1 when a MAC address may be a card's own: not all zero, not a group address (bit 0 of the first byte). */
int k1_siec_mac_dobry(const uint8_t mac[6]);
/* A MAC address in two message fields: a = bytes 0..3 (byte 0 lowest), b = bytes 4..5 (0..0xFFFF), and back. */
void k1_siec_mac_do_pol(const uint8_t mac[6], int32_t *a, int32_t *b);
void k1_siec_mac_z_pol(int32_t a, int32_t b, uint8_t mac[6]);
/* "52:54:00:4b:31:01" (sysfs address, an optional newline) → 0, or -1 when it is not exactly that form. */
int k1_siec_mac_z_tekstu(const char *t, uint8_t mac[6]);

#endif

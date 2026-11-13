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
enum { K1_SIEC_BLAD_KARTA = -1, K1_SIEC_BLAD_JADRO = -2, K1_SIEC_BLAD_CZAS = -3, K1_SIEC_BLAD_MIN = -3 };

/* The clock from the internet (0.9.0 step 8, T5): siec asks up to K1_SIEC_ZRODLA_MAKS time servers at once (SNTP, port
 * 123; each through a UDP socket nadzorca opens and connects — SIEC_CZAS_GNIAZDO) and asks nadzorca to set the clock
 * only when most answers agree (SIEC_ZEGAR). Seconds since 1970 (UTC) within these bounds — nadzorca also checks that
 * the time is not before this K1's build. */
#define K1_SIEC_ZRODLA_MAKS 6
#define K1_SIEC_PORT_CZASU 123
#define K1_SIEC_CZAS_MIN 1767225600LL /* 2026-01-01 */
#define K1_SIEC_CZAS_MAKS 4102444800LL /* 2100-01-01 */
/* 1 when `a` (host order) may be a time server: one computer on the internet or in the home network — not 0.x, 127.x,
 * 224.x and above (multicast, reserved, broadcast). */
int k1_siec_serwer_dobry(uint32_t a);

/* What the person sees (0.9.0 step 7): the state of the network on the top bar and in Ustawienia → Sieć
 * (K1_KOM_SIEC_STAN a). */
enum k1_siec_stan {
    K1_SIEC_BRAK_KARTY = 0, /* no wired card with a driver in K1 */
    K1_SIEC_BEZ_KABLA = 1,  /* a wired card, no cable */
    K1_SIEC_LACZENIE = 2,   /* a cable is in, the router has not given an address (yet) */
    K1_SIEC_POLACZONO = 3,  /* an address from the router is set */
};
#define K1_SIEC_STAN_MAKS 3
/* The network cards of the computer (K1_KOM_SIEC_KARTA): wired ones nadzorca uses, and every PCI network device —
 * also those K1 has no driver for (Wi-Fi: a later version), so that the person can tell us its identifier. */
#define K1_SIEC_LISTA_MAKS 8
enum { K1_SIEC_PRZEWODOWA = 1, K1_SIEC_WIFI = 2, K1_SIEC_INNA = 3 }; /* PCI class 02 00, 02 80, other 02 xx */
#define K1_SIEC_KF_USB 1        /* on USB (an adapter): the identifier is idVendor:idProduct, else PCI vendor:device */
#define K1_SIEC_KF_STEROWNIK 2  /* the kernel has a driver for it */
#define K1_SIEC_KF_KABEL 4      /* a cable is in (wired with a driver only) */
#define K1_SIEC_KF_AKTYWNA 8    /* the card siec uses (wired with a driver and a cable only) */
#define K1_SIEC_KF_WSZYSTKIE 15
/* Details of a wired card with a driver (K1_KOM_SIEC_SZCZEGOLY, SIEC_STEROWNIK, SIEC_ADRESY — Ustawienia → Sieć). */
#define K1_SIEC_PREDKOSC_MAKS 800000 /* Mb/s: anything above is a driver's error */
enum { K1_SIEC_DUPLEKS_NIEZNANY = 0, K1_SIEC_DUPLEKS_POL = 1, K1_SIEC_DUPLEKS_PELNY = 2 };
#define K1_SIEC_STEROWNIK_MAKS 20 /* bytes of a driver's name in a message: a-z 0-9 _ - */
/* 1 when the 20 bytes of `t` are a driver's name: 1..20 of [a-z0-9_-], the rest zero. */
int k1_siec_sterownik_dobry(const char t[K1_SIEC_STEROWNIK_MAKS]);
/* A driver's name in the five fields a..e of a message (cut at 20 bytes; checked by the receiver), and back. */
void k1_siec_sterownik_do_pol(const char *nazwa, int32_t p[5]);
void k1_siec_sterownik_z_pol(const int32_t p[5], char nazwa[K1_SIEC_STEROWNIK_MAKS + 1]);

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

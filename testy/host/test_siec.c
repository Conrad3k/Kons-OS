/* K1 — host test of the network parts (0.9.0 step 6, ADR 0039): the shared checks of addresses (k1_siec.c), the
 * messages between nadzorca and siec (k1_kom_siec.c), DHCP packets (src/siec/dhcp.c: what we send, what we take —
 * every rejected kind and every shortened packet) and the DHCP client (src/siec/klient.c: every state and timer).
 * The replies of the "router" are built here by hand, byte by byte, not with the code under test. ASan/UBSan. */
#include <stdint.h>
#include <string.h>

#include "dhcp.h"
#include "k1_komunikat.h"
#include "k1_siec.h"
#include "k1_test_host.h"
#include "klient.h"

#define IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static const uint8_t MAC[6] = { 0x52, 0x54, 0x00, 0x4b, 0x31, 0x01 };
static const uint8_t ROUTER_MAC[6] = { 0x52, 0x54, 0x00, 0xee, 0x00, 0x02 };

static void p16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void p32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t g16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t g32(const uint8_t *p)
{
    return g16(p) << 16 | g16(p + 2);
}

/* The internet checksum, written again here (independent of dhcp.c): 0 over a correct header. */
static uint16_t suma(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    uint32_t s = 0;
    size_t i;
    for (i = 0; i < na; i += 2)
        s += (uint32_t)a[i] << 8 | (i + 1 < na ? a[i + 1] : 0);
    for (i = 0; i < nb; i += 2)
        s += (uint32_t)b[i] << 8 | (i + 1 < nb ? b[i + 1] : 0);
    while (s >> 16)
        s = (s & 0xffff) + (s >> 16);
    return (uint16_t)~s;
}

static uint16_t suma_udp(const uint8_t *ip, const uint8_t *udp, size_t n)
{
    uint8_t ps[12];
    memcpy(ps, ip + 12, 8);
    ps[8] = 0;
    ps[9] = 17;
    p16(ps + 10, (uint32_t)n);
    return suma(ps, sizeof ps, udp, n);
}

/* A reply of a router: the datagram into `b`, its length. `opcje` (n_op bytes) go after the magic cookie as given. */
static size_t odpowiedz(uint8_t *b, uint32_t xid, const uint8_t *mac, uint32_t yiaddr, const uint8_t *opcje, size_t n_op)
{
    size_t dl_dhcp = 240 + n_op, dl = 28 + dl_dhcp;
    uint8_t *u = b + 20, *d = b + 28;
    memset(b, 0, dl);
    b[0] = 0x45;
    p16(b + 2, (uint32_t)dl);
    b[8] = 64;
    b[9] = 17;
    p32(b + 12, IP(192, 168, 1, 1));
    p32(b + 16, 0xffffffffu);
    p16(b + 10, suma(b, 20, NULL, 0));
    p16(u, 67);
    p16(u + 2, 68);
    p16(u + 4, (uint32_t)(8 + dl_dhcp));
    d[0] = 2;
    d[1] = 1;
    d[2] = 6;
    p32(d + 4, xid);
    p32(d + 16, yiaddr);
    memcpy(d + 28, mac, 6);
    p32(d + 236, 0x63825363u);
    memcpy(d + 240, opcje, n_op);
    p16(u + 6, suma_udp(b, u, 8 + dl_dhcp));
    return dl;
}

/* Options of a typical home router: type, server, mask /24, router, lease 3600, end. */
static size_t opcje(uint8_t *o, int typ, uint32_t serwer, uint32_t maska, uint32_t router, uint32_t dzierzawa)
{
    size_t n = 0;
    o[n++] = 53, o[n++] = 1, o[n++] = (uint8_t)typ;
    if (serwer)
        o[n++] = 54, o[n++] = 4, p32(o + n, serwer), n += 4;
    if (maska)
        o[n++] = 1, o[n++] = 4, p32(o + n, maska), n += 4;
    if (router)
        o[n++] = 3, o[n++] = 4, p32(o + n, router), n += 4;
    if (dzierzawa)
        o[n++] = 51, o[n++] = 4, p32(o + n, dzierzawa), n += 4;
    o[n++] = 255;
    return n;
}

static size_t typowa(uint8_t *b, int typ, uint32_t xid, uint32_t yiaddr, uint32_t dzierzawa)
{
    uint8_t o[64];
    size_t n = opcje(o, typ, IP(192, 168, 1, 1), 0xffffff00u, IP(192, 168, 1, 1), dzierzawa);
    return odpowiedz(b, xid, MAC, yiaddr, o, n);
}

static void t_adresy(void)
{
    uint8_t m[6], m2[6];
    int32_t a, b;
    EXPECT(k1_siec_adres_dobry(IP(192, 168, 1, 10), 24, IP(192, 168, 1, 1)));
    EXPECT(k1_siec_adres_dobry(IP(10, 0, 2, 15), 24, 0));
    EXPECT(k1_siec_adres_dobry(IP(10, 1, 2, 3), 8, IP(10, 0, 0, 1)));
    EXPECT(k1_siec_adres_dobry(IP(172, 16, 0, 1), 30, IP(172, 16, 0, 2)));
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 10), 7, 0));   /* mask too short */
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 10), 31, 0));  /* too long */
    EXPECT(!k1_siec_adres_dobry(IP(0, 1, 2, 3), 24, 0));
    EXPECT(!k1_siec_adres_dobry(IP(127, 0, 0, 2), 8, 0));
    EXPECT(!k1_siec_adres_dobry(IP(224, 0, 0, 5), 24, 0));     /* multicast */
    EXPECT(!k1_siec_adres_dobry(IP(240, 1, 1, 1), 24, 0));
    EXPECT(!k1_siec_adres_dobry(0xffffffffu, 24, 0));
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 0), 24, 0));   /* the network itself */
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 255), 24, 0)); /* its broadcast */
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 10), 24, IP(192, 168, 2, 1))); /* router elsewhere */
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 10), 24, IP(192, 168, 1, 10))); /* router = us */
    EXPECT(!k1_siec_adres_dobry(IP(192, 168, 1, 10), 24, IP(192, 168, 1, 255)));
    EXPECT(k1_siec_maska(24) == 0xffffff00u && k1_siec_maska(0) == 0 && k1_siec_maska(32) == 0xffffffffu);
    EXPECT(k1_siec_prefiks(0xffffff00u) == 24 && k1_siec_prefiks(0) == 0 && k1_siec_prefiks(0xfffffffcu) == 30);
    EXPECT(k1_siec_prefiks(0xff00ff00u) == -1 && k1_siec_prefiks(0x00ffffffu) == -1);

    EXPECT(k1_siec_mac_z_tekstu("52:54:00:4b:31:01\n", m) == 0 && memcmp(m, MAC, 6) == 0);
    EXPECT(k1_siec_mac_z_tekstu("52:54:00:4B:31:01", m) == 0 && memcmp(m, MAC, 6) == 0);
    EXPECT(k1_siec_mac_z_tekstu("52:54:00:4b:31", m) == -1);
    EXPECT(k1_siec_mac_z_tekstu("52:54:00:4b:31:01:", m) == -1);
    EXPECT(k1_siec_mac_z_tekstu("52-54-00-4b-31-01", m) == -1);
    EXPECT(k1_siec_mac_z_tekstu("52:54:00:4b:31:0g", m) == -1);
    EXPECT(k1_siec_mac_z_tekstu("", m) == -1);
    EXPECT(k1_siec_mac_dobry(MAC));
    memset(m, 0, 6);
    EXPECT(!k1_siec_mac_dobry(m));
    memset(m, 0xff, 6);
    EXPECT(!k1_siec_mac_dobry(m)); /* broadcast: a group address */
    memcpy(m, (const uint8_t[]){ 0xfe, 0xff, 0x80, 0x81, 0xff, 0xfe }, 6);
    k1_siec_mac_do_pol(m, &a, &b);
    k1_siec_mac_z_pol(a, b, m2);
    EXPECT(memcmp(m, m2, 6) == 0 && b >= 0 && b <= 0xffff);
}

static int dobry(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat o;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &o) == 0;
}

static void t_komunikaty(void)
{
    int32_t a, b;
    uint8_t zly[6] = { 0x01, 0, 0x5e, 0, 0, 1 };
    enum k1_kanal_kierunek DO = K1_KIER_DO_NADZORCY, OD = K1_KIER_OD_NADZORCY;
    k1_siec_mac_do_pol(MAC, &a, &b);
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 2, a, b, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 2, a, b, 0, 0, 0), DO)); /* wrong direction */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 0, a, b, 0, 0, 0), OD)); /* no card 0 */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 2, a, 0x10000, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 2, a, b, 1, 0, 0), OD));
    k1_siec_mac_do_pol(zly, &a, &b);
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 2, a, b, 0, 0, 0), OD)); /* a multicast MAC */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, 2, 0, 0, 0, 0, 0), OD));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SIEC_KABEL, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_SIEC_ADRES, DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ROZLACZONO, 2, K1_SIEC_KABEL_WYJETY, 0, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ROZLACZONO, 2, K1_SIEC_KARTA_ZNIKNELA, 0, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ROZLACZONO, 2, 3, 0, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(192, 168, 1, 10), 24, (int32_t)IP(192, 168, 1, 1),
                                   3600, 0), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 24, 0, 10, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 24, 0, 9, 0), DO));  /* lease */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 24, 0, -1, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 255), 24, 0, 60, 0), DO)); /* broadcast */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(224, 0, 0, 1), 24, 0, 60, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 32, 0, 60, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 24, (int32_t)IP(10, 0, 3, 1), 60, 0),
                  DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 24, 0, 60, 1), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRES, 2, (int32_t)IP(10, 0, 2, 15), 24, 0, 60, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZWOLNIJ, 2, 0, 0, 0, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZWOLNIJ, 2, 1, 0, 0, 0, 0), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, 2, K1_KOM_SIEC_ADRES, 0, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, 2, K1_KOM_SIEC_ZWOLNIJ, K1_SIEC_BLAD_JADRO, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, 2, K1_KOM_SIEC_ADRES, -3, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, 2, K1_KOM_SIEC_KABEL, 0, 0, 0, 0), OD));
}

/* What we send: headers and checksums right (checked by the code of this file), the options of each kind. */
/* 0.9.0 step 7: the network for gui and Ustawienia — the state, the list of cards, the details of a card. */
static void t_stan(void)
{
    enum k1_kanal_kierunek OD = K1_KIER_OD_NADZORCY;
    const int P = K1_SIEC_PRZEWODOWA, ST = K1_SIEC_KF_STEROWNIK, KA = K1_SIEC_KF_KABEL, AK = K1_SIEC_KF_AKTYWNA;
    int32_t a, b, p[5];
    char t[K1_SIEC_STEROWNIK_MAKS + 1];
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, K1_SIEC_POLACZONO, 2, 120, 130, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, K1_SIEC_BRAK_KARTY, 0, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, K1_SIEC_POLACZONO, 2, 120, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, K1_SIEC_LACZENIE, 2, 120, 0, 0), OD)); /* "since" only connected */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, 4, 2, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, K1_SIEC_BEZ_KABLA, K1_SIEC_LISTA_MAKS + 1, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 1, K1_SIEC_BEZ_KABLA, 1, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, K1_SIEC_BEZ_KABLA, 1, 0, -1, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, (int32_t)0x808610d3, P, ST | KA | AK, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 7, 0x168c0042, K1_SIEC_WIFI, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, K1_SIEC_LISTA_MAKS, 0x168c0042, K1_SIEC_WIFI, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, 0x168c0042, K1_SIEC_WIFI, KA, 0, 0), OD)); /* Wi-Fi with a cable */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, 0x10ec8136, P, KA, 0, 0), OD));         /* no driver */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, 0x10ec8136, P, ST | AK, 0, 0), OD));    /* active, no cable */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, 0x10ec8136, 4, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, 0x10ec8136, P, 16, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_KARTA, 0, 0x10ec8136, P, ST, 1, 0), OD));
    k1_siec_mac_do_pol(MAC, &a, &b);
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, 0, 1000, K1_SIEC_DUPLEKS_PELNY, a, b, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, 0, 0, K1_SIEC_DUPLEKS_NIEZNANY, a, b, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, 0, K1_SIEC_PREDKOSC_MAKS + 1, 0, a, b, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, 0, 100, 3, a, b, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, 0, 100, 2, 0, 0, 0), OD)); /* no MAC */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, 0, -1, 2, a, b, 0), OD));
    k1_siec_sterownik_do_pol("e1000e", p);
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_STEROWNIK, 0, p[0], p[1], p[2], p[3], p[4]), OD));
    k1_siec_sterownik_z_pol(p, t);
    EXPECT(strcmp(t, "e1000e") == 0);
    k1_siec_sterownik_do_pol("virtio_net-12345678901", p); /* cut at 20 bytes */
    k1_siec_sterownik_z_pol(p, t);
    EXPECT(strcmp(t, "virtio_net-123456789") == 0 && k1_siec_sterownik_dobry(t));
    k1_siec_sterownik_do_pol("", p);
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STEROWNIK, 0, p[0], p[1], p[2], p[3], p[4]), OD)); /* empty */
    k1_siec_sterownik_do_pol("R8169", p);
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STEROWNIK, 0, p[0], p[1], p[2], p[3], p[4]), OD)); /* not a-z */
    k1_siec_sterownik_do_pol("r8169", p);
    p[4] = 0x41; /* a byte after the name's end */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_STEROWNIK, 0, p[0], p[1], p[2], p[3], p[4]), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRESY, 0, (int32_t)IP(10, 0, 2, 15), 24, (int32_t)IP(10, 0, 2, 2), 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRESY, 0, (int32_t)IP(127, 0, 0, 1), 8, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ADRESY, 0, (int32_t)IP(10, 0, 2, 15), 24, 0, 1, 0), OD));
}

static void t_wysylane(void)
{
    uint8_t b[K1_DHCP_PAKIET_MAKS];
    struct k1_dhcp_zapytanie z = { .typ = K1_DHCP_DISCOVER, .xid = 0x12345678, .cel = 0xffffffffu };
    size_t n;
    const uint8_t *d = b + 28, *o = d + 240;
    memcpy(z.mac, MAC, 6);
    n = k1_dhcp_buduj(&z, b, sizeof b);
    EXPECT(n == 328 && g16(b + 2) == n && b[0] == 0x45 && b[9] == 17 && suma(b, 20, NULL, 0) == 0);
    EXPECT(g32(b + 12) == 0 && g32(b + 16) == 0xffffffffu && g16(b + 20) == 68 && g16(b + 22) == 67);
    EXPECT(g16(b + 24) == n - 20 && suma_udp(b, b + 20, n - 20) == 0);
    EXPECT(d[0] == 1 && d[1] == 1 && d[2] == 6 && g32(d + 4) == 0x12345678 && memcmp(d + 28, MAC, 6) == 0);
    EXPECT(g32(d + 236) == 0x63825363u && o[0] == 53 && o[1] == 1 && o[2] == K1_DHCP_DISCOVER && o[3] == 55);
    z.typ = K1_DHCP_REQUEST;
    z.zadany = IP(192, 168, 1, 10);
    z.serwer = IP(192, 168, 1, 1);
    n = k1_dhcp_buduj(&z, b, sizeof b);
    EXPECT(n > 0 && o[2] == K1_DHCP_REQUEST && o[3] == 50 && g32(o + 5) == z.zadany && o[9] == 54 &&
           g32(o + 11) == z.serwer && suma_udp(b, b + 20, n - 20) == 0);
    memset(&z.zadany, 0, sizeof z.zadany);
    z.serwer = 0;
    z.ciaddr = IP(192, 168, 1, 10);
    z.cel = IP(192, 168, 1, 1); /* renewing: from our address straight to the router */
    n = k1_dhcp_buduj(&z, b, sizeof b);
    EXPECT(n > 0 && g32(b + 12) == z.ciaddr && g32(b + 16) == z.cel && g32(d + 12) == z.ciaddr && o[3] == 55 &&
           suma(b, 20, NULL, 0) == 0 && suma_udp(b, b + 20, n - 20) == 0);
    /* NEGATIVE: a type we never send, a group MAC, a too small buffer */
    z.typ = K1_DHCP_ACK;
    EXPECT(k1_dhcp_buduj(&z, b, sizeof b) == 0);
    z.typ = K1_DHCP_DISCOVER;
    EXPECT(k1_dhcp_buduj(&z, b, 100) == 0);
    z.mac[0] = 1;
    EXPECT(k1_dhcp_buduj(&z, b, sizeof b) == 0);
}

/* What we take: a typical OFFER, ACK, NAK, and everything else rejected with its reason. */
static void t_odbierane(void)
{
    uint8_t b[700], o[80];
    struct k1_dhcp_odpowiedz r;
    size_t n = typowa(b, K1_DHCP_OFFER, 7, IP(192, 168, 1, 10), 3600), i, no;
    int wszystkie_krotkie_odrzucone = 1;
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_OFFER && r.adres == IP(192, 168, 1, 10) && r.prefiks == 24 &&
           r.router == IP(192, 168, 1, 1) && r.serwer == IP(192, 168, 1, 1) && r.dzierzawa == 3600);
    EXPECT(k1_dhcp_czytaj(b, n + 30, 7, MAC, &r) == K1_DHCP_OFFER); /* padding after the datagram */
    for (i = 0; i < n; i++) /* NEGATIVE: every shortened packet */
        if (k1_dhcp_czytaj(b, i, 7, MAC, &r) >= 0)
            wszystkie_krotkie_odrzucone = 0;
    EXPECT(wszystkie_krotkie_odrzucone);
    EXPECT(k1_dhcp_czytaj(b, n, 8, MAC, &r) == K1_DHCP_OBCY && r.typ == 0);
    EXPECT(k1_dhcp_czytaj(b, n, 7, ROUTER_MAC, &r) == K1_DHCP_OBCY);
    b[10] ^= 1; /* IP checksum */
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_IP);
    b[10] ^= 1;
    b[6] |= 0x20; /* more fragments */
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_IP);
    b[6] &= (uint8_t)~0x20;
    b[60] ^= 0x40; /* a byte of the message: the UDP checksum no longer fits */
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_UDP);
    b[60] ^= 0x40;
    b[26] = b[27] = 0; /* no UDP checksum: allowed in IPv4 */
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_OFFER);
    b[21] = 69; /* from port 69 */
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_UDP);

    n = typowa(b, K1_DHCP_ACK, 7, IP(192, 168, 1, 10), 0);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ACK && r.dzierzawa == 3600); /* no lease: an hour */
    n = typowa(b, K1_DHCP_ACK, 7, IP(192, 168, 1, 10), 3);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ACK && r.dzierzawa == K1_SIEC_DZIERZAWA_MIN);
    n = typowa(b, K1_DHCP_ACK, 7, IP(192, 168, 1, 10), 0xffffffffu);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ACK && r.dzierzawa == K1_SIEC_DZIERZAWA_MAKS);
    no = opcje(o, K1_DHCP_NAK, IP(192, 168, 1, 1), 0, 0, 0);
    n = odpowiedz(b, 7, MAC, 0, o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_NAK && r.serwer == IP(192, 168, 1, 1) && r.adres == 0);
    no = opcje(o, K1_DHCP_OFFER, IP(10, 0, 2, 2), 0, 0, 60);
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_OFFER && r.prefiks == 24 && r.router == 0); /* no mask: /24 */

    /* NEGATIVE: the options */
    no = opcje(o, K1_DHCP_OFFER, 0, 0xffffff00u, 0, 60);
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLE_OPCJE); /* no server */
    no = opcje(o, K1_DHCP_OFFER, IP(10, 0, 2, 2), 0, 0, 60) - 1;
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLE_OPCJE); /* no end mark */
    no = opcje(o, K1_DHCP_OFFER, IP(10, 0, 2, 2), 0, 0, 60);
    o[no - 1] = 12, o[no] = 200; /* an option longer than what is left */
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no + 1);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLE_OPCJE);
    no = opcje(o, K1_DHCP_OFFER, IP(10, 0, 2, 2), 0, 0, 60);
    o[4] = 5; /* option 54 with length 5 */
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLE_OPCJE);
    no = opcje(o, K1_DHCP_REQUEST, IP(10, 0, 2, 2), 0, 0, 60);
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_TYP);
    /* NEGATIVE: an address that cannot be ours */
    no = opcje(o, K1_DHCP_OFFER, IP(10, 0, 2, 2), 0xff00ff00u, 0, 60);
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_ADRES);
    no = opcje(o, K1_DHCP_OFFER, IP(10, 0, 2, 2), 0xffffff00u, IP(10, 0, 3, 1), 60);
    n = odpowiedz(b, 7, MAC, IP(10, 0, 2, 15), o, no);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_ADRES);
    n = typowa(b, K1_DHCP_ACK, 7, 0, 60);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_ADRES);
    n = typowa(b, K1_DHCP_ACK, 7, IP(127, 0, 0, 1), 60);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_ADRES);
    /* NEGATIVE: not a reply at all */
    n = typowa(b, K1_DHCP_OFFER, 7, IP(192, 168, 1, 10), 60);
    b[28] = 1; /* BOOTREQUEST (another client's) */
    p16(b + 26, 0);
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_NAGLOWEK);
    b[28] = 2;
    b[28 + 236] = 0; /* the magic cookie */
    EXPECT(k1_dhcp_czytaj(b, n, 7, MAC, &r) == K1_DHCP_ZLY_NAGLOWEK);
    EXPECT(strcmp(k1_dhcp_powod(K1_DHCP_OBCY), "nie do nas (obca transakcja albo karta)") == 0 &&
           strcmp(k1_dhcp_powod(0), "?") == 0 && strcmp(k1_dhcp_powod(-8), "?") == 0);
}

static struct k1_dhcp_odpowiedz od(int typ, uint32_t adres, uint32_t serwer, uint32_t dzierzawa)
{
    struct k1_dhcp_odpowiedz o = { .typ = typ, .adres = adres, .serwer = serwer, .prefiks = 24,
                                   .router = IP(192, 168, 1, 1), .dzierzawa = dzierzawa };
    return o;
}

static void t_klient(void)
{
    struct k1_dhcp_klient k;
    struct k1_dhcp_odpowiedz o;
    uint32_t xid;
    const uint32_t S = IP(192, 168, 1, 1), A = IP(192, 168, 1, 10);
    int i;
    EXPECT(k1_dhcp_start(&k, MAC, 1234, 100.0) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_WYBOR &&
           k.zapytanie.typ == K1_DHCP_DISCOVER && k.zapytanie.cel == 0xffffffffu && !k.do_serwera && k.nastepny == 104.0);
    xid = k.xid;
    EXPECT(k1_dhcp_czas(&k, 103.9) == 0);
    EXPECT(k1_dhcp_czas(&k, 104.0) == K1_DHCP_A_WYSLIJ && k.nastepny == 112.0 && k.xid == xid && k.zapytanie.sekundy == 4);
    for (i = 0; i < 6; i++)
        k1_dhcp_czas(&k, k.nastepny);
    EXPECT(k.odstep == K1_DHCP_ODSTEP_MAKS && k.stan == K1_DHCP_WYBOR); /* retries: 4, 8, 16, 32, 64, 64 … */
    o = od(K1_DHCP_ACK, A, S, 3600);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 500) == 0 && k.stan == K1_DHCP_WYBOR); /* an ACK before an offer */
    o = od(K1_DHCP_OFFER, A, S, 3600);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 500) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_ZADANIE &&
           k.zapytanie.typ == K1_DHCP_REQUEST && k.zapytanie.zadany == A && k.zapytanie.serwer == S &&
           k.zapytanie.ciaddr == 0 && k.zapytanie.cel == 0xffffffffu);
    o = od(K1_DHCP_ACK, A, IP(192, 168, 1, 2), 3600); /* NEGATIVE: an ACK of another server */
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 501) == 0 && k.stan == K1_DHCP_ZADANIE);
    o = od(K1_DHCP_ACK, A, S, 3600);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 501) == K1_DHCP_A_USTAW && k.stan == K1_DHCP_ZWIAZANY && k.adres == A &&
           k.prefiks == 24 && k.router == S && k.t1 == 2301 && k.t2 == 501 + 3150 && k.koniec == 4101);
    o = od(K1_DHCP_OFFER, A, S, 3600);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 600) == 0 && k1_dhcp_czas(&k, 2300) == 0);
    /* T1: renewing — REQUEST from our address straight to our router */
    EXPECT(k1_dhcp_czas(&k, 2301) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_ODNAWIANIE && k.do_serwera &&
           memcmp(k.serwer_mac, ROUTER_MAC, 6) == 0 && k.zapytanie.ciaddr == A && k.zapytanie.cel == S &&
           k.zapytanie.zadany == 0 && k.zapytanie.serwer == 0 && k.zapytanie.typ == K1_DHCP_REQUEST);
    o = od(K1_DHCP_ACK, A, S, 3600);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 2302) == 0 && k.stan == K1_DHCP_ZWIAZANY && k.koniec == 5902); /* same */
    /* T2: rebinding — to everyone; an ACK with another address is set */
    EXPECT(k1_dhcp_czas(&k, k.t2) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_PONOWNIE && !k.do_serwera &&
           k.zapytanie.cel == 0xffffffffu && k.zapytanie.ciaddr == A);
    o = od(K1_DHCP_ACK, IP(192, 168, 1, 11), IP(192, 168, 1, 2), 600);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 6000) == K1_DHCP_A_USTAW && k.adres == IP(192, 168, 1, 11) &&
           k.serwer == IP(192, 168, 1, 2));
    /* the lease ends without an answer: the address goes, a new round with a new transaction */
    xid = k.xid;
    EXPECT(k1_dhcp_czas(&k, k.t1) == K1_DHCP_A_WYSLIJ && k1_dhcp_czas(&k, k.t2) == K1_DHCP_A_WYSLIJ);
    EXPECT(k1_dhcp_czas(&k, k.koniec) == (K1_DHCP_A_ZWOLNIJ | K1_DHCP_A_WYSLIJ) && k.stan == K1_DHCP_WYBOR &&
           k.xid != xid && k.adres == 0 && k.zapytanie.typ == K1_DHCP_DISCOVER);

    /* NAK while renewing: the address goes at once */
    o = od(K1_DHCP_OFFER, A, S, 100);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 7000);
    o = od(K1_DHCP_ACK, A, S, 100);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 7000) == K1_DHCP_A_USTAW && k.t1 == 7050);
    k1_dhcp_czas(&k, 7050);
    o = od(K1_DHCP_NAK, 0, S, 0);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 7051) == (K1_DHCP_A_ZWOLNIJ | K1_DHCP_A_WYSLIJ) && k.stan == K1_DHCP_WYBOR);
    /* NAK to our REQUEST: a new round; a router that never answers the REQUEST: after 4 tries too */
    o = od(K1_DHCP_OFFER, A, S, 100);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 8000);
    o = od(K1_DHCP_NAK, 0, S, 0);
    EXPECT(k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 8001) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_WYBOR &&
           k.zapytanie.typ == K1_DHCP_DISCOVER);
    o = od(K1_DHCP_OFFER, A, S, 100);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 9000);
    for (i = 0; i < 3; i++)
        EXPECT(k1_dhcp_czas(&k, k.nastepny) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_ZADANIE &&
               k.zapytanie.typ == K1_DHCP_REQUEST);
    EXPECT(k1_dhcp_czas(&k, k.nastepny) == K1_DHCP_A_WYSLIJ && k.stan == K1_DHCP_WYBOR);
    /* T1, T2 from the router: used when they make sense, else 1/2 and 7/8 */
    o = od(K1_DHCP_OFFER, A, S, 1000);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 0);
    o = od(K1_DHCP_ACK, A, S, 1000);
    o.t1 = 300;
    o.t2 = 600;
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 0);
    EXPECT(k.t1 == 300 && k.t2 == 600);
    o.t1 = 5000;
    o.t2 = 900;
    k1_dhcp_czas(&k, 300);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 0);
    EXPECT(k.t1 == 500 && k.t2 == 900);
    /* the cable out: nothing more to do */
    k1_dhcp_stop(&k);
    EXPECT(k.stan == K1_DHCP_BEZ_KABLA && k1_dhcp_czas(&k, 1e9) == 0 && k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 1) == 0);
    /* a seed of 0 still gives changing transaction numbers */
    k1_dhcp_start(&k, MAC, 0, 0);
    xid = k.xid;
    k1_dhcp_czas(&k, 4);
    o = od(K1_DHCP_OFFER, A, S, 100);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 5);
    o = od(K1_DHCP_NAK, 0, S, 0);
    k1_dhcp_odebrano(&k, &o, ROUTER_MAC, 6);
    EXPECT(k.xid != xid && k.xid != 0);
}

int main(void)
{
    t_adresy();
    t_komunikaty();
    t_stan();
    t_wysylane();
    t_odbierane();
    t_klient();
    return K1_TEST_KONIEC("test_siec");
}

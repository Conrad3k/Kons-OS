/* K1 siec — DHCP packets. See dhcp.h. */
#include "dhcp.h"

#include <string.h>

#include "k1_siec.h"

#define IP_NAGL 20
#define UDP_NAGL 8
#define DHCP_STALE 236       /* op … file: the fixed part of a DHCP message */
#define CIASTKO 0x63825363u  /* the magic cookie before the options */
#define PORT_SERWERA 67
#define PORT_KLIENTA 68

static void u16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t c16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

static uint32_t c32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* The internet checksum (RFC 1071) of `n` bytes, added to `s`. */
static uint32_t suma(uint32_t s, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n; i += 2)
        s += c16(p + i);
    if (n & 1)
        s += (uint32_t)p[n - 1] << 8;
    return s;
}

static uint16_t zloz(uint32_t s)
{
    while (s >> 16)
        s = (s & 0xffffu) + (s >> 16);
    return (uint16_t)~s;
}

/* The UDP checksum over the pseudo header (source, destination, protocol 17, length) and the UDP datagram. */
static uint16_t suma_udp(uint32_t zrodlo, uint32_t cel, const uint8_t *udp, size_t n)
{
    uint8_t pseudo[12];
    u32(pseudo, zrodlo);
    u32(pseudo + 4, cel);
    pseudo[8] = 0;
    pseudo[9] = 17;
    u16(pseudo + 10, (uint32_t)n);
    return zloz(suma(suma(0, pseudo, sizeof pseudo), udp, n));
}

static uint8_t *opcja32(uint8_t *p, uint8_t kod, uint32_t v)
{
    p[0] = kod;
    p[1] = 4;
    u32(p + 2, v);
    return p + 6;
}

size_t k1_dhcp_buduj(const struct k1_dhcp_zapytanie *z, uint8_t *bufor, size_t maks)
{
    uint8_t *ip = bufor, *udp = bufor + IP_NAGL, *d = udp + UDP_NAGL, *o;
    size_t dl_dhcp, dl;
    uint16_t s;
    if ((z->typ != K1_DHCP_DISCOVER && z->typ != K1_DHCP_REQUEST) || maks < K1_DHCP_PAKIET_MAKS ||
        !k1_siec_mac_dobry(z->mac))
        return 0;
    memset(bufor, 0, K1_DHCP_PAKIET_MAKS);
    d[0] = 1; /* BOOTREQUEST */
    d[1] = 1; /* Ethernet */
    d[2] = 6;
    u32(d + 4, z->xid);
    u16(d + 8, z->sekundy);
    /* flags: 0 — the router may answer to our MAC (the kernel gives the packet socket every IPv4 datagram) */
    u32(d + 12, z->ciaddr);
    memcpy(d + 28, z->mac, 6);
    o = d + DHCP_STALE;
    u32(o, CIASTKO);
    o += 4;
    *o++ = 53;
    *o++ = 1;
    *o++ = (uint8_t)z->typ;
    if (z->zadany != 0)
        o = opcja32(o, 50, z->zadany);
    if (z->serwer != 0)
        o = opcja32(o, 54, z->serwer);
    *o++ = 55; /* what we want to know: the mask, the router, the lease, T1, T2, the time servers */
    *o++ = 6;
    *o++ = 1;
    *o++ = 3;
    *o++ = 42;
    *o++ = 51;
    *o++ = 58;
    *o++ = 59;
    *o++ = 57; /* the largest message we take: 576 */
    *o++ = 2;
    u16(o, K1_DHCP_PAKIET_MAKS);
    o += 2;
    *o++ = 255;
    dl_dhcp = (size_t)(o - d);
    if (dl_dhcp < 300)
        dl_dhcp = 300; /* BOOTP's minimum: some routers ignore shorter messages (padding is zeros) */
    dl = IP_NAGL + UDP_NAGL + dl_dhcp;

    ip[0] = 0x45;
    u16(ip + 2, (uint32_t)dl);
    u16(ip + 6, 0x4000); /* do not fragment */
    ip[8] = 64;
    ip[9] = 17;
    u32(ip + 12, z->ciaddr);
    u32(ip + 16, z->cel);
    u16(ip + 10, zloz(suma(0, ip, IP_NAGL)));
    u16(udp, PORT_KLIENTA);
    u16(udp + 2, PORT_SERWERA);
    u16(udp + 4, (uint32_t)(UDP_NAGL + dl_dhcp));
    s = suma_udp(z->ciaddr, z->cel, udp, UDP_NAGL + dl_dhcp);
    u16(udp + 6, s != 0 ? s : 0xffffu);
    return dl;
}

/* The IP and UDP headers: where the DHCP message starts (*d, *n_d), or a reason < 0. */
static int naglowki(const uint8_t *ip, size_t n, const uint8_t **d, size_t *n_d)
{
    size_t ihl, dl, dl_udp;
    const uint8_t *udp;
    uint32_t zrodlo, cel;
    if (n < IP_NAGL || (ip[0] >> 4) != 4)
        return K1_DHCP_ZLY_IP;
    ihl = (size_t)(ip[0] & 15) * 4;
    dl = c16(ip + 2);
    if (ihl < IP_NAGL || dl < ihl || dl > n || zloz(suma(0, ip, ihl)) != 0 || (c16(ip + 6) & 0x3fff) != 0)
        return K1_DHCP_ZLY_IP; /* the more-fragments bit or an offset: a piece of a datagram */
    if (ip[9] != 17 || dl - ihl < UDP_NAGL)
        return K1_DHCP_ZLY_UDP;
    udp = ip + ihl;
    dl_udp = c16(udp + 4);
    if (c16(udp) != PORT_SERWERA || c16(udp + 2) != PORT_KLIENTA || dl_udp < UDP_NAGL || dl_udp > dl - ihl)
        return K1_DHCP_ZLY_UDP;
    zrodlo = c32(ip + 12);
    cel = c32(ip + 16);
    if (c16(udp + 6) != 0 && suma_udp(zrodlo, cel, udp, dl_udp) != 0)
        return K1_DHCP_ZLY_UDP;
    *d = udp + UDP_NAGL;
    *n_d = dl_udp - UDP_NAGL;
    return 0;
}

/* Option 42: up to two time servers that can be one (the rest, and bad ones, skipped — not a reason to reject). */
static void czas_z_opcji(const uint8_t *p, uint8_t dl, struct k1_dhcp_odpowiedz *o)
{
    unsigned i;
    o->n_czas = 0;
    for (i = 0; i + 4 <= dl && o->n_czas < 2; i += 4)
        if (k1_siec_serwer_dobry(c32(p + i)))
            o->czas[o->n_czas++] = c32(p + i);
}

/* The options we use; the rest is skipped. Every length is checked against the end of the message. */
static int opcje(const uint8_t *p, size_t n, struct k1_dhcp_odpowiedz *o, uint32_t *maska)
{
    size_t i = 0;
    int typ = 0;
    while (i < n) {
        uint8_t kod = p[i], dl;
        if (kod == 0) {
            i++;
            continue;
        }
        if (kod == 255)
            break;
        if (i + 2 > n || i + 2 + p[i + 1] > n)
            return K1_DHCP_ZLE_OPCJE;
        dl = p[i + 1];
        if ((kod == 53 && dl != 1) || ((kod == 54 || kod == 1 || kod == 51 || kod == 58 || kod == 59) && dl != 4) ||
            ((kod == 3 || kod == 42) && (dl < 4 || dl % 4 != 0)))
            return K1_DHCP_ZLE_OPCJE;
        if (kod == 53)
            typ = p[i + 2];
        else if (kod == 54)
            o->serwer = c32(p + i + 2);
        else if (kod == 1)
            *maska = c32(p + i + 2);
        else if (kod == 3)
            o->router = c32(p + i + 2);
        else if (kod == 51)
            o->dzierzawa = c32(p + i + 2);
        else if (kod == 42)
            czas_z_opcji(p + i + 2, dl, o);
        else if (kod == 58)
            o->t1 = c32(p + i + 2);
        else if (kod == 59)
            o->t2 = c32(p + i + 2);
        i += 2u + dl;
    }
    if (i >= n || typ == 0 || o->serwer == 0)
        return K1_DHCP_ZLE_OPCJE; /* no end mark, no type, no server */
    return typ;
}

int k1_dhcp_czytaj(const uint8_t *ip, size_t n, uint32_t xid, const uint8_t mac[6], struct k1_dhcp_odpowiedz *o)
{
    const uint8_t *d;
    size_t n_d;
    uint32_t maska = 0;
    int r = naglowki(ip, n, &d, &n_d), typ;
    memset(o, 0, sizeof *o);
    if (r < 0)
        return r;
    if (n_d < DHCP_STALE + 4 || d[0] != 2 || d[1] != 1 || d[2] != 6 || c32(d + DHCP_STALE) != CIASTKO)
        return K1_DHCP_ZLY_NAGLOWEK;
    if (c32(d + 4) != xid || memcmp(d + 28, mac, 6) != 0)
        return K1_DHCP_OBCY;
    typ = opcje(d + DHCP_STALE + 4, n_d - DHCP_STALE - 4, o, &maska);
    if (typ < 0)
        return typ;
    if (typ != K1_DHCP_OFFER && typ != K1_DHCP_ACK && typ != K1_DHCP_NAK) {
        memset(o, 0, sizeof *o);
        return K1_DHCP_ZLY_TYP;
    }
    o->typ = typ;
    if (typ == K1_DHCP_NAK)
        return typ;
    o->adres = c32(d + 16);
    o->prefiks = maska != 0 ? k1_siec_prefiks(maska) : 24;
    if (o->dzierzawa == 0)
        o->dzierzawa = 3600;
    if (o->dzierzawa < K1_SIEC_DZIERZAWA_MIN)
        o->dzierzawa = K1_SIEC_DZIERZAWA_MIN;
    if (o->dzierzawa > K1_SIEC_DZIERZAWA_MAKS)
        o->dzierzawa = K1_SIEC_DZIERZAWA_MAKS;
    if (!k1_siec_adres_dobry(o->adres, o->prefiks, o->router)) {
        memset(o, 0, sizeof *o);
        return K1_DHCP_ZLY_ADRES;
    }
    return typ;
}

const char *k1_dhcp_powod(int powod)
{
    static const char *const nazwy[] = { "zły nagłówek IP", "zły nagłówek UDP", "to nie odpowiedź DHCP",
                                         "nie do nas (obca transakcja albo karta)", "złe opcje DHCP",
                                         "nieoczekiwany rodzaj wiadomości DHCP", "adres, maska albo router nie do przyjęcia" };
    return powod < 0 && powod >= K1_DHCP_POWOD_MIN ? nazwy[-powod - 1] : "?";
}

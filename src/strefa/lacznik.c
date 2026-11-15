/* K1 strefa — the connector's protocols, SOCKS5 and DNS (lacznik.h). Every length is checked against what came;
 * nothing is assumed about the other side. */
#include <string.h>

#include "k1_siec.h"
#include "lacznik.h"

#define WSKAZNIKI_MAKS 16 /* compression pointers followed in one name */

int socks_powitanie(const uint8_t *b, size_t n, size_t *zuzyte)
{
    size_t i;
    if (n < 1)
        return 0;
    if (b[0] != 5)
        return -1;
    if (n < 2)
        return 0;
    if (b[1] == 0)
        return -1;
    if (n < 2u + b[1])
        return 0;
    for (i = 0; i < b[1]; i++)
        if (b[2 + i] == 0) {
            *zuzyte = 2u + b[1];
            return 1;
        }
    return -1;
}

static int litera(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

int nazwa_dobra(const char *s, size_t n)
{
    size_t i, etykieta = 0;
    if (n == 0 || n > NAZWA_MAKS)
        return 0;
    for (i = 0; i < n; i++) {
        if (s[i] == '.') {
            if (etykieta == 0 || i + 1 == n)
                return 0;
            etykieta = 0;
        } else if (!litera(s[i]) || ++etykieta > 63) {
            return 0;
        }
    }
    return 1;
}

int nazwa_ipv4(const char *s, uint32_t *a)
{
    uint32_t v = 0, czesc;
    int i, cyfry;
    for (i = 0; i < 4; i++) {
        for (czesc = 0, cyfry = 0; *s >= '0' && *s <= '9' && cyfry < 4; s++, cyfry++)
            czesc = czesc * 10u + (uint32_t)(*s - '0');
        if (cyfry == 0 || cyfry > 3 || czesc > 255 || (i < 3 && *s++ != '.'))
            return 0;
        v = v << 8 | czesc;
    }
    if (*s != '\0')
        return 0;
    *a = v;
    return 1;
}

int socks_prosba(const uint8_t *b, size_t n, size_t *zuzyte, struct socks_cel *c)
{
    size_t dl, adr;
    if (n < 4)
        return 0;
    if (b[0] != 5 || b[2] != 0)
        return -SOCKS_BLAD;
    if (b[3] == 4)
        return -SOCKS_TYP_ADRESU; /* IPv6: K1 has none */
    if (b[3] != 1 && b[3] != 3)
        return -SOCKS_TYP_ADRESU;
    if (b[3] == 1) {
        adr = 4;
        dl = 4u + 4u + 2u;
    } else {
        if (n < 5)
            return 0;
        adr = b[4];
        dl = 4u + 1u + adr + 2u;
    }
    if (n < dl)
        return 0;
    if (b[1] != 1)
        return -SOCKS_POLECENIE; /* only CONNECT: no BIND, no UDP */
    memset(c, 0, sizeof *c);
    c->port = (uint16_t)(b[dl - 2] << 8 | b[dl - 1]);
    if (c->port == 0)
        return -SOCKS_BLAD;
    if (b[3] == 1) {
        c->adres = (uint32_t)b[4] << 24 | (uint32_t)b[5] << 16 | (uint32_t)b[6] << 8 | b[7];
        if (!k1_siec_serwer_dobry(c->adres))
            return -SOCKS_NIE_WOLNO; /* 0.0.0.0, loopback, multicast: never an address to reach */
    } else {
        if (!nazwa_dobra((const char *)b + 5, adr))
            return -SOCKS_BLAD;
        memcpy(c->nazwa, b + 5, adr);
        c->nazwa[adr] = '\0';
        if (nazwa_ipv4(c->nazwa, &c->adres)) {
            c->nazwa[0] = '\0';
            if (!k1_siec_serwer_dobry(c->adres)) /* "00.0.0.0" too: an address, never a name to look up */
                return -SOCKS_NIE_WOLNO;
        }
    }
    if (c->adres != 0 && !k1_siec_serwer_dobry(c->adres))
        return -SOCKS_NIE_WOLNO;
    *zuzyte = dl;
    return 1;
}

void socks_odpowiedz(uint8_t o[10], int kod)
{
    memset(o, 0, 10);
    o[0] = 5;
    o[1] = (uint8_t)kod;
    o[3] = 1;
}

size_t dns_zapytanie(uint8_t *b, size_t maks, uint16_t id, const char *nazwa)
{
    size_t n = strlen(nazwa), i = 12, od = 0, k;
    if (!nazwa_dobra(nazwa, n) || maks < 12 + n + 2 + 4)
        return 0;
    memset(b, 0, 12);
    b[0] = (uint8_t)(id >> 8), b[1] = (uint8_t)id;
    b[2] = 0x01; /* RD */
    b[5] = 1;    /* QDCOUNT */
    for (k = 0; k <= n; k++)
        if (k == n || nazwa[k] == '.') {
            b[i++] = (uint8_t)(k - od);
            memcpy(b + i, nazwa + od, k - od);
            i += k - od;
            od = k + 1;
        }
    b[i++] = 0;
    b[i++] = 0, b[i++] = 1; /* A */
    b[i++] = 0, b[i++] = 1; /* IN */
    return i;
}

/* Skips the name at *p (labels or a pointer); 0, or -1 for a name past the end or a pointer loop. */
static int pomin_nazwe(const uint8_t *b, size_t n, size_t *p)
{
    size_t i = *p;
    for (;;) {
        if (i >= n)
            return -1;
        if (b[i] == 0) {
            *p = i + 1;
            return 0;
        }
        if ((b[i] & 0xC0) == 0xC0) {
            if (i + 2 > n)
                return -1;
            *p = i + 2; /* the pointed name is not read here: its length is the pointer's 2 bytes */
            return 0;
        }
        if (b[i] & 0xC0)
            return -1;
        i += 1u + b[i];
    }
}

static char male(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

/* The question's name (uncompressed in our own question; a pointer is followed backwards, a bounded number of times)
 * against `nazwa`, case-insensitively; *p past it. 1 the same, 0 not, -1 malformed. */
static int ta_sama(const uint8_t *b, size_t n, size_t *p, const char *nazwa)
{
    size_t i = *p, j = 0, za = 0, skoki = 0, k;
    for (;;) {
        if (i >= n)
            return -1;
        if ((b[i] & 0xC0) == 0xC0) {
            size_t cel;
            if (i + 2 > n || ++skoki > WSKAZNIKI_MAKS)
                return -1;
            cel = (size_t)(b[i] & 0x3F) << 8 | b[i + 1];
            if (za == 0)
                za = i + 2;
            if (cel >= i)
                return -1; /* only backwards: no loops */
            i = cel;
            continue;
        }
        if (b[i] & 0xC0)
            return -1;
        if (b[i] == 0) {
            *p = za ? za : i + 1;
            return nazwa[j] == '\0';
        }
        if (i + 1u + b[i] > n)
            return -1;
        if (j > 0 && nazwa[j++] != '.')
            return 0;
        for (k = 0; k < b[i]; k++, j++)
            if (nazwa[j] == '\0' || male((char)b[i + 1 + k]) != male(nazwa[j]))
                return 0;
        i += 1u + b[i];
    }
}

int dns_odpowiedz(const uint8_t *b, size_t n, uint16_t id, const char *nazwa, uint32_t *adres)
{
    size_t p = 12;
    unsigned an, i;
    if (n < 12 || (uint16_t)(b[0] << 8 | b[1]) != id || !(b[2] & 0x80) || (b[2] & 0x78) != 0 || b[4] != 0 || b[5] != 1)
        return -1; /* not an answer to our standard query with one question */
    if (ta_sama(b, n, &p, nazwa) != 1 || p + 4 > n || b[p] != 0 || b[p + 1] != 1 || b[p + 2] != 0 || b[p + 3] != 1)
        return -1;
    p += 4;
    if ((b[3] & 0x0F) != 0)
        return 0; /* NXDOMAIN, SERVFAIL, …: no address */
    an = (unsigned)(b[6] << 8 | b[7]);
    for (i = 0; i < an; i++) {
        unsigned typ, klasa, dl;
        if (pomin_nazwe(b, n, &p) != 0 || p + 10 > n)
            return -1;
        typ = (unsigned)(b[p] << 8 | b[p + 1]);
        klasa = (unsigned)(b[p + 2] << 8 | b[p + 3]);
        dl = (unsigned)(b[p + 8] << 8 | b[p + 9]);
        p += 10;
        if (p + dl > n)
            return -1;
        if (typ == 1 && klasa == 1 && dl == 4) {
            uint32_t a = (uint32_t)b[p] << 24 | (uint32_t)b[p + 1] << 16 | (uint32_t)b[p + 2] << 8 | b[p + 3];
            if (k1_siec_serwer_dobry(a)) {
                *adres = a;
                return 1;
            }
        }
        p += dl;
    }
    return 0;
}

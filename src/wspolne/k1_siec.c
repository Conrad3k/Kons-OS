/* K1 — the network: shared checks of addresses. See k1_siec.h. */
#include "k1_siec.h"

#include <string.h>

uint32_t k1_siec_maska(int prefiks)
{
    if (prefiks <= 0)
        return 0;
    if (prefiks >= 32)
        return 0xffffffffu;
    return 0xffffffffu << (32 - prefiks);
}

int k1_siec_prefiks(uint32_t maska)
{
    int p = 0;
    while (p < 32 && (maska & (0x80000000u >> p)) != 0)
        p++;
    return k1_siec_maska(p) == maska ? p : -1;
}

/* An address one of the computers of a network may have: not 0.x, 127.x, 224.x and above. */
static int zwykly(uint32_t a)
{
    uint32_t pierwszy = a >> 24;
    return pierwszy != 0 && pierwszy != 127 && pierwszy < 224;
}

/* Neither the network itself (all host bits 0) nor its broadcast (all 1). */
static int w_sieci(uint32_t a, uint32_t maska)
{
    return (a & ~maska) != 0 && (a & ~maska) != ~maska;
}

int k1_siec_adres_dobry(uint32_t adres, int prefiks, uint32_t router)
{
    uint32_t m;
    if (prefiks < K1_SIEC_PREFIKS_MIN || prefiks > K1_SIEC_PREFIKS_MAKS)
        return 0;
    m = k1_siec_maska(prefiks);
    if (!zwykly(adres) || !w_sieci(adres, m))
        return 0;
    if (router == 0)
        return 1;
    return router != adres && zwykly(router) && w_sieci(router, m) && (router & m) == (adres & m);
}

int k1_siec_mac_dobry(const uint8_t mac[6])
{
    static const uint8_t zera[6];
    return memcmp(mac, zera, 6) != 0 && (mac[0] & 1) == 0;
}

void k1_siec_mac_do_pol(const uint8_t mac[6], int32_t *a, int32_t *b)
{
    uint32_t x = (uint32_t)mac[0] | (uint32_t)mac[1] << 8 | (uint32_t)mac[2] << 16 | (uint32_t)mac[3] << 24;
    memcpy(a, &x, sizeof x); /* the bit pattern, also when the top bit is set */
    *b = (int32_t)((uint32_t)mac[4] | (uint32_t)mac[5] << 8);
}

void k1_siec_mac_z_pol(int32_t a, int32_t b, uint8_t mac[6])
{
    uint32_t x;
    memcpy(&x, &a, sizeof x);
    mac[0] = (uint8_t)x;
    mac[1] = (uint8_t)(x >> 8);
    mac[2] = (uint8_t)(x >> 16);
    mac[3] = (uint8_t)(x >> 24);
    mac[4] = (uint8_t)b;
    mac[5] = (uint8_t)((uint32_t)b >> 8);
}

static int szesnastkowa(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

int k1_siec_mac_z_tekstu(const char *t, uint8_t mac[6])
{
    int i;
    for (i = 0; i < 6; i++) {
        int g = szesnastkowa(t[3 * i]), d = g < 0 ? -1 : szesnastkowa(t[3 * i + 1]);
        if (g < 0 || d < 0 || (i < 5 && t[3 * i + 2] != ':'))
            return -1;
        mac[i] = (uint8_t)(g << 4 | d);
    }
    return t[17] == '\0' || (t[17] == '\n' && t[18] == '\0') ? 0 : -1;
}

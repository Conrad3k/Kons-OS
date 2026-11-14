/* K1 siec — SNTP packets: the question and the checking of the answer. See sntp.h. */
#include "sntp.h"

#include "k1_siec.h"

static uint32_t c32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t c64(const uint8_t *p)
{
    return (uint64_t)c32(p) << 32 | c32(p + 4);
}

void k1_sntp_zapytanie(uint8_t b[K1_SNTP_DLUGOSC], uint64_t los)
{
    int i;
    for (i = 0; i < K1_SNTP_DLUGOSC; i++)
        b[i] = 0;
    b[0] = 0 << 6 | 4 << 3 | 3; /* no leap warning, version 4, mode 3 (a client) */
    for (i = 0; i < 8; i++)
        b[40 + i] = (uint8_t)(los >> (56 - 8 * i));
}

/* An NTP timestamp (seconds since 1900 in 32 bits + fraction) as seconds since 1970. The 32 bits wrap in 2036 (era 1):
 * a value below 2^31 is taken as era 1 — K1 never runs before 2026, nor after 2104. */
static double unix_z_ntp(uint64_t t)
{
    uint64_t s = t >> 32;
    if (s < 0x80000000ULL)
        s += 0x100000000ULL;
    return (double)((int64_t)s - (int64_t)K1_NTP_OD_1900) + (double)(t & 0xffffffffULL) / 4294967296.0;
}

int k1_sntp_czytaj(const uint8_t *b, size_t n, uint64_t los, double *czas)
{
    unsigned skok, wersja, tryb, warstwa;
    double opoznienie, rozrzut;
    *czas = 0;
    if (n < K1_SNTP_DLUGOSC || n > K1_SNTP_ODPOWIEDZ_MAKS)
        return K1_SNTP_ZLA_DLUGOSC;
    skok = b[0] >> 6;
    wersja = (b[0] >> 3) & 7;
    tryb = b[0] & 7;
    warstwa = b[1];
    if ((wersja != 3 && wersja != 4) || tryb != 4)
        return K1_SNTP_ZLY_TRYB;
    if (los == 0 || c64(b + 24) != los)
        return K1_SNTP_NIE_DO_NAS;
    if (warstwa == 0 || warstwa >= 16 || skok == 3 || c64(b + 32) == 0 || c64(b + 40) == 0)
        return K1_SNTP_BEZ_CZASU;
    opoznienie = (double)(int32_t)c32(b + 4) / 65536.0; /* NTP short format: 16.16 seconds */
    rozrzut = (double)c32(b + 8) / 65536.0;
    if (opoznienie < 0 || opoznienie / 2 + rozrzut >= 1.0)
        return K1_SNTP_NIEPEWNY;
    *czas = unix_z_ntp(c64(b + 40));
    if (*czas < (double)K1_SIEC_CZAS_MIN || *czas > (double)K1_SIEC_CZAS_MAKS) {
        *czas = 0;
        return K1_SNTP_ZLY_CZAS;
    }
    return 0;
}

const char *k1_sntp_powod(int powod)
{
    static const char *const p[] = { "zła długość", "nie serwer czasu", "nie do nas (inny znacznik)",
                                     "serwer bez czasu", "serwer niepewny", "czas poza granicami" };
    return powod <= -1 && powod >= K1_SNTP_POWOD_MIN ? p[-powod - 1] : "?";
}

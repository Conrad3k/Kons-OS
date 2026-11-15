/* K1 — host test of the zones' connector (0.10.0 step 2, ADR 0043 T9): SOCKS5 as the browser speaks it and what we
 * refuse (src/strefa/lacznik.c), DNS questions and hostile answers, the owner's D3 rule (k1_siec_dla_strefy) and the
 * messages between nadzorca and a zone's VMM (k1_kom_strefa.c). Answers are built here byte by byte. */
#include <string.h>

#include "k1_komunikat.h"
#include "k1_siec.h"
#include "k1_test_host.h"
#include "lacznik.h"

#define IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

static void t_powitanie(void)
{
    static const uint8_t dobre[] = { 5, 2, 2, 0 }, bez_zera[] = { 5, 1, 2 }, socks4[] = { 4, 1, 0 }, zero[] = { 5, 0 };
    size_t z = 0;
    EXPECT(socks_powitanie(dobre, 1, &z) == 0 && socks_powitanie(dobre, 3, &z) == 0);
    EXPECT(socks_powitanie(dobre, sizeof dobre, &z) == 1 && z == 4);
    EXPECT(socks_powitanie(bez_zera, sizeof bez_zera, &z) == -1); /* only username/password: refused */
    EXPECT(socks_powitanie(socks4, sizeof socks4, &z) == -1);
    EXPECT(socks_powitanie(zero, sizeof zero, &z) == -1); /* no methods */
}

static void t_prosba(void)
{
    static const uint8_t ipv4[] = { 5, 1, 0, 1, 93, 184, 216, 34, 0x01, 0xBB };
    static const uint8_t nazwa[] = { 5, 1, 0, 3, 11, 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm', 0, 80 };
    static const uint8_t ipv6[] = { 5, 1, 0, 4 }, bind[] = { 5, 2, 0, 1, 1, 2, 3, 4, 0, 80 },
                         udp[] = { 5, 3, 0, 1, 1, 2, 3, 4, 0, 80 }, rsv[] = { 5, 1, 1, 1, 1, 2, 3, 4, 0, 80 },
                         port0[] = { 5, 1, 0, 1, 1, 2, 3, 4, 0, 0 }, zla[] = { 5, 1, 0, 3, 3, 'a', ' ', 'b', 0, 80 },
                         petla[] = { 5, 1, 0, 1, 127, 0, 0, 1, 0, 80 }, lit[] = { 5, 1, 0, 3, 7, '1', '.', '2', '.', '3',
                                                                                 '.', '4', 0, 80 };
    uint8_t dluga[4 + 1 + 255 + 2] = { 5, 1, 0, 3, 255 };
    struct socks_cel c;
    size_t z = 0, i;
    EXPECT(socks_prosba(ipv4, 9, &z, &c) == 0);
    EXPECT(socks_prosba(ipv4, sizeof ipv4, &z, &c) == 1 && z == 10 && c.adres == IP(93, 184, 216, 34) &&
           c.port == 443 && c.nazwa[0] == '\0');
    EXPECT(socks_prosba(nazwa, sizeof nazwa, &z, &c) == 1 && c.adres == 0 && strcmp(c.nazwa, "example.com") == 0 &&
           c.port == 80);
    EXPECT(socks_prosba(lit, sizeof lit, &z, &c) == 1 && c.adres == IP(1, 2, 3, 4));
    EXPECT(socks_prosba(ipv6, sizeof ipv6, &z, &c) == -SOCKS_TYP_ADRESU);
    EXPECT(socks_prosba(bind, sizeof bind, &z, &c) == -SOCKS_POLECENIE);
    EXPECT(socks_prosba(udp, sizeof udp, &z, &c) == -SOCKS_POLECENIE);
    EXPECT(socks_prosba(rsv, sizeof rsv, &z, &c) == -SOCKS_BLAD);
    EXPECT(socks_prosba(port0, sizeof port0, &z, &c) == -SOCKS_BLAD);
    EXPECT(socks_prosba(zla, sizeof zla, &z, &c) == -SOCKS_BLAD);
    EXPECT(socks_prosba(petla, sizeof petla, &z, &c) == -SOCKS_NIE_WOLNO);
    {   /* 0.0.0.0 (found by fuzz_lacznik: once taken for a name to look up) */
        static const uint8_t nic[] = { 5, 1, 0, 1, 0, 0, 0, 0, 0, 80 };
        EXPECT(socks_prosba(nic, sizeof nic, &z, &c) == -SOCKS_NIE_WOLNO);
    }
    for (i = 0; i < 255; i++)
        dluga[5 + i] = 'a';
    EXPECT(socks_prosba(dluga, sizeof dluga, &z, &c) == -SOCKS_BLAD); /* a 255-byte label */
    {
        uint8_t o[10];
        socks_odpowiedz(o, SOCKS_NIE_WOLNO);
        EXPECT(o[0] == 5 && o[1] == 2 && o[2] == 0 && o[3] == 1 && o[9] == 0);
    }
}

static void t_nazwy(void)
{
    uint32_t a;
    EXPECT(nazwa_dobra("www.example.com", 15) && nazwa_dobra("a-b_c.pl", 8));
    EXPECT(!nazwa_dobra("", 0) && !nazwa_dobra(".pl", 3) && !nazwa_dobra("a..pl", 5) && !nazwa_dobra("pl.", 3) &&
           !nazwa_dobra("ą.pl", 4) && !nazwa_dobra("a b", 3));
    EXPECT(nazwa_ipv4("10.0.2.2", &a) && a == IP(10, 0, 2, 2));
    EXPECT(!nazwa_ipv4("1.2.3", &a) && !nazwa_ipv4("1.2.3.256", &a) && !nazwa_ipv4("1.2.3.4.5", &a) &&
           !nazwa_ipv4("a.b.c.d", &a) && !nazwa_ipv4("0001.2.3.4", &a));
}

/* An answer to `pyt` (our question, n bytes) with `an` records written after it from `rek`. */
static size_t odpowiedz(uint8_t *b, const uint8_t *pyt, size_t n, int rcode, int an, const uint8_t *rek, size_t n_rek)
{
    memcpy(b, pyt, n);
    b[2] = 0x81, b[3] = (uint8_t)(0x80 | rcode), b[7] = (uint8_t)an;
    if (n_rek > 0)
        memcpy(b + n, rek, n_rek);
    return n + n_rek;
}

static void t_dns(void)
{
    static const uint8_t a_rek[] = { 0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 93, 184, 216, 34 };
    static const uint8_t cname_a[] = { 0xC0, 12, 0, 5, 0, 1, 0, 0, 0, 60, 0, 4, 1, 'x', 0xC0, 12,
                                       0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 8, 8, 4, 4 };
    static const uint8_t petla[] = { 0xC0, 40, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 1, 2, 3, 4 };
    static const uint8_t prywatny[] = { 0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 127, 0, 0, 1 };
    static const uint8_t za_dlugi[] = { 0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 40, 1, 2, 3, 4 };
    uint8_t q[300], b[600];
    uint32_t a = 0;
    size_t n = dns_zapytanie(q, sizeof q, 0x1234, "example.com"), m;
    EXPECT(n == 12 + 13 + 4 && q[0] == 0x12 && q[1] == 0x34 && q[2] == 1 && q[5] == 1 && q[12] == 7 && q[20] == 3);
    EXPECT(dns_zapytanie(q, 20, 1, "example.com") == 0 && dns_zapytanie(q, sizeof q, 1, "zła nazwa") == 0);
    m = odpowiedz(b, q, n, 0, 1, a_rek, sizeof a_rek);
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.com", &a) == 1 && a == IP(93, 184, 216, 34));
    EXPECT(dns_odpowiedz(b, m, 0x1234, "EXAMPLE.com", &a) == 1);
    EXPECT(dns_odpowiedz(b, m, 0x1235, "example.com", &a) == -1);   /* another identifier */
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.org", &a) == -1);   /* another question */
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.co", &a) == -1);
    EXPECT(dns_odpowiedz(b, m - 1, 0x1234, "example.com", &a) == -1); /* cut short */
    m = odpowiedz(b, q, n, 0, 2, cname_a, sizeof cname_a);
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.com", &a) == 1 && a == IP(8, 8, 4, 4)); /* through a CNAME */
    m = odpowiedz(b, q, n, 3, 0, NULL, 0);
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.com", &a) == 0); /* NXDOMAIN */
    m = odpowiedz(b, q, n, 0, 1, prywatny, sizeof prywatny);
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.com", &a) == 0); /* 127.0.0.1 is not an address of the internet */
    m = odpowiedz(b, q, n, 0, 1, za_dlugi, sizeof za_dlugi);
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.com", &a) == -1);
    m = odpowiedz(b, q, n, 0, 1, petla, sizeof petla);
    /* a forward pointer in a record's name: never followed (no loop), the record itself is read */
    EXPECT(dns_odpowiedz(b, m, 0x1234, "example.com", &a) == 1 && a == IP(1, 2, 3, 4));
    {   /* the question's name as a pointer to itself: a loop, refused */
        uint8_t z[64] = { 0x12, 0x34, 0x81, 0x80, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 12, 0, 1, 0, 1 };
        EXPECT(dns_odpowiedz(z, 18, 0x1234, "example.com", &a) == -1);
        z[2] = 0x01; /* not an answer (QR 0) */
        EXPECT(dns_odpowiedz(z, 18, 0x1234, "example.com", &a) == -1);
    }
}

static void t_d3(void)
{
    uint32_t nasz = IP(192, 168, 1, 23), router = IP(192, 168, 1, 1);
    EXPECT(k1_siec_dla_strefy(IP(93, 184, 216, 34), nasz, 24, router));        /* the internet */
    EXPECT(k1_siec_dla_strefy(router, nasz, 24, router));                       /* the router */
    EXPECT(!k1_siec_dla_strefy(IP(192, 168, 1, 50), nasz, 24, router));         /* a printer at home */
    EXPECT(!k1_siec_dla_strefy(IP(10, 0, 0, 5), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(172, 20, 0, 1), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(169, 254, 1, 1), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(100, 64, 0, 1), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(127, 0, 0, 1), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(224, 0, 0, 251), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(255, 255, 255, 255), nasz, 24, router));
    EXPECT(!k1_siec_dla_strefy(IP(203, 0, 113, 9), nasz, 24, router));
    /* a public address on the home network: the neighbours stay closed, the router open */
    EXPECT(!k1_siec_dla_strefy(IP(81, 2, 3, 10), IP(81, 2, 3, 4), 28, IP(81, 2, 3, 1)));
    EXPECT(k1_siec_dla_strefy(IP(81, 2, 3, 1), IP(81, 2, 3, 4), 28, IP(81, 2, 3, 1)));
    EXPECT(k1_siec_dla_strefy(IP(81, 2, 3, 20), IP(81, 2, 3, 4), 28, IP(81, 2, 3, 1)));
}

static int dobry(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat o;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &o) == 0;
}

static void t_komunikaty(void)
{
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(93, 184, 216, 34), 443, K1_STREFA_TCP, 0, 0),
                 K1_KIER_DO_NADZORCY));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, K1_STREFA_POLACZENIA_MAKS, 0, 0, K1_STREFA_DNS, 0, 0),
                 K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 0, (int32_t)IP(1, 2, 3, 4), 443, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, K1_STREFA_POLACZENIA_MAKS + 1, (int32_t)IP(1, 2, 3, 4), 443, 0,
                                    0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(1, 2, 3, 4), 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(1, 2, 3, 4), 65536, 0, 0, 0),
                  K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(127, 0, 0, 1), 80, 0, 0, 0),
                  K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, 5, 53, K1_STREFA_DNS, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(1, 2, 3, 4), 80, 2, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(1, 2, 3, 4), 80, 0, 1, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 1, (int32_t)IP(1, 2, 3, 4), 80, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_STREFA_GNIAZDO, 3, K1_STREFA_DNS, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_GNIAZDO, 3, 2, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_STREFA_ODMOWA, 3, K1_STREFA_ODM_ADRES, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_ODMOWA, 3, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_STREFA_ODMOWA, 3, K1_STREFA_ODM_MAKS + 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_STREFA_GNIAZDO, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_STREFA_ODMOWA, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_STREFA_POLACZ, K1_KIER_DO_NADZORCY));
}

int main(void)
{
    t_powitanie();
    t_prosba();
    t_nazwy();
    t_dns();
    t_d3();
    t_komunikaty();
    return K1_TEST_KONIEC("test_lacznik");
}

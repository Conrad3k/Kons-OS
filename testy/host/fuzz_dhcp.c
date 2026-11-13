/* libFuzzer harness for siec's DHCP packets and client (0.9.0 step 6): arbitrary datagrams from the network, one after
 * another, into a client in every state. A packet is either rejected or gives an address that could be ours; the
 * client's lease always has T1 ≤ T2 ≤ its end, and whatever it wants to send can be built. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dhcp.h"
#include "k1_siec.h"
#include "klient.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static const uint8_t MAC[6] = { 0x52, 0x54, 0x00, 0x4b, 0x31, 0x01 };

static void sprawdz_klienta(const struct k1_dhcp_klient *k, int akcje)
{
    uint8_t b[K1_DHCP_PAKIET_MAKS];
    if (k->stan < K1_DHCP_BEZ_KABLA || k->stan > K1_DHCP_PONOWNIE)
        abort();
    if (k->stan >= K1_DHCP_ZWIAZANY &&
        (!k1_siec_adres_dobry(k->adres, k->prefiks, k->router) || k->t1 > k->t2 || k->t2 > k->koniec))
        abort();
    if ((akcje & K1_DHCP_A_WYSLIJ) && k1_dhcp_buduj(&k->zapytanie, b, sizeof b) == 0)
        abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_dhcp_klient k;
    double t = 0;
    size_t i = 0;
    k1_dhcp_start(&k, MAC, 42, t);
    /* the input: packets of [2 bytes length][length bytes], each one also a step of the clock */
    while (i + 2 <= size) {
        size_t n = (size_t)data[i] << 8 | data[i + 1];
        struct k1_dhcp_odpowiedz o;
        uint8_t *p;
        int r;
        i += 2;
        if (n > size - i)
            n = size - i;
        p = malloc(n ? n : 1); /* its own buffer: ASan sees every read past its end */
        if (p == NULL)
            return 0;
        memcpy(p, data + i, n);
        /* the transaction of the client, or the one in the packet (so that checked packets reach the client too) */
        r = k1_dhcp_czytaj(p, n, n >= 36 ? (uint32_t)p[32] << 24 | (uint32_t)p[33] << 16 | (uint32_t)p[34] << 8 | p[35]
                                         : k.xid, MAC, &o);
        free(p);
        if (r > 0 && r != K1_DHCP_NAK && !k1_siec_adres_dobry(o.adres, o.prefiks, o.router))
            abort();
        if (r > 0 && (o.dzierzawa < K1_SIEC_DZIERZAWA_MIN || o.dzierzawa > K1_SIEC_DZIERZAWA_MAKS) && r != K1_DHCP_NAK)
            abort();
        if (r > 0)
            sprawdz_klienta(&k, k1_dhcp_odebrano(&k, &o, MAC, t));
        t += (double)(n % 4096);
        sprawdz_klienta(&k, k1_dhcp_czas(&k, t));
        i += n;
    }
    return 0;
}

/* libFuzzer harness for the zones' connector (0.10.0 step 2, ADR 0043 T9): the input is what a hostile zone (SOCKS5
 * greeting and request) or a hostile network (a DNS answer) sends. Nothing is read past the input; a request taken
 * gives a port, and an address that can be one or a name that passes nazwa_dobra; an address from DNS can be one. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_siec.h"
#include "lacznik.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct socks_cel c;
    uint32_t a = 0;
    size_t z = 0;
    int w;
    if (size < 1)
        return 0;
    if (data[0] & 1) { /* SOCKS5 */
        w = socks_powitanie(data + 1, size - 1, &z);
        if (w == 1 && (z < 3 || z > size - 1))
            abort();
        w = socks_prosba(data + 1, size - 1, &z, &c);
        if (w == 1 && (z > size - 1 || c.port == 0 ||
                       (c.adres == 0 ? !nazwa_dobra(c.nazwa, strlen(c.nazwa)) : !k1_siec_serwer_dobry(c.adres))))
            abort();
        if (w < 0 && -w != SOCKS_BLAD && -w != SOCKS_POLECENIE && -w != SOCKS_TYP_ADRESU && -w != SOCKS_NIE_WOLNO)
            abort();
    } else { /* a DNS answer to our question for "example.com", id 0x1234 */
        uint8_t b[600];
        size_t n = size - 1 < sizeof b ? size - 1 : sizeof b;
        memcpy(b, data + 1, n);
        w = dns_odpowiedz(b, n, 0x1234, "example.com", &a);
        if (w == 1 && !k1_siec_serwer_dobry(a))
            abort();
    }
    return 0;
}

/* libFuzzer harness for the service protocol (Makefile: fuzz). The first input byte selects the
 * channel direction; every accepted message must satisfy the protocol invariants, rejected ones
 * must leave the output zeroed. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_komunikat.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_komunikat k, zero;
    enum k1_kanal_kierunek kier;
    if (size < 1)
        return 0;
    kier = (enum k1_kanal_kierunek)(data[0] % (K1_KIER_MAKS + 2)); /* includes invalid 0 and K1_KIER_MAKS + 1 */
    memset(&zero, 0, sizeof zero);
    if (k1_komunikat_sprawdz(data + 1, size - 1, kier, &k) != 0) {
        if (memcmp(&k, &zero, sizeof k) != 0)
            abort();
        return 0;
    }
    if (size - 1 != sizeof k || k.wersja != K1_KOMUNIKAT_WERSJA || k.dlugosc != sizeof k || k.zarezerwowane != 0)
        abort();
    if (kier < K1_KIER_STEROWANIE || kier > K1_KIER_MAKS)
        abort();
    if (memcmp(&k, data + 1, sizeof k) != 0)
        abort();
    return 0;
}

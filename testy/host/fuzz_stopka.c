/* libFuzzer harness for the footer of the download zone and the HTTP "Date" it carries (0.13.0, ADR 0056): the header
 * comes from the network into the zone, the footer from the zone into the writing program — arbitrary bytes. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_stopka.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t b[K1_STOPKA] = { 0 };
    uint64_t c, d = k1_data_http((const char *)data, size);
    if (d != 0 && d >= 4102444800ull)
        abort(); /* only 1970…2099 */
    memcpy(b, data, size < sizeof b ? size : sizeof b);
    if (k1_stopka_czytaj(b, &c) == 0) {
        uint8_t z[K1_STOPKA];
        if (c != 0 && (c < K1_STOPKA_OD || c >= K1_STOPKA_DO))
            abort();
        k1_stopka_zbuduj(c, z);
        if (memcmp(z, b, sizeof z) != 0)
            abort(); /* accepted only what goes back the same */
    } else if (c != 0) {
        abort();
    }
    return 0;
}

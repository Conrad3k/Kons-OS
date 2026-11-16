/* libFuzzer harness for the input event protocol (Makefile: fuzz). Every accepted message must
 * satisfy the protocol invariants; rejected ones must leave the output zeroed. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_zdarzenie.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_zdarzenie z, zero;
    memset(&zero, 0, sizeof zero);
    if (k1_zdarzenie_sprawdz(data, size, &z) != 0) {
        if (memcmp(&z, &zero, sizeof z) != 0)
            abort();
        return 0;
    }
    if (size != sizeof z || z.wersja != K1_ZDARZENIE_WERSJA || z.dlugosc != sizeof z || z.zarezerwowane != 0)
        abort();
    if (z.typ == K1_ZD_PRZEWIJANIE) {
        if (z.dx < -K1_ZD_PRZEWIN_MAX || z.dx > K1_ZD_PRZEWIN_MAX || z.dy < -K1_ZD_PRZEWIN_MAX ||
            z.dy > K1_ZD_PRZEWIN_MAX || (z.dx == 0 && z.dy == 0) || (z.kod != K1_ZD_KOLKO && z.kod != K1_ZD_TOUCHPAD))
            abort();
        return 0;
    }
    if (z.dx < -K1_ZD_RUCH_MAX || z.dx > K1_ZD_RUCH_MAX || z.dy < -K1_ZD_RUCH_MAX || z.dy > K1_ZD_RUCH_MAX)
        abort();
    if (z.typ != K1_ZD_KLAWISZ && z.typ != K1_ZD_RUCH && z.typ != K1_ZD_PRZYCISK)
        abort();
    return 0;
}

/* libFuzzer harness for the clipboard's text check (0.11.0 step 5): any bytes; what is taken must be valid UTF-8
 * without NUL and control characters (checked here again, byte by byte, the other way round). */
#include <stdlib.h>

#include "k1_schowek.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!k1_schowek_dobry(data, size))
        return 0;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == 0 || c == 0x7f || (c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0xc0 || c == 0xc1 || c >= 0xf5)
            abort();
        if (c >= 0x80 && c < 0xc0 && (i == 0 || data[i - 1] < 0x80))
            abort(); /* a continuation byte after an ASCII one */
    }
    return 0;
}

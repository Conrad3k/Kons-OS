/* libFuzzer harness for k1_cmdline (Makefile: fuzz). Arbitrary bytes as the command line,
 * several keys and output sizes; ASan/UBSan catch memory errors, asserts catch logic errors. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_cmdline.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const char *const keys[] = { "k1.rdzen", "a", "k1.rdzen.x" };
    char out[K1_CMDLINE_VALUE_MAX];
    size_t outsz = size > 0 ? (size_t)(data[0] % sizeof out) + 1 : sizeof out;
    size_t k;
    for (k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        enum k1_cmdline_result r = k1_cmdline_get((const char *)data, size, keys[k], out, outsz);
        if (strnlen(out, outsz) >= outsz)
            abort(); /* output must always be NUL-terminated */
        if (r != K1_CMDLINE_FOUND && out[0] != '\0')
            abort();
        if (r == K1_CMDLINE_FOUND)
            (void)k1_valid_block_device(out);
    }
    return 0;
}

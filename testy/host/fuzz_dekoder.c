/* libFuzzer harness for dekoder (src/dekoder/obraz.c, wuffs.c; 0.8.0 step 4, ADR 0032): the input is a whole file.
 * Whatever it holds, decoding must end with a picture of the asked size or an error — never a crash, a read outside
 * the input or a leak (ASan). The size asked for comes from the first byte, so turning and shrinking run too. Seed:
 * testy/host/korpus-dekoder (small PNG and JPEG files, one with EXIF orientation). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "k1_dekoder.h"
#include "obraz.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct obraz o;
    uint32_t bok;
    int r;
    if (size < 1)
        return 0;
    bok = 1u + data[0] % 64u;
    r = obraz_przygotuj(data + 1, size - 1, bok, bok, &o);
    if (r == 0) {
        if (o.px == NULL || o.w == 0 || o.h == 0 || o.w > bok || o.h > bok)
            abort();
        free(o.px);
    } else if (r < K1_DEK_BLAD_MIN || r > K1_DEK_BLAD_FORMAT || o.px != NULL) {
        abort();
    }
    return 0;
}

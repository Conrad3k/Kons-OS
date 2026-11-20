/* libFuzzer harness: a request to the film decoder (src/wspolne/k1_filmy.c; 0.10.9 step 2). The first bytes choose the
 * film (its codec, size, surfaces, which hold a decoded picture) and the target; the rest is the request as the client
 * wrote it in the window. Accepted requests must keep every slice inside the data and every frame inside the film. */
#include <stdlib.h>
#include <string.h>

#include "k1_filmy.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_filmy_film f;
    struct k1_filmy_czesci c;
    uint8_t *p;
    uint32_t cel;
    if (size < 8)
        return 0;
    f.kodek = data[0] % (K1_KODEKI_MAKS + 1);
    f.w = 16u + (uint32_t)data[1] * 16u, f.h = 16u + (uint32_t)data[2] * 8u;
    f.klatki = 1u + data[3] % K1_FILMY_KLATEK_MAKS;
    f.zdekodowane = (uint32_t)data[4] | (uint32_t)data[5] << 8;
    cel = data[6] % K1_FILMY_KLATEK_MAKS;
    p = malloc(size - 7 ? size - 7 : 1); /* exactly the request: ASan sees a read beyond it */
    memcpy(p, data + 7, size - 7);
    if (k1_filmy_sprawdz(p, size - 7, &f, cel) == 0) {
        struct k1_h264_plaster s;
        k1_filmy_czesci(p, &c);
        if (c.dane + c.n_danych != size - 7 || cel >= f.klatki)
            abort();
        for (uint32_t i = 0; i < c.n_plastrow; i++) {
            memcpy(&s, p + c.plasterki + (size_t)i * sizeof s, sizeof s);
            if ((uint64_t)s.dane_od + s.dane_ile > c.n_danych)
                abort();
        }
    }
    free(p);
    return 0;
}

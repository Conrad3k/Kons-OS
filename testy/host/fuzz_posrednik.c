/* libFuzzer harness: a stream of requests to the graphics broker (src/wspolne/k1_posrednik.c; 0.10.9 step 5). The first
 * bytes choose the size of the replies' area; the rest is the stream as the client wrote it in the window. Each accepted
 * request must lie inside the stream, have a known kind, and its reply must fit its place. */
#include <stdlib.h>
#include <string.h>

#include "k1_posrednik.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t miejsce;
    uint8_t *p;
    size_t o = 0;
    if (size < 2)
        return 0;
    miejsce = (uint32_t)data[0] << 8 | data[1];
    p = malloc(size - 2 ? size - 2 : 1); /* exactly the stream: ASan sees a read beyond it */
    memcpy(p, data + 2, size - 2);
    while (o < size - 2) {
        struct k1_posr_prosba_ok ok;
        if (k1_posr_sprawdz(p + o, size - 2 - o, miejsce, &ok) != 0)
            break;
        if (ok.len < 16 || ok.len > size - 2 - o || ok.cmd < 1 || ok.cmd > K1_POSR_PROSBY_MAKS ||
            (ok.odp != 0 && (uint64_t)ok.rsp_off + ok.odp > miejsce))
            abort();
        if (ok.cmd == K1_POSR_IOCTL && k1_posr_ioctl_rozmiar(ok.ioctl) == 0)
            abort();
        o += ok.len;
    }
    free(p);
    return 0;
}

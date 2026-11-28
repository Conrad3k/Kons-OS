/* libFuzzer harness: the guest's 3D commands of virtio-gpu to the bridge of fast graphics (src/strefa/gpu_most.c; 0.10.9
 * step 6). The input is a sequence of commands, each: 2 bytes of length, then the command (its header and fields as the
 * guest wrote them); the fake broker answers as the first byte says. Nothing may be read beyond a command, every reply
 * must fit, every mapping must lie inside the host-visible region. */
#include <stdlib.h>
#include <string.h>

#include "gpu_most_falszywy.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static struct most m;
static uint8_t odp[MOST_ODP_MAKS];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t o = 1;
    if (size < 1)
        return 0;
    memset(&f, 0, sizeof f);
    f.odp = data[0] % 4 == 0 ? 0 : data[0] % 4 == 1 ? K1_POSR_BLAD_POLE : data[0] % 4 == 2 ? K1_POSR_BLAD_PAMIEC : MOST_PADL;
    most_start(&m, &f_lacze, 0x1916, 7, 0x010600);
    if (data[0] & 0x80) { /* 0.13.2 step 3: with the film context */
        const struct k1_filmy_zbior z = { K1_FILMY_CAPSET_WERSJA, 7, 4096, 2304 };
        most_filmy(&m, &z);
    }
    while (o + 2 <= size) {
        size_t n = (size_t)data[o] | (size_t)data[o + 1] << 8;
        uint8_t *p;
        uint32_t dl = 0;
        struct most_czeka c;
        const char *powod = NULL;
        o += 2;
        if (n > size - o)
            n = size - o;
        p = malloc(n ? n : 1); /* exactly the command: ASan sees a read beyond it */
        memcpy(p, data + o, n);
        if (most_polecenie(&m, p, n, odp, &dl, &c, &powod) < 0 ? powod == NULL : dl < 24 || dl > MOST_ODP_MAKS)
            abort();
        if (f.mapy && (f.ost_gpa < MOST_OKNO_BAZA || f.ost_gpa >= MOST_OKNO_BAZA + MOST_OKNO_DL))
            abort();
        free(p);
        o += n;
    }
    most_reset(&m);
    return 0;
}

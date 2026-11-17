/* libFuzzer harness for the balloon's lists, blocks and statistics (src/strefa/strony.c; 0.10.1 step 2, ADR 0044): the
 * input is what a hostile guest's driver gives. Every run of pages accepted lies whole in the memfd of the guest's
 * memory (two parts: below the devices and from 4 GiB), on whole pages; every statistic accepted has a known tag. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "strony.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#define DOL (64u << 10)
#define CALA (96u << 10)

static uint8_t pam[CALA];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* a tiny guest with the same shape: 64 KiB "below the devices", 32 KiB "from 4 GiB" */
    const struct gosc_pamiec m = { pam, CALA, DOL };
    struct strony_zakres z[STRONY_LISTA_MAKS];
    uint64_t st[STRONY_STAT_MAKS];
    uint32_t jest;
    const char *powod;
    int n = strony_lista(data, size, &m, z, &powod), i;
    for (i = 0; i < n; i++)
        if (z[i].od % STRONA || z[i].dl == 0 || z[i].dl % STRONA || z[i].od > CALA || z[i].dl > CALA - z[i].od)
            abort();
    if (strony_statystyki(data, size, st, &jest, &powod) == 0 && (jest >> STRONY_STAT_MAKS) != 0)
        abort();
    if (size >= 16) {
        uint64_t od, dl;
        struct strony_zakres b;
        memcpy(&od, data, 8);
        memcpy(&dl, data + 8, 8);
        od %= CALA;
        dl %= CALA - od + 1;
        if (strony_blok(&m, pam + od, dl, &b, &powod) == 0 && (b.od != od || b.dl != dl || od % STRONA || dl % STRONA))
            abort();
    }
    return 0;
}

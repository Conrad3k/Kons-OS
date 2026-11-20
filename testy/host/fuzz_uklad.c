/* libFuzzer harness for the K1-EFI layout block and GRUB's environment block (M6c): arbitrary bytes. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_uklad.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t b[K1_UKLAD_BAJTY], e[K1_GRUBENV_BAJTY], e2[K1_GRUBENV_BAJTY];
    struct k1_uklad u;
    struct k1_grubenv g, h;
    if (size < K1_GRUBENV_BAJTY)
        return 0;
    memcpy(b, data, sizeof b);
    if (k1_uklad_czytaj(b, 4096ull << 20, &u) == 0 &&
        (u.slot_bajty < K1_SLOT_MIN || u.slot_bajty > K1_SLOT_MAKS || u.slot_bajty % K1_SLOT_MIB != 0 || u.slot_off[0] + u.slot_bajty > u.fat_bajty ||
         u.slot_off[1] + u.slot_bajty > u.fat_bajty || u.grubenv_off + K1_GRUBENV_BAJTY > u.fat_bajty))
        abort();
    memcpy(e, data, sizeof e);
    if (k1_grubenv_czytaj(e, &g) == 0) {
        k1_grubenv_zbuduj(&g, e2); /* what we would write back reads as the same */
        if (k1_grubenv_czytaj(e2, &h) != 0 || memcmp(&g, &h, sizeof g) != 0)
            abort();
    }
    return 0;
}

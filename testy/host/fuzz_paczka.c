/* libFuzzer harness for the update package's header, its front's layout block and the waiting room's mark (0.10.5
 * step 2, ADR 0046 T4): arbitrary bytes from the network or the disk. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_paczka.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t b[K1_PACZKA_NAGLOWEK], przod[K1_UKLAD_OFF + K1_UKLAD_BAJTY];
    struct k1_paczka p;
    struct k1_uklad u;
    struct k1_pocz z;
    memset(b, 0, sizeof b);
    memcpy(b, data, size < sizeof b ? size : sizeof b);
    if (k1_paczka_czytaj(b, &p) == 0) {
        if (p.slot_bajty != p.slot.fat_bajty || p.efi_bajty != p.start.fat_bajty || p.tyl_od > p.efi_bajty ||
            p.tyl_bajty > p.efi_bajty - p.tyl_od || p.przod_bajty > K1_PACZKA_PRZOD_MAKS)
            abort();
        memset(przod, 0, sizeof przod);
        if (size > sizeof b)
            memcpy(przod + K1_UKLAD_OFF, data + sizeof b,
                   size - sizeof b < K1_UKLAD_BAJTY ? size - sizeof b : K1_UKLAD_BAJTY);
        if (k1_paczka_uklad(&p, przod, &u) == 0 && u.grubenv_off + 1024 > p.przod_bajty)
            abort();
    }
    if (k1_pocz_czytaj(b, &z) == 0 && (z.wersja == 0 || z.ogl_bajty == 0))
        abort();
    return 0;
}

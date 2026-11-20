/* libFuzzer harness for the manifest reader and signature check (M6a): arbitrary 512 bytes. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_manifest.h"
#include "k1_uklad.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t klucz[32] = { 1, 2, 3 };
    uint8_t buf[K1_MANIFEST_BAJTY];
    struct k1_manifest m;
    if (size < K1_MANIFEST_BAJTY)
        return 0;
    memcpy(buf, data, sizeof buf);
    if (k1_manifest_czytaj(buf, &m) == 0) {
        if (m.fat_bajty == 0 || m.fat_bajty > (m.format == K1_MANIFEST_SLOT ? k1_slot_man_off(K1_SLOT_MAKS) : K1_MANIFEST_EFI_MAKS) ||
            (m.format != K1_MANIFEST_SLOT && m.format != K1_MANIFEST_START) || m.wersja > 0xffffff ||
            strnlen(m.tekst, sizeof m.tekst) >= sizeof m.tekst)
            abort();
    }
    (void)k1_manifest_sprawdz_podpis(buf, klucz);
    return 0;
}

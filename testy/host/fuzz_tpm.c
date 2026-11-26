/* libFuzzer harness for the TPM's replies and K1's record in it (0.13.0 step 2, ADR 0057): a TPM is foreign code in a
 * chip, its bytes come to nadzorca like any other data from outside. The first byte picks the command. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_tpm.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_tpm_odpowiedz o;
    struct k1_tpm_rekord r;
    uint8_t b[K1_TPM_REKORD], c[K1_TPM_REKORD];
    if (size < 9)
        return 0;
    if (k1_tpm_odpowiedz((enum k1_tpm_polecenie)(data[0] % (K1_TPM_POLECEN + 1)), data + 1, size - 1, &o) == 0 &&
        size - 1 > K1_TPM_RAMKA)
        abort();
    memset(b, 0, sizeof b);
    memcpy(b, data + 1, size - 1 < sizeof b ? size - 1 : sizeof b);
    if (k1_tpm_rekord_czytaj(b, &r) == 0) {
        k1_tpm_rekord_zbuduj(&r, c);
        if (memcmp(b, c, sizeof b) != 0 || r.generacja > K1_GEN_MAKS)
            abort(); /* accepted only what goes back the same */
        if (k1_tpm_zapamietaj(&r, data + 1, (uint64_t)data[0] << 8, data[0] & 1) == 1 &&
            k1_tpm_ocen(&r, data + 1, (uint64_t)data[0] << 8) != K1_TPM_ZGODNE && memcmp(data + 1, "\0\0\0\0\0\0\0\0", 8) != 0)
            abort(); /* what was just remembered is not older than itself */
        k1_tpm_rekord_zbuduj(&r, c);
        if (k1_tpm_rekord_czytaj(c, &r) != 0)
            abort();
    }
    return 0;
}

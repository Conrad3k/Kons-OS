/* libFuzzer harness: the sound service's side of a client's stream (src/wspolne/k1_dzwiek.c; 0.12.0 step 1). The input is
 * what a hostile client does: each 3 bytes a step — write N frames (ZAPISANO, any N the message allows) or take a period —
 * the ring itself filled with the input. The count may never say more waits than the ring holds, a take never gives more
 * than waits, and mixing and the output never overflow (UBSan). Step 2: the same bytes as the record gui remembers in
 * skarbiec (D5) — what is read is always within its ranges. */
#include <stdlib.h>
#include <string.h>

#include "k1_dzwiek.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static int16_t pamiec[K1_DZW_PIERSCIEN * K1_DZW_KANALY], kopia[K1_DZW_OKRES * K1_DZW_KANALY],
        wyj[K1_DZW_OKRES * K1_DZW_KANALY];
    static int32_t suma[K1_DZW_OKRES * K1_DZW_KANALY];
    struct k1_dzw_pierscien p;
    struct k1_dzw_zapamietane z;
    if (k1_dzw_zapamietane_czytaj(data, size, &z) == 0)
        for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++)
            if (z.glosnosc[w] < 0 || z.glosnosc[w] > K1_DZW_GLOSNOSC_MAKS || (z.wyciszone[w] != 0 && z.wyciszone[w] != 1))
                abort();
    if (size < 4)
        return 0;
    memcpy(&p.zapisane, data, 4); /* any starting point: the counters wrap */
    p.pobrane = p.zapisane;
    for (size_t i = 0; i < sizeof pamiec; i++)
        ((uint8_t *)pamiec)[i] = data[i % size];
    memset(suma, 0, sizeof suma);
    for (size_t i = 4; i + 3 <= size; i += 3) {
        uint32_t n = (uint32_t)data[i + 1] | (uint32_t)data[i + 2] << 8, przed = k1_dzw_czeka(&p);
        if (data[i] & 1) {
            n = 1 + n % K1_DZW_PIERSCIEN; /* ZAPISANO's range */
            if (k1_dzw_zapisano(&p, n) == 0 ? przed + n > K1_DZW_PIERSCIEN : przed + n <= K1_DZW_PIERSCIEN)
                abort();
        } else {
            uint32_t w = k1_dzw_pobierz(&p, pamiec, kopia, K1_DZW_OKRES);
            if (w > przed || w > K1_DZW_OKRES || k1_dzw_czeka(&p) != przed - w)
                abort();
            k1_dzw_dodaj(suma, kopia, w);
            k1_dzw_wyjscie(wyj, suma, K1_DZW_OKRES, k1_dzw_wzmocnienie(data[i + 1] % 128, data[i + 2] & 1));
        }
        if (k1_dzw_czeka(&p) > K1_DZW_PIERSCIEN)
            abort();
    }
    return 0;
}

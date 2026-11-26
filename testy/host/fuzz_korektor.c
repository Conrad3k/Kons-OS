/* libFuzzer harness: the system equalizer (src/wspolne/k1_korektor.c; 0.12.3 step 2). The input is what a hostile steering
 * client sends: the bands in a message's fields (any bits — k1_kor_z_pol must refuse what is beyond ±12 dB or in the
 * unused bytes, and accept only what it gives back the same) and the sum it plays through them, with a second setting
 * taking over in the middle (the crossing). The filters must stay finite and bounded for any accepted setting (the sum of
 * the service is at most 12 streams at full scale), the level before them never above 1, and a flat equalizer must not
 * touch a sample (ASan, UBSan). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "k1_dzwiek.h"
#include "k1_korektor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int pasma_z(const uint8_t *d, int8_t p[K1_KOR_PASMA_N])
{
    int32_t pola[3], z[3];
    int8_t q[K1_KOR_PASMA_N];
    memcpy(pola, d, sizeof pola);
    if (k1_kor_z_pol(pola, p) != 0)
        return -1;
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        if (p[i] > K1_KOR_MAKS || p[i] < -K1_KOR_MAKS)
            abort();
    k1_kor_do_pol(p, z);
    if (memcmp(z, pola, sizeof z) != 0 || k1_kor_z_pol(z, q) != 0 || memcmp(p, q, sizeof q) != 0)
        abort(); /* accepted only what goes back the same */
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static float s[K1_DZW_OKRES * K1_DZW_KANALY], kopia[K1_DZW_OKRES * K1_DZW_KANALY];
    int8_t p[K1_KOR_PASMA_N], q[K1_KOR_PASMA_N];
    struct k1_korektor k;
    size_t od = 24;
    if (size < 24 || pasma_z(data, p) != 0)
        return 0;
    if (pasma_z(data + 12, q) != 0)
        memset(q, 0, sizeof q);
    k1_kor_start(&k);
    k1_kor_ustaw(&k, p);
    if (!(k.nowe.wzm > 0.0 && k.nowe.wzm <= 1.0))
        abort();
    for (int okres = 0; okres < 6; okres++) {
        for (size_t i = 0; i < K1_DZW_OKRES * K1_DZW_KANALY; i++, od += 2) { /* the sum: up to 12 streams at full scale */
            int16_t v = size > 1 ? (int16_t)(data[od % size] | data[(od + 1) % size] << 8) : 0;
            s[i] = (float)v / 32768.0f * (float)(1 + data[i % size] % 12);
        }
        memcpy(kopia, s, sizeof s);
        if (okres == 3)
            k1_kor_ustaw(&k, q);
        int liczyl = k1_kor_licz(&k, s, K1_DZW_OKRES);
        if (!liczyl && memcmp(kopia, s, sizeof s) != 0)
            abort(); /* flat and no change: untouched */
        for (size_t i = 0; i < K1_DZW_OKRES * K1_DZW_KANALY; i++)
            if (!isfinite(s[i]) || s[i] > 64.0f || s[i] < -64.0f)
                abort();
    }
    return 0;
}

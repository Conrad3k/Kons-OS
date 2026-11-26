/* libFuzzer harness: Kons Spatial Audio (src/wspolne/k1_przestrzen.c, k1_prz_licz.c; 0.12.3 step 3). The input is what a
 * hostile steering client sends: two scenes in a message's fields (any bits — k1_prz_z_pol must refuse what is beyond the
 * ranges and accept only what it gives back the same), the output (the speakers or the headphones), and the sum it plays
 * through them, with the second scene (and maybe the other output) taking over in the middle (the crossing). The scene
 * must stay finite and bounded for any accepted scene (the sum of the service is at most 12 streams at full scale, the
 * level never above 1), and a scene off must not touch a sample (ASan, UBSan). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "k1_dzwiek.h"
#include "k1_przestrzen.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int scena_z(const uint8_t *d, struct k1_prz_scena *s)
{
    int32_t pola[4], z[4];
    struct k1_prz_scena t;
    memcpy(pola, d, sizeof pola);
    if (k1_prz_z_pol(pola, s) != 0)
        return -1;
    if (!k1_prz_poprawna(s))
        abort();
    k1_prz_do_pol(s, z);
    if (memcmp(z, pola, sizeof z) != 0 || k1_prz_z_pol(z, &t) != 0 || memcmp(&t, s, sizeof t) != 0)
        abort(); /* accepted only what goes back the same */
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static float s[K1_DZW_OKRES * K1_DZW_KANALY], kopia[K1_DZW_OKRES * K1_DZW_KANALY];
    static struct k1_przestrzen p;
    struct k1_prz_scena a, b;
    int wa, wb;
    size_t od = 34;
    if (size < 34 || scena_z(data, &a) != 0)
        return 0;
    if (scena_z(data + 16, &b) != 0)
        k1_prz_scena_start(&b);
    wa = data[32] & 1 ? K1_DZW_WYJSCIE_SLUCHAWKI : K1_DZW_WYJSCIE_GLOSNIKI;
    wb = data[33] & 1 ? K1_DZW_WYJSCIE_SLUCHAWKI : K1_DZW_WYJSCIE_GLOSNIKI;
    k1_prz_start(&p);
    k1_prz_ustaw(&p, &a, wa);
    if (!(p.nowe.wzm > 0.0 && p.nowe.wzm <= 1.0))
        abort();
    for (int okres = 0; okres < 6; okres++) {
        for (size_t i = 0; i < K1_DZW_OKRES * K1_DZW_KANALY; i++, od += 2) { /* the sum: up to 12 streams at full scale */
            int16_t v = (int16_t)(data[od % size] | data[(od + 1) % size] << 8);
            s[i] = (float)v / 32768.0f * (float)(1 + data[i % size] % 12);
        }
        memcpy(kopia, s, sizeof s);
        if (okres == 3)
            k1_prz_ustaw(&p, &b, wb);
        int liczyl = k1_prz_licz(&p, s, K1_DZW_OKRES);
        if (!liczyl && memcmp(kopia, s, sizeof s) != 0)
            abort(); /* off and no change: untouched */
        for (size_t i = 0; i < K1_DZW_OKRES * K1_DZW_KANALY; i++)
            if (!isfinite(s[i]) || s[i] > 64.0f || s[i] < -64.0f)
                abort();
    }
    return 0;
}

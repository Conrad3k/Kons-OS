/* libFuzzer harness: the sound service's side of a client's stream (src/wspolne/k1_dzwiek.c; 0.12.0 step 1). The input is
 * what a hostile client does: each 3 bytes a step — write N frames (ZAPISANO, any N the message allows) or take a period —
 * the ring itself filled with the input. The count may never say more waits than the ring holds, a take never gives more
 * than waits, and mixing and the output never overflow (UBSan). Step 2: the same bytes as the record gui remembers in
 * skarbiec (D5) — what is read is always within its ranges. 0.12.3 (ADR 0055): the ring in either format (F32: any
 * bytes — NaN, infinities, huge numbers), the path in floats — the sum always a finite number, never beyond ±1.0 after
 * the limiter, the card's samples of any width within its bytes (ASan). */
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "k1_dzw_tor.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t pamiec[K1_DZW_PIERSCIEN * K1_DZW_RAMKA_MAKS], kopia[K1_DZW_OKRES * K1_DZW_RAMKA_MAKS],
        wyj[K1_DZW_OKRES * K1_DZW_KANALY * 4];
    static float suma[K1_DZW_OKRES * K1_DZW_KANALY];
    struct k1_dzw_pierscien p;
    struct k1_dzw_ogranicznik o;
    struct k1_dzw_dither d;
    int format;
    uint32_t ramka;
    struct k1_dzw_zapamietane z;
    if (k1_dzw_zapamietane_czytaj(data, size, &z) == 0)
        for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++)
            if (z.glosnosc[w] < 0 || z.glosnosc[w] > K1_DZW_GLOSNOSC_MAKS || (z.wyciszone[w] != 0 && z.wyciszone[w] != 1))
                abort();
    if (size < 4)
        return 0;
    memcpy(&p.zapisane, data, 4); /* any starting point: the counters wrap */
    p.pobrane = p.zapisane;
    format = data[0] & 1;
    ramka = k1_dzw_ramka_bajty(format);
    for (size_t i = 0; i < sizeof pamiec; i++)
        pamiec[i] = data[i % size];
    k1_dzw_ogranicznik_start(&o);
    k1_dzw_dither_start(&d);
    for (size_t i = 4; i + 3 <= size; i += 3) {
        uint32_t n = (uint32_t)data[i + 1] | (uint32_t)data[i + 2] << 8, przed = k1_dzw_czeka(&p);
        if (data[i] & 1) {
            n = 1 + n % K1_DZW_PIERSCIEN; /* ZAPISANO's range */
            if (k1_dzw_zapisano(&p, n) == 0 ? przed + n > K1_DZW_PIERSCIEN : przed + n <= K1_DZW_PIERSCIEN)
                abort();
        } else {
            uint32_t w = k1_dzw_pobierz(&p, pamiec, ramka, kopia, K1_DZW_OKRES);
            if (w > przed || w > K1_DZW_OKRES || k1_dzw_czeka(&p) != przed - w)
                abort();
            memset(suma, 0, sizeof suma);
            for (int k = 0; k < 12; k++) /* as many streams as the service has clients, each loud */
                k1_dzw_dodaj(suma, kopia, format, w, 1.0f);
            for (size_t k = 0; k < (size_t)w * K1_DZW_KANALY; k++)
                if (!isfinite(suma[k]) || fabsf(suma[k]) > 12.0f)
                    abort();
            k1_dzw_glosnosc(suma, K1_DZW_OKRES, k1_dzw_wzmocnienie(data[i + 1] % 128, data[i + 2] & 1));
            k1_dzw_ogranicz(&o, suma, K1_DZW_OKRES);
            for (size_t k = 0; k < K1_DZW_OKRES * K1_DZW_KANALY; k++)
                if (fabsf(suma[k]) > 1.0f)
                    abort();
            k1_dzw_na_karte(wyj, suma, K1_DZW_OKRES, 2 + data[i + 1] % 3, 16 + data[i + 2] % 9, &d);
        }
        if (k1_dzw_czeka(&p) > K1_DZW_PIERSCIEN)
            abort();
    }
    return 0;
}

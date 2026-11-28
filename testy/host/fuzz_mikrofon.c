/* libFuzzer harness: the microphone's path in the sound service (src/wspolne/k1_mikrofon.c; 0.13.5 step 1, ADR 0065).
 * The input is what a card could record — any bytes, in either of its formats and with one or two channels — and the
 * level and the starting point of a listener's ring from the first bytes. After the level, the tone and the mute every
 * sample is a finite number within ±1.0, the bar's level is 0..100, the ring is written only within itself (ASan) and
 * its count moves by exactly the frames written. The same bytes as the microphone's state in DZWIEK_STAN e: what is read
 * is within its ranges and written back the same. */
#include <stdlib.h>
#include <string.h>

#include <math.h>

#include "k1_mikrofon.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static uint8_t pierscien[K1_DZW_PIERSCIEN * K1_DZW_RAMKA_MAKS];
    static float s[K1_DZW_OKRES * K1_DZW_KANALY];
    uint32_t zapisane, ramki, faza, przed;
    int bajty, kanaly, format, poziom;
    int32_t pole;
    struct k1_dzw_mik m;
    if (size < 8)
        return 0;
    memcpy(&pole, data, 4);
    if (k1_dzw_mik_z_pola(pole, &m) == 0 &&
        (m.zrodlo < 0 || m.zrodlo > K1_DZW_ZRODLO_MAKS || m.poziom < 0 || m.poziom > 100 || k1_dzw_mik_pole(&m) != pole))
        abort();
    memcpy(&zapisane, data, 4); /* any starting point: the count wraps */
    bajty = data[4] & 1 ? 4 : 2;
    kanaly = data[4] & 2 ? 1 : 2;
    format = data[4] >> 2 & 1;
    poziom = data[5] % 101;
    memcpy(&faza, data, 4);
    data += 8;
    size -= 8;
    ramki = (uint32_t)(size / (size_t)(bajty * kanaly));
    if (ramki > K1_DZW_OKRES)
        ramki = K1_DZW_OKRES;
    k1_mik_z_karty(s, data, ramki, bajty, kanaly);
    if (data[-3] & 1)
        k1_mik_ton(s, ramki, &faza);
    k1_mik_wzmocnij(s, ramki, data[-3] & 2 ? 0.0f : k1_mik_wzmocnienie(poziom));
    for (uint32_t i = 0; i < 2 * ramki; i++)
        if (!isfinite(s[i]) || s[i] > 1.0f || s[i] < -1.0f || ((data[-3] & 2) && s[i] != 0.0f))
            abort();
    poziom = k1_mik_poziom(s, ramki);
    if (poziom < 0 || poziom > 100)
        abort();
    przed = zapisane;
    k1_mik_do_pierscienia(pierscien, format, &zapisane, s, ramki);
    if (zapisane - przed != ramki)
        abort();
    return 0;
}

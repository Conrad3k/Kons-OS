/* K1 — the microphone (0.13.5 step 1, ADR 0065): the card's frames to floats, the level, the bar, the test tone and a
 * listener's ring. Pure functions (host test test_mikrofon, fuzzing fuzz_mikrofon). See k1_mikrofon.h. */
#include "k1_mikrofon.h"

#include <string.h>

#include "k1_korektor.h"

#define LN10 2.302585092994046
#define POZIOM_KROK 0.9332543007969910 /* 10^(−0.6/20): the bar's step of 0.6 dB (100 steps: −60..0 dBFS) */

float k1_mik_wzmocnienie(int poziom)
{
    if (poziom <= 0)
        return 0.0f;
    if (poziom > K1_DZW_GLOSNOSC_MAKS)
        poziom = K1_DZW_GLOSNOSC_MAKS;
    return (float)k1_kor_exp((double)(poziom - K1_DZW_MIK_START) * 0.02 * LN10); /* 10^((p − 50) · 0.4 / 20) */
}

void k1_mik_z_karty(float *wyj, const uint8_t *we, uint32_t ramki, int bajty, int kanaly)
{
    int k = kanaly == 1 ? 1 : 2, b = bajty == 4 ? 4 : 2;
    for (uint32_t i = 0; i < ramki; i++)
        for (int c = 0; c < 2; c++) {
            const uint8_t *p = we + ((size_t)i * (size_t)k + (size_t)(c < k ? c : 0)) * (size_t)b;
            float v;
            if (b == 4) {
                uint32_t u = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
                v = (float)((double)(int32_t)u / 2147483648.0);
            } else {
                uint16_t u = (uint16_t)(p[0] | p[1] << 8);
                v = (float)(int16_t)u / 32768.0f;
            }
            wyj[2 * i + (uint32_t)c] = v;
        }
}

void k1_mik_wzmocnij(float *s, uint32_t ramki, float wzm)
{
    for (uint32_t i = 0; i < 2 * ramki; i++) {
        float v = s[i] * wzm;
        s[i] = v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
    }
}

int k1_mik_poziom(const float *s, uint32_t ramki)
{
    float szczyt = 0.0f;
    double prog = 1.0;
    int p = 100;
    for (uint32_t i = 0; i < 2 * ramki; i++) {
        float v = s[i] < 0 ? -s[i] : s[i];
        if (v > szczyt) /* NaN never: k1_mik_z_karty makes only finite numbers */
            szczyt = v;
    }
    while (p > 0 && (double)szczyt < prog) {
        prog *= POZIOM_KROK;
        p--;
    }
    return p;
}

void k1_mik_ton(float *s, uint32_t ramki, uint32_t *faza)
{
    const uint32_t okres = K1_DZW_CZESTOTLIWOSC / K1_MIK_TON_HZ;
    for (uint32_t i = 0; i < ramki; i++) {
        /* sin(2πk/48) = −sin(2πk/48 − π): the argument within ±π, where k1_kor_sin is exact */
        double kat = 6.283185307179586 * (double)((*faza)++ % okres) / (double)okres - 3.141592653589793;
        float v = (float)(-k1_kor_sin(kat)) * K1_MIK_TON_SZCZYT;
        s[2 * i] += v;
        s[2 * i + 1] += v;
    }
}

void k1_mik_do_pierscienia(uint8_t *pierscien, int format, uint32_t *zapisane, const float *s, uint32_t ramki)
{
    uint32_t bajty = k1_dzw_ramka_bajty(format);
    if (bajty == 0)
        return;
    for (uint32_t i = 0; i < ramki; i++) {
        uint8_t *cel = pierscien + (size_t)((*zapisane)++ % K1_DZW_PIERSCIEN) * bajty;
        if (format == K1_DZW_FORMAT_F32) {
            memcpy(cel, &s[2 * i], 2 * sizeof(float));
            continue;
        }
        for (int c = 0; c < 2; c++) {
            float v = s[2 * i + (uint32_t)c] * 32768.0f;
            int32_t q = (int32_t)(v + (v < 0 ? -0.5f : 0.5f));
            int16_t w = (int16_t)(q > 32767 ? 32767 : q < -32767 ? -32767 : q);
            memcpy(cel + 2 * c, &w, 2);
        }
    }
}

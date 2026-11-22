/* K1 — the clipboard's text check (k1_schowek.h). */
#include "k1_schowek.h"

/* One UTF-8 character at t (n bytes left): its length, 0 when it is not valid. */
static size_t znak(const uint8_t *t, size_t n, uint32_t *k)
{
    static const uint32_t min[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    size_t d = t[0] < 0x80 ? 1 : (t[0] & 0xe0) == 0xc0 ? 2 : (t[0] & 0xf0) == 0xe0 ? 3 : (t[0] & 0xf8) == 0xf0 ? 4 : 0;
    uint32_t v;
    if (d == 0 || d > n)
        return 0;
    v = d == 1 ? t[0] : d == 2 ? t[0] & 0x1fu : d == 3 ? t[0] & 0x0fu : t[0] & 0x07u;
    for (size_t i = 1; i < d; i++) {
        if ((t[i] & 0xc0) != 0x80)
            return 0;
        v = v << 6 | (t[i] & 0x3fu);
    }
    if (v < min[d] || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
        return 0;
    *k = v;
    return d;
}

int k1_schowek_dobry(const uint8_t *t, size_t n)
{
    if (t == NULL || n < 1 || n > K1_SCHOWEK_MAKS)
        return 0;
    for (size_t i = 0; i < n;) {
        uint32_t k;
        size_t d = znak(t + i, n - i, &k);
        if (d == 0 || (k < 0x20 && k != '\t' && k != '\n' && k != '\r') || k == 0x7f)
            return 0;
        i += d;
    }
    return 1;
}

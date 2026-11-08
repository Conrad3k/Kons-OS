/* K1 — little-endian integers in on-disk formats (manifest, layout block, battery history, skarbiec). */
#ifndef K1_BAJTY_H
#define K1_BAJTY_H

#include <stdint.h>

static inline uint64_t k1_c_le(const uint8_t *p, int n)
{
    uint64_t v = 0;
    while (n-- > 0)
        v = v << 8 | p[n];
    return v;
}

static inline void k1_z_le(uint8_t *p, uint64_t v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static inline uint16_t k1_c16(const uint8_t *p) { return (uint16_t)k1_c_le(p, 2); }
static inline uint32_t k1_c32(const uint8_t *p) { return (uint32_t)k1_c_le(p, 4); }
static inline uint64_t k1_c64(const uint8_t *p) { return k1_c_le(p, 8); }
static inline void k1_z16(uint8_t *p, uint16_t v) { k1_z_le(p, v, 2); }
static inline void k1_z32(uint8_t *p, uint32_t v) { k1_z_le(p, v, 4); }
static inline void k1_z64(uint8_t *p, uint64_t v) { k1_z_le(p, v, 8); }

#endif

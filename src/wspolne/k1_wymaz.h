/* K1 — wiping secrets (0.9.1 step 2: Wi-Fi passwords and keys): `n` bytes set to zero so that the compiler does not drop
 * the stores (the memory is not read afterwards). */
#ifndef K1_WYMAZ_H
#define K1_WYMAZ_H

#include <stddef.h>
#include <stdint.h>

static inline void k1_wymaz(void *p, size_t n)
{
    volatile uint8_t *v = p;
    while (n-- > 0)
        *v++ = 0;
}

#endif

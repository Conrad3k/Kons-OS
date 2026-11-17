/* K1 strefa — the guest's lines with numbers (linia.h). Part of the VMM. */
#include <string.h>

#include "linia.h"

int linia_liczby(const char *l, const char *slowo, uint32_t *w, int n, uint32_t maks)
{
    size_t d = strlen(slowo);
    int i;
    if (strncmp(l, slowo, d) != 0)
        return 0;
    l += d;
    for (i = 0; i < n; i++) {
        uint64_t v = 0;
        int cyfry = 0;
        if (*l++ != ' ')
            return 0;
        for (; *l >= '0' && *l <= '9'; l++, cyfry++) {
            if (cyfry > 0 && v == 0) /* a leading zero */
                return 0;
            v = v * 10u + (uint64_t)(*l - '0');
            if (v > maks)
                return 0;
        }
        if (cyfry == 0)
            return 0;
        w[i] = (uint32_t)v;
    }
    return *l == '\0';
}

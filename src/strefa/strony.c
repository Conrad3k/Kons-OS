/* K1 strefa — the balloon's lists, blocks and statistics (strony.h). Part of the VMM. */
#include "strony.h"

#include <string.h>

int strony_lista(const uint8_t *b, uint64_t bajty, const struct gosc_pamiec *m, struct strony_zakres *z,
                 const char **powod)
{
    uint64_t i;
    int n = 0;
    if (bajty % 4 != 0 || bajty / 4 > STRONY_LISTA_MAKS) {
        *powod = "balon: lista stron nie z całych numerów albo za długa";
        return -1;
    }
    for (i = 0; i < bajty / 4; i++) {
        uint32_t nr;
        const uint8_t *p;
        uint64_t od;
        memcpy(&nr, b + 4 * i, 4);
        p = gp_wsk(m, (uint64_t)nr * STRONA, STRONA);
        if (p == NULL) {
            *powod = "balon: strona poza pamięcią gościa";
            return -1;
        }
        od = (uint64_t)(p - m->baza);
        if (n > 0 && z[n - 1].od + z[n - 1].dl == od)
            z[n - 1].dl += STRONA;
        else
            z[n++] = (struct strony_zakres){ od, STRONA };
    }
    return n;
}

int strony_blok(const struct gosc_pamiec *m, const uint8_t *p, uint64_t dl, struct strony_zakres *z, const char **powod)
{
    uint64_t od = (uint64_t)(p - m->baza);
    if (dl == 0 || od % STRONA != 0 || dl % STRONA != 0) {
        *powod = "balon: zgłoszony blok nie z całych stron";
        return -1;
    }
    *z = (struct strony_zakres){ od, dl };
    return 0;
}

int strony_statystyki(const uint8_t *b, uint64_t bajty, uint64_t st[STRONY_STAT_MAKS], uint32_t *jest,
                      const char **powod)
{
    uint64_t i;
    *jest = 0;
    if (bajty % STRONY_STAT_DL != 0 || bajty / STRONY_STAT_DL > STRONY_STAT_MAKS) {
        *powod = "balon: statystyki złej długości";
        return -1;
    }
    for (i = 0; i < bajty / STRONY_STAT_DL; i++) {
        uint16_t tag;
        uint64_t v;
        memcpy(&tag, b + STRONY_STAT_DL * i, 2);
        memcpy(&v, b + STRONY_STAT_DL * i + 2, 8);
        if (tag >= STRONY_STAT_MAKS || (*jest & (1u << tag))) {
            *powod = "balon: nieznana albo powtórzona statystyka";
            return -1;
        }
        st[tag] = v;
        *jest |= 1u << tag;
    }
    return 0;
}

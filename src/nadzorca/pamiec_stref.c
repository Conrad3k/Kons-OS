/* K1 nadzorca — the memory of the zones (pamiec_stref.h). Part of nadzorca. Since 0.10.1 step 2 (ADR 0044) a zone has a
 * balloon: its guest has its whole upper limit from the start, but K1 holds only what it really uses — the rest goes
 * back (src/strefa/balon.c). The limit is the computer's memory minus K1's reserve (D2); the VMM's sandbox gets it plus
 * the VMM's own room (strefy.c) — fixed for the zone's whole life (T4). */
#include "pamiec_stref.h"

/* K1's reserve: from `od_mib` of the computer's memory on (the last row that fits counts). */
static const struct {
    uint64_t od_mib;
    uint32_t rezerwa_mib;
} REZERWA[] = {
    { 0, 768 },     /* 4 GB (IdeaPad 110) */
    { 6144, 1024 }, /* 8 GB (X230) and more */
};

uint32_t pamiec_rezerwa_k1(uint64_t ram_mib)
{
    uint32_t r = REZERWA[0].rezerwa_mib;
    for (unsigned i = 0; i < sizeof REZERWA / sizeof REZERWA[0]; i++)
        if (ram_mib >= REZERWA[i].od_mib)
            r = REZERWA[i].rezerwa_mib;
    return r;
}

uint32_t pamiec_granica(uint64_t ram_mib, uint32_t start_mib)
{
    uint64_t r = pamiec_rezerwa_k1(ram_mib), g = ram_mib > r ? ram_mib - r : 0;
    g &= ~(uint64_t)1;
    if (g > PAMIEC_STREFY_MAKS)
        g = PAMIEC_STREFY_MAKS;
    return g > start_mib ? (uint32_t)g : start_mib;
}

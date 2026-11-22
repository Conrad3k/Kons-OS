/* K1 nadzorca — the memory of the zones (0.10.1, ADR 0044): how much a zone may get (step 2) and the rozdzielacz that
 * shares the computer's memory between K1 and the zones (step 3, T6). Pure (test_pamiec_stref). */
#ifndef K1_PAMIEC_STREF_H
#define K1_PAMIEC_STREF_H

#include <stdint.h>

/* What K1 keeps for itself, by the computer's memory (D3's recommendation; one table: pamiec_stref.c). */
uint32_t pamiec_rezerwa_k1(uint64_t ram_mib);

/* A zone's upper limit in MiB (D2, the owner 2026-11-16: the computer's memory minus K1's reserve), never below its
 * memory at the start (its row of K1_STREFY), whole 2 MiB, at most PAMIEC_STREFY_MAKS. */
#define PAMIEC_STREFY_MAKS 262144u /* = K1_STREFA_PAMIEC_MAKS (k1_komunikat.h) */
uint32_t pamiec_granica(uint64_t ram_mib, uint32_t start_mib);

/* One running zone as the rozdzielacz sees it (MiB). What the VMM said (STREFA_PAMIEC) is a hint, never the truth:
 * the twarda granica is the sandbox's memory.max (ADR 0044 T3, T4). */
struct pamiec_strefy {
    int fokus;              /* its window has the focus (gui: STREFA_FOKUS) */
    uint32_t granica, start; /* its upper limit, its memory at the start */
    int32_t w_k1;           /* the guest's memory K1 holds now */
    int32_t dostepna;       /* available in the guest by its own statistics; -1: not known yet */
    int32_t balon, cel;     /* in the balloon now; K1's target (the rozdzielacz changes it) */
    int32_t filmy;          /* 0.10.9 step 2 (T8): the buffers of its film decoder — no balloon gives them back, but they
                             * count when K1 chooses the zone to close */
};
#define PAMIEC_STREF_MAKS 8 /* zones at once (≥ K1_STREFY_MAKS) */

/* How much more K1 gets back when the zone's balloon grows as far as it may: the memory K1 holds of the guest that the
 * guest does not need (its page cache), above its zapas of free memory; the focused one never below its memory at the
 * start (MiB); 0
 * while its statistics are not known. The free pages the guest already reported are K1's anyway. */
int32_t pamiec_moze_oddac(const struct pamiec_strefy *z);

/* Once a second: K1 has `dostepna_mib` free (and droppable), keeps `rezerwa_mib`. Below the reserve the balloons grow
 * (the zones in the background first, the one with the most to give first; the focused one last; a step a second at
 * most; never into a guest's zapas): the target makes the guest as big as what K1 should hold of it; with the reserve and
 * some more again they shrink (the focused one first). Returns PAMIEC_DOBRZE; PAMIEC_MALO
 * when K1 stays below its reserve and no balloon can take more (D3 (2): the window); PAMIEC_KRYTYCZNIE when it has also
 * less than a quarter of the reserve — *zamknij is then the zone in the background with the most memory (its guest and its
 * film decoder; D3 (3)), -1:
 * none (only the focused one: the kernel's OOM killer decides). */
enum { PAMIEC_DOBRZE = 0, PAMIEC_MALO = 1, PAMIEC_KRYTYCZNIE = 2 };
int pamiec_rozdziel(int64_t dostepna_mib, uint32_t rezerwa_mib, struct pamiec_strefy *s, int n, int *zamknij);

#endif

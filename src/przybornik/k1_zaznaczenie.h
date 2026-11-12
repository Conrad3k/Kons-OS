/* K1 przybornik — the selection of many entries (0.8.2 step 6, L24): a click selects one, Ctrl+click adds or takes
 * away one, Shift+click selects from the last one clicked (the anchor) to this one in the order shown (with Ctrl: adds
 * that range), Ctrl+A — everything shown. Entries by number (k1_dokument.h); the order shown is the caller's.
 * Pure functions — host test testy/host/test_przeciaganie.c. */
#ifndef K1_ZAZNACZENIE_H
#define K1_ZAZNACZENIE_H

#include <stdint.h>

#include "k1_dokument.h"

#define K1_ZAZN_MAKS K1_DOK_MAKS

struct k1_zaznaczenie {
    uint32_t id[K1_ZAZN_MAKS];
    int n;
    uint32_t kotwica; /* the entry Shift+click counts from (0: none) */
};

void k1_zazn_wyczysc(struct k1_zaznaczenie *z);
void k1_zazn_jeden(struct k1_zaznaczenie *z, uint32_t id);    /* a click: only this one (0: none) */
void k1_zazn_przelacz(struct k1_zaznaczenie *z, uint32_t id); /* Ctrl+click */
/* Shift+click on `id`: from the anchor to `id` in `kolejnosc` (n entries in the order shown); `dodaj` (Ctrl+Shift):
 * to what is selected already. Without an anchor shown: like a click. */
void k1_zazn_zakres(struct k1_zaznaczenie *z, const uint32_t *kolejnosc, int n, uint32_t id, int dodaj);
void k1_zazn_wszystko(struct k1_zaznaczenie *z, const uint32_t *kolejnosc, int n); /* Ctrl+A */
int k1_zazn_jest(const struct k1_zaznaczenie *z, uint32_t id);
/* Only entries for which `jest` says 1 stay (the list changed: gone, or shown elsewhere now); how many stay. */
int k1_zazn_przytnij(struct k1_zaznaczenie *z, int (*jest)(uint32_t id));

#endif

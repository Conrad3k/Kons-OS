/* K1 — the VA driver of the zone: what comes back from K1, checked before the driver uses it (0.13.2 step 4): the content
 * of the capset K1_FILMY and the VMM's answer in the film context's shared memory. Both are written by the VMM — the
 * driver takes nothing it did not expect (a capset of another version or with unknown codecs is no film context; an
 * answer with another number or an unknown result is no answer). Pure (test_va_strefy, fuzz_va_strefy). */
#include <string.h>

#include "sterownik.h"

int va_zbior_dobry(const struct k1_filmy_zbior *z, size_t n)
{
    uint32_t znane = (1u << K1_KODEKI_MAKS) - 1u;
    return n == sizeof *z && z->wersja == K1_FILMY_CAPSET_WERSJA && (z->kodeki & znane) != 0 &&
           (z->kodeki & ~znane) == 0 && z->maks_w >= K1_FILMY_MIN_WH && z->maks_w <= K1_FILMY_MAKS_W &&
           z->maks_h >= K1_FILMY_MIN_WH && z->maks_h <= K1_FILMY_MAKS_H;
}

int va_odpowiedz(const void *wsp, uint32_t numer)
{
    struct k1_kf_odpowiedz o;
    memcpy(&o, wsp, sizeof o);
    return o.numer == numer && o.wynik >= K1_KF_BLAD_DEKODER && o.wynik <= 0 ? o.wynik : VA_BRAK_ODPOWIEDZI;
}

/* K1 — the VA driver of the zone: VA surfaces → the film's surface numbers (0.13.2 step 4). A film in the decoder has
 * a fixed set of surfaces (K1_KF_OTWORZ d), but FFmpeg's pool with VA-API 1.x grows as it needs and its VA context
 * gets no list of them: a surface gets a number when it is first a target, keeps it while it holds a picture, and
 * gives it up for a new target only when the picture being decoded does not refer to it (a picture no longer in the
 * decoded picture buffer is never a reference again). Pure (test_va_strefy). */
#include "sterownik.h"

void va_klatki_zacznij(struct va_klatki *k)
{
    for (int i = 0; i < VA_KLATKI; i++)
        k->pow[i] = VA_INVALID_SURFACE, k->zdekodowana[i] = 0;
}

static int uzywana(VASurfaceID s, const VASurfaceID *uzywane, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (uzywane[i] == s)
            return 1;
    return 0;
}

int va_klatki_cel(struct va_klatki *k, VASurfaceID cel, const VASurfaceID *uzywane, size_t n)
{
    int wolny = -1, zwolnij = -1;
    if (cel == VA_INVALID_SURFACE)
        return -1;
    for (int i = 0; i < VA_KLATKI; i++) {
        if (k->pow[i] == cel) {
            k->zdekodowana[i] = 0;
            return i;
        }
        if (k->pow[i] == VA_INVALID_SURFACE && wolny < 0)
            wolny = i;
        else if (k->pow[i] != VA_INVALID_SURFACE && zwolnij < 0 && !uzywana(k->pow[i], uzywane, n))
            zwolnij = i;
    }
    if (wolny < 0)
        wolny = zwolnij;
    if (wolny >= 0)
        k->pow[wolny] = cel, k->zdekodowana[wolny] = 0;
    return wolny;
}

void va_klatki_gotowa(struct va_klatki *k, int numer, int dobrze)
{
    if (numer >= 0 && numer < VA_KLATKI)
        k->zdekodowana[numer] = dobrze != 0;
}

int va_klatki_numer(const struct va_klatki *k, VASurfaceID s)
{
    for (int i = 0; i < VA_KLATKI; i++)
        if (k->pow[i] == s && s != VA_INVALID_SURFACE)
            return k->zdekodowana[i] ? i : -1;
    return -1;
}

void va_klatki_zapomnij(struct va_klatki *k, VASurfaceID s)
{
    for (int i = 0; i < VA_KLATKI; i++)
        if (k->pow[i] == s)
            k->pow[i] = VA_INVALID_SURFACE, k->zdekodowana[i] = 0;
}

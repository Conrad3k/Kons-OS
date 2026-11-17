/* K1 nadzorca — the zones' balloons (0.10.1 step 2, ADR 0044 T3): what each VMM says of its guest's memory
 * (STREFA_PAMIEC, at most once a second — a hint, never the truth: the twarda granica is its sandbox's memory.max) and
 * what K1 asks of the balloon (STREFA_BALON). In step 2 nadzorca keeps every balloon empty and inflates one only on
 * the test program's request (the mechanics); who gets how much memory when the computer runs short — step 3 (T6).
 * Part of nadzorca. */
#include "nadzorca.h"

static struct k1_komunikat pamiec[K1_STREFY_MAKS + 1]; /* the last STREFA_PAMIEC of each zone (typ 0: none) */

void strefa_pamiec(int nr, const struct k1_komunikat *k)
{
    if (nr >= 1 && nr <= K1_STREFY_MAKS)
        pamiec[nr] = *k;
}

void strefa_pamiec_koniec(int nr)
{
    const struct k1_komunikat *k = nr >= 1 && nr <= K1_STREFY_MAKS ? &pamiec[nr] : NULL;
    if (k == NULL || k->typ == 0)
        return;
    k1_log(TAG, "strefa %d: pamięć na końcu — w K1 %d MiB, balon %d MiB (cel %d), oddane %d MiB", nr, k->a, k->c, k->d,
           k->e);
    memset(&pamiec[nr], 0, sizeof pamiec[nr]);
}

void strefa_balon(int nr, int32_t mib)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_BALON, (uint32_t)nr, mib, 0, 0, 0, 0);
    if (nr < 1 || nr > K1_STREFY_MAKS || strefa_kanal[nr] < 0 || mib < 0 || mib > K1_STREFA_PAMIEC_MAKS) {
        k1_log(TAG, "strefa %d: balon — strefa nie działa", nr);
        return;
    }
    if (k1_kanal_wyslij(strefa_kanal[nr], &k, sizeof k, -1) != 0)
        k1_log(TAG, "strefa %d: balon: %s", nr, strerror(errno));
    else
        k1_log(TAG, "strefa %d: cel balonu %d MiB", nr, mib);
}

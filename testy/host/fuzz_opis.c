/* libFuzzer harness (0.10.2 step 2, ADR 0045): the screen's description from a client and from ekran
 * (src/wspolne/k1_opis.c). The first byte picks the sender; an accepted description must keep every record inside the
 * size and its names ended — what ekran and the test program then use without checking again. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_opis.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct k1_opis_el el[K1_OPIS_EKRAN_MAKS];
    struct k1_opis_naglowek n;
    enum k1_opis_od od;
    int r;
    if (size < 1)
        return 0;
    od = data[0] & 1 ? K1_OPIS_OD_EKRANU : K1_OPIS_OD_KLIENTA;
    r = k1_opis_sprawdz(data + 1, size - 1, od, 640, 480, el);
    if (r < 0)
        return 0;
    memcpy(&n, data + 1, sizeof n);
    for (int i = 0; i < r; i++)
        if (el[i].w <= 0 || el[i].h <= 0 || el[i].x < 0 || el[i].y < 0 || el[i].x + el[i].w > n.szer ||
            el[i].y + el[i].h > n.wys || strnlen(el[i].nazwa, sizeof el[i].nazwa) >= sizeof el[i].nazwa ||
            (od == K1_OPIS_OD_KLIENTA && (n.szer != 640 || n.wys != 480)))
            abort();
    return 0;
}

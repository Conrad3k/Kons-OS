#include "k1_zdarzenie.h"

#include <string.h>

int k1_zdarzenie_sprawdz(const void *buf, size_t len, struct k1_zdarzenie *out)
{
    struct k1_zdarzenie z;

    memset(out, 0, sizeof *out);
    if (buf == NULL || len != sizeof z)
        return -1;
    memcpy(&z, buf, sizeof z); /* no aliasing/alignment assumptions about buf */
    if (z.wersja != K1_ZDARZENIE_WERSJA || z.dlugosc != sizeof z || z.zarezerwowane != 0)
        return -1;
    switch (z.typ) {
    case K1_ZD_KLAWISZ:
        if (z.kod == 0 || z.kod > K1_ZD_KOD_MAX || (z.kod >= 0x100 && z.kod < 0x160) || z.wartosc < 0 ||
            z.wartosc > 2 || z.dx != 0 || z.dy != 0)
            return -1;
        break;
    case K1_ZD_RUCH:
        if (z.kod != 0 || z.wartosc != 0 || z.dx < -K1_ZD_RUCH_MAX || z.dx > K1_ZD_RUCH_MAX ||
            z.dy < -K1_ZD_RUCH_MAX || z.dy > K1_ZD_RUCH_MAX || (z.dx == 0 && z.dy == 0))
            return -1;
        break;
    case K1_ZD_PRZYCISK:
        if (z.kod < 0x110 || z.kod > 0x117 || z.wartosc < 0 || z.wartosc > 1 || z.dx != 0 || z.dy != 0)
            return -1;
        break;
    default:
        return -1;
    }
    *out = z;
    return 0;
}

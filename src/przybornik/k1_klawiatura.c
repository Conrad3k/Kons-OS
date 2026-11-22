#include "k1_klawiatura.h"

#include <linux/input-event-codes.h>

#include <string.h>

#include "k1_komunikat.h"
#include "k1_rysuj.h"

uint32_t k1_znak_klawisza(unsigned kod, unsigned mody)
{
    /* Rows of the US layout from KEY_1 (2) to KEY_SLASH (53): normal / shift. */
    static const char zwykle[] = "1234567890-=\0\0qwertyuiop[]\0\0asdfghjkl;'`\0\\zxcvbnm,./";
    static const char shift[] = "!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
    static const struct {
        unsigned kod;
        uint16_t male, duze;
    } pl[] = { { KEY_A, 0x105, 0x104 }, { KEY_C, 0x107, 0x106 }, { KEY_E, 0x119, 0x118 },
               { KEY_L, 0x142, 0x141 }, { KEY_N, 0x144, 0x143 }, { KEY_O, 0xf3, 0xd3 },
               { KEY_S, 0x15b, 0x15a }, { KEY_X, 0x17a, 0x179 }, { KEY_Z, 0x17c, 0x17b } };
    int s = (mody & K1_MOD_SHIFT) != 0;
    unsigned i;

    if (mody & (K1_MOD_CTRL | K1_MOD_ALT))
        return 0;
    if (mody & K1_MOD_ALTGR) {
        for (i = 0; i < sizeof pl / sizeof pl[0]; i++)
            if (pl[i].kod == kod)
                return s ? pl[i].duze : pl[i].male;
        return 0;
    }
    if (kod == KEY_SPACE)
        return ' ';
    if (kod >= KEY_1 && kod <= KEY_SLASH) {
        char c = (s ? shift : zwykle)[kod - KEY_1];
        return (unsigned char)c;
    }
    /* the numeric keypad (NumLock on: digits) — KEY_KP7 (71) … KEY_KPDOT (83), KEY_KPSLASH (98) */
    if (kod >= KEY_KP7 && kod <= KEY_KPDOT)
        return (unsigned char)"789-456+1230."[kod - KEY_KP7];
    if (kod == KEY_KPASTERISK)
        return '*';
    if (kod == KEY_KPSLASH)
        return '/';
    return 0;
}

void k1_pole_wklej(char *t, size_t *dl, size_t maks, const char *z, size_t n)
{
    for (size_t i = 0; i < n;) {
        size_t c = 1;
        while (i + c < n && ((unsigned char)z[i + c] & 0xc0) == 0x80)
            c++;
        if ((unsigned char)z[i] < 0x20 && z[i] != '\t' && z[i] != '\n') {
            i += c;
            continue;
        }
        if (*dl + c > maks)
            break;
        if ((unsigned char)z[i] < 0x20)
            t[(*dl)++] = ' ';
        else
            memcpy(t + *dl, z + i, c), *dl += c;
        i += c;
    }
    t[*dl] = '\0';
}

int k1_pole_klawisz(char *t, size_t *dl, size_t maks, unsigned kod, unsigned mody)
{
    uint32_t z;
    char u[4];
    int n;
    if (kod == KEY_BACKSPACE) {
        if (*dl > 0) {
            *dl = k1_utf8_poczatek(t, *dl - 1);
            t[*dl] = '\0';
        }
        return 1;
    }
    if ((z = k1_znak_klawisza(kod, mody)) < 0x20)
        return 0;
    n = k1_utf8_zapisz(z, u, sizeof u);
    if (n > 0 && *dl + (size_t)n <= maks) {
        memcpy(t + *dl, u, (size_t)n);
        *dl += (size_t)n;
        t[*dl] = '\0';
    }
    return 1;
}

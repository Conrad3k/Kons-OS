/* K1 nadzorca — kernel device events. See uevent.h. */
#include "uevent.h"

#include <string.h>

/* Copies `wartosc` (`n` bytes, no NUL) if it fits; values with '/' only where allowed. */
static int kopiuj(char *cel, size_t rozmiar, const char *wartosc, size_t n, int ukosnik)
{
    size_t i;
    if (n == 0 || n >= rozmiar)
        return -1;
    for (i = 0; i < n; i++) {
        char c = wartosc[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
              (ukosnik && c == '/')))
            return -1;
    }
    memcpy(cel, wartosc, n);
    cel[n] = '\0';
    return 0;
}

int k1_uevent_parsuj(const char *buf, size_t n, struct k1_uevent *u)
{
    size_t i = 0, l;
    const char *at;
    int ma_akcje = 0;
    memset(u, 0, sizeof *u);
    if (buf == NULL || n == 0 || n > 8192)
        return -1;
    /* header "action@devpath": the kernel's format (udev's own packets start with "libudev") */
    l = strnlen(buf, n);
    if (l == n || (at = memchr(buf, '@', l)) == NULL || at == buf || at[1] != '/')
        return -1;
    i = l + 1;
    while (i < n) {
        const char *p = buf + i, *eq;
        l = strnlen(p, n - i);
        if (l == n - i)
            return -1; /* the last value must end with NUL */
        eq = memchr(p, '=', l);
        if (eq != NULL) {
            size_t kl = (size_t)(eq - p), vl = l - kl - 1;
            const char *v = eq + 1;
            if (kl == 6 && memcmp(p, "ACTION", 6) == 0) {
                ma_akcje = 1;
                u->akcja = vl == 3 && memcmp(v, "add", 3) == 0      ? K1_UE_DODANE
                           : vl == 6 && memcmp(v, "remove", 6) == 0 ? K1_UE_USUNIETE
                           : vl == 6 && memcmp(v, "change", 6) == 0 ? K1_UE_ZMIANA
                                                                    : K1_UE_INNA;
            } else if (kl == 9 && memcmp(p, "SUBSYSTEM", 9) == 0) {
                u->rodzaj = vl == 5 && memcmp(v, "input", 5) == 0           ? K1_UE_WEJSCIE
                            : vl == 5 && memcmp(v, "block", 5) == 0         ? K1_UE_DYSK
                            : vl == 12 && memcmp(v, "power_supply", 12) == 0 ? K1_UE_ZASILANIE
                            : vl == 5 && memcmp(v, "sound", 5) == 0         ? K1_UE_DZWIEK
                                                                            : K1_UE_NIEISTOTNE;
            } else if (kl == 7 && memcmp(p, "DEVNAME", 7) == 0) {
                if (kopiuj(u->devname, sizeof u->devname, v, vl, 1) != 0)
                    u->devname[0] = '\0';
            } else if (kl == 8 && memcmp(p, "PARTNAME", 8) == 0) {
                if (kopiuj(u->partname, sizeof u->partname, v, vl, 0) != 0)
                    u->partname[0] = '\0';
            }
        }
        i += l + 1;
    }
    return ma_akcje ? 0 : -1;
}

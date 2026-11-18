/* K1 gosc — the picture on the guest's display (kms.c). The pattern the test checks on K1's screenshot: a dark blue
 * background, yellow squares in the four corners, a band across the middle — green after an even number of keys,
 * orange after an odd one — and a white square where the last click was. */
#include <stdarg.h>
#include <stdio.h>

#include "gosc.h"
#include "kms.h"

#define TLO 0xff20304au
#define ROG 0xffffd700u
#define PAS_PARZYSTY 0xff2e8b57u
#define PAS_NIEPARZYSTY 0xffd2691eu
#define KLIK 0xffffffffu
#define ROG_BOK 24
#define ABS_MAKS 32767

static struct kms k;

void kms_log(const char *fmt, ...)
{
    char b[160];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    loguj("%s", b);
}

int obraz_start(void)
{
    return kms_start(&k);
}

int obraz_zmiany_fd(void)
{
    return k.px != NULL ? kms_zmiany_fd() : -1;
}

/* 0.10.5 step 4: K1 resized the window — the pattern on the whole new display (test-strefa checks it). */
int obraz_zmiana(void)
{
    return kms_zmiana(&k);
}

static void prostokat(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kolor)
{
    int32_t i, j;
    for (j = y < 0 ? 0 : y; j < y + h && j < (int32_t)k.wys; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < (int32_t)k.szer; i++)
            k.px[(size_t)j * k.krok + (size_t)i] = kolor;
}

void obraz_rysuj(unsigned klawisze, unsigned klikniecia, int32_t x, int32_t y)
{
    int32_t w = (int32_t)k.szer, h = (int32_t)k.wys;
    if (k.px == NULL)
        return;
    prostokat(0, 0, w, h, TLO);
    prostokat(0, 0, ROG_BOK, ROG_BOK, ROG);
    prostokat(w - ROG_BOK, 0, ROG_BOK, ROG_BOK, ROG);
    prostokat(0, h - ROG_BOK, ROG_BOK, ROG_BOK, ROG);
    prostokat(w - ROG_BOK, h - ROG_BOK, ROG_BOK, ROG_BOK, ROG);
    prostokat(w / 8, h / 2 - h / 10, w - w / 4, h / 5, klawisze % 2 ? PAS_NIEPARZYSTY : PAS_PARZYSTY);
    if (klikniecia > 0 && x >= 0 && y >= 0)
        prostokat((int32_t)((int64_t)x * (w - 1) / ABS_MAKS) - 12, (int32_t)((int64_t)y * (h - 1) / ABS_MAKS) - 12, 24, 24,
                  KLIK);
    kms_pokaz(0, 0, k.szer, k.wys);
}

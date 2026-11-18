/* K1 kompozytor — the guest's display (kompozytor.h): the zone kernel's virtio-gpu through kms.c (shared with gosc). */
#include <stdarg.h>
#include <stdio.h>

#include "kms.h"
#include "kompozytor.h"

struct wyswietlacz ekran;

void kms_log(const char *fmt, ...)
{
    char b[160];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    kloguj("%s", b);
}

int ekran_start(void)
{
    struct kms k;
    if (kms_start(&k) != 0)
        return -1;
    ekran = (struct wyswietlacz){ k.px, (int32_t)k.szer, (int32_t)k.wys, (int32_t)k.krok };
    return 0;
}

int ekran_zmiany_fd(void)
{
    return kms_zmiany_fd();
}

int ekran_zmiana(void)
{
    struct kms k = { ekran.px, (uint32_t)ekran.szer, (uint32_t)ekran.wys, (uint32_t)ekran.krok };
    if (!kms_zmiana(&k))
        return 0;
    ekran = (struct wyswietlacz){ k.px, (int32_t)k.szer, (int32_t)k.wys, (int32_t)k.krok };
    scena_zmiana(0, 0, ekran.szer, ekran.wys);
    return 1;
}

void ekran_pokaz(int32_t x, int32_t y, int32_t w, int32_t h)
{
    kms_pokaz((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h);
}

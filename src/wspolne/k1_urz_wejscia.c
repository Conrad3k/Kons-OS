/* K1 — an input device as nadzorca sees it in sysfs (0.13.0 step 5, ADR 0060). See k1_urz_wejscia.h. */
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_urz_wejscia.h"

#define SLOWO_CYFR 16 /* hex digits of one 64-bit word */
#define SLOW_MAKS 32  /* the longest bitmap K1 reads (KEY_MAX + 1 = 768 bits = 12 words) with room */

static int cyfra(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* The words of the bitmap, the most significant first, into s[]; how many, or -1. A trailing newline is allowed. */
static int slowa(const char *t, uint64_t s[SLOW_MAKS])
{
    int n = 0, cyfr = 0;
    uint64_t v = 0;
    for (;; t++) {
        int d = cyfra(*t);
        if (d >= 0) {
            if (++cyfr > SLOWO_CYFR)
                return -1;
            v = v << 4 | (uint64_t)d;
            continue;
        }
        if (*t != ' ' && *t != '\n' && *t != '\0')
            return -1;
        if (cyfr == 0 || n == SLOW_MAKS) /* an empty word (two spaces, a leading space) or too many */
            return -1;
        s[n++] = v;
        v = 0, cyfr = 0;
        if (*t == '\0' || (*t == '\n' && t[1] == '\0'))
            return n;
        if (*t == '\n')
            return -1;
    }
}

int k1_urz_bit(const char *bitmapa, unsigned nr)
{
    uint64_t s[SLOW_MAKS];
    int n = bitmapa == NULL ? 0 : slowa(bitmapa, s);
    unsigned od_prawej = nr / 64;
    if (bitmapa == NULL)
        return 0;
    if (n < 0)
        return -1;
    if (od_prawej >= (unsigned)n)
        return 0;
    return (int)(s[(unsigned)n - 1 - od_prawej] >> (nr % 64) & 1);
}

static int jest(const char *b, unsigned nr)
{
    return k1_urz_bit(b, nr) == 1;
}

int k1_urz_rodzaj(const struct k1_urz_bity *b)
{
    int klawisze = jest(b->ev, EV_KEY);
    if (klawisze && jest(b->key, KEY_A) && jest(b->key, KEY_SPACE))
        return K1_URZ_KLAWIATURA;
    if (jest(b->ev, EV_REL) && jest(b->rel, REL_X) && jest(b->rel, REL_Y))
        return K1_URZ_MYSZ;
    if (jest(b->ev, EV_ABS) && jest(b->abs, ABS_X) && jest(b->abs, ABS_Y) && klawisze && jest(b->key, BTN_TOUCH) &&
        jest(b->key, BTN_TOOL_FINGER) && !jest(b->prop, INPUT_PROP_DIRECT))
        return K1_URZ_MYSZ; /* a touchpad (wejscie: TOUCHPAD) */
    return K1_URZ_INNE;
}

int k1_urz_na_usb(const char *sciezka)
{
    /* .../0000:00:14.0/usb1/1-2/1-2:1.0/0003:046D:C52B.0001/input/input7/event7: a USB bus ("usbN") on the way */
    const char *p = sciezka;
    while ((p = strstr(p, "/usb")) != NULL) {
        p += 4;
        if (*p >= '1' && *p <= '9')
            return 1;
    }
    return 0;
}

static uint32_t fnv(uint32_t h, const char *t, size_t maks)
{
    size_t i;
    for (i = 0; t != NULL && t[i] != '\0' && t[i] != '\n' && i < maks; i++)
        h = (h ^ (uint8_t)t[i]) * 16777619u;
    return h;
}

uint32_t k1_urz_znak(const char *vendor, const char *product, const char *nazwa)
{
    uint32_t h = fnv(2166136261u, vendor, 8);
    h = fnv((h ^ ':') * 16777619u, product, 8);
    h = fnv((h ^ ':') * 16777619u, nazwa, 128);
    return h != 0 ? h : 1;
}

/* K1 — battery history of earlier boots. See k1_bat_arch.h. */
#include "k1_bat_arch.h"
#include "k1_bajty.h"

#include <string.h>

static int probka_ok(uint8_t v)
{
    return (v & 0x7f) <= 100;
}

int k1_bat_arch_czytaj(const void *buf, size_t n, struct k1_bat_arch *a)
{
    const uint8_t *b = buf;
    size_t off = 8;
    int i, ile;
    memset(a, 0, sizeof *a);
    if (b == NULL || n < 8 || memcmp(b, "K1B2", 4) != 0 || b[6] != 0 || b[7] != 0)
        return -1;
    ile = k1_c16(b + 4);
    if (ile > K1_BAT_ARCH_SESJE)
        return -1;
    for (i = 0; i < ile; i++) {
        struct k1_bat_sesja *s = &a->s[i];
        uint16_t j;
        if (n - off < 8)
            goto zle;
        s->start_min = k1_c32(b + off);
        s->interwal = k1_c16(b + off + 4);
        s->n = k1_c16(b + off + 6);
        off += 8;
        if (s->interwal < 1 || s->n < 1 || n - off < s->n || a->probek + s->n > K1_BAT_ARCH_PROBKI ||
            (i > 0 && s->start_min < a->s[i - 1].start_min))
            goto zle;
        for (j = 0; j < s->n; j++)
            if (!probka_ok(b[off + j]))
                goto zle;
        s->od = (uint16_t)a->probek;
        memcpy(a->p + a->probek, b + off, s->n);
        a->probek += s->n;
        off += s->n;
    }
    if (off != n)
        goto zle;
    a->n = ile;
    return 0;
zle:
    memset(a, 0, sizeof *a);
    return -1;
}

size_t k1_bat_arch_zapisz(const struct k1_bat_arch *a, const struct k1_bat_sesja *biezaca, const uint8_t *probki,
                          uint32_t teraz_min, uint8_t *buf, size_t maks)
{
    const struct k1_bat_sesja *lista[K1_BAT_ARCH_SESJE + 1];
    const uint8_t *dane[K1_BAT_ARCH_SESJE + 1];
    int n = 0, i, od = 0, suma = 0;
    size_t off = 8;
    for (i = 0; i < a->n; i++) {
        if (a->s[i].start_min + (uint32_t)K1_BAT_ARCH_DNI * 24 * 60 < teraz_min)
            continue; /* older than a week */
        lista[n] = &a->s[i];
        dane[n++] = a->p + a->s[i].od;
    }
    if (biezaca != NULL && biezaca->n > 0 && biezaca->interwal > 0) {
        lista[n] = biezaca;
        dane[n++] = probki;
    }
    /* the newest ones that fit the limits */
    for (od = n; od > 0 && n - (od - 1) <= K1_BAT_ARCH_SESJE && suma + lista[od - 1]->n <= K1_BAT_ARCH_PROBKI; od--)
        suma += lista[od - 1]->n;
    if (maks < 8 + (size_t)(n - od) * 8 + (size_t)suma)
        return 0;
    memcpy(buf, "K1B2", 4);
    k1_z16(buf + 4, (uint16_t)(n - od));
    buf[6] = buf[7] = 0;
    for (i = od; i < n; i++) {
        k1_z32(buf + off, lista[i]->start_min);
        k1_z16(buf + off + 4, lista[i]->interwal);
        k1_z16(buf + off + 6, lista[i]->n);
        memcpy(buf + off + 8, dane[i], lista[i]->n);
        off += 8 + lista[i]->n;
    }
    return off;
}

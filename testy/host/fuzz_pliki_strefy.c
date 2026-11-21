/* libFuzzer harness for the zone's files port frames (0.11.0 step 2, ADR 0052): the input is what a hostile zone sends
 * (bit 0 of the first byte: from K1 instead), given in parts whose sizes the input chooses. A frame taken must follow
 * the rules: a number in range, a name without NUL and of its length, bytes within the frame and the limit; written
 * again it reads back the same. */
#include <stdlib.h>
#include <string.h>

#include "k1_pliki_strefy.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static struct k1_ps_czytnik c, c2;
static uint8_t b[K1_PS_RAMKA_MAKS];

static void znowu(const struct k1_ps_ramka *r, int od_strefy)
{
    struct k1_ps_ramka o;
    size_t n = k1_ps_zapisz(r, od_strefy, b, sizeof b), u;
    memset(&c2, 0, sizeof c2);
    c2.od_strefy = od_strefy;
    if (n == 0 || k1_ps_czytaj(&c2, b, n, &u, &o) != 1 || u != n || o.typ != r->typ || o.nr != r->nr ||
        o.rozmiar != r->rozmiar || o.wynik != r->wynik || o.nl != r->nl || strcmp(o.nazwa, r->nazwa) != 0 || o.dl != r->dl)
        abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t od = 1, u;
    struct k1_ps_ramka r;
    if (size < 2)
        return 0;
    memset(&c, 0, sizeof c);
    c.od_strefy = !(data[0] & 1);
    while (od < size) {
        size_t po = 1 + data[od] % 97u, ile;
        od++;
        ile = size - od < po ? size - od : po;
        for (;;) {
            int w = k1_ps_czytaj(&c, data + od, ile, &u, &r);
            if (u > ile)
                abort();
            od += u, ile -= u;
            if (w < 0)
                return 0;
            if (w == 0)
                break;
            if (r.nr < 1 || r.nr > K1_PS_NR_MAKS ||
                (c.od_strefy && r.typ == K1_PS_ZGLOS && (r.nl < 1 || strlen(r.nazwa) != r.nl)) ||
                (c.od_strefy && r.typ == K1_PS_DANE && (r.dl < 1 || r.dl > K1_PS_DANE_MAKS)) || r.wynik > K1_PS_W_MAKS)
                abort();
            znowu(&r, c.od_strefy);
        }
    }
    return 0;
}

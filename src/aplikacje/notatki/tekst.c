/* K1 Notatki — the open note as text: greedy word wrap into lines, cursor movement, insert and delete
 * (UTF-8, whole characters only). */
#include <string.h>

#include "k1_teraz.h"
#include "notatki.h"

struct linia linie[MAKS_LINII];
uint32_t n_linii;

static int32_t krok(uint32_t kod)
{
    const struct k1_glif *g = k1_glif(&k1_czcionki[K1_CZ_TEKST], kod);
    if (g == NULL)
        g = k1_glif(&k1_czcionki[K1_CZ_TEKST], '?');
    return g ? g->krok : 0;
}

/* Greedy word wrap of the open note into lines of at most `szer` px. */
void uloz(int32_t szer)
{
    uint32_t i = 0, start = 0, spacja = UINT32_MAX;
    int32_t w = 0;
    n_linii = 0;
    while (i < b->dl && n_linii < MAKS_LINII - 1) {
        uint32_t kod;
        int l = k1_utf8(b->tekst + i, b->dl - i, &kod);
        if (kod == '\n') {
            linie[n_linii].od = start;
            linie[n_linii++].do_ = i;
            start = i + (uint32_t)l;
            i = start;
            w = 0;
            spacja = UINT32_MAX;
            continue;
        }
        w += krok(kod);
        if (w > szer * 64 && i > start) {
            uint32_t koniec = spacja != UINT32_MAX && spacja > start ? spacja : i;
            linie[n_linii].od = start;
            linie[n_linii++].do_ = koniec;
            start = koniec == spacja ? spacja + 1 : koniec;
            i = start;
            w = 0;
            spacja = UINT32_MAX;
            continue;
        }
        if (kod == ' ')
            spacja = i;
        i += (uint32_t)l;
    }
    linie[n_linii].od = start;
    linie[n_linii++].do_ = b->dl;
}

uint32_t linia_kursora(void)
{
    uint32_t i;
    for (i = 0; i + 1 < n_linii; i++)
        if (kursor < linie[i + 1].od)
            return i;
    return n_linii - 1;
}

int32_t x_w_linii(uint32_t ln, uint32_t pos)
{
    uint32_t i = linie[ln].od;
    int32_t x = 0;
    while (i < pos && i < linie[ln].do_) {
        uint32_t kod;
        int l = k1_utf8(b->tekst + i, b->dl - i, &kod);
        x += krok(kod);
        i += (uint32_t)l;
    }
    return (x + 32) / 64;
}

uint32_t pozycja_w_linii(uint32_t ln, int32_t x)
{
    uint32_t i = linie[ln].od;
    int32_t px = 0;
    while (i < linie[ln].do_) {
        uint32_t kod;
        int l = k1_utf8(b->tekst + i, b->dl - i, &kod);
        int32_t a = krok(kod);
        if ((px + a / 2 + 32) / 64 > x)
            break;
        px += a;
        i += (uint32_t)l;
    }
    return i;
}

uint32_t poprzedni(uint32_t p)
{
    return (uint32_t)k1_utf8_poczatek(b->tekst, p - 1);
}

uint32_t nastepny(uint32_t p)
{
    uint32_t kod;
    return p + (uint32_t)k1_utf8(b->tekst + p, b->dl - p, &kod);
}

void wstaw(const char *s, uint32_t n)
{
    if (b->dl + n > MAKS_TEKST)
        return;
    memmove(b->tekst + kursor + n, b->tekst + kursor, b->dl - kursor);
    memcpy(b->tekst + kursor, s, n);
    b->dl += n;
    kursor += n;
    b->tekst[b->dl] = '\0';
    zmiana = k1_teraz();
    pytanie = 0;
    pelno = 0;
}

void usun(uint32_t od, uint32_t do_)
{
    memmove(b->tekst + od, b->tekst + do_, b->dl - do_);
    b->dl -= do_ - od;
    b->tekst[b->dl] = '\0';
    kursor = od;
    zmiana = k1_teraz();
    pytanie = 0;
    pelno = 0;
}

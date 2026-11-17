/* K1 przybornik — the elements of a surface for the screen's description (k1_opis_klient.h; 0.10.2 step 2, ADR 0045). */
#include "k1_opis_klient.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klient.h"
#include "k1_memfd.h"
#include "k1_rysuj.h"

struct k1_el_tab {
    int n;
    uint64_t wyslany; /* the hash of the table ekran has (0: nothing sent since the description was switched on) */
    struct k1_opis_el el[K1_OPIS_MAKS];
};

static int wlaczony;
static struct k1_powierzchnia *cel;

void k1_opis_ustaw(int w)
{
    wlaczony = w;
}

int k1_opis_wlaczony(void)
{
    return wlaczony;
}

static struct k1_el_tab *tabela(struct k1_powierzchnia *p)
{
    if (p->el == NULL)
        p->el = calloc(1, sizeof *p->el);
    return p->el;
}

void k1_el_cel(struct k1_powierzchnia *p)
{
    cel = p;
}

void k1_el_od_nowa(struct k1_powierzchnia *p, const char *nazwa)
{
    struct k1_el_tab *t;
    cel = p;
    if (!wlaczony || (t = tabela(p)) == NULL)
        return;
    t->n = 0;
    if (nazwa != NULL)
        k1_el(K1_EL_POWIERZCHNIA, nazwa, 0, 0, p->szer, p->wys, 0);
}

void k1_el(enum k1_el_rodzaj rodzaj, const char *nazwa, int32_t x, int32_t y, int32_t w, int32_t h, unsigned stan)
{
    struct k1_el_tab *t;
    struct k1_opis_el e = { .rodzaj = (uint8_t)rodzaj, .stan = (uint8_t)(stan & (K1_EL_WYBRANY | K1_EL_NIEDOSTEPNY)) };
    size_t dl;
    int32_t x1, y1;
    if (!wlaczony || cel == NULL || nazwa == NULL || (t = tabela(cel)) == NULL || t->n >= K1_OPIS_MAKS)
        return;
    if (rodzaj == K1_EL_POWIERZCHNIA && t->n != 0)
        return;
    x1 = x + w > cel->szer ? cel->szer : x + w;
    y1 = y + h > cel->wys ? cel->wys : y + h;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (x1 <= x || y1 <= y)
        return;
    dl = strlen(nazwa);
    if (dl >= sizeof e.nazwa) /* cut on a character's boundary */
        dl = k1_utf8_poczatek(nazwa, sizeof e.nazwa - 1);
    memcpy(e.nazwa, nazwa, dl);
    e.x = (int16_t)x, e.y = (int16_t)y, e.w = (int16_t)(x1 - x), e.h = (int16_t)(y1 - y);
    if (!k1_opis_nazwa_dobra(e.nazwa, sizeof e.nazwa))
        return;
    for (int i = 0; i < t->n; i++) /* drawn twice (a partial redraw): the same element once */
        if (t->el[i].rodzaj == e.rodzaj && t->el[i].x == e.x && t->el[i].y == e.y && strcmp(t->el[i].nazwa, e.nazwa) == 0) {
            t->el[i] = e;
            return;
        }
    t->el[t->n++] = e;
}

static uint64_t skrot(const struct k1_el_tab *t)
{
    const uint8_t *b = (const uint8_t *)t->el;
    uint64_t h = UINT64_C(1469598103934665603) ^ (uint64_t)t->n;
    for (size_t i = 0; i < (size_t)t->n * sizeof t->el[0]; i++)
        h = (h ^ b[i]) * UINT64_C(1099511628211);
    return h | 1;
}

void k1_opis_wyslij(struct k1_klient *k, struct k1_powierzchnia *p)
{
    static uint8_t buf[sizeof(struct k1_opis_naglowek) + K1_OPIS_MAKS * sizeof(struct k1_opis_el)];
    struct k1_el_tab *t = p->el;
    struct k1_komunikat m;
    uint64_t h;
    size_t dl;
    int fd;
    if (!wlaczony || t == NULL || (h = skrot(t)) == t->wyslany)
        return;
    dl = sizeof(struct k1_opis_naglowek) + (size_t)t->n * sizeof t->el[0];
    k1_opis_naglowek(buf, (uint16_t)t->n, p->szer, p->wys);
    memcpy(buf + sizeof(struct k1_opis_naglowek), t->el, (size_t)t->n * sizeof t->el[0]);
    fd = k1_memfd_z_danymi("k1-opis", buf, dl);
    if (fd < 0)
        return; /* tried again with the next commit */
    m = k1_komunikat_nowy(K1_KOM_OPIS, p->id, t->n, 0, 0, 0, 0);
    if (k1_kanal_wyslij(k->fd, &m, sizeof m, fd) == 0)
        t->wyslany = h;
    close(fd);
}

void k1_opis_usun(struct k1_powierzchnia *p)
{
    if (cel == p)
        cel = NULL;
    free(p->el);
    p->el = NULL;
}

/* K1 — the screen's description for the tests: checking its format (k1_opis.h; 0.10.2 step 2, ADR 0045). Pure: no
 * system calls; tested on the host (test_opis) and fuzzed (fuzz_opis). */
#include "k1_opis.h"

#include <string.h>

#include "k1_komunikat.h"

int k1_opis_nazwa_dobra(const char *n, size_t maks)
{
    size_t dl = strnlen(n, maks);
    if (dl == 0 || dl >= maks)
        return 0;
    for (size_t j = dl; j < maks; j++)
        if (n[j] != 0)
            return 0;
    return k1_napis_dobry(n, dl);
}

void k1_opis_naglowek(uint8_t *b, uint16_t ile, int32_t szer, int32_t wys)
{
    struct k1_opis_naglowek n = { .wersja = K1_OPIS_WERSJA, .ile = ile, .szer = szer, .wys = wys };
    memcpy(n.magia, K1_OPIS_MAGIA, 4);
    memcpy(b, &n, sizeof n);
}

/* One record: its kind for the sender, the state bits, the rectangle inside `szer` × `wys`, the name. */
static int rekord_dobry(const struct k1_opis_el *e, enum k1_opis_od od, int32_t szer, int32_t wys, size_t i)
{
    int okno = e->rodzaj == K1_EL_OKNO || e->rodzaj == K1_EL_PANEL;
    if (e->rodzaj < K1_EL_POWIERZCHNIA || e->rodzaj > K1_EL_MAKS || (e->stan & ~K1_EL_STANY) != 0)
        return 0;
    if (od == K1_OPIS_OD_KLIENTA &&
        (okno || e->okno != 0 || (e->rodzaj == K1_EL_POWIERZCHNIA && i != 0) || (e->stan & (K1_EL_FOKUS | K1_EL_ZMIN))))
        return 0;
    if (od == K1_OPIS_OD_EKRANU && (e->rodzaj == K1_EL_POWIERZCHNIA || (!okno && (e->stan & (K1_EL_FOKUS | K1_EL_ZMIN))) ||
                                    (e->stan & K1_EL_ZMIN) || (e->rodzaj == K1_EL_PANEL && e->okno != 0)))
        return 0;
    if (e->w <= 0 || e->h <= 0 || e->x < 0 || e->y < 0 || e->x + e->w > szer || e->y + e->h > wys)
        return 0;
    return k1_opis_nazwa_dobra(e->nazwa, sizeof e->nazwa);
}

int k1_opis_sprawdz(const uint8_t *b, size_t dl, enum k1_opis_od od, int32_t szer, int32_t wys, struct k1_opis_el *el)
{
    struct k1_opis_naglowek n;
    size_t maks = od == K1_OPIS_OD_KLIENTA ? K1_OPIS_MAKS : K1_OPIS_EKRAN_MAKS;
    if (b == NULL || dl < sizeof n)
        return -1;
    memcpy(&n, b, sizeof n);
    if (memcmp(n.magia, K1_OPIS_MAGIA, 4) != 0 || n.wersja != K1_OPIS_WERSJA || n.ile > maks ||
        dl != sizeof n + (size_t)n.ile * sizeof(struct k1_opis_el) || n.szer <= 0 || n.wys <= 0 ||
        n.szer > K1_POW_MAKS_BOK || n.wys > K1_POW_MAKS_BOK || (od == K1_OPIS_OD_KLIENTA && (n.szer != szer || n.wys != wys)))
        return -1;
    for (size_t i = 0; i < n.ile; i++) {
        struct k1_opis_el e;
        memcpy(&e, b + sizeof n + i * sizeof e, sizeof e);
        if (!rekord_dobry(&e, od, n.szer, n.wys, i))
            return -1;
        el[i] = e;
    }
    return (int)n.ile;
}

/* K1 — the list of a folder on a USB stick (0.8.0 step 3). See k1_pendrive.h. */
#include "k1_pendrive.h"

#include <string.h>

#include "k1_bajty.h"

size_t k1_pen_lista_zapisz(const struct k1_pen_wpis *w, int n, uint32_t folder, int wiecej, uint8_t *buf)
{
    memcpy(buf, "K1P1", 4);
    k1_z32(buf + 4, (uint32_t)n);
    k1_z32(buf + 8, folder);
    k1_z32(buf + 12, wiecej ? 1u : 0u);
    for (int i = 0; i < n; i++) {
        uint8_t *b = buf + K1_PEN_LISTA_NAGL + (size_t)i * K1_PEN_WPIS_BAJTY;
        memset(b, 0, K1_PEN_WPIS_BAJTY);
        k1_z32(b, w[i].id);
        b[4] = w[i].rodzaj;
        b[5] = w[i].typ;
        k1_z64(b + 8, w[i].rozmiar);
        k1_z64(b + 16, (uint64_t)w[i].czas);
        memcpy(b + 24, w[i].nazwa, strnlen(w[i].nazwa, K1_DOK_NAZWA - 1));
    }
    return K1_PEN_LISTA_NAGL + (size_t)n * K1_PEN_WPIS_BAJTY;
}

static int wpis_czytaj(const uint8_t *b, uint32_t folder, struct k1_dok_wpis *w)
{
    size_t n = strnlen((const char *)b + 24, K1_DOK_NAZWA), i;
    memset(w, 0, sizeof *w);
    for (i = n; i < K1_DOK_NAZWA; i++)
        if (b[24 + i] != 0)
            return -1; /* bytes after the NUL */
    w->id = k1_c32(b);
    w->rodzaj = b[4];
    w->typ = b[5];
    w->rozmiar = k1_c64(b + 8);
    w->zmieniono = (int64_t)k1_c64(b + 16);
    w->rodzic = folder;
    w->pochodzenie = K1_POCH_PENDRIVE;
    if (w->id < 1 || w->id > K1_PEN_ID_MAKS || w->id == folder || b[6] != 0 || b[7] != 0 || w->zmieniono < 0 ||
        n >= K1_DOK_NAZWA || !k1_dok_nazwa_poprawna((const char *)b + 24, n) ||
        (w->rodzaj == K1_DOK_PLIK ? w->typ < 1 || w->typ > K1_TYPY_MAKS || w->rozmiar > 0xffffffffu
                                  : w->rodzaj != K1_DOK_FOLDER || w->typ != 0 || w->rozmiar != 0)) {
        memset(w, 0, sizeof *w);
        return -1;
    }
    memcpy(w->nazwa, b + 24, n);
    return 0;
}

int k1_pen_lista_czytaj(const uint8_t *buf, size_t dl, uint32_t folder, struct k1_dok_wpis *w, int maks, int *wiecej)
{
    uint32_t n, flagi;
    if (dl < K1_PEN_LISTA_NAGL || memcmp(buf, "K1P1", 4) != 0)
        return -1;
    n = k1_c32(buf + 4);
    flagi = k1_c32(buf + 12);
    if (n > K1_PEN_WPISOW || (int)n > maks || dl != K1_PEN_LISTA_NAGL + (size_t)n * K1_PEN_WPIS_BAJTY ||
        k1_c32(buf + 8) != folder || flagi > 1)
        return -1;
    for (int i = 0; i < (int)n; i++) {
        if (wpis_czytaj(buf + K1_PEN_LISTA_NAGL + (size_t)i * K1_PEN_WPIS_BAJTY, folder, &w[i]) != 0)
            return -1;
        for (int j = 0; j < i; j++)
            if (w[j].id == w[i].id)
                return -1;
    }
    *wiecej = (int)flagi;
    return (int)n;
}

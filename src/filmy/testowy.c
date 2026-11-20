/* K1 filmy — the test decoder (0.10.9 step 2, plan T7, S17): no card, grey frames. QEMU has no Intel card, so the tests
 * of the decoder's way (the window, the checks, the memory limit, a frame back) run with this engine; only the card is
 * missing. Chosen by nadzorca only on request of the test program (TEST_FILMY b=1 — the test image), never for a zone's
 * VMM: it decodes nothing, so it could not stand in for the card anyway. A surface is memory of its own (so that the
 * memory of the films counts as with the card: T8); a decoded picture is grey, its luma 16 + the request's first byte
 * of data mod 220 — the test sees which request made it. Its films together at most 32 MiB (the card's: K1_FILMY_PAMIEC):
 * the tests run K1 in 512 MiB and still reach the limit. */
#include <stdlib.h>
#include <string.h>

#include "filmy.h"

static uint8_t *powierzchnie[K1_FILMY_FILMOW_MAKS + 1][K1_FILMY_KLATEK_MAKS];

static int start(int32_t *bity, uint32_t *w, uint32_t *h, int *kod)
{
    *bity = (int32_t)((1u << K1_KODEKI_MAKS) - 1u);
    *w = K1_FILMY_MAKS_W;
    *h = K1_FILMY_MAKS_H;
    *kod = 0;
    return K1_FILMY_KARTA_UMIE;
}

static void zamknij(int film)
{
    for (int i = 0; i < K1_FILMY_KLATEK_MAKS; i++) {
        free(powierzchnie[film][i]);
        powierzchnie[film][i] = NULL;
    }
}

static int otworz(int film, const struct k1_filmy_film *f)
{
    size_t n = (size_t)f->w * f->h * 3 / 2;
    for (uint32_t i = 0; i < f->klatki; i++) {
        powierzchnie[film][i] = malloc(n);
        if (powierzchnie[film][i] == NULL) {
            zamknij(film);
            return K1_FILMY_BLAD_PAMIEC;
        }
        memset(powierzchnie[film][i], 0x10, n); /* touched: counted in the cgroup like the card's buffers */
    }
    return 0;
}

static int klatka(int film, const struct k1_filmy_film *f, uint32_t cel, const uint8_t *p, const struct k1_filmy_czesci *c)
{
    size_t y = (size_t)f->w * f->h;
    uint8_t *s = powierzchnie[film][cel];
    memset(s, 16 + p[c->dane] % 220, y);
    memset(s + y, 128, y / 2);
    return 0;
}

static int pobierz(int film, const struct k1_filmy_film *f, uint32_t cel, uint8_t *nv12)
{
    uint32_t rzad = k1_filmy_rzad(f->w), uv_h = (f->h + 1) / 2;
    const uint8_t *s = powierzchnie[film][cel];
    for (uint32_t r = 0; r < f->h; r++)
        memcpy(nv12 + (size_t)r * rzad, s + (size_t)r * f->w, f->w);
    for (uint32_t r = 0; r < uv_h && r < f->h / 2; r++)
        memcpy(nv12 + (size_t)rzad * f->h + (size_t)r * rzad, s + (size_t)f->w * f->h + (size_t)r * f->w, f->w);
    return 0;
}

const struct silnik silnik_testowy = { "dekoder testowy", 32ull << 20, start, otworz, klatka, pobierz, zamknij };

/* Host picture of the screen "Wyłączanie" (0.10.7 step 6): the same k1_wylaczanie ekran draws, as a PPM image.
 *   podglad_wylaczania SZER WYS CIEMNY RESTART [T_MS] > obraz.ppm
 * scripts/test-wzorce.sh compares it with testy/wzorce/wylaczanie-jasny.png / -ciemny.png (T = 0: the still frame of
 * the test, T8). Checks on the way (ASan/UBSan): drawn in pieces — as ekran draws damaged rectangles — it is the same
 * picture to the pixel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_opis_klient.h"
#include "k1_wylaczanie.h"

void k1_el(enum k1_el_rodzaj rodzaj, const char *nazwa, int32_t x, int32_t y, int32_t w, int32_t h, unsigned stan)
{
    (void)rodzaj, (void)nazwa, (void)x, (void)y, (void)w, (void)h, (void)stan; /* k1_okno.c reports its parts */
}

int main(int argc, char **argv)
{
    long w = argc > 1 ? strtol(argv[1], NULL, 10) : 1280, h = argc > 2 ? strtol(argv[2], NULL, 10) : 800;
    int ciemny = argc > 3 ? atoi(argv[3]) : 0, restart = argc > 4 ? atoi(argv[4]) : 0;
    double t = argc > 5 ? atof(argv[5]) / 1000.0 : 0.0;
    const struct k1_motyw *m = k1_motyw(ciemny);
    uint32_t *cale, *kawalki;
    int32_t kw, kh, x, y;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return 2;
    cale = calloc((size_t)w * (size_t)h, sizeof *cale);
    kawalki = calloc((size_t)w * (size_t)h, sizeof *kawalki);
    if (cale == NULL || kawalki == NULL)
        return 1;
    k1_wylaczanie(&(struct k1_obraz){ cale, (uint32_t)w, (uint32_t)h, (uint32_t)w }, 0, 0, (int32_t)w, (int32_t)h, m,
                  restart, t);
    kw = (int32_t)w / 3 + 1;
    kh = (int32_t)h / 5 + 1;
    for (y = 0; y < h; y += kh)
        for (x = 0; x < w; x += kw) {
            int32_t pw = x + kw > w ? (int32_t)w - x : kw, ph = y + kh > h ? (int32_t)h - y : kh;
            struct k1_obraz o = { kawalki + (size_t)y * (size_t)w + (size_t)x, (uint32_t)pw, (uint32_t)ph, (uint32_t)w };
            k1_wylaczanie(&o, x, y, (int32_t)w, (int32_t)h, m, restart, t);
        }
    if (memcmp(cale, kawalki, (size_t)w * (size_t)h * sizeof *cale) != 0) {
        fprintf(stderr, "podglad_wylaczania: rysowany kawałkami różni się od całego\n");
        return 1;
    }
    printf("P6\n%ld %ld\n255\n", w, h);
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
        unsigned char rgb[3] = { (unsigned char)(cale[i] >> 16), (unsigned char)(cale[i] >> 8), (unsigned char)cale[i] };
        fwrite(rgb, 1, 3, stdout);
    }
    free(cale);
    free(kawalki);
    return 0;
}

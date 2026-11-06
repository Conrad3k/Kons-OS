/* Host preview of the K1 renderer: writes the wallpaper (+ cursor) as a PPM image.
 *   podglad_grafiki SZER WYS X Y WCISNIETY > obraz.ppm
 * Used to look at the design without booting QEMU, and by the host tests (ASan/UBSan). */
#include <stdio.h>
#include <stdlib.h>

#include "k1_grafika.h"

int main(int argc, char **argv)
{
    unsigned long w = argc > 1 ? strtoul(argv[1], NULL, 10) : 1280;
    unsigned long h = argc > 2 ? strtoul(argv[2], NULL, 10) : 800;
    long cx = argc > 3 ? strtol(argv[3], NULL, 10) : -100, cy = argc > 4 ? strtol(argv[4], NULL, 10) : -100;
    int pressed = argc > 5 ? atoi(argv[5]) : 0;
    uint32_t sprite[K1_KURSOR_SZER * K1_KURSOR_WYS];
    struct k1_obraz o;
    size_t i;

    if (w == 0 || h == 0 || w > 8192 || h > 8192)
        return 2;
    o.szer = (uint32_t)w;
    o.wys = (uint32_t)h;
    o.krok = (uint32_t)w;
    o.px = calloc((size_t)w * (size_t)h, sizeof *o.px);
    if (o.px == NULL)
        return 1;
    k1_rysuj_tlo(&o);
    k1_rysuj_kursor(sprite, pressed);
    k1_naloz_sprite(&o, sprite, K1_KURSOR_SZER, K1_KURSOR_WYS, (int32_t)cx - K1_KURSOR_HOT_X,
                    (int32_t)cy - K1_KURSOR_HOT_Y);
    printf("P6\n%lu %lu\n255\n", w, h);
    for (i = 0; i < (size_t)w * (size_t)h; i++) {
        unsigned char rgb[3] = { (unsigned char)(o.px[i] >> 16), (unsigned char)(o.px[i] >> 8),
                                 (unsigned char)o.px[i] };
        fwrite(rgb, 1, 3, stdout);
    }
    free(o.px);
    return 0;
}

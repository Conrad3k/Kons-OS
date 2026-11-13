/* K1 — the measurement of Podgląd's view (0.8.5 step 2): the time of one frame of zooming and dragging for a picture of
 * 1600 × 1600 at every level of zoom — the exact filter (k1_skaluj, as up to 0.8.3 at every frame) against the fast way
 * of 0.8.5 (a smaller copy, bilinear: skala.c) — and the parts of drawing the window. Built with -O2 like the image
 * (not ASan), not part of testy-host: `make build/obj/pomiar_podgladu && build/obj/pomiar_podgladu`.
 * Performance cannot be seen in QEMU without KVM (SKILL, pitfalls): this runs on the host. The X230 (i5-3320M) is
 * roughly 2–3 × slower per core than a current laptop; the threshold of the plan: a frame ≤ 1/3 of the old one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "k1_grafika.h"
#include "k1_rysuj.h"
#include "k1_skaluj.h"
#include "skala.h"

#define BOK 1600
#define OW 958 /* OBSZAR_W, OBSZAR_H of Podgląd (podglad.h) */
#define OH 584

static uint32_t zdj[BOK * BOK], pol[(BOK / 2) * (BOK / 2)], cwierc[(BOK / 4) * (BOK / 4)], widok[OW * OH],
    okno[960 * 640];

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ms per call of `f` (the best of a few rounds of `n`) */
#define CZAS(n, f)                                                                                                     \
    ({                                                                                                                 \
        double naj = 1e9;                                                                                              \
        for (int r_ = 0; r_ < 3; r_++) {                                                                               \
            double t0_ = teraz();                                                                                      \
            for (int i_ = 0; i_ < (n); i_++)                                                                           \
                f;                                                                                                     \
            double d_ = (teraz() - t0_) * 1000.0 / (n);                                                                \
            naj = d_ < naj ? d_ : naj;                                                                                 \
        }                                                                                                              \
        naj;                                                                                                           \
    })

int main(void)
{
    static const double poziomy[] = { 1, 1.5, 2, 3, 4, 6, 8 };
    double fit = (double)OH / BOK, suma_przed = 0, suma_po = 0;
    struct sk_obraz kopie[SK_KOPIE] = { { zdj, BOK, BOK }, { pol, 0, 0 }, { cwierc, 0, 0 } };
    struct k1_obraz o = { okno, 960, 640, 960 };
    double t_polowy;
    for (uint32_t i = 0; i < BOK * BOK; i++) /* a photo-like picture: gradients and noise */
        zdj[i] = 0xff000000u | ((i % BOK * 255 / BOK) << 16) | ((i / BOK * 255 / BOK) << 8) | (uint32_t)(rand() & 0xff);
    t_polowy = CZAS(3, (sk_polowa(kopie[0], pol, &kopie[1].w, &kopie[1].h),
                        sk_polowa(kopie[1], cwierc, &kopie[2].w, &kopie[2].h)));
    printf("kopie 800² i 400² (raz na zdjęcie i obrót): %.1f ms\n", t_polowy);
    printf("poziom  widok      dokładnie (0.8.3)  szybko (0.8.5)  kopia\n");
    for (size_t p = 0; p < sizeof poziomy / sizeof poziomy[0]; p++) {
        double s = fit * poziomy[p];
        int32_t ww = (int32_t)(BOK * s + 0.5) < OW ? (int32_t)(BOK * s + 0.5) : OW;
        int32_t wh = (int32_t)(BOK * s + 0.5) < OH ? (int32_t)(BOK * s + 0.5) : OH;
        double vw = ww / s, vh = wh / s, x = (BOK - vw) / 2, y = (BOK - vh) / 2;
        int k = sk_kopia(s);
        double f = (double)kopie[k].w / BOK;
        double a = CZAS(3, k1_skaluj(zdj, BOK, BOK, BOK, (struct k1_wycinek){ x, y, vw, vh }, widok, (uint32_t)ww,
                                     (uint32_t)wh, (uint32_t)ww));
        double b = CZAS(10, sk_szybko(kopie[k], x * f, y * f, vw * f, vh * f, widok, ww, wh, ww));
        suma_przed += a;
        suma_po += b;
        printf("%4.1f×   %4dx%-4d  %8.1f ms        %8.1f ms     %s\n", poziomy[p], ww, wh, a, b,
               k == 0 ? "1600²" : k == 1 ? "800²" : "400²");
    }
    printf("średnio: dokładnie %.1f ms, szybko %.1f ms (%.1f %%)\n", suma_przed / 7, suma_po / 7, 100.0 * suma_po / suma_przed);
    printf("rysowanie okna: tło z zaokrągleniem %.2f ms, zdjęcie 958×584 mieszaniem %.2f ms, wierszami %.2f ms\n",
           CZAS(20, k1_zaokraglony(&o, 1, 44, 958, 595, 11, k1_kolor(0x1c1b19, 255))),
           CZAS(20, k1_naloz_sprite(&o, widok, OW, OH, 1, 44)),
           CZAS(20, ({ for (int r = 0; r < OH; r++) memcpy(okno + (size_t)(44 + r) * 960 + 1, widok + (size_t)r * OW, OW * 4); })));
    return 0;
}

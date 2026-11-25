/* K1 przybornik — the icons (K1_IK_*): strokes on a 24×24 grid, drawn with the signed distances of k1_rysuj.c.
 * Split from k1_rysuj.c before the speaker's levels (0.12.0 step 1: the file was at 560 lines). */
#include "k1_rysuj.h"
#include "k1_rysuj_wew.h"

#include <math.h>
#include <stddef.h>

enum { P_KRESKA = 1, P_OKRAG, P_PROST, P_KOLO, P_LUK };
struct prym {
    int typ;
    float a, b, c, d, e; /* kreska: x0 y0 x1 y1; okrag/kolo: cx cy r; prost: x y w h r; luk: cx cy r od do (deg) */
};

static const struct prym ik_zegar[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KRESKA, 12, 12, 12, 7.5f, 0 },
                                        { P_KRESKA, 12, 12, 15, 13.5f, 0 } };
static const struct prym ik_notatki[] = { { P_PROST, 5, 3, 14, 18, 2.5f }, { P_KRESKA, 8.5f, 8, 15.5f, 8, 0 },
                                          { P_KRESKA, 8.5f, 12, 15.5f, 12, 0 }, { P_KRESKA, 8.5f, 16, 12.5f, 16, 0 } };
static const struct prym ik_ustawienia[] = {
    { P_KRESKA, 4, 7, 20, 7, 0 },   { P_KRESKA, 4, 17, 20, 17, 0 }, { P_KOLO, 9, 7, 2.6f, 0, 0 },
    { P_KOLO, 15, 17, 2.6f, 0, 0 },
};
static const struct prym ik_glosnik[] = {
    { P_KRESKA, 4, 9.5f, 7.5f, 9.5f, 0 }, { P_KRESKA, 7.5f, 9.5f, 12, 5.5f, 0 }, { P_KRESKA, 12, 5.5f, 12, 18.5f, 0 },
    { P_KRESKA, 12, 18.5f, 7.5f, 14.5f, 0 }, { P_KRESKA, 7.5f, 14.5f, 4, 14.5f, 0 }, { P_KRESKA, 4, 14.5f, 4, 9.5f, 0 },
    { P_LUK, 12, 12, 4, 50, 130 },        { P_LUK, 12, 12, 7.5f, 45, 135 },
};
static const struct prym ik_zasilanie[] = { { P_LUK, 12, 13, 7, 40, 320 }, { P_KRESKA, 12, 4, 12, 11, 0 } };
static const struct prym ik_restart[] = { { P_LUK, 12, 12, 7.5f, 60, 330 },
                                          { P_KRESKA, 18.5f, 5, 18.5f, 8.6f, 0 },
                                          { P_KRESKA, 18.5f, 8.6f, 14.9f, 8.6f, 0 } };
static const struct prym ik_zamknij[] = { { P_KRESKA, 7.5f, 7.5f, 16.5f, 16.5f, 0 }, { P_KRESKA, 16.5f, 7.5f, 7.5f, 16.5f, 0 } };
static const struct prym ik_minimalizuj[] = { { P_KRESKA, 7.5f, 12, 16.5f, 12, 0 } };
/* 0.10.5 step 4 (D8): maximize — a square; restore — two squares, the back one only where the front does not cover it */
static const struct prym ik_maksymalizuj[] = { { P_PROST, 6.5f, 6.5f, 11, 11, 2 } };
static const struct prym ik_przywroc[] = { { P_PROST, 5, 9, 10, 10, 2 }, { P_KRESKA, 9, 9, 9, 7, 0 },
                                          { P_KRESKA, 10.5f, 5.5f, 17, 5.5f, 0 }, { P_KRESKA, 18.5f, 7, 18.5f, 13.5f, 0 },
                                          { P_KRESKA, 18.5f, 15, 15, 15, 0 } };
/* 0.10.9 step 6 (D7): the mark of fast graphics — a chip with its pins */
static const struct prym ik_karta[] = {
    { P_PROST, 6.5f, 6.5f, 11, 11, 2 },     { P_PROST, 9.5f, 9.5f, 5, 5, 1 },       { P_KRESKA, 9.5f, 3.5f, 9.5f, 6.5f, 0 },
    { P_KRESKA, 14.5f, 3.5f, 14.5f, 6.5f, 0 }, { P_KRESKA, 9.5f, 17.5f, 9.5f, 20.5f, 0 }, { P_KRESKA, 14.5f, 17.5f, 14.5f, 20.5f, 0 },
    { P_KRESKA, 3.5f, 9.5f, 6.5f, 9.5f, 0 },  { P_KRESKA, 3.5f, 14.5f, 6.5f, 14.5f, 0 }, { P_KRESKA, 17.5f, 9.5f, 20.5f, 9.5f, 0 },
    { P_KRESKA, 17.5f, 14.5f, 20.5f, 14.5f, 0 },
};
static const struct prym ik_klodka[] = { { P_PROST, 5.5f, 10.5f, 13, 10, 2.5f }, { P_LUK, 12, 10.5f, 4, -90, 90 },
                                         { P_KRESKA, 8, 10.5f, 8, 9, 0 }, { P_KRESKA, 16, 10.5f, 16, 9, 0 } };
static const struct prym ik_info[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KOLO, 12, 8, 1.2f, 0, 0 },
                                       { P_KRESKA, 12, 11, 12, 16.5f, 0 } };
static const struct prym ik_mysz[] = { { P_PROST, 6.5f, 3.5f, 11, 17, 5.5f }, { P_KRESKA, 12, 7, 12, 10, 0 } };
static const struct prym ik_wyglad[] = { { P_PROST, 3.5f, 5, 17, 14, 2.5f }, { P_KRESKA, 6.5f, 16, 10.5f, 11, 0 },
                                        { P_KRESKA, 10.5f, 11, 13, 14, 0 }, { P_KRESKA, 13, 14, 15, 12, 0 },
                                        { P_KRESKA, 15, 12, 17.5f, 16, 0 }, { P_KOLO, 15.5f, 8.5f, 1.3f, 0, 0 } };

/* activity: a frame with a pulse line */
static const struct prym ik_zasoby[] = { { P_PROST, 3.5f, 4.5f, 17, 15, 2.5f }, { P_KRESKA, 6.5f, 13, 9.5f, 13, 0 },
                                        { P_KRESKA, 9.5f, 13, 11.5f, 8.5f, 0 }, { P_KRESKA, 11.5f, 8.5f, 13.5f, 15.5f, 0 },
                                        { P_KRESKA, 13.5f, 15.5f, 15, 12, 0 },  { P_KRESKA, 15, 12, 17.5f, 12, 0 } };
/* display: a monitor on a stand */
static const struct prym ik_ekran[] = { { P_PROST, 3.5f, 4.5f, 17, 12, 2 }, { P_KRESKA, 12, 16.5f, 12, 19.5f, 0 },
                                       { P_KRESKA, 8, 19.5f, 16, 19.5f, 0 } };

/* battery: a body with a terminal and a half fill (0.5.5, Ustawienia → Bateria) */
static const struct prym ik_bateria[] = { { P_PROST, 3.5f, 7.5f, 15, 9, 2 }, { P_KRESKA, 20.5f, 10.5f, 20.5f, 13.5f, 0 },
                                         { P_KRESKA, 6.5f, 12, 11.5f, 12, 0 } };

/* disk: a drive body, its line and the activity light (M5f, Instalator) */
static const struct prym ik_dysk[] = { { P_PROST, 3.5f, 5.5f, 17, 13, 2.5f }, { P_KRESKA, 3.5f, 13.5f, 20.5f, 13.5f, 0 },
                                       { P_KOLO, 16.5f, 16, 1.2f, 0, 0 } };

/* 0.9.0 step 7 (D1: sketch C): a network plug (RJ45) — its body, four contacts, the latch, the cable (not without one);
 * Wi-Fi: three arcs over a dot (the list of cards in Ustawienia → Sieć) */
#define WTYCZKA                                                                                                     \
    { P_PROST, 5, 6, 14, 11, 2 }, { P_KRESKA, 8.5f, 9.5f, 8.5f, 11.5f, 0 }, { P_KRESKA, 11, 9.5f, 11, 11.5f, 0 },  \
        { P_KRESKA, 13.5f, 9.5f, 13.5f, 11.5f, 0 }, { P_KRESKA, 16, 9.5f, 16, 11.5f, 0 }, { P_PROST, 9.5f, 17, 5, 2.5f, 0.6f }
static const struct prym ik_siec[] = { WTYCZKA, { P_KRESKA, 12, 19.5f, 12, 22.5f, 0 } };
static const struct prym ik_siec_bez[] = { WTYCZKA };
static const struct prym ik_wifi[] = { { P_LUK, 12, 19, 4.5f, -45, 45 }, { P_LUK, 12, 19, 9, -45, 45 },
                                       { P_LUK, 12, 19, 13.5f, -45, 45 }, { P_KOLO, 12, 19, 1.3f, 0, 0 } };

/* 0.10.0 step 2 (D1: A): a globe — its outline, the equator and a meridian (two arcs) */
static const struct prym ik_przegladarka[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KRESKA, 3.5f, 12, 20.5f, 12, 0 },
                                               { P_LUK, 19, 12, 9, -140, -40 }, { P_LUK, 5, 12, 9, 40, 140 } };

/* 0.10.5 step 1 (D1: icon B, the arrow turned down — the owner 2026-11-17): an arrow down in a circle — a version to get */
static const struct prym ik_aktualizacje[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KRESKA, 12, 7.5f, 12, 16.5f, 0 },
                                               { P_KRESKA, 8.5f, 13, 12, 16.5f, 0 }, { P_KRESKA, 12, 16.5f, 15.5f, 13, 0 } };

static const struct prym ik_kalkulator[] = {
    { P_PROST, 5.5f, 3, 13, 18, 2.5f }, { P_KRESKA, 8.5f, 7.5f, 15.5f, 7.5f, 0 }, { P_KOLO, 9, 12, 1.2f, 0, 0 },
    { P_KOLO, 12, 12, 1.2f, 0, 0 },     { P_KOLO, 15, 12, 1.2f, 0, 0 },          { P_KOLO, 9, 15.5f, 1.2f, 0, 0 },
    { P_KOLO, 12, 15.5f, 1.2f, 0, 0 },  { P_KOLO, 15, 15.5f, 1.2f, 0, 0 },       { P_KRESKA, 9, 18.5f, 15, 18.5f, 0 },
};
/* 0.8.0 Pliki: a folder (body and tab), a page with a folded corner, a picture (as Wygląd), Kosz (empty: two
 * ribs; full: a crumpled sheet inside), a note, a camera, a download into a tray, an arrow up */
static const struct prym ik_folder[] = { { P_PROST, 3.5f, 7.5f, 17, 11.5f, 2 }, { P_KRESKA, 4.5f, 7.5f, 6.5f, 5, 0 },
                                         { P_KRESKA, 6.5f, 5, 10, 5, 0 }, { P_KRESKA, 10, 5, 11.5f, 7.5f, 0 } };
static const struct prym ik_plik[] = { { P_KRESKA, 6, 3.5f, 13.5f, 3.5f, 0 },  { P_KRESKA, 13.5f, 3.5f, 18.5f, 8.5f, 0 },
                                       { P_KRESKA, 18.5f, 8.5f, 18.5f, 20.5f, 0 }, { P_KRESKA, 18.5f, 20.5f, 6, 20.5f, 0 },
                                       { P_KRESKA, 6, 20.5f, 6, 3.5f, 0 },     { P_KRESKA, 13.5f, 3.5f, 13.5f, 8.5f, 0 },
                                       { P_KRESKA, 13.5f, 8.5f, 18.5f, 8.5f, 0 } };
static const struct prym ik_kosz[] = { { P_KRESKA, 4.5f, 6.5f, 19.5f, 6.5f, 0 }, { P_KRESKA, 9.5f, 6.5f, 10, 4, 0 },
                                       { P_KRESKA, 10, 4, 14, 4, 0 },          { P_KRESKA, 14, 4, 14.5f, 6.5f, 0 },
                                       { P_KRESKA, 6.5f, 6.5f, 7.5f, 19.5f, 0 }, { P_KRESKA, 7.5f, 19.5f, 16.5f, 19.5f, 0 },
                                       { P_KRESKA, 16.5f, 19.5f, 17.5f, 6.5f, 0 }, { P_KRESKA, 10.5f, 10, 10.5f, 16, 0 },
                                       { P_KRESKA, 13.5f, 10, 13.5f, 16, 0 } };
static const struct prym ik_kosz_pelny[] = { { P_KRESKA, 4.5f, 6.5f, 19.5f, 6.5f, 0 }, { P_KRESKA, 9.5f, 6.5f, 10, 4, 0 },
                                             { P_KRESKA, 10, 4, 14, 4, 0 },          { P_KRESKA, 14, 4, 14.5f, 6.5f, 0 },
                                             { P_KRESKA, 6.5f, 6.5f, 7.5f, 19.5f, 0 }, { P_KRESKA, 7.5f, 19.5f, 16.5f, 19.5f, 0 },
                                             { P_KRESKA, 16.5f, 19.5f, 17.5f, 6.5f, 0 }, { P_KOLO, 12, 13, 3.2f, 0, 0 } };
static const struct prym ik_muzyka[] = { { P_KOLO, 8, 17, 2.4f, 0, 0 },          { P_KOLO, 15.5f, 15, 2.4f, 0, 0 },
                                         { P_KRESKA, 10.2f, 17, 10.2f, 6, 0 },    { P_KRESKA, 17.7f, 15, 17.7f, 4, 0 },
                                         { P_KRESKA, 10.2f, 6, 17.7f, 4, 0 } };
static const struct prym ik_wideo[] = { { P_PROST, 3.5f, 6.5f, 12, 11, 2.5f }, { P_KRESKA, 15.5f, 10.5f, 20.5f, 7.5f, 0 },
                                        { P_KRESKA, 20.5f, 7.5f, 20.5f, 16.5f, 0 }, { P_KRESKA, 20.5f, 16.5f, 15.5f, 13.5f, 0 } };
static const struct prym ik_pobrane[] = { { P_KRESKA, 12, 4, 12, 14.5f, 0 },      { P_KRESKA, 7.5f, 10, 12, 14.5f, 0 },
                                          { P_KRESKA, 12, 14.5f, 16.5f, 10, 0 },  { P_KRESKA, 4.5f, 15.5f, 4.5f, 19.5f, 0 },
                                          { P_KRESKA, 4.5f, 19.5f, 19.5f, 19.5f, 0 }, { P_KRESKA, 19.5f, 19.5f, 19.5f, 15.5f, 0 } };
static const struct prym ik_w_gore[] = { { P_KRESKA, 12, 19, 12, 5.5f, 0 }, { P_KRESKA, 6.5f, 11, 12, 5.5f, 0 },
                                         { P_KRESKA, 12, 5.5f, 17.5f, 11, 0 } };

static float sd_prym(const struct prym *p, float x, float y, float g)
{
    switch (p->typ) {
    case P_KRESKA:
        return k1_rw_sd_kreska(x, y, p->a, p->b, p->c, p->d) - g / 2;
    case P_OKRAG:
        return fabsf(sqrtf((x - p->a) * (x - p->a) + (y - p->b) * (y - p->b)) - p->c) - g / 2;
    case P_KOLO:
        return sqrtf((x - p->a) * (x - p->a) + (y - p->b) * (y - p->b)) - p->c;
    case P_PROST:
        return fabsf(k1_rw_sd_box(x, y, p->a + p->c / 2, p->b + p->d / 2, p->c / 2, p->d / 2, p->e)) - g / 2;
    case P_LUK: {
        /* angle 0 = up, clockwise; the stroke between `od` and `do` (degrees) */
        float dx = x - p->a, dy = y - p->b, kat = atan2f(dx, -dy) * 57.29578f, srodek = (p->d + p->e) / 2,
              pol = (p->e - p->d) / 2, roz = kat - srodek;
        while (roz > 180)
            roz -= 360;
        while (roz < -180)
            roz += 360;
        if (fabsf(roz) <= pol)
            return fabsf(sqrtf(dx * dx + dy * dy) - p->c) - g / 2;
        {
            float k1 = (srodek + (roz > 0 ? pol : -pol)) / 57.29578f;
            return sqrtf((x - p->a - sinf(k1) * p->c) * (x - p->a - sinf(k1) * p->c) +
                         (y - p->b + cosf(k1) * p->c) * (y - p->b + cosf(k1) * p->c)) -
                   g / 2;
        }
    }
    default:
        return 1e9f;
    }
}

#define IK(t) { t, sizeof t / sizeof t[0] }
void k1_ikona(struct k1_obraz *o, enum k1_ikona ik, float x, float y, float rozmiar, uint32_t kolor)
{
    static const struct {
        const struct prym *p;
        size_t n;
    } t[] = {
        [K1_IK_ZEGAR] = IK(ik_zegar),       [K1_IK_NOTATKI] = IK(ik_notatki),
        [K1_IK_USTAWIENIA] = IK(ik_ustawienia), [K1_IK_GLOSNIK] = IK(ik_glosnik),
        [K1_IK_ZASILANIE] = IK(ik_zasilanie), [K1_IK_RESTART] = IK(ik_restart),
        [K1_IK_ZAMKNIJ] = IK(ik_zamknij),   [K1_IK_MINIMALIZUJ] = IK(ik_minimalizuj),
        [K1_IK_MAKSYMALIZUJ] = IK(ik_maksymalizuj), [K1_IK_PRZYWROC] = IK(ik_przywroc),
        [K1_IK_KLODKA] = IK(ik_klodka),     [K1_IK_INFO] = IK(ik_info),
        [K1_IK_MYSZ] = IK(ik_mysz),         [K1_IK_WYGLAD] = IK(ik_wyglad),
        [K1_IK_ZASOBY] = IK(ik_zasoby),     [K1_IK_EKRAN] = IK(ik_ekran),
        [K1_IK_BATERIA] = IK(ik_bateria),   [K1_IK_DYSK] = IK(ik_dysk),
        [K1_IK_KALKULATOR] = IK(ik_kalkulator),
        [K1_IK_FOLDER] = IK(ik_folder),     [K1_IK_PLIK] = IK(ik_plik),
        [K1_IK_OBRAZ] = IK(ik_wyglad),      [K1_IK_KOSZ] = IK(ik_kosz),
        [K1_IK_KOSZ_PELNY] = IK(ik_kosz_pelny), [K1_IK_MUZYKA] = IK(ik_muzyka),
        [K1_IK_WIDEO] = IK(ik_wideo),       [K1_IK_POBRANE] = IK(ik_pobrane),
        [K1_IK_W_GORE] = IK(ik_w_gore),     [K1_IK_SIEC] = IK(ik_siec),
        [K1_IK_SIEC_BEZ_KABLA] = IK(ik_siec_bez), [K1_IK_WIFI] = IK(ik_wifi),
        [K1_IK_PRZEGLADARKA] = IK(ik_przegladarka), [K1_IK_AKTUALIZACJE] = IK(ik_aktualizacje),
        [K1_IK_KARTA] = IK(ik_karta),
    };
    float s = rozmiar / 24.0f, g = 1.6f;
    int32_t x0, y0, x1, y1, i, j;
    size_t k;
    if ((unsigned)ik >= sizeof t / sizeof t[0] || t[ik].p == NULL ||
        !k1_rw_ramka(o, x, y, x + rozmiar, y + rozmiar, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++) {
            float ux = ((float)i + 0.5f - x) / s, uy = ((float)j + 0.5f - y) / s, d = 1e9f;
            uint32_t c;
            for (k = 0; k < t[ik].n; k++)
                d = fminf(d, sd_prym(&t[ik].p[k], ux, uy, g));
            c = pokr(d * s);
            if (c)
                row[i] = mieszaj(row[i], kolor, c);
        }
    }
}

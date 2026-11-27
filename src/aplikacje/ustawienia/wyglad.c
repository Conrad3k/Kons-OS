/* K1 Ustawienia — Wygląd (the wallpapers: their thumbnails, the photo card) and Ekran (the screen modes, the largest
 * first); the theme's segmented buttons stay in main.c. Moved out of main.c in 0.13.0 step 4 (before Informacje grew).
 * Part of Ustawienia (ustawienia.h). */
#include <stdio.h>

#include "k1_log.h"
#include "ustawienia.h"

/* screen modes (0.10.7 step 2): rows of three buttons, the largest first */
#define TRYB_W 152
#define TRYB_H 36
#define TRYB_ODST_X 12
#define TRYB_ODST_Y 8
#define TRYBY_W_RZEDZIE 3

#define KARTA_W 100
#define KARTA_H 64
#define KARTA_ODST 12
#define KARTY_W_RZEDZIE 4
#define RZAD_H (KARTA_H + 40) /* card, its name and a gap */

static int hover_karta = -1, hover_tryb = -1, tryby_zalogowane;
static uint32_t miniatury[K1_TAPETY][KARTA_W * KARTA_H];
static const char *const nazwy_tapet[] = { "Glina", "Zmierzch", "Noc", "Świt", "Las" };
_Static_assert(sizeof nazwy_tapet / sizeof nazwy_tapet[0] == K1_TAPETY, "one name per wallpaper");

static int32_t karty_y(void)
{
    return seg_y() + SEG_H + 44;
}

/* Top-left corner of thumbnail i: KARTY_W_RZEDZIE in a row, then the next row. */
static int32_t karta_x(int i)
{
    return TRESC_X + (i % KARTY_W_RZEDZIE) * (KARTA_W + KARTA_ODST);
}

static int32_t karta_y(int i)
{
    return karty_y() + (i / KARTY_W_RZEDZIE) * RZAD_H;
}

/* Wallpaper thumbnails: rendered once by the same code ekran uses. */
void wyglad_start(void)
{
    static uint32_t duzy[4 * KARTA_W * KARTA_H];
    int i, x, y;
    for (i = 0; i < K1_TAPETY; i++) {
        /* rendered at twice the size and averaged 2×2: smooth edges in a small picture */
        struct k1_obraz o = { duzy, 2 * KARTA_W, 2 * KARTA_H, 2 * KARTA_W };
        k1_rysuj_tlo(&o, i);
        for (y = 0; y < KARTA_H; y++)
            for (x = 0; x < KARTA_W; x++) {
                const uint32_t *p = duzy + (size_t)(2 * y) * (2 * KARTA_W) + (size_t)(2 * x);
                uint32_t a = p[0], b = p[1], c = p[2 * KARTA_W], d = p[2 * KARTA_W + 1], r = 0, k;
                for (k = 0; k < 24; k += 8)
                    r |= ((((a >> k) & 0xffu) + ((b >> k) & 0xffu) + ((c >> k) & 0xffu) + ((d >> k) & 0xffu) + 2u) / 4u) << k;
                miniatury[i][y * KARTA_W + x] = r;
            }
    }
}

/* The cards: the painted wallpapers, then "Zdjęcie" when ekran keeps a photo (0.8.0, set from Pliki). */
static int kart(void)
{
    return K1_TAPETY + (apl.k.ustawienia[K1_UST_ZDJECIE] ? 1 : 0);
}

static int tapeta_karty(int i)
{
    return i == K1_TAPETY ? K1_TAPETA_ZDJECIE : i;
}

/* The photo card (Ustawienia never sees the photo itself — only ekran and Pliki do): a picture icon; without a photo,
 * how to set one. */
static void karta_zdjecia(struct k1_obraz *o, const struct k1_motyw *m)
{
    int32_t x = karta_x(K1_TAPETY), y = karta_y(K1_TAPETY);
    if (!apl.k.ustawienia[K1_UST_ZDJECIE]) {
        k1_tekst(o, K1_CZ_MALA, x, y + 22, "Własne zdjęcie: w Plikach", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, x, y + 40, "wybierz je i kliknij", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, x, y + 58, "„Ustaw jako tapetę”.", k1_kolor(m->tekst3, 255), 0);
        return;
    }
    k1_zaokraglony(o, (float)x, (float)y, KARTA_W, KARTA_H, 8, k1_kolor(m->pole, 255));
    k1_ikona(o, K1_IK_OBRAZ, (float)x + KARTA_W / 2 - 14, (float)y + KARTA_H / 2 - 14, 28, k1_kolor(m->tekst2, 255));
}

void wyglad_karty(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i;
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, karty_y() - 14, "Tapeta", k1_kolor(m->tekst, 255), 0);
    karta_zdjecia(o, m);
    for (i = 0; i < kart(); i++) {
        int32_t x = karta_x(i), y = karta_y(i), px, py;
        int wybrana = apl.k.ustawienia[K1_UST_TAPETA] == tapeta_karty(i);
        for (py = 0; py < KARTA_H && i < K1_TAPETY; py++)
            for (px = 0; px < KARTA_W; px++) {
                /* rounded corners (radius 8): skip the pixels outside */
                int32_t dx = px < 8 ? 8 - px : (px >= KARTA_W - 8 ? px - (KARTA_W - 9) : 0);
                int32_t dy = py < 8 ? 8 - py : (py >= KARTA_H - 8 ? py - (KARTA_H - 9) : 0);
                if (dx * dx + dy * dy > 64)
                    continue;
                o->px[(size_t)(y + py) * o->krok + (size_t)(x + px)] = 0xff000000u | miniatury[i][py * KARTA_W + px];
            }
        if (wybrana)
            k1_obrys(o, (float)x - 3, (float)y - 3, KARTA_W + 6, KARTA_H + 6, 11, 2.0f, k1_kolor(m->akcent, 255));
        else
            k1_obrys(o, (float)x, (float)y, KARTA_W, KARTA_H, 8, 1.0f,
                     k1_kolor(m->tekst, hover_karta == i ? 70 : 24));
        k1_tekst(o, wybrana ? K1_CZ_MALA_MOCNA : K1_CZ_MALA, x, y + KARTA_H + 20, i < K1_TAPETY ? nazwy_tapet[i] : "Zdjęcie",
                 k1_kolor(wybrana ? m->tekst : m->tekst2, 255), 0);
        k1_el(K1_EL_KAFELEK, i < K1_TAPETY ? nazwy_tapet[i] : "Zdjęcie", x, y, KARTA_W, KARTA_H, wybrana ? K1_EL_WYBRANY : 0);
    }
}

static int karta_pod(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i < kart(); i++) {
        int32_t kx = karta_x(i), ky = karta_y(i);
        if (x >= kx && x < kx + KARTA_W && y >= ky && y < ky + KARTA_H)
            return i;
    }
    return -1;
}

/* Where mode nr is: its place among the modes ordered from the largest (the same size: ekran's order). */
static void tryb_pole(int nr, int32_t *x, int32_t *y)
{
    int p = 0;
    long pole = (long)apl.k.tryby[nr].szer * apl.k.tryby[nr].wys;
    for (int i = 1; i <= apl.k.n_trybow; i++) {
        long q = (long)apl.k.tryby[i].szer * apl.k.tryby[i].wys;
        p += q > pole || (q == pole && i < nr);
    }
    *x = TRESC_X + (p % TRYBY_W_RZEDZIE) * (TRYB_W + TRYB_ODST_X);
    *y = seg_y() + (p / TRYBY_W_RZEDZIE) * (TRYB_H + TRYB_ODST_Y);
}

static int tryb_pod(int32_t x, int32_t y)
{
    int nr;
    for (nr = 1; nr <= apl.k.n_trybow; nr++) {
        int32_t tx, ty;
        tryb_pole(nr, &tx, &ty);
        if (x >= tx && x < tx + TRYB_W && y >= ty && y < ty + TRYB_H)
            return nr;
    }
    return -1;
}

/* A mode's label in its button, centred: "1280 × 800", the recommended one with a small "zalecana" after it. */
static void tryb_napis(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, const char *t, int wybrany,
                       int zalecany)
{
    enum k1_cz cz = wybrany ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST;
    int32_t w = k1_tekst_szer(cz, t), wz = zalecany ? 6 + k1_tekst_szer(K1_CZ_MALA, "zalecana") : 0;
    int32_t tx = x + (TRYB_W - w - wz) / 2;
    k1_tekst(o, cz, tx, y + 23, t, k1_kolor(wybrany ? m->na_akcencie : m->tekst, 255), 0);
    if (zalecany)
        k1_tekst(o, K1_CZ_MALA, tx + w + 6, y + 23, "zalecana", k1_kolor(wybrany ? m->na_akcencie : m->tekst2,
                 wybrany ? 200 : 255), 0);
}

void ekran_tryby(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[128];
    int nr, biezacy = apl.k.ustawienia[K1_UST_ROZDZIELCZOSC];
    for (nr = 1; nr <= apl.k.n_trybow; nr++) {
        int32_t x, y;
        int wybrany = nr == biezacy;
        tryb_pole(nr, &x, &y);
        if (wybrany)
            k1_zaokraglony(o, (float)x, (float)y, TRYB_W, TRYB_H, 8, k1_kolor(m->akcent, 255));
        else
            k1_zaokraglony(o, (float)x, (float)y, TRYB_W, TRYB_H, 8, k1_kolor(hover_tryb == nr ? m->najechanie : m->pole, 255));
        if (!wybrany)
            k1_obrys(o, (float)x, (float)y, TRYB_W, TRYB_H, 8, 1.0f, k1_kolor(m->tekst, 24));
        snprintf(buf, sizeof buf, "%d × %d", (int)apl.k.tryby[nr].szer, (int)apl.k.tryby[nr].wys);
        tryb_napis(o, m, x, y, buf, wybrany, nr == apl.k.zalecany);
        k1_el(K1_EL_PRZYCISK, buf, x, y, TRYB_W, TRYB_H, wybrany ? K1_EL_WYBRANY : 0);
    }
    if (apl.k.n_trybow <= 1) {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + TRYB_H + 28, "Karta graficzna podaje tylko ten jeden tryb (obraz z firmware).",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + TRYB_H + 48, "W VirtualBoxie wybierz kartę VMSVGA, aby mieć więcej rozdzielczości.",
                 k1_kolor(m->tekst3, 255), 0);
    } else {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, "Wybór obowiązuje do wyłączenia komputera.", k1_kolor(m->tekst3, 255),
                 0);
    }
}

/* The tests click the mode buttons by this line. */
void ekran_loguj(void)
{
    char buf[400];
    size_t n = 0;
    int nr;
    if (tryby_zalogowane++)
        return;
    for (nr = 1; nr <= apl.k.n_trybow && n < sizeof buf - 16; nr++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, " %dx%d", (int)apl.k.tryby[nr].szer, (int)apl.k.tryby[nr].wys);
    buf[n] = '\0';
    k1_log(TAG, "tryby ekranu:%s (przyciski w opisie ekranu: „<szer.> × <wys.>”)", buf);
}

void wyglad_uklad(int32_t *x, int32_t *y, int32_t *krok)
{
    *x = TRESC_X + KARTA_W / 2;
    *y = karty_y() + KARTA_H / 2;
    *krok = KARTA_W + KARTA_ODST;
}

/* The pointer at (x, y) — x < 0: nowhere: the card and the mode under it; 1 = they changed (redraw). */
int wyglad_najechanie(int32_t x, int32_t y)
{
    int karta = sekcja == S_WYGLAD && x >= 0 ? karta_pod(x, y) : -1, tryb = sekcja == S_EKRAN && x >= 0 ? tryb_pod(x, y) : -1;
    if (karta == hover_karta && tryb == hover_tryb)
        return 0;
    hover_karta = karta;
    hover_tryb = tryb;
    return 1;
}

/* A click at (x, y): a mode or a wallpaper to ekran; 1 = handled. */
int wyglad_klik(int32_t x, int32_t y)
{
    int tryb = sekcja == S_EKRAN ? tryb_pod(x, y) : -1, karta = sekcja == S_WYGLAD ? karta_pod(x, y) : -1;
    if (tryb >= 0) {
        k1_log(TAG, "zmiana ustawienia %d → %d", K1_UST_ROZDZIELCZOSC, tryb);
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_ROZDZIELCZOSC, tryb, 0, 0, 0));
    } else if (karta >= 0) {
        k1_log(TAG, "zmiana ustawienia %d → %d", K1_UST_TAPETA, tapeta_karty(karta));
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_TAPETA, tapeta_karty(karta), 0, 0, 0));
    }
    return tryb >= 0 || karta >= 0;
}

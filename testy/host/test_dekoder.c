/* K1 — host test of dekoder (0.8.0 step 4, ADR 0032): pictures made by Pillow (testy/host/obrazy_dekoder.py) —
 * PNG and JPEG, progressive, palette, 16-bit, transparency, EXIF orientation — decoded, shrunk and turned; damaged
 * files, too large ones and non-pictures give an error, never a crash (ASan/UBSan). Also k1_skaluj on its own.
 * 0.8.2 step 3: GIF (the first frame), BMP, WebP lossy and lossless; photos of 48 and 100 Mpx decode, larger ones
 * and ones that do not fit in dekoder's memory are refused before any of it is taken, saying how large they are.
 * Usage: test_dekoder KATALOG */
#include <stdlib.h>
#include <string.h>

#include "k1_dekoder.h"
#include "k1_dokument.h"
#include "k1_skaluj.h"
#include "k1_test_host.h"
#include "obraz.h"

static const char *katalog;

static uint8_t *wczytaj(const char *nazwa, size_t *n)
{
    char sciezka[512];
    uint8_t *d;
    long dl;
    FILE *f;
    snprintf(sciezka, sizeof sciezka, "%s/%s", katalog, nazwa);
    f = fopen(sciezka, "rb");
    if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (dl = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "brak %s\n", sciezka);
        exit(2);
    }
    d = malloc((size_t)dl + 1);
    if (d == NULL || fread(d, 1, (size_t)dl, f) != (size_t)dl)
        exit(2);
    fclose(f);
    *n = (size_t)dl;
    return d;
}

/* Channel `c` (0 blue, 1 green, 2 red, 3 alpha) of pixel x, y. */
static int kanal(const struct obraz *o, uint32_t x, uint32_t y, int c)
{
    return (int)(o->px[(size_t)y * o->w + x] >> (8 * c)) & 0xff;
}

static int blisko(const struct obraz *o, uint32_t x, uint32_t y, int r, int g, int b, int a)
{
    int tol = 24; /* JPEG is lossy; colours along the edge between halves blur a little */
    return abs(kanal(o, x, y, 2) - r) <= tol && abs(kanal(o, x, y, 1) - g) <= tol && abs(kanal(o, x, y, 0) - b) <= tol &&
           abs(kanal(o, x, y, 3) - a) <= tol;
}

static int przygotuj(const char *nazwa, uint32_t mw, uint32_t mh, struct obraz *o)
{
    size_t n;
    uint8_t *d = wczytaj(nazwa, &n);
    int r = obraz_przygotuj(d, n, mw, mh, o);
    free(d);
    return r;
}

static void t_polowy(const char *nazwa)
{
    struct obraz o;
    EXPECT(przygotuj(nazwa, K1_DEK_MAKS_W, K1_DEK_MAKS_H, &o) == 0);
    EXPECT(o.w == 64 && o.h == 48); /* never enlarged */
    EXPECT(o.px != NULL && blisko(&o, 8, 24, 255, 0, 0, 255) && blisko(&o, 56, 24, 0, 0, 255, 255));
    free(o.px);
    EXPECT(przygotuj(nazwa, 16, 16, &o) == 0); /* the proportions stay: 16 × 12 */
    EXPECT(o.w == 16 && o.h == 12 && blisko(&o, 2, 6, 255, 0, 0, 255) && blisko(&o, 13, 6, 0, 0, 255, 255));
    EXPECT(o.zw == 64 && o.zh == 48); /* the photo's own size (0.8.2 step 4: Podgląd shows it) */
    free(o.px);
}

static void t_rodzaje(void)
{
    struct obraz o;
    EXPECT(przygotuj("alfa.png", 100, 100, &o) == 0 && o.w == 32);
    EXPECT(blisko(&o, 4, 16, 0, 0, 0, 0) && blisko(&o, 28, 16, 0, 255, 0, 255)); /* premultiplied: clear = 0 */
    free(o.px);
    EXPECT(przygotuj("paleta.png", 100, 100, &o) == 0 && blisko(&o, 8, 24, 255, 0, 0, 255));
    free(o.px);
    EXPECT(przygotuj("szary16.png", 100, 100, &o) == 0 && o.w == 16 && blisko(&o, 8, 8, 128, 128, 128, 255));
    free(o.px);
    EXPECT(przygotuj("szary.jpg", 100, 100, &o) == 0 && blisko(&o, 8, 8, 128, 128, 128, 255));
    free(o.px);
}

/* 60 × 40 with the red corner at the top left, as stored; turned by EXIF orientation 3, 6, 8. */
static void t_obrot(void)
{
    struct obraz o;
    size_t n;
    uint8_t *d = wczytaj("obrot6.jpg", &n);
    EXPECT(obraz_orientacja(d, n) == 6);
    free(d);
    EXPECT(przygotuj("obrot6.jpg", 200, 200, &o) == 0 && o.w == 40 && o.h == 60); /* turned right: the corner is top right */
    EXPECT(blisko(&o, 35, 5, 255, 0, 0, 255) && blisko(&o, 5, 5, 255, 255, 255, 255));
    free(o.px);
    EXPECT(przygotuj("obrot8.jpg", 200, 200, &o) == 0 && o.w == 40 && o.h == 60); /* turned left: bottom left */
    EXPECT(blisko(&o, 5, 55, 255, 0, 0, 255) && blisko(&o, 5, 5, 255, 255, 255, 255));
    free(o.px);
    EXPECT(przygotuj("obrot3.jpg", 200, 200, &o) == 0 && o.w == 60 && o.h == 40); /* upside down: bottom right */
    EXPECT(blisko(&o, 55, 35, 255, 0, 0, 255) && blisko(&o, 5, 5, 255, 255, 255, 255));
    free(o.px);
    EXPECT(przygotuj("obrot6.jpg", 20, 20, &o) == 0 && o.w == 13 && o.h == 20); /* shrunk after turning */
    EXPECT(o.zw == 40 && o.zh == 60); /* the photo's size as seen: turned too */
    free(o.px);
    d = wczytaj("kolory.jpg", &n);
    EXPECT(obraz_orientacja(d, n) == 1);
    free(d);
}

/* 0.8.2 step 3: the formats Wuffs reads besides PNG and JPEG — each recognised as skarbiec does (k1_dok_typ). */
static void t_formaty(void)
{
    static const struct {
        const char *nazwa;
        int typ;
    } pliki[] = { { "kolory.gif", K1_TYP_GIF },  { "kolory.bmp", K1_TYP_BMP },           { "paleta.bmp", K1_TYP_BMP },
                  { "kolory.webp", K1_TYP_WEBP }, { "kolory-stratny.webp", K1_TYP_WEBP } };
    struct obraz o;
    for (size_t i = 0; i < sizeof pliki / sizeof pliki[0]; i++) {
        size_t n;
        uint8_t *d = wczytaj(pliki[i].nazwa, &n);
        EXPECT(k1_dok_typ(d, n, n) == pliki[i].typ && obraz_rodzaj(d, n) == pliki[i].typ);
        EXPECT(k1_typ_rodzina(pliki[i].typ) == K1_RODZINA_OBRAZY && obraz_czytelny(pliki[i].typ));
        free(d);
        t_polowy(pliki[i].nazwa);
    }
    EXPECT(przygotuj("animacja.gif", 100, 100, &o) == 0 && o.w == 64 && blisko(&o, 50, 40, 255, 0, 0, 255)); /* frame 1 */
    free(o.px);
    EXPECT(przygotuj("alfa-stratny.webp", 100, 100, &o) == 0 && o.w == 32);
    EXPECT(blisko(&o, 4, 16, 0, 0, 0, 0) && blisko(&o, 28, 16, 0, 255, 0, 255));
    free(o.px);
    /* every type of family "obrazy" has a decoder, and no other one does */
    for (int t = 1; t <= K1_TYPY_MAKS; t++)
        EXPECT(obraz_czytelny(t) == (k1_typ_rodzina(t) == K1_RODZINA_OBRAZY));
    EXPECT(k1_dok_typ((const uint8_t *)"BM to tylko tekst", 17, 17) == K1_TYP_TEKST);
    EXPECT(k1_dok_typ((const uint8_t *)"RIFF\0\0\0\0WAVE", 12, 12) == K1_TYP_INNY);
}

/* Photos of 48 Mpx (8000 × 6000) and 100 Mpx (10000 × 10000): plain and progressive, shrunk to the wallpaper. */
static void t_duze(void)
{
    static const char *const pliki[] = { "48mpx.jpg", "48mpx-prog.jpg", "100mpx.jpg", "100mpx-prog.jpg" };
    struct obraz o;
    for (size_t i = 0; i < sizeof pliki / sizeof pliki[0]; i++) {
        int r = przygotuj(pliki[i], K1_DEK_MAKS_W, K1_DEK_MAKS_H, &o);
        EXPECT(r == 0 && o.h == K1_DEK_MAKS_H && (o.w == K1_DEK_MAKS_H || o.w == 2133));
        if (r == 0) /* red on the left, blue on the right, the gradient at the bottom */
            EXPECT(blisko(&o, 100, 100, 255, 0, 0, 255) && blisko(&o, o.w - 100, 100, 0, 0, 255, 255));
        free(o.px);
    }
}

static void t_zle(void)
{
    struct obraz o;
    EXPECT(przygotuj("duzy.png", 100, 100, &o) == K1_DEK_BLAD_DUZY && o.px == NULL);
    EXPECT(o.w == 30000 && o.h == 30000); /* dekoder says how large */
    EXPECT(przygotuj("101mpx.jpg", 100, 100, &o) == K1_DEK_BLAD_DUZY && o.px == NULL && o.w == 10050 && o.h == 10000);
    /* 100 Mpx is allowed, but 4 progressive channels (CMYK) need 0.8 GB of work area besides 0.4 GB of pixels */
    EXPECT(przygotuj("cmyk-prog.jpg", 100, 100, &o) == K1_DEK_BLAD_PAMIEC && o.px == NULL && o.w == 10000);
    EXPECT(przygotuj("uciety.jpg", 100, 100, &o) == K1_DEK_BLAD_USZKODZONY && o.px == NULL);
    EXPECT(przygotuj("zla-suma.png", 100, 100, &o) == K1_DEK_BLAD_USZKODZONY && o.px == NULL);
    EXPECT(przygotuj("tekst.txt", 100, 100, &o) == K1_DEK_BLAD_FORMAT && o.px == NULL);
    EXPECT(obraz_przygotuj((const uint8_t *)"", 0, 10, 10, &o) == K1_DEK_BLAD_FORMAT);
    {
        /* EXIF that points outside itself, and one cut off right after the marker */
        static const uint8_t z1[] = { 0xff, 0xd8, 0xff, 0xe1, 0x00, 0x16, 'E', 'x', 'i', 'f', 0, 0, 'I', 'I', 42, 0,
                                      0xff, 0xff, 0xff, 0x7f, 1, 0, 0x12, 0x01, 3, 0 };
        static const uint8_t z2[] = { 0xff, 0xd8, 0xff, 0xe1, 0xff, 0xff, 'E', 'x' };
        EXPECT(obraz_orientacja(z1, sizeof z1) == 1 && obraz_orientacja(z2, sizeof z2) == 1);
    }
}

static void t_skaluj(void)
{
    uint32_t z[16], c[4], d[9];
    uint32_t w, h;
    for (int i = 0; i < 16; i++) /* 4 × 4: the left half 0x40, the right half 0xC0 in every channel */
        z[i] = (i % 4) < 2 ? 0x40404040u : 0xc0c0c0c0u;
    EXPECT(k1_skaluj(z, 4, 4, 4, (struct k1_wycinek){ 0, 0, 4, 4 }, c, 2, 2, 2) == 0);
    EXPECT(c[0] == 0x40404040u && c[1] == 0xc0c0c0c0u && c[2] == 0x40404040u && c[3] == 0xc0c0c0c0u);
    EXPECT(k1_skaluj(z, 4, 4, 4, (struct k1_wycinek){ 0, 0, 4, 4 }, c, 1, 1, 1) == 0 && c[0] == 0x80808080u);
    EXPECT(k1_skaluj(z, 4, 4, 4, (struct k1_wycinek){ 1, 1, 2, 2 }, d, 3, 3, 3) == 0); /* enlarged: stays in range */
    EXPECT(d[0] == 0x40404040u && d[8] == 0xc0c0c0c0u);
    EXPECT(k1_skaluj(z, 4, 4, 4, (struct k1_wycinek){ 3, 0, 2, 4 }, c, 1, 1, 1) == -1); /* outside the source */
    EXPECT(k1_skaluj(z, 4, 4, 3, (struct k1_wycinek){ 0, 0, 4, 4 }, c, 1, 1, 1) == -1); /* row shorter than the width */
    k1_skaluj_zmiesc(4000, 3000, 2560, 1600, &w, &h);
    EXPECT(w == 2133 && h == 1600);
    k1_skaluj_zmiesc(100, 50, 2560, 1600, &w, &h);
    EXPECT(w == 100 && h == 50);
    k1_skaluj_zmiesc(10000, 1, 200, 150, &w, &h);
    EXPECT(w == 200 && h == 1);
    {
        struct k1_wycinek r = k1_skaluj_wypelnij(2133, 1600, 1366, 768); /* a 4:3 photo on a 16:9 screen */
        EXPECT(r.x == 0 && r.w == 2133 && r.h > 1199 && r.h < 1200.1 && r.y > 200 && r.y < 200.5);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    katalog = argv[1];
    t_polowy("kolory.png");
    t_polowy("kolory.jpg");
    t_polowy("progresywny.jpg");
    t_rodzaje();
    t_obrot();
    t_formaty();
    if (getenv("K1_DUZE_ZDJECIA") == NULL || strcmp(getenv("K1_DUZE_ZDJECIA"), "1") == 0)
        t_duze();
    t_zle();
    t_skaluj();
    return K1_TEST_KONIEC("test_dekoder");
}

/* Host unit tests for the GUI toolkit (src/przybornik) and the renderer: Polish local time with the
 * DST switches, keyboard layout, UTF-8, text and shapes at the image borders, wallpaper variants
 * (determinism, tiny sizes), window frame hit testing, composition clipping. ASan/UBSan build. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_czas.h"
#include "k1_klawiatura.h"
#include "k1_komunikat.h"
#include "k1_okno.h"
#include "k1_rysuj.h"
#include "k1_test_host.h"
#include "k1_wachlarz.h"

static int czas(int64_t utc, int r, int m, int d, int g, int mi, int s, int dt, int letni)
{
    struct k1_data x;
    k1_czas_warszawa(utc, &x);
    return x.rok == r && x.miesiac == m && x.dzien == d && x.godzina == g && x.minuta == mi && x.sekunda == s &&
           x.dzien_tygodnia == dt && x.letni == letni;
}

static void t_czas(void)
{
    EXPECT(czas(1774745999, 2026, 3, 29, 1, 59, 59, 0, 0)); /* last second of CET */
    EXPECT(czas(1774746000, 2026, 3, 29, 3, 0, 0, 0, 1));   /* CEST starts: 02:00 → 03:00 */
    EXPECT(czas(1792889999, 2026, 10, 25, 2, 59, 59, 0, 1));
    EXPECT(czas(1792890000, 2026, 10, 25, 2, 0, 0, 0, 0));  /* back to CET */
    EXPECT(czas(1793989800, 2026, 11, 6, 19, 30, 0, 5, 0)); /* Friday */
    EXPECT(czas(1709249400, 2024, 3, 1, 0, 30, 0, 5, 0));   /* leap day + 1 h → 1 March */
    EXPECT(czas(-3600, 1970, 1, 1, 0, 0, 0, 4, 0));         /* before the epoch */
    EXPECT(czas(0, 1970, 1, 1, 1, 0, 0, 4, 0));
    EXPECT(strcmp(k1_dzien_tygodnia(5), "piątek") == 0 && strcmp(k1_dzien_tygodnia(7), "?") == 0);
    EXPECT(strcmp(k1_miesiac_dopelniacz(11), "listopada") == 0 && strcmp(k1_miesiac_skrot(0), "?") == 0);
    EXPECT(strcmp(k1_dzien_skrot(-1), "?") == 0);
}

static void t_klawiatura(void)
{
    EXPECT(k1_znak_klawisza(KEY_A, 0) == 'a');
    EXPECT(k1_znak_klawisza(KEY_A, K1_MOD_SHIFT) == 'A');
    EXPECT(k1_znak_klawisza(KEY_A, K1_MOD_ALTGR) == 0x105);
    EXPECT(k1_znak_klawisza(KEY_Z, K1_MOD_ALTGR | K1_MOD_SHIFT) == 0x17b);
    EXPECT(k1_znak_klawisza(KEY_X, K1_MOD_ALTGR) == 0x17a);
    EXPECT(k1_znak_klawisza(KEY_O, K1_MOD_ALTGR) == 0xf3);
    EXPECT(k1_znak_klawisza(KEY_B, K1_MOD_ALTGR) == 0);
    EXPECT(k1_znak_klawisza(KEY_1, K1_MOD_SHIFT) == '!');
    EXPECT(k1_znak_klawisza(KEY_SLASH, 0) == '/');
    EXPECT(k1_znak_klawisza(KEY_SPACE, 0) == ' ');
    EXPECT(k1_znak_klawisza(KEY_A, K1_MOD_CTRL) == 0);
    EXPECT(k1_znak_klawisza(KEY_ENTER, 0) == 0 && k1_znak_klawisza(KEY_LEFTSHIFT, 0) == 0 &&
           k1_znak_klawisza(KEY_F1, 0) == 0 && k1_znak_klawisza(0x2ff, 0) == 0);
}

static void t_utf8(void)
{
    uint32_t k;
    char b[4];
    EXPECT(k1_utf8("\xc5\xbc", 2, &k) == 2 && k == 0x17c);
    EXPECT(k1_utf8("\xe2\x80\xa6", 3, &k) == 3 && k == 0x2026);
    EXPECT(k1_utf8_poczatek("a\xc5\xbc", 2) == 1 && k1_utf8_poczatek("a\xc5\xbc", 1) == 1);
    EXPECT(k1_utf8_poczatek("\xe2\x80\xa6", 2) == 0 && k1_utf8_poczatek("\x80\x80", 1) == 0);
    {
        char t[8] = "z\xc3\xb3";
        size_t dl = 3;
        EXPECT(k1_pole_klawisz(t, &dl, 7, KEY_BACKSPACE, 0) == 1 && dl == 1 && strcmp(t, "z") == 0); /* ó: both bytes */
        EXPECT(k1_pole_klawisz(t, &dl, 7, KEY_BACKSPACE, 0) == 1 && dl == 0 && t[0] == '\0');
        EXPECT(k1_pole_klawisz(t, &dl, 7, KEY_BACKSPACE, 0) == 1 && dl == 0); /* empty: stays empty */
    }
    EXPECT(k1_utf8("\xff", 1, &k) == 1 && k == 0xfffd);
    EXPECT(k1_utf8("\xc0\x80", 2, &k) == 1 && k == 0xfffd);     /* overlong */
    EXPECT(k1_utf8("\xed\xa0\x80", 3, &k) == 1 && k == 0xfffd); /* surrogate */
    EXPECT(k1_utf8("\xe2\x82", 2, &k) == 1 && k == 0xfffd);     /* truncated */
    EXPECT(k1_utf8("", 0, &k) == 1 && k == 0xfffd);
    EXPECT(k1_utf8_zapisz(0x105, b, 4) == 2 && memcmp(b, "\xc4\x85", 2) == 0);
    EXPECT(k1_utf8_zapisz(0x105, b, 1) == 0);
    EXPECT(k1_utf8_zapisz(0xd800, b, 4) == 0);
}

static void t_rysowanie(void)
{
    static uint32_t px[64 * 32], a[64 * 32];
    struct k1_obraz o = { px, 64, 32, 64 }, jeden = { px, 1, 1, 1 };
    int v;
    EXPECT(k1_tekst_szer(K1_CZ_TEKST, "") == 0);
    EXPECT(k1_tekst_szer(K1_CZ_TEKST, "Zażółć gęślą jaźń") > 100);
    EXPECT(k1_tekst_szer(K1_CZ_ZEGAR, "12:34") > 100);
    EXPECT(k1_tekst(&o, K1_CZ_TEKST, -30, 5, "Długi tekst, który nie mieści się", 0xffffffff, 50) <= 50);
    k1_tekst(&o, K1_CZ_TYTUL, 60, 40, "\xff\xfe zły UTF-8", 0xffffffff, 0);
    k1_zaokraglony(&o, -10, -10, 200, 100, 12, k1_kolor(0x123456, 128));
    k1_obrys(&o, 50, 20, 30, 30, 40, 1, 0xff000000u);
    k1_kreska(&o, -100, -100, 200, 200, 3, 0xffffffff);
    k1_cien(&o, 10, 10, 40, 10, 8, 4, 16, 60);
    k1_cien(&o, 10, 10, 0, 10, 8, 4, 16, 60);
    for (v = 1; v <= K1_IK_EKRAN; v++)
        k1_ikona(&o, (enum k1_ikona)v, 50, 20, 24, 0xffffffff);
    k1_ikona(&o, (enum k1_ikona)99, 0, 0, 24, 0xffffffff);
    k1_rysuj_znak(&o, 60, 30, 26);
    k1_wyczysc(&jeden, 0);
    k1_tekst(&jeden, K1_CZ_MALA, 0, 0, "Ą", 0xffffffff, 0);
    EXPECT(k1_kolor(0xffffff, 0) == 0 && k1_kolor(0xff8000, 255) == 0xffff8000u && (k1_kolor(0xffffff, 300) >> 24) == 255);
    /* wallpapers: every variant, odd and tiny sizes, deterministic */
    for (v = -1; v <= K1_TAPETY; v++) {
        struct k1_obraz t = { a, 13, 7, 13 }, t1 = { px, 13, 7, 13 };
        k1_rysuj_tlo(&t, v);
        k1_rysuj_tlo(&t1, v);
        EXPECT(memcmp(a, px, 13 * 7 * sizeof(uint32_t)) == 0);
    }
    {
        struct k1_obraz t = { a, 1, 1, 1 };
        k1_rysuj_tlo(&t, 0);
    }
    /* composition clipping: a source partly outside the destination */
    {
        struct k1_obraz d = { px, 64, 32, 64 }, s = { a, 13, 7, 13 };
        k1_skladaj(&d, &s, -5, -3, 0, 0, 64, 32);
        k1_skladaj(&d, &s, 60, 30, 0, 0, 64, 32);
        k1_skladaj(&d, &s, 10, 10, 12, 12, 2, 2);
        k1_skladaj(&d, &s, 1000, 1000, 0, 0, 64, 32);
    }
}

/* 0.8.5 step 2: rounded boxes skip the distance inside (k1_rysuj.c, wnetrze) — the picture must stay the same to the bit.
 * The reference: the per-pixel way of 0.8.3, copied here. */
static uint32_t ref_mieszaj(uint32_t dst, uint32_t src, uint32_t cov)
{
    uint32_t sa = ((src >> 24) * cov) >> 8, ia = 255 - sa;
    uint32_t a = sa + (((dst >> 24) * ia + 127) / 255);
    uint32_t r = ((((src >> 16) & 0xff) * cov) >> 8) + ((((dst >> 16) & 0xff) * ia + 127) / 255);
    uint32_t g = ((((src >> 8) & 0xff) * cov) >> 8) + ((((dst >> 8) & 0xff) * ia + 127) / 255);
    uint32_t b = (((src & 0xff) * cov) >> 8) + (((dst & 0xff) * ia + 127) / 255);
    return ((a > 255 ? 255 : a) << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}

static void ref_box(uint32_t *px, int32_t W, int32_t H, float x, float y, float w, float h, float r, float g, uint32_t kolor)
{
    float cx = x + w / 2, cy = y + h / 2;
    r = r < 0 ? 0 : (r > fminf(w, h) / 2 ? fminf(w, h) / 2 : r);
    for (int32_t j = 0; j < H; j++)
        for (int32_t i = 0; i < W; i++) {
            float qx = fabsf((float)i + 0.5f - cx) - w / 2 + r, qy = fabsf((float)j + 0.5f - cy) - h / 2 + r;
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r, v;
            if (i < (int32_t)floorf(x - 1) || j < (int32_t)floorf(y - 1) || i >= (int32_t)ceilf(x + w + 1) ||
                j >= (int32_t)ceilf(y + h + 1))
                continue;
            if (g > 0)
                d = fabsf(d + g / 2) - g / 2;
            v = 0.5f - d;
            v = v < 0 ? 0 : (v > 1 ? 1 : v);
            if ((uint32_t)(v * 256.0f))
                px[j * W + i] = ref_mieszaj(px[j * W + i], kolor, (uint32_t)(v * 256.0f));
        }
}

static void t_zaokraglone(void)
{
    static uint32_t a[97 * 61], b[97 * 61];
    struct k1_obraz o = { a, 97, 61, 97 };
    int zle = 0;
    srand(7);
    for (int n = 0; n < 400; n++) {
        float x = (float)(rand() % 1200) / 10.0f - 20, y = (float)(rand() % 800) / 10.0f - 10;
        float w = (float)(rand() % 1100) / 10.0f, h = (float)(rand() % 700) / 10.0f, r = (float)(rand() % 300) / 10.0f;
        float g = n % 3 == 0 ? (float)(rand() % 30) / 10.0f + 0.5f : 0;
        uint32_t kolor = k1_kolor((uint32_t)rand() & 0xffffffu, n % 2 ? 255u : (uint32_t)(rand() % 256));
        for (int i = 0; i < 97 * 61; i++)
            a[i] = b[i] = 0xff000000u | ((uint32_t)i * 2654435761u >> 8);
        if (g > 0)
            k1_obrys(&o, x, y, w, h, r, g, kolor);
        else
            k1_zaokraglony(&o, x, y, w, h, r, kolor);
        ref_box(b, 97, 61, x, y, w, h, r, g, kolor);
        zle += memcmp(a, b, sizeof a) != 0;
    }
    EXPECT(zle == 0);
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    k1_kolo(&o, 48, 30, 20, 0xff102030u); /* a circle: a box with r = half the side */
    ref_box(b, 97, 61, 28, 10, 40, 40, 20, 0, 0xff102030u);
    EXPECT(memcmp(a, b, sizeof a) == 0);
}

static void t_okno(void)
{
    const char *const et[] = { "A", "B", "C" };
    int32_t r = 0;
    int i, n = 0;
    /* scrolling (0.10.0 step 4): a notch is 3 rows; a touchpad's small parts add up; a turn drops what waited */
    EXPECT(k1_przewin_wiersze(&r, K1_PRZEWIN_ZABEK) == K1_WIERSZE_NA_ZABEK && r == 0);
    EXPECT(k1_przewin_wiersze(&r, -2 * K1_PRZEWIN_ZABEK) == -2 * K1_WIERSZE_NA_ZABEK);
    for (i = 0; i < 45; i++)
        n += k1_przewin_wiersze(&r, 10); /* 450 parts of a notch: 11 rows and a part */
    EXPECT(n == 11 && r > 0);
    EXPECT(k1_przewin_wiersze(&r, -10) == 0 && r < 0); /* the turn: the 30 that waited are gone */
    EXPECT(k1_przewin_wiersze(&r, K1_PRZEWIN_MAKS) == 64 * K1_WIERSZE_NA_ZABEK);
    EXPECT(k1_trafienie_ramki(400, 300, 10, 10) == K1_TR_TYTUL);
    EXPECT(k1_trafienie_ramki(400, 300, 10, K1_OKNO_TYTUL) == K1_TR_TRESC);
    EXPECT(k1_trafienie_ramki(400, 300, 400 - 8 - 14, 22) == K1_TR_ZAMKNIJ);
    EXPECT(k1_trafienie_ramki(400, 300, 400 - 8 - 28 - 4 - 14, 22) == K1_TR_MINIMALIZUJ);
    EXPECT(k1_trafienie_ramki(400, 300, -1, 10) == K1_TR_BRAK && k1_trafienie_ramki(400, 300, 10, 300) == K1_TR_BRAK);
    EXPECT(k1_segment_trafienie(0, 0, 308, 36, 3, 5, 10) == 0);
    EXPECT(k1_segment_trafienie(0, 0, 308, 36, 3, 303, 10) == 2);
    EXPECT(k1_segment_trafienie(0, 0, 308, 36, 3, 150, 40) == -1);
    EXPECT(k1_segment_trafienie(0, 0, 308, 36, 0, 150, 10) == -1);
    EXPECT(k1_motyw(0) != k1_motyw(1) && k1_motyw(7) == k1_motyw(1));
    {
        static uint32_t px[200 * 100];
        struct k1_obraz o = { px, 200, 100, 200 };
        k1_ramka_okna(&o, k1_motyw(0), "Bardzo długi tytuł okna, który się nie zmieści", 1, K1_TR_ZAMKNIJ);
        k1_segment(&o, k1_motyw(1), 0, 50, 200, 36, et, 3, 1, 2);
        k1_przelacznik(&o, k1_motyw(0), 190, 90, 1, 0);
        EXPECT((px[0] >> 24) == 0 && (px[100 * 50] >> 24) == 255); /* rounded corner transparent, middle opaque */
    }
}

/* 0.9.1: the Wi-Fi fan — more bars cover more of it, nothing outside its square, faint part under the bars. */
static void t_wachlarz(void)
{
    static uint32_t px[32 * 32];
    struct k1_obraz o = { px, 32, 32, 32 };
    int poziom, pop = -1, i;
    for (poziom = 0; poziom <= 4; poziom++) {
        int jasne = 0;
        memset(px, 0, sizeof px);
        k1_wachlarz(&o, 8, 8, 16, poziom, 0xffffff, 0, 255);
        for (i = 0; i < 32 * 32; i++) {
            jasne += (px[i] >> 24) > 128;
            if ((px[i] >> 24) != 0 && (i % 32 < 7 || i % 32 > 25 || i / 32 < 7 || i / 32 > 23))
                jasne = -1000; /* outside the square of the icon */
        }
        EXPECT(poziom == 0 ? jasne == 0 : jasne > pop);
        pop = jasne;
    }
    memset(px, 0, sizeof px);
    k1_wachlarz(&o, 8, 8, 16, 0, 0xffffff, 60, 255);
    EXPECT((px[16 * 32 + 16] >> 24) > 50 && (px[16 * 32 + 16] >> 24) < 70); /* the faint fan in the middle */
    k1_wachlarz(&o, -40, -40, 16, 4, 0xffffff, 60, 255);                  /* off the image: no harm (ASan) */
    k1_wachlarz(&o, 30, 30, 64, 4, 0xffffff, 60, 255);
}

int main(void)
{
    t_wachlarz();
    t_czas();
    t_klawiatura();
    t_utf8();
    t_rysowanie();
    t_zaokraglone();
    t_okno();
    return K1_TEST_KONIEC("test_przybornik");
}

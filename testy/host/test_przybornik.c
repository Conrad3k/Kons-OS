/* Host unit tests for the GUI toolkit (src/przybornik) and the renderer: Polish local time with the
 * DST switches, keyboard layout, UTF-8, text and shapes at the image borders, wallpaper variants
 * (determinism, tiny sizes), window frame hit testing, composition clipping. ASan/UBSan build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_czas.h"
#include "k1_klawiatura.h"
#include "k1_komunikat.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

static int failures, count;

#define EXPECT(cond)                                                        \
    do {                                                                    \
        count++;                                                            \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

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
    k1_cien(&o, 10, 10, 40, 10, 8, 16, 60);
    for (v = 1; v <= K1_IK_WYGLAD; v++)
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

static void t_okno(void)
{
    const char *const et[] = { "A", "B", "C" };
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

int main(void)
{
    t_czas();
    t_klawiatura();
    t_utf8();
    t_rysowanie();
    t_okno();
    if (failures) {
        fprintf(stderr, "test_przybornik: %d z %d BŁĘDNYCH\n", failures, count);
        return 1;
    }
    printf("test_przybornik: OK (%d przypadków)\n", count);
    return 0;
}

/* Sketches of the boot password screen (M5e) for the owner to choose from, drawn on the host with
 * the same code K1 uses (wallpapers, K1 mark, Plex fonts, toolkit shapes). Not part of the image.
 *     szkice_logowania WARIANT [SZER WYS] > plik.ppm      (WARIANT: 0 = Karta, 1 = Zegar, 2 = Papier) */
#include <stdio.h>
#include <stdlib.h>

#include "k1_grafika.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

#define KREM 0xf3ede2u
#define CIEMNY 0x0e0c0bu

static void kropki(struct k1_obraz *o, float x, float cy, int n, uint32_t kolor)
{
    int i;
    for (i = 0; i < n; i++)
        k1_kolo(o, x + (float)i * 16, cy, 4.2f, kolor);
}

/* A round button with an arrow → */
static void strzalka(struct k1_obraz *o, float cx, float cy, float r, uint32_t tlo, uint32_t kolor)
{
    k1_kolo(o, cx, cy, r, tlo);
    k1_kreska(o, cx - 7, cy, cx + 6, cy, 2.0f, kolor);
    k1_kreska(o, cx + 1, cy - 5, cx + 6.5f, cy, 2.0f, kolor);
    k1_kreska(o, cx + 1, cy + 5, cx + 6.5f, cy, 2.0f, kolor);
}

static void srodek(struct k1_obraz *o, enum k1_cz cz, int32_t cx, int32_t y, const char *t, uint32_t kolor)
{
    k1_tekst(o, cz, cx - k1_tekst_szer(cz, t) / 2, y, t, kolor, 0);
}

static void dolne_ikony(struct k1_obraz *o, uint32_t kolor)
{
    int32_t W = (int32_t)o->szer, H = (int32_t)o->wys;
    k1_tekst(o, K1_CZ_MALA_MOCNA, W - 120, H - 26, "73%", kolor, 0);
    k1_obrys(o, (float)W - 86, (float)H - 37, 22, 12, 3, 1.25f, kolor);
    k1_zaokraglony(o, (float)W - 85 + 22.5f, (float)H - 33, 2.5f, 4, 1, kolor);
    k1_zaokraglony(o, (float)W - 84, (float)H - 35, 13, 8, 1.5f, kolor);
    k1_ikona(o, K1_IK_ZASILANIE, (float)W - 44, (float)H - 43, 20, kolor);
}

/* A: the wallpaper dimmed, the clock above a cream card with the K1 mark and the field. */
static void karta(struct k1_obraz *o)
{
    const struct k1_motyw *m = k1_motyw(0);
    int32_t W = (int32_t)o->szer, H = (int32_t)o->wys, cw = 360, ch = 236, cx = W / 2, x = cx - cw / 2,
            y = H / 2 - 40;
    k1_rysuj_tlo(o, 0);
    k1_prostokat(o, 0, 0, W, H, k1_kolor(CIEMNY, 110));
    srodek(o, K1_CZ_ZEGAR, cx, y - 64, "11:42", k1_kolor(KREM, 255));
    srodek(o, K1_CZ_TEKST, cx, y - 30, "sobota, 7 listopada", k1_kolor(KREM, 220));
    k1_cien(o, (float)x, (float)y, (float)cw, (float)ch, 20, 8, 28, 90);
    k1_zaokraglony(o, (float)x, (float)y, (float)cw, (float)ch, 20, k1_kolor(m->tlo, 250));
    k1_rysuj_znak(o, (float)cx, (float)y + 46, 40);
    srodek(o, K1_CZ_TYTUL, cx, y + 110, "Witaj z powrotem", k1_kolor(m->tekst, 255));
    k1_zaokraglony(o, (float)x + 32, (float)y + 140, (float)cw - 64, 48, 12, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)x + 32, (float)y + 140, (float)cw - 64, 48, 12, 1.5f, k1_kolor(m->akcent, 255));
    kropki(o, (float)x + 54, (float)y + 164, 8, k1_kolor(m->tekst, 255));
    strzalka(o, (float)(x + cw - 56), (float)y + 164, 16, k1_kolor(m->akcent, 255), k1_kolor(0xffffff, 255));
    srodek(o, K1_CZ_MALA, cx, y + 216, "Hasło chroni zaszyfrowane dane", k1_kolor(m->tekst3, 255));
    srodek(o, K1_CZ_MALA, cx, y + ch + 34, "Zapomniane hasło = nowe konto (stare dane zostaną utracone)",
           k1_kolor(KREM, 170));
    dolne_ikony(o, k1_kolor(KREM, 230));
}

/* B: like a lock screen — a big clock on the wallpaper, a translucent pill for the password. */
static void zegar(struct k1_obraz *o)
{
    int32_t W = (int32_t)o->szer, H = (int32_t)o->wys, cx = W / 2, pw = 340, py = H - 190;
    k1_rysuj_tlo(o, 1);
    k1_prostokat(o, 0, 0, W, H, k1_kolor(CIEMNY, 70));
    k1_rysuj_znak(o, 40, 38, 22);
    k1_tekst(o, K1_CZ_MALA_MOCNA, 60, 43, "Kons K1", k1_kolor(KREM, 230), 0);
    k1_tekst(o, K1_CZ_ZEGAR, 64, H / 2 - 20, "11:42", k1_kolor(KREM, 255), 0);
    k1_tekst(o, K1_CZ_TYTUL, 68, H / 2 + 22, "sobota, 7 listopada", k1_kolor(KREM, 230), 0);
    srodek(o, K1_CZ_MALA_MOCNA, cx, py - 14, "Hasło", k1_kolor(KREM, 210));
    k1_zaokraglony(o, (float)(cx - pw / 2), (float)py, (float)pw, 48, 24, k1_kolor(KREM, 46));
    k1_obrys(o, (float)(cx - pw / 2), (float)py, (float)pw, 48, 24, 1.25f, k1_kolor(KREM, 120));
    kropki(o, (float)(cx - pw / 2 + 26), (float)py + 24, 8, k1_kolor(KREM, 255));
    strzalka(o, (float)(cx + pw / 2 - 26), (float)py + 24, 16, k1_kolor(0xd97757, 255), k1_kolor(CIEMNY, 255));
    srodek(o, K1_CZ_MALA, cx, py + 78, "Enter — odblokuj   ·   zapomniane hasło = nowe konto", k1_kolor(KREM, 160));
    dolne_ikony(o, k1_kolor(KREM, 230));
}

/* C: warm paper without the wallpaper, editorial: the mark and name on the left, the form on the right. */
static void papier(struct k1_obraz *o)
{
    const struct k1_motyw *m = k1_motyw(0);
    int32_t W = (int32_t)o->szer, H = (int32_t)o->wys, lx = W / 2 - 300, rx = W / 2 + 40, y = H / 2;
    k1_wyczysc(o, k1_kolor(0xf4efe6, 255));
    k1_rysuj_znak(o, (float)lx + 56, (float)y - 40, 96);
    k1_tekst(o, K1_CZ_TYTUL, lx, y + 52, "Kons K1", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, lx, y + 76, "Twój komputer, twoje dane.", k1_kolor(m->tekst3, 255), 0);
    k1_prostokat(o, W / 2, y - 120, 1, 240, k1_kolor(m->tekst, 30));
    k1_tekst(o, K1_CZ_TYTUL, rx, y - 70, "Dzień dobry", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, rx, y - 40, "Wpisz hasło, żeby odblokować dane.", k1_kolor(m->tekst2, 255), 0);
    k1_zaokraglony(o, (float)rx, (float)y - 16, 280, 44, 10, k1_kolor(0xffffff, 255));
    k1_obrys(o, (float)rx, (float)y - 16, 280, 44, 10, 1.25f, k1_kolor(m->tekst, 40));
    kropki(o, (float)rx + 20, (float)y + 6, 8, k1_kolor(m->tekst, 255));
    k1_zaokraglony(o, (float)rx, (float)y + 44, 120, 40, 10, k1_kolor(m->akcent, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, rx + 60 - k1_tekst_szer(K1_CZ_TEKST_MOCNA, "Odblokuj") / 2, y + 69, "Odblokuj",
             k1_kolor(m->na_akcencie, 255), 0);
    k1_tekst(o, K1_CZ_MALA, rx, y + 112, "Zapomniane hasło = nowe konto.", k1_kolor(m->tekst3, 255), 0);
    k1_tekst(o, K1_CZ_MALA, 40, H - 28, "sobota, 7 listopada · 11:42", k1_kolor(m->tekst2, 255), 0);
    dolne_ikony(o, k1_kolor(m->tekst2, 255));
}

int main(int argc, char **argv)
{
    int wariant = argc > 1 ? atoi(argv[1]) : 0;
    unsigned long w = argc > 2 ? strtoul(argv[2], NULL, 10) : 1366, h = argc > 3 ? strtoul(argv[3], NULL, 10) : 768;
    struct k1_obraz o;
    size_t i;
    if (w < 800 || h < 600 || w > 4096 || h > 4096)
        return 2;
    o.szer = (uint32_t)w;
    o.wys = (uint32_t)h;
    o.krok = (uint32_t)w;
    o.px = calloc((size_t)w * (size_t)h, sizeof *o.px);
    if (o.px == NULL)
        return 1;
    if (wariant == 1)
        zegar(&o);
    else if (wariant == 2)
        papier(&o);
    else
        karta(&o);
    printf("P6\n%lu %lu\n255\n", w, h);
    for (i = 0; i < (size_t)w * (size_t)h; i++) {
        unsigned char rgb[3] = { (unsigned char)(o.px[i] >> 16), (unsigned char)(o.px[i] >> 8), (unsigned char)o.px[i] };
        fwrite(rgb, 1, 3, stdout);
    }
    free(o.px);
    return 0;
}

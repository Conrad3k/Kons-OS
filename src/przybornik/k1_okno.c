#include "k1_okno.h"

static const struct k1_motyw jasny = {
    .tlo = 0xfaf9f6, .pole = 0xffffff, .linia = 0xe5e2dc, .najechanie = 0xf0eee9, .zaznaczenie = 0xebe8e1,
    .tekst = 0x1f1e1c, .tekst2 = 0x5e5c57, .tekst3 = 0x8d8a84,
    .akcent = 0xbc5a37, .na_akcencie = 0xffffff, .akcent_tekst = 0xa94f30, .panel_alfa = 240,
};
static const struct k1_motyw ciemny = {
    .tlo = 0x262523, .pole = 0x302f2c, .linia = 0x3c3a37, .najechanie = 0x32312e, .zaznaczenie = 0x3a3834,
    .tekst = 0xeeebe5, .tekst2 = 0xaba79f, .tekst3 = 0x7d7a73,
    .akcent = 0xd97757, .na_akcencie = 0x1f1e1c, .akcent_tekst = 0xe08a6c, .panel_alfa = 240,
};

const struct k1_motyw *k1_motyw(int c)
{
    return c ? &ciemny : &jasny;
}

#define PRZYCISK 28
#define MARGINES_PRZ 8

void k1_ramka_okna(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, int aktywne, int najechany)
{
    float w = (float)o->szer, h = (float)o->wys;
    float bx = w - MARGINES_PRZ - PRZYCISK, by = (K1_OKNO_TYTUL - PRZYCISK) / 2.0f;
    k1_wyczysc(o, 0);
    k1_zaokraglony(o, 0, 0, w, h, K1_OKNO_PROMIEN, k1_kolor(m->tlo, 255));
    k1_obrys(o, 0, 0, w, h, K1_OKNO_PROMIEN, 1.0f, k1_kolor(m->tekst, 26));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, 16, 27, tytul, k1_kolor(aktywne ? m->tekst : m->tekst3, 255), (int32_t)(w - 120));
    if (najechany == K1_TR_ZAMKNIJ)
        k1_zaokraglony(o, bx, by, PRZYCISK, PRZYCISK, 8, k1_kolor(m->najechanie, 255));
    k1_ikona(o, K1_IK_ZAMKNIJ, bx + 6, by + 6, 16, k1_kolor(m->tekst2, 255));
    bx -= PRZYCISK + 4;
    if (najechany == K1_TR_MINIMALIZUJ)
        k1_zaokraglony(o, bx, by, PRZYCISK, PRZYCISK, 8, k1_kolor(m->najechanie, 255));
    k1_ikona(o, K1_IK_MINIMALIZUJ, bx + 6, by + 6, 16, k1_kolor(m->tekst2, 255));
}

enum k1_trafienie k1_trafienie_ramki(uint32_t szer, uint32_t wys, int32_t x, int32_t y)
{
    int32_t bx = (int32_t)szer - MARGINES_PRZ - PRZYCISK, by = (K1_OKNO_TYTUL - PRZYCISK) / 2;
    if (x < 0 || y < 0 || x >= (int32_t)szer || y >= (int32_t)wys)
        return K1_TR_BRAK;
    if (y >= K1_OKNO_TYTUL)
        return K1_TR_TRESC;
    if (y >= by && y < by + PRZYCISK) {
        if (x >= bx && x < bx + PRZYCISK)
            return K1_TR_ZAMKNIJ;
        if (x >= bx - PRZYCISK - 4 && x < bx - 4)
            return K1_TR_MINIMALIZUJ;
    }
    return K1_TR_TYTUL;
}

void k1_segment(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h,
                const char *const *etykiety, int n, int wybrany, int najechany)
{
    int i;
    float sw;
    if (n <= 0)
        return;
    sw = (float)(w - 8) / (float)n;
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 10, k1_kolor(m->zaznaczenie, 255));
    for (i = 0; i < n; i++) {
        float sx = (float)x + 4 + sw * (float)i;
        int32_t tw = k1_tekst_szer(K1_CZ_TEKST, etykiety[i]);
        if (i == wybrany) {
            k1_zaokraglony(o, sx, (float)y + 4, sw, (float)h - 8, 6, k1_kolor(m->pole, 255));
            k1_obrys(o, sx, (float)y + 4, sw, (float)h - 8, 6, 1.0f, k1_kolor(m->tekst, 20));
        } else if (i == najechany) {
            k1_zaokraglony(o, sx, (float)y + 4, sw, (float)h - 8, 6, k1_kolor(m->najechanie, 255));
        }
        k1_tekst(o, i == wybrany ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, (int32_t)(sx + (sw - (float)tw) / 2),
                 y + h / 2 + 5, etykiety[i], k1_kolor(i == wybrany ? m->tekst : m->tekst2, 255), 0);
    }
}

int k1_segment_trafienie(int32_t x, int32_t y, int32_t w, int32_t h, int n, int32_t px, int32_t py)
{
    int32_t i;
    if (n <= 0 || px < x + 4 || px >= x + w - 4 || py < y || py >= y + h)
        return -1;
    i = (px - x - 4) * n / (w - 8);
    return i < n ? (int)i : n - 1;
}

void k1_przelacznik(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int wlaczony, int niedostepny)
{
    uint32_t tlo = niedostepny ? k1_kolor(m->linia, 255) : (wlaczony ? k1_kolor(m->akcent, 255) : k1_kolor(m->tekst3, 255));
    k1_zaokraglony(o, (float)x, (float)y, 36, 20, 10, tlo);
    k1_kolo(o, (float)x + (wlaczony ? 26.0f : 10.0f), (float)y + 10, 7, k1_kolor(niedostepny ? m->pole : 0xffffff, 255));
}

/* Host tests of Podgląd (0.8.2 step 4): the order of the pictures (src/aplikacje/podglad/kolejnosc.c) — the pictures of
 * one folder in the order of Pliki, ← / → around, the picture after the one put in Kosz, "3 z 27". ASan/UBSan build. */
#include <string.h>

#include "k1_test_host.h"
#include "kolejnosc.h"
#include "skala.h"

static struct k1_dok_wpis w[10];
static int n;

static void dodaj(uint32_t id, uint32_t rodzic, int rodzaj, int typ, const char *nazwa, int64_t kosz)
{
    memset(&w[n], 0, sizeof w[n]);
    w[n].id = id;
    w[n].rodzic = rodzic;
    w[n].rodzaj = (uint8_t)rodzaj;
    w[n].typ = (uint8_t)typ;
    w[n].kosz = kosz;
    w[n].rozmiar = 10;
    strcpy(w[n++].nazwa, nazwa);
}

static void t_kolejnosc(void)
{
    int zdj[10], ile;
    dodaj(101, K1_FOLDER_OBRAZY, K1_DOK_PLIK, K1_TYP_JPEG, "b.jpg", 0);
    dodaj(102, K1_FOLDER_OBRAZY, K1_DOK_PLIK, K1_TYP_TEKST, "a opis.txt", 0); /* not a picture */
    dodaj(103, K1_FOLDER_OBRAZY, K1_DOK_PLIK, K1_TYP_PNG, "A.png", 0);
    dodaj(104, K1_FOLDER_OBRAZY, K1_DOK_FOLDER, 0, "Album", 0);           /* a folder */
    dodaj(105, K1_FOLDER_OBRAZY, K1_DOK_PLIK, K1_TYP_GIF, "Z.gif", 0);
    dodaj(106, K1_FOLDER_OBRAZY, K1_DOK_PLIK, K1_TYP_WEBP, "c.webp", 1700000000); /* in Kosz */
    dodaj(107, 104, K1_DOK_PLIK, K1_TYP_BMP, "d.bmp", 0);                  /* another folder */
    dodaj(108, K1_FOLDER_OBRAZY, K1_DOK_PLIK, K1_TYP_PDF, "e.pdf", 0);     /* not a picture */
    ile = kol_zdjecia(w, n, K1_FOLDER_OBRAZY, zdj);
    EXPECT(ile == 3 && w[zdj[0]].id == 103 && w[zdj[1]].id == 101 && w[zdj[2]].id == 105); /* A, b, Z: as Pliki */
    EXPECT(kol_pozycja(w, zdj, ile, 101) == 1 && kol_pozycja(w, zdj, ile, 102) == -1 && kol_pozycja(w, zdj, ile, 107) == -1);
    EXPECT(kol_zdjecia(w, n, 104, zdj) == 1 && w[zdj[0]].id == 107);
    EXPECT(kol_zdjecia(w, n, K1_FOLDER_PULPIT, zdj) == 0);
    EXPECT(kol_zdjecie(&w[0]) && !kol_zdjecie(&w[1]) && !kol_zdjecie(&w[3]) && !kol_zdjecie(&w[5]));
}

static void t_kroki(void)
{
    char t[16];
    EXPECT(kol_krok(0, 3, 1) == 1 && kol_krok(2, 3, 1) == 0 && kol_krok(0, 3, -1) == 2); /* around, both ways */
    EXPECT(kol_krok(0, 1, 1) == 0 && kol_krok(0, 0, 1) == -1 && kol_krok(-1, 3, 1) == 0 && kol_krok(1, 3, -7) == 0);
    EXPECT(kol_po_usunieciu(1, 2) == 1 && kol_po_usunieciu(2, 2) == 1 && kol_po_usunieciu(0, 0) == -1 &&
           kol_po_usunieciu(-1, 4) == 0); /* the next one; at the end the one before; none left */
    kol_tekst(2, 27, t, sizeof t);
    EXPECT(strcmp(t, "3 z 27") == 0);
    kol_tekst(0, 0, t, sizeof t);
    EXPECT(t[0] == '\0');
    kol_tekst(5, 3, t, sizeof t);
    EXPECT(t[0] == '\0');
    kol_tekst(999, 1000, t, 4); /* cut, never overrun */
    EXPECT(strlen(t) == 3);
}

/* 0.8.5 step 2: the fast way of the view — which copy, the copies, bilinear sampling */
static void t_skala(void)
{
    static uint32_t z[7 * 5], p[3 * 2], c[16 * 16];
    struct sk_obraz o = { z, 7, 5 };
    int32_t pw, ph;
    EXPECT(sk_kopia(1.0) == 0 && sk_kopia(4.0) == 0 && sk_kopia(0.6) == 0); /* zoomed in or near 1:1: the picture */
    EXPECT(sk_kopia(0.5) == 1 && sk_kopia(0.3) == 1 && sk_kopia(0.25) == 2 && sk_kopia(0.01) == 2);
    for (int i = 0; i < 7 * 5; i++)
        z[i] = 0xff000000u | (uint32_t)(i * 7) << 16 | (uint32_t)(i % 7 * 30) << 8 | (uint32_t)(i / 7 * 50);
    sk_polowa(o, p, &pw, &ph);
    EXPECT(pw == 3 && ph == 2);
    EXPECT(p[0] == (0xff000000u | (((0 + 7 + 49 + 56 + 2) / 4) << 16) | (((0 + 30 + 0 + 30 + 2) / 4) << 8) | ((0 + 0 + 50 + 50 + 2) / 4)));
    EXPECT(sk_nieprzezroczysty(o) && sk_nieprzezroczysty((struct sk_obraz){ p, 3, 2 }));
    /* 1:1 at whole pixels: the same pixels */
    sk_szybko(o, 0, 0, 7, 5, c, 7, 5, 16);
    EXPECT(memcmp(c, z, 7 * 4) == 0 && memcmp(c + 4 * 16, z + 4 * 7, 7 * 4) == 0);
    /* enlarged 2×: the corners stay, a middle pixel lies between its neighbours */
    sk_szybko(o, 0, 0, 7, 5, c, 14, 10, 16);
    EXPECT(c[0] == z[0] && c[9 * 16 + 13] == z[34]);
    EXPECT(((c[16 + 3] >> 8) & 0xff) >= 30 && ((c[16 + 3] >> 8) & 0xff) <= 60);
    /* a part past the edge: clamped, never outside the picture (ASan) */
    sk_szybko(o, -1, -1, 9, 7, c, 16, 16, 16);
    sk_szybko(o, 6.5, 4.5, 1, 1, c, 3, 3, 16);
    EXPECT(c[0] == z[34]);
    z[3] &= 0x80ffffffu;
    EXPECT(!sk_nieprzezroczysty(o));
}

int main(void)
{
    t_kolejnosc();
    t_kroki();
    t_skala();
    return K1_TEST_KONIEC("test_podglad");
}

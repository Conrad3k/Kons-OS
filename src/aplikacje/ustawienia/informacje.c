/* K1 Ustawienia — Informacje (0.10.7 step 2, the owner's D3: a card): the K1 mark, "Kons K1" and the version; under
 * them the screen, the kernel and where the data is, the labels in one column and the values in the next. The kernel's
 * version comes from jadro/wersja.env at build time (K1_JADRO, Makefile). The room for files is its own section
 * (Miejsce, miejsce.c). Part of Ustawienia (ustawienia.h). */
#include <stdio.h>

#include "ustawienia.h"

#define ZNAK 72       /* the K1 mark „Warstwy”, px */
#define ETYKIETA_W 96 /* the column of the labels */
#define DANE_KROK 28  /* one line of the data */

void informacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char ekran[32], wersja[48];
    const char *etykiety[] = { "Ekran", "Jądro", "Dane" }, *wartosci[] = { ekran, "Linux " K1_JADRO, haslo_gdzie_dane() };
    int32_t y = seg_y(), dane = y + ZNAK + 108;
    naglowek(o, m, "Informacje");
    k1_rysuj_znak(o, (float)TRESC_X + ZNAK / 2.0f, (float)y + ZNAK / 2.0f, ZNAK);
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, y + ZNAK + 40, "Kons K1", k1_kolor(m->tekst, 255), 0);
    snprintf(wersja, sizeof wersja, "Wersja %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, y + ZNAK + 66, wersja, k1_kolor(m->tekst2, 255), 0);
    zmienne("Wersja systemu", y + ZNAK + 48, 26); /* a new version every release */
    snprintf(ekran, sizeof ekran, "%d × %d", (int)apl.k.szer_ekranu, (int)apl.k.wys_ekranu);
    for (int i = 0; i < 3; i++) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, dane + i * DANE_KROK, etykiety[i], k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X + ETYKIETA_W, dane + i * DANE_KROK, wartosci[i], k1_kolor(m->tekst, 255),
                 apl.szer - TRESC_X - ETYKIETA_W - 24);
    }
    k1_el(K1_EL_ZMIENNE, "Jądro", TRESC_X + ETYKIETA_W - 4, dane + DANE_KROK - 20, apl.szer - TRESC_X - ETYKIETA_W - 12,
          26, 0); /* the robot releases a new kernel (ADR 0047) */
}

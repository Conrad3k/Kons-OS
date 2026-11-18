/* K1 Ustawienia — Informacje: the version, the screen, the kernel, where the data is. The room for files moved to its
 * own section in 0.8.5 (Miejsce, miejsce.c). Part of Ustawienia (ustawienia.h). */
#include <stdio.h>

#include "ustawienia.h"

void informacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[128];
    naglowek(o, m, "Informacje");
    snprintf(buf, sizeof buf, "Wersja systemu    %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, buf, k1_kolor(m->tekst, 255), 0);
    zmienne("Wersja systemu", seg_y() - 2, 26); /* a new version every release */
    snprintf(buf, sizeof buf, "Ekran    %d × %d", (int)apl.k.szer_ekranu, (int)apl.k.wys_ekranu);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 44, buf, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 72, "Jądro    Linux 6.18 (własna konfiguracja)", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 100, haslo_gdzie_dane(), k1_kolor(m->tekst, 255), 0);
}

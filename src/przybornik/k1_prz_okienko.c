/* K1 przybornik — the windows after a drop (k1_przeciaganie.h): the question or the refusal, a name taken in the
 * target. Apart from the table (k1_przeciaganie.c) because they need the menu (k1_menu) — ekran draws the signs only. */
#include <stdio.h>

#include "k1_liczba.h"
#include "k1_przeciaganie.h"

void k1_prz_okienko(struct k1_menu *m, const struct k1_prz_ocena *o)
{
    k1_menu_zacznij(m, K1_MENU_SZER_PYTANIE);
    k1_menu_tekst(m, o->tekst)->mocna = 1;
    if (o->opis[0] != '\0')
        k1_menu_tekst(m, o->opis);
    k1_menu_kreska(m);
    if (o->dzialanie != K1_PD_ODMOWA) {
        k1_menu_dodaj(m, "Przenieś do Kosza", K1_PO_TAK, 0)->mocna = 1;
        k1_menu_dodaj(m, "Anuluj", K1_PO_ANULUJ, 0);
    } else {
        k1_menu_dodaj(m, "OK", K1_PO_OK, 0)->mocna = 1;
    }
}

void k1_prz_okienko_nazwa(struct k1_menu *m, const char *cel, const char *nazwa, int zajetych, int ile)
{
    char t[256];
    k1_menu_zacznij(m, K1_MENU_SZER_PYTANIE);
    if (zajetych == 1)
        snprintf(t, sizeof t, "W „%s” jest już „%s”.", cel, nazwa);
    else
        snprintf(t, sizeof t, "W „%s” %s już %d %s o tych samych nazwach.", cel, k1_odmiana(zajetych, "jest", "są", "jest"),
                 zajetych, k1_odmiana(zajetych, "element", "elementy", "elementów"));
    k1_menu_tekst(m, t)->mocna = 1;
    k1_menu_tekst(m, zajetych == 1 ? "„Zachowaj oba”: przenoszony dostanie numer, np. „nazwa (2)”."
                                   : "„Zachowaj oba”: przenoszone dostaną numery, np. „nazwa (2)”.");
    k1_menu_kreska(m);
    k1_menu_dodaj(m, "Zachowaj oba", K1_PO_ZACHOWAJ, 0)->mocna = 1;
    if (ile > zajetych) /* the others go on */
        snprintf(t, sizeof t, zajetych == 1 ? "Pomiń ten" : "Pomiń te %d", zajetych);
    else
        snprintf(t, sizeof t, "Pomiń");
    k1_menu_dodaj(m, t, K1_PO_POMIN, 0);
    k1_menu_dodaj(m, "Anuluj", K1_PO_ANULUJ, 0);
}

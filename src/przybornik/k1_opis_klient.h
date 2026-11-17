/* K1 przybornik — the elements of a surface for the screen's description (0.10.2 step 2, ADR 0045; format k1_opis.h).
 * One table per surface, filled where the element is drawn (T4: k1_przycisk, the rows of k1_menu, the window frame's
 * buttons, k1_segment — and the shell's own panels with k1_el), sent to ekran with the commit when it changed — only
 * while ekran asks (K1_UST_OPIS = 1: the test image with its test client). Otherwise every call returns at once and
 * nothing is allocated. Names are only K1's own words (labels, titles): never what the user typed, file or session
 * names (invariant 8). */
#ifndef K1_OPIS_KLIENT_H
#define K1_OPIS_KLIENT_H

#include <stdint.h>

#include "k1_opis.h"

struct k1_klient;
struct k1_powierzchnia;

/* Surface p's table from scratch (a full drawing of it starts); `nazwa`: the surface's own name (a window: its title,
 * a panel: "Menu Start"…; NULL: none). Makes p the surface k1_el adds to. */
void k1_el_od_nowa(struct k1_powierzchnia *p, const char *nazwa);
/* The surface k1_el adds to (k1_pow_rysuj sets it; a program drawing two surfaces at once sets it itself). */
void k1_el_cel(struct k1_powierzchnia *p);
/* An element of the current surface (its coordinates; cut to the surface; a name K1 cannot describe is left out). */
void k1_el(enum k1_el_rodzaj rodzaj, const char *nazwa, int32_t x, int32_t y, int32_t w, int32_t h, unsigned stan);

/* przybornik itself (k1_klient.c): setting K1_UST_OPIS came; a commit of p (sends its table when it changed); p goes. */
void k1_opis_ustaw(int wlaczony);
int k1_opis_wlaczony(void);
void k1_opis_wyslij(struct k1_klient *k, struct k1_powierzchnia *p);
void k1_opis_usun(struct k1_powierzchnia *p);

#endif

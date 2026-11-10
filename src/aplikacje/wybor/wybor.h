/* K1 Wybór pliku — the file picker (0.8.0 step 2, ADR 0030), a window of K1 itself (never in the Start menu):
 *   main.c   the window: the request, the standard folders, the path, the list, Otwórz / Anuluj, keys and mouse
 *   dane.c   skarbiec (fd 4): the request (DOK_PROSBA), the list of names (no content), the answer (DOK_WYBRANO) */
#ifndef K1_WYBOR_H
#define K1_WYBOR_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_dokument.h"

#define TAG "K1-WYBOR"
#define FD_SKARBIEC 4

/* Layout (window coordinates) */
#define SZER 700
#define WYS 470
#define BOK 176                         /* the standard folders on the left */
#define PROSBA_Y (K1_OKNO_TYTUL + 26)   /* baseline of "<application> asks for a file" */
#define NAV_Y (K1_OKNO_TYTUL + 52)
#define NAV_WIERSZ 34
#define LISTA_X (BOK + 16)
#define LISTA_W (SZER - LISTA_X - 16)
#define SCIEZKA_Y (K1_OKNO_TYTUL + 48)  /* the "up" button and the path */
#define WIERSZE_Y (K1_OKNO_TYTUL + 90)
#define WIERSZ 30
#define STOPKA 60                       /* the buttons at the bottom */
#define PRZYCISK_W 104
#define PRZYCISK_H 34

extern struct k1_apl apl;
extern struct k1_dok_wpis wpisy[K1_DOK_MAKS];
extern int n_wpisow, gotowe;
extern struct prosba {
    int jest, apl, typ; /* who asks (K1_APL_*), for which type (0: any) */
} prosba;
extern const char *blad; /* a message above the buttons (NULL: none) */

/* dane.c */
int dane_start(void);   /* fd 4, or -1 without skarbiec */
void dane_odbierz(void);
const struct k1_dok_wpis *wpis(uint32_t id);
void odpowiedz(uint32_t id); /* the user's choice (0: cancelled); the window closes when skarbiec takes it */

/* main.c */
void po_liscie(void);   /* a new list: the folder shown and the selection kept when they still exist */

#endif

/* K1 Ustawienia — state shared by the modules (0.7.5 split): main.c (sections, Wygląd, Ekran, Mysz,
 * Data i czas, Informacje), bateria.c (Bateria: from nadzorca, fd 4), haslo.c (Hasło: through skarbiec, fd 5). */
#ifndef K1_USTAWIENIA_H
#define K1_USTAWIENIA_H

#include "k1_aplikacja.h"

#define TAG "K1-USTAWIENIA"
#define FD_NADZORCA 4
#define FD_SKARBIEC 5 /* M5e: the password */
#define BOK 188        /* side navigation width */
#define NAV_Y (K1_OKNO_TYTUL + 8)
#define NAV_WIERSZ 36
#define TRESC_X (BOK + 28)
#define SEG_W 260
#define SEG_H 36

enum { S_WYGLAD, S_EKRAN, S_BATERIA, S_MYSZ, S_CZAS, S_HASLO, S_INFO, S_LICZBA };

extern struct k1_apl apl;
extern int sekcja;

/* The control of the current section: segmented buttons at (TRESC_X, seg_y). */
static inline int seg_y(void)
{
    return K1_OKNO_TYTUL + 104;
}
/* Title and one-line description of a section. */
void naglowek(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, const char *opis);

/* bateria.c */
int bateria_odbierz(void); /* what nadzorca sent; 1 = the Bateria section must be redrawn */
void bateria_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
void bateria_loguj(void); /* the tests check the chart by this line */

/* haslo.c */
int haslo_start(void);      /* fd 5 when there is a channel to skarbiec, else -1 */
void haslo_odbierz(void);   /* what skarbiec said */
void haslo_info(void);      /* asks skarbiec for its state */
void haslo_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int haslo_klik(int32_t x, int32_t y);                    /* 1 = handled */
void haslo_klawisz(unsigned kod, int wartosc, unsigned mody);
void haslo_zamknij(void);   /* leaving the section: the form closes, typed text is wiped */
void haslo_loguj(void);     /* the tests click by this line */
const char *haslo_gdzie_dane(void); /* the Informacje line about the data */

#endif

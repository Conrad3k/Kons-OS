/* K1 Notatki — state shared by the modules (0.7.5 split): main.c (window: list, text, footer, keys,
 * mouse), spis.c (the notes and the list in skarbiec, fd 4), tekst.c (line wrap and editing of the open note). */
#ifndef K1_NOTATKI_H
#define K1_NOTATKI_H

#include <stdint.h>

#include "k1_aplikacja.h"

#define TAG "K1-NOTATKI"
#define FD_SKARBIEC 4
#define MAKS_TEKST 16384
#define MAKS_LINII 1024
#define MAKS_NOTATEK 32
#define OB_SPIS 1                  /* the list of notes */
#define OB_PIERWSZA K1_OB_NOTATKA  /* ids OB_PIERWSZA .. OB_PIERWSZA + MAKS_NOTATEK - 1 */
#define SPIS_ZNAK "K1N1"

struct notatka {
    int id;
    uint32_t dl;
    char tekst[MAKS_TEKST + 1];
};

extern struct k1_apl apl;
extern struct notatka nt[MAKS_NOTATEK]; /* in list order */
extern int n_nt, biez;                  /* number of notes, the open one */
extern struct notatka *b;               /* = &nt[biez] */
extern uint32_t kursor, pierwsza;       /* cursor (byte) in the open note, first visible line */
extern int lista_od;                    /* first visible row of the list */
extern int pytanie;                     /* "delete this note?" shown in the footer */
extern int pelno;                       /* the limit of notes was hit (message in the footer) */
extern double zmiana;                   /* when the open note last changed (0: saved) */
extern int zapis_blad, skarb, tryb;     /* tryb: K1_SKARB_TRWALY / K1_SKARB_RAM */

void popraw_przewiniecie(int do_biezacej); /* main.c: keeps the list's scroll in range */

/* spis.c */
void wybierz_indeks(int i);
void wczytaj(void);       /* all notes from skarbiec at start */
void zapisz(void);        /* the open note (and the list, when its order changed) */
void odbierz_wyniki(void);
void otworz(int i);
void nowa(void);
void usun_biezaca(void);

/* tekst.c: the open note in lines of at most `szer` px */
struct linia {
    uint32_t od, do_;
};
extern struct linia linie[MAKS_LINII];
extern uint32_t n_linii;
void uloz(int32_t szer);
uint32_t linia_kursora(void);
int32_t x_w_linii(uint32_t ln, uint32_t pos);
uint32_t pozycja_w_linii(uint32_t ln, int32_t x);
uint32_t poprzedni(uint32_t p);
uint32_t nastepny(uint32_t p);
void wstaw(const char *s, uint32_t n);
void usun(uint32_t od, uint32_t do_);

#endif

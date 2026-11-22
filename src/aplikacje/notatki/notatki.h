/* K1 Notatki — state shared by the modules (0.7.5 split; 0.8.0: notes are files, ADR 0030):
 *   main.c   the window: the list, the text, the footer, keys and the mouse
 *   spis.c   the notes in the window: order, opening, a new one, deleting
 *   pliki.c  skarbiec (fd 4): the list of files, their content, saving (the name follows the first line), the file
 *            picker; nadzorca (fd 5): a file to open
 *   tekst.c  line wrap and editing of the open note
 *   schowek.c selecting, Ctrl+C / Ctrl+X / Ctrl+V (0.11.0 step 5) */
#ifndef K1_NOTATKI_H
#define K1_NOTATKI_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_dokument.h"

#define TAG "K1-NOTATKI"
#define FD_SKARBIEC 4
#define FD_NADZORCA 5
#define MAKS_TEKST 16384
#define MAKS_LINII 1024
#define MAKS_NOTATEK 256 /* the newest; more files: "Pokazano 256 najnowszych notatek" (0.8.1) */

struct notatka {
    uint32_t nr;           /* this window's own number (never changes, unlike the place in the list) */
    uint32_t id;           /* the file in skarbiec; 0: not created yet (a new note) */
    uint32_t dl;
    int64_t zmieniono;     /* from the entry: the list is newest first */
    int wlasna;            /* made by Notatki (else: a text file the user opened in Notatki) */
    int wczytana;          /* its content is here (until then nothing is saved over the file) */
    int blad_odczytu;      /* skarbiec could not give its content: read only, asked again at the next change */
    int zapisywana;        /* ZAPISZ sent, its result not here yet (how many) */
    int tworzona;          /* NOWY sent, its number not here yet */
    int do_zapisu;         /* changed while it was being created: save once it has a number */
    char nazwa[K1_DOK_NAZWA];   /* the file's name */
    char auto_n[K1_DOK_NAZWA];  /* the name its first line gave at the last save (the name follows it until
                                 * the user renames the file in Pliki) */
    char tekst[MAKS_TEKST + 1];
};

extern struct k1_apl apl;
extern struct notatka nt[MAKS_NOTATEK]; /* in list order */
extern int n_nt, biez;                  /* number of notes, the open one */
extern struct notatka *b;               /* = &nt[biez] */
extern uint32_t kursor, pierwsza;       /* cursor (byte) in the open note, first visible line */
extern int lista_od;                    /* first visible row of the list */
extern int pytanie;                     /* "delete this note?" shown in the footer */
extern const char *komunikat;           /* a message for the footer (NULL: none) */
extern double zmiana;                   /* when the open note last changed (0: saved) */
extern int zapis_blad, skarb, tryb;     /* tryb: K1_SKARB_TRWALY / K1_SKARB_RAM */

void popraw_przewiniecie(int do_biezacej); /* main.c: keeps the list's scroll in range */

/* spis.c */
void wybierz_indeks(int i);
struct notatka *dodaj_notatke(void);  /* an empty slot at the end, or NULL when full */
void uloz_liste(void);                /* newest first (new notes on top); the open one stays open */
void otworz(int i);
void nowa(void);
void usun_biezaca(void);
void pokaz_plik(uint32_t id);         /* select the note of file `id` (opened through Pliki or the picker) */

/* pliki.c */
void wczytaj(void);       /* the channel to skarbiec; the old notes moved; the list asked for */
void zapisz(void);        /* the open note (a new file, or new content; the name follows its first line) */
void do_kosza(uint32_t id);
void zamknij_dany(uint32_t id); /* a file given to Notatki, closed here: kept out of the list */
void wybierz_plik(void);  /* "Otwórz…": the file picker of K1 */
void skarb_odbierz(void);    /* fd 4 */
void nadzorca_odbierz(void); /* fd 5 */

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

/* schowek.c (0.11.0 step 5): the selection and the clipboard */
int zaznaczenie(uint32_t *od, uint32_t *do_); /* 1 and its bytes when something is selected */
void zaznaczenie_zapomnij(void);
void przed_ruchem(unsigned kod, unsigned mody); /* a move: with Shift the selection starts or goes on; else it ends */
int usun_zaznaczenie(void);                 /* 1 when there was one (deleted now) */
int schowek_klawisz(unsigned kod, unsigned mody); /* Ctrl+A, Ctrl+C, Ctrl+X: 1 when taken */
void wklej(const char *t, size_t n);        /* apl.wklej: the clipboard's text at the cursor */
uint32_t nastepny(uint32_t p);
void wstaw(const char *s, uint32_t n);
void usun(uint32_t od, uint32_t do_);

#endif

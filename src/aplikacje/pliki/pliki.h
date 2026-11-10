/* K1 Pliki — state shared by the modules (0.8.0, ADR 0029):
 *   main.c   the window: places on the left, the path, the list, keys and the mouse
 *   przyciski.c the buttons of the place and what they do
 *   rysuj.c  drawing the list, the preview on the right and the footer
 *   dane.c   the files from skarbiec (fd 4): the list, a preview, requests and their results; places from
 *            nadzorca (fd 5, when the desktop opens Pliki at a file or Kosz) */
#ifndef K1_PLIKI_H
#define K1_PLIKI_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_dokument.h"

#define TAG "K1-PLIKI"
#define FD_SKARBIEC 4
#define FD_NADZORCA 5

/* Layout (window coordinates) */
#define SZER 920
#define WYS 560
#define BOK 196                        /* the places on the left */
#define NAV_Y (K1_OKNO_TYTUL + 10)
#define NAV_WIERSZ 34
#define KOSZ_Y (NAV_Y + K1_FOLDERY_MAKS * NAV_WIERSZ + 14)
#define LISTA_X (BOK + 16)
#define PODGLAD_W 232                  /* the preview on the right */
#define LISTA_W (SZER - LISTA_X - PODGLAD_W - 24)
#define SCIEZKA_Y (K1_OKNO_TYTUL + 10) /* the "up" button and the path */
#define PRZYCISKI_Y (K1_OKNO_TYTUL + 52)
#define PRZYCISK_H 32
#define NAGL_Y (K1_OKNO_TYTUL + 104)   /* column titles (baseline) */
#define WIERSZE_Y (K1_OKNO_TYTUL + 114)
#define WIERSZ 32
#define STOPKA 40
#define PRZEWIJANIE 26                 /* the strip with the scroll arrows (only when needed) */
#define PRZYCISKI_MAKS 6               /* buttons of a place at most */

/* the place shown: a folder, or Kosz */
struct miejsce {
    uint32_t folder; /* standard 1..6 or a user folder; 0 with kosz */
    int kosz;
};

enum pytanie { PYT_BRAK = 0, PYT_USUN, PYT_OPROZNIJ }; /* "for good?" in the footer */

extern struct k1_apl apl;
extern struct miejsce tu;
extern struct k1_dok_wpis wpisy[K1_DOK_MAKS]; /* everything skarbiec sent */
extern int n_wpisow;
extern uint32_t w_koszu;
extern int widok[K1_DOK_MAKS]; /* indexes into wpisy of what this place shows, sorted */
extern int n_widok, od_wiersza;
extern uint32_t zaznaczony;    /* entry, 0 = none */
extern int najechany;          /* row under the pointer, -1 */
extern struct edycja {
    uint32_t id; /* entry being renamed (0: none) */
    char t[K1_DOK_NAZWA];
    size_t dl;
    int calosc; /* the whole name is selected: the first key replaces it (as in Windows) */
} edycja;
extern enum pytanie pytanie;
extern const char *blad;       /* a message for the footer (NULL: none) */
extern int gotowe;             /* the first list has arrived */

/* main.c */
int wierszy_widac(void);
int przewijanie_potrzebne(void);

/* przyciski.c */
enum { B_NOWY, B_NAZWA, B_USUN, B_PRZYWROC, B_NA_ZAWSZE, B_OPROZNIJ, B_TAK, B_ANULUJ, B_OTWORZ_W, B_APL };
extern int rodzaje[PRZYCISKI_MAKS], apl_przycisku[PRZYCISKI_MAKS]; /* what the buttons of przyciski() do, in order */
extern int wybor_aplikacji;           /* "Otwórz w…" pressed: the buttons are the applications that open the file */
int przyciski(struct k1_przycisk *p); /* the buttons of this place: how many */
int dostepny(int r);                  /* whether button kind `r` can do anything now */
void wykonaj(int r, int a);           /* button kind `r` (B_APL: application `a`) */
void otworz_w(int a);                 /* the selected file in application `a` (Pliki: its preview) */
void zacznij_edycje(void);
void skoncz_edycje(int zapisz);

/* dane.c */
int dane_start(void);              /* fd 4, or -1 without skarbiec */
void dane_odbierz(void);           /* fd 4 */
void nadzorca_odbierz(void);       /* fd 5 */
const struct k1_dok_wpis *wpis(uint32_t id);
void przejdz(struct miejsce m);    /* show a place (selection and scroll from the start) */
void loguj_widok(void);
void ulozy_widok(void);            /* what this place shows, sorted: folders first, then by name */
void do_skarbca(int typ, uint32_t obiekt, int32_t a); /* a request without data */
void nowy_folder(void);
void zmien_nazwe(uint32_t id, const char *t, size_t dl);
void podglad_zadaj(uint32_t id);   /* the start of a text file for the preview */
extern char podglad[K1_DOK_PODGLAD + 1];
extern uint32_t podglad_id, podglad_dl;

/* rysuj.c */
void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_podglad(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_stopke(struct k1_obraz *o, const struct k1_motyw *m);
void data_tekst(int64_t t, char *buf, size_t n);   /* "10.11.2026 14:03" */
void rozmiar_tekst(uint64_t b, char *buf, size_t n); /* "1,4 MB" */
enum k1_ikona ikona_wpisu(const struct k1_dok_wpis *w);

#endif

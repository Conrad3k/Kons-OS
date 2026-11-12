/* K1 Podgląd — state shared by the modules (0.8.2 step 4, ADR 0037):
 *   main.c      the window: the picture, the floating bar (‹ "3 z 27" ›, the buttons), the label with the name and
 *               details, keys and the mouse
 *   dane.c      skarbiec (fd 4): the pictures Podgląd may see, Kosz, the file picker; the file to open (fd 5)
 *   obraz.c     the picture shown and the wallpaper — through a dekoder (k1_dekodowanie, fd 6 to nadzorca)
 *   widok.c     the view: turning, fitting, zooming (− +) with a short animation, dragging
 *   kolejnosc.c the order of the pictures (pure functions, host test) */
#ifndef K1_PODGLAD_H
#define K1_PODGLAD_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_dokument.h"

#define TAG "K1-PODGLAD"
#define FD_SKARBIEC 4
#define FD_PLIK 5      /* nadzorca: the file to open (PLIK_OTWORZ) */
#define FD_NADZORCA 6  /* nadzorca: a dekoder for one picture (DEKODER → DEKODER_KANAL) */
#define KANAL_SKARBIEC 0 /* apl.kanaly[] */
#define KANAL_PLIK 1
#define KANAL_NADZORCA 2
#define KANAL_DEKODER 3

/* Layout (window coordinates; sketch C chosen by the owner, docs/WYGLAD.md): the picture on the whole window, a floating
 * bar at the bottom, the name and details on a label at the top left */
#define SZER 960
#define WYS 640
#define OBSZAR_X 1                            /* where the picture may be: never enlarged above 100 % */
#define OBSZAR_Y K1_OKNO_TYTUL
#define OBSZAR_W (SZER - 2)
#define OBSZAR_H (WYS - K1_OKNO_TYTUL - K1_OKNO_PROMIEN) /* above the rounded corners */
#define BOK 1600 /* dekoder gives at most BOK × BOK (K1_DEK_MAKS_H): any turn fits, and zooming in has pixels to show */
#define PASEK_H 44                            /* the floating bar */
#define PASEK_Y (WYS - 16 - PASEK_H)

extern struct k1_apl apl;
extern struct k1_dok_wpis wpisy[K1_DOK_MAKS]; /* what skarbiec lets Podgląd see: pictures of the folders it was given */
extern int n_wpisow;
extern int zdjecia[K1_DOK_MAKS], n_zdjec;     /* the pictures of the folder of the one shown, in order (indexes into wpisy) */
extern int pozycja;                           /* of the one shown among them (-1: none) */
extern const char *komunikat;                 /* a message in the bar (NULL: none) */

/* dane.c */
void dane_start(void);
const struct k1_dok_wpis *wpis(uint32_t id);
const struct k1_dok_wpis *biezace(void);      /* the picture shown, or NULL */
void pokaz(int poz);                          /* show the picture at position `poz` of zdjecia */
void do_kosza(void);                          /* the picture shown into Kosz (D2: no question; Kosz keeps it 30 days) */
void wybierz(void);                           /* "Otwórz…": the file picker with pictures only */

/* obraz.c */
void obraz_start(void);
void obraz_zadaj(const struct k1_dok_wpis *w); /* decode it for the window (if not done already) */
void obraz_tapeta(const struct k1_dok_wpis *w);
int obraz_gotowy(const struct k1_dok_wpis *w); /* its picture is decoded and shown */
int obraz_tresc(const struct k1_komunikat *k, int fd); /* DOK_TRESC: 1 when it was a piece of a picture */
int obraz_wynik(const struct k1_komunikat *k);         /* DOK_WYNIK: 1 when a piece could not be read */
void obraz_nadzorca(void);                    /* fd 6 can be read: DEKODER_KANAL */
/* Draws the picture of `w` (or why there is none) centred in the area; the size of the photo into *zw × *zh
 * (0 while unknown). */
void obraz_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t *zw, int32_t *zh);
extern const char *tapeta_stan;               /* how setting the wallpaper goes (NULL: nothing) */

/* widok.c — the view: turned, fitted, zoomed (with a short animation), moved by dragging */
void widok_nowy(const uint32_t *px, int32_t w, int32_t h); /* the picture from dekoder (NULL: none), fitted, not turned */
void widok_obroc(void);                       /* R: a quarter turn to the right (the file stays as it is) */
int widok_mozna(int krok);                    /* whether widok_powieksz(krok) would change anything */
void widok_powieksz(int krok);                /* +1 / -1 a level in or out, 0 fitted again */
int widok_powiekszony(void);                  /* larger than fitted: dragging moves it */
void widok_przesun(int32_t dx, int32_t dy);   /* dragged by dx, dy screen pixels */
int widok_zegar(void);                        /* the animation: ms to its next frame, -1 none */
int widok_rysuj(struct k1_obraz *o);          /* 1 when drawn */
void widok_rozmiar(int32_t *w, int32_t *h);

#endif

/* K1 Pliki — state shared by the modules (0.8.0, ADR 0029):
 *   main.c   the window: places on the left, the path, the list, keys and the mouse
 *   przyciski.c the buttons of the place and what they do
 *   rysuj.c  drawing the list, the preview on the right and the footer
 *   dane.c   the files from skarbiec (fd 4): the list, a preview, requests and their results; places from
 *            nadzorca (fd 5, when the desktop opens Pliki at a file or Kosz)
 *   pendrive.c the USB stick (fd 6 from nadzorca, the channel to czytnik) and the import (0.8.0 step 3)
 *   obrazy.c the preview of a picture and "Ustaw jako tapetę" — through a dekoder (0.8.0 step 4) */
#ifndef K1_PLIKI_H
#define K1_PLIKI_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_dokument.h"

#define TAG "K1-PLIKI"
#define FD_SKARBIEC 4
#define FD_NADZORCA 5
#define FD_PENDRIVE 6
#define KANAL_PENDRIVE 2 /* apl.kanaly[]: 0 skarbiec, 1 nadzorca (fd 5), 2 nadzorca (fd 6), 3 czytnik, 4 dekoder */
#define KANAL_CZYTNIK 3
#define KANAL_DEKODER 4

/* Layout (window coordinates) */
#define SZER 920
#define WYS 560
#define BOK 196                        /* the places on the left */
#define NAV_Y (K1_OKNO_TYTUL + 10)
#define NAV_WIERSZ 34
#define KOSZ_Y (NAV_Y + K1_FOLDERY_MAKS * NAV_WIERSZ + 14)
#define LISTA_X (BOK + 16)
#define PODGLAD_W 232                  /* the preview on the right */
#define PODGLAD_X (SZER - PODGLAD_W - 12)
#define MIN_W (PODGLAD_W - 32)         /* a picture in the preview (0.8.0 step 4) */
#define MIN_H 150
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
#define PODGLAD_TAPETA_Y (SCIEZKA_Y + 16 + MIN_H + 40) /* "Ustaw jako tapetę" under the picture and its name */

/* the place shown: a folder, Kosz, or a folder on the USB stick */
struct miejsce {
    uint32_t folder; /* standard 1..6 or a user folder; 0 with kosz; pendrive: its folder (0: the main one) */
    int kosz;
    int pendrive;
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
const struct k1_dok_wpis *wiersz_wpisu(int i); /* row `i` of the place shown */
const struct k1_dok_wpis *wpis_tu(uint32_t id); /* an entry of the place shown (on the stick: from czytnik) */

/* przyciski.c */
enum { B_NOWY, B_NAZWA, B_USUN, B_PRZYWROC, B_NA_ZAWSZE, B_OPROZNIJ, B_TAK, B_ANULUJ, B_OTWORZ_W, B_APL, B_IMPORTUJ,
       B_IMPORTUJ_WSZ };
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
void do_skarbca_z_fd(struct k1_komunikat m, int fd);  /* a request with a memfd (the import) */
void nowy_folder(void);
void przywroc(uint32_t id);        /* from Kosz (its folder in Kosz: Dokumenty, and the footer says so) */
void zmien_nazwe(uint32_t id, const char *t, size_t dl);
void podglad_zadaj(uint32_t id);   /* the start of a text file for the preview */
extern char podglad[K1_DOK_PODGLAD + 1];
extern int32_t miejsce_zajete, miejsce_calosc; /* the room for files, MiB (0.8.1 step 3) */
int malo_miejsca(void);            /* under a tenth of it left: the footer says so */
extern uint32_t podglad_id, podglad_dl;

/* pendrive.c */
enum { PEN_ZAMKNIETY = 1, PEN_CZEKA = 2, PEN_GOTOWY = 3 }; /* pen_stan; < 0: K1_PEN_BLAD_* */
extern int pen_jest;  /* nadzorca: a stick is plugged in (the place "Pendrive" is shown) */
extern int pen_stan;
extern struct k1_dok_wpis pen[];
extern int n_pen, importuje;
void pendrive_start(void);
void pendrive_otworz(void);         /* the place "Pendrive" clicked */
void pendrive_wejdz(uint32_t id);   /* a folder on the stick */
int pendrive_w_gore_mozna(void);
void pendrive_w_gore(void);
void pendrive_importuj(int wszystkie); /* the selected file, or every file of the folder shown */
int pendrive_wynik_skarbca(const struct k1_komunikat *k); /* 1: the result of an import's DOK_NOWY */
const struct k1_dok_wpis *pen_wpis(uint32_t id);
uint32_t pen_folder(void);
int pen_sciezka(const char **nazwy, int maks); /* "Pendrive", then the folders entered */
const char *pendrive_stopka(void);

/* obrazy.c */
extern const char *tapeta_stan;    /* the wallpaper being set, or how it went (the footer; NULL: nothing) */
void obrazy_start(void);
int obraz_pliku(const struct k1_dok_wpis *w); /* a PNG or JPEG file of the user's (not on the stick) */
void obraz_podglad(uint32_t id);   /* decode for the preview (if not done already) */
void obraz_tapeta(uint32_t id);    /* decode and set as the wallpaper */
int obraz_tresc(const struct k1_komunikat *k, int fd); /* DOK_TRESC: 1 when it was the picture's file */
void obraz_kanal(int fd);          /* DEKODER_KANAL from nadzorca */
int32_t obraz_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t x, int32_t y);
int obraz_przycisk(struct k1_przycisk *p); /* "Ustaw jako tapetę": 1 when shown */

/* rysuj.c */
void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_podglad(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_stopke(struct k1_obraz *o, const struct k1_motyw *m);
void data_tekst(int64_t t, char *buf, size_t n);   /* "10.11.2026 14:03" */
void rozmiar_tekst(uint64_t b, char *buf, size_t n); /* "1,4 MB" */
enum k1_ikona ikona_wpisu(const struct k1_dok_wpis *w);

#endif

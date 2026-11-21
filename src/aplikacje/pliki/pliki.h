/* K1 Pliki — state shared by the modules (0.8.0, ADR 0029):
 *   main.c   the window: places on the left, the path, the list
 *   wskaznik.c the mouse and keys
 *   przyciski.c the buttons of the place and what they do
 *   menu.c   the menu under the right mouse button (0.8.2 step 5: the desktop's, from przybornik k1_menu)
 *   rysuj.c  drawing the list, the details on the right and the footer
 *   dane.c   the files from skarbiec (fd 4): the list, the start of a text, requests and their results; places from
 *            nadzorca (fd 5, when the desktop opens Pliki at a file or Kosz)
 *   pendrive.c the USB stick (fd 6 from nadzorca, the channel to czytnik) and the import (0.8.0 step 3)
 *   obrazy.c the thumbnail of a picture and "Ustaw jako tapetę" — through a dekoder (0.8.0 step 4)
 *   przeciaganie.c dragging onto folders, Kosz and the stick, and its windows (0.8.2 step 6)
 *   zbiorczo.c one action on many selected entries (0.8.2 step 6) */
#ifndef K1_PLIKI_H
#define K1_PLIKI_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_dokument.h"
#include "k1_menu.h"
#include "k1_zaznaczenie.h"

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
#define SZCZEGOLY_W 232                  /* the details on the right ("szczegóły") */
#define SZCZEGOLY_X (SZER - SZCZEGOLY_W - 12)
#define MIN_W (SZCZEGOLY_W - 32)         /* a thumbnail in the details (0.8.0 step 4) */
#define MIN_H 150
#define LISTA_W lista_w()             /* 0.10.7 step 3: the whole width without the details */
#define SCIEZKA_Y (K1_OKNO_TYTUL + 10) /* the "up" button and the path */
#define PRZYCISKI_Y (K1_OKNO_TYTUL + 52)
#define PRZYCISK_H 32
#define NAGL_Y (K1_OKNO_TYTUL + 104)   /* column titles (baseline) */
#define WIERSZE_Y (K1_OKNO_TYTUL + 114)
#define WIERSZ 32
#define STOPKA 40
#define PRZEWIJANIE 26                 /* the strip with the scroll arrows (only when needed) */
#define PRZYCISKI_MAKS 6               /* buttons of a place at most */
#define PUSTY_ZNAK 64                  /* an empty folder: the grey mark „Warstwy” (0.10.7 step 3) */
#define SZCZEGOLY_TAPETA_Y (SCIEZKA_Y + 16 + MIN_H + 40) /* "Ustaw jako tapetę" under the picture and its name */

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
extern uint32_t zaznaczony;    /* the entry clicked last (its details, the menu, renaming), 0 = none */
extern struct k1_zaznaczenie zazn; /* every entry selected (0.8.2 step 6: Ctrl+click, Shift+click, Ctrl+A) */
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
extern int nad_miejscem, nad_przyciskiem, nad_w_gore, nad_tapeta, nad_pustym; /* under the pointer (drawn highlighted) */
int szczegoly_widac(void); /* the details on the right: only with something selected (0.10.7 step 3, D4) */
int32_t lista_w(void);     /* the list's width: up to the details, or to the window's edge */
int wierszy_widac(void);
int wiersz_zaznaczonego(void);
void zaznacz_wiersz(int i);
void zaznacz_jeden(uint32_t id);       /* only this entry selected (0: none) */
const uint32_t *kolejnosc_widoku(void); /* the ids of the rows shown, in their order (n_widok of them) */
int w_gore_mozna(void);
void w_gore(void);
int przewijanie_potrzebne(void);
const struct k1_dok_wpis *wiersz_wpisu(int i); /* row `i` of the place shown */
const struct k1_dok_wpis *wpis_tu(uint32_t id); /* an entry of the place shown (on the stick: from czytnik) */

/* wskaznik.c — the mouse and keys */
void otworz(uint32_t id); /* a folder: into it; a file: its default application, or why there is none (the footer) */
void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk);
void klawisz(unsigned kod, int wartosc, unsigned mody);
void przewin(int32_t x, int32_t y, int32_t dx, int32_t dy); /* the wheel or two fingers (0.10.0 step 4) */
int przewin_zegar(void);                                    /* apl.zegar: the scrolled list to the log */
void klik_zapomnij(void); /* the next click is never the second of a double click */
int miejsce_pod(int32_t x, int32_t y); /* a place on the left: 0..K1_FOLDERY_MAKS-1 a folder, then Kosz, the stick; -1 */
int wiersz_pod(int32_t x, int32_t y);  /* a row of the list shown, or -1 */

/* przyciski.c */
enum { B_NOWY, B_NAZWA, B_USUN, B_PRZYWROC, B_NA_ZAWSZE, B_OPROZNIJ, B_TAK, B_ANULUJ, B_OTWORZ, B_IMPORTUJ, B_IMPORTUJ_WSZ };
extern int rodzaje[PRZYCISKI_MAKS];   /* what the buttons of przyciski() do, in order */
int przyciski(struct k1_przycisk *p); /* the buttons of this place now (some only with a selection): how many */
int przycisk_pustego(struct k1_przycisk *p); /* "Nowy folder" in the middle of an empty folder: 1 when shown */
int dostepny(int r);                  /* whether button kind `r` can do anything now */
void wykonaj(int r);                  /* button kind `r` */
void do_sesji(int strefa, int otworz);  /* "Wyślij do", "Otwórz za pomocą" a session (0.11.0 step 3) */
void otworz_w(int a);                 /* the selected file in application `a` ("Otwórz za pomocą") */
void zacznij_edycje(void);
void skoncz_edycje(int zapisz);

/* menu.c — the menu under the right mouse button (0.8.2 step 5) */
void menu_otworz(uint32_t id, int32_t x, int32_t y); /* entry `id` of the place shown, at the pointer */
int menu_otwarte(void);
void menu_zamknij(void);
void menu_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int menu_wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk); /* 1: the menu took it */

/* menu.c: the window of a question or a refusal after dragging (k1_prz_okienko…, its look is the menu's): the answer
 * (K1_PO_*) goes to `wybrano` — K1_PO_ANULUJ when it is closed otherwise; `rodzaj` names it in the log */
void okienko_otworz(const struct k1_menu *m, const char *rodzaj, int32_t x, int32_t y, void (*wybrano)(int));

/* przeciaganie.c — dragging with the left button (0.8.2 step 6) */
void przec_wcisniecie(int32_t x, int32_t y, int wr, int mj); /* pressed over row `wr` or place `mj` */
int przec_ruch(int32_t x, int32_t y);       /* 1 while dragging (the pointer is the drag's) */
int przec_puszczenie(int32_t x, int32_t y); /* 1: it was dragged and dropped (not a click) */
int przec_trwa(void);
int przec_cel_wiersz(int i);   /* row i / place mj is where a drop goes now (drawn as the target) */
int przec_cel_miejsca(int mj);
void przec_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
void usun_zaznaczone(int32_t x, int32_t y); /* Delete, "Usuń": into Kosz, the window at (x, y) asks first (D8) */

/* zbiorczo.c — one request per entry, one after another; the footer says how it went */
void zbiorczo(int typ, const uint32_t *id, int n, uint32_t folder, int b); /* DOK_PRZENIES (b: keep both names) … */
int zbiorczo_trwa(void);
int zbiorczo_wynik(const struct k1_komunikat *k); /* DOK_WYNIK: 1 when it was the answer to one of them */

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
void szczegoly_zadaj(uint32_t id);   /* what the details of entry `id` need: the start of a text, a thumbnail */
extern char poczatek[K1_DOK_POCZATEK + 1];
extern int32_t miejsce_zajete, miejsce_calosc; /* the room for files, MiB (0.8.1 step 3) */
extern int miejsce_w_pamieci;      /* no K1-DANE: files are kept in memory (0.8.2 step 2) */
int malo_miejsca(void);            /* under a tenth of it left: the footer says so */
extern uint32_t poczatek_id, poczatek_dl;

/* pendrive.c */
enum { PEN_ZAMKNIETY = 1, PEN_CZEKA = 2, PEN_GOTOWY = 3 }; /* pen_stan; < 0: K1_PEN_BLAD_* */
extern int pen_jest;  /* nadzorca: a stick is plugged in (the place "Pendrive" is shown) */
extern int pen_stan;
extern struct k1_dok_wpis pen[];
extern int n_pen, importuje, pen_laduje; /* pen_laduje: the next list of the folder is on the way (0.8.2) */
void pendrive_start(void);
void pendrive_otworz(void);         /* the place "Pendrive" clicked */
void pendrive_wejdz(uint32_t id);   /* a folder on the stick */
int pendrive_w_gore_mozna(void);
void pendrive_w_gore(void);
/* the files selected, or every file of the folder shown; into `folder` (dropped on it), 0: pictures to Obrazy, others
 * to Dokumenty */
void pendrive_importuj(int wszystkie, uint32_t folder);
int pendrive_wynik_skarbca(const struct k1_komunikat *k); /* 1: the result of an import's DOK_NOWY */
const struct k1_dok_wpis *pen_wpis(uint32_t id);
uint32_t pen_folder(void);
int pen_sciezka(const char **nazwy, int maks); /* "Pendrive", then the folders entered */
const char *pendrive_stopka(void);

/* obrazy.c */
extern const char *tapeta_stan;    /* the wallpaper being set, or how it went (the footer; NULL: nothing) */
void obrazy_start(int skarbiec);   /* skarbiec: its channel (-1: none) */
int obraz_pliku(const struct k1_dok_wpis *w); /* a picture file of the user's (family "obrazy", not on the stick) */
void obraz_miniatura(uint32_t id); /* decode a thumbnail for the details (if not done already) */
void obraz_tapeta(uint32_t id);    /* decode and set as the wallpaper */
int obraz_tresc(const struct k1_komunikat *k, int fd); /* DOK_TRESC: 1 when it was a piece of a picture */
int obraz_wynik(const struct k1_komunikat *k);         /* DOK_WYNIK: 1 when a piece of a picture could not be read */
void obraz_kanal(int fd);          /* DEKODER_KANAL from nadzorca */
int32_t obraz_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t x, int32_t y);
int obraz_przycisk(struct k1_przycisk *p); /* "Ustaw jako tapetę": 1 when shown */

/* rysuj.c */
void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_szczegoly(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_stopke(struct k1_obraz *o, const struct k1_motyw *m);
void rysuj_cel(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h); /* a drop goes here */
enum k1_ikona ikona_wpisu(const struct k1_dok_wpis *w);

#endif

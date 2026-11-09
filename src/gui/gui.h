/* K1 gui — the desktop shell: state shared by its modules (0.7.5 split, AGENTS.md "Czytelność").
 *
 *   main.c            start, the loop, layout of the panels on the screen
 *   okna.c            the window list from ekran, starting / showing / minimising / closing applications
 *   pasek_gorny.c     the top bar (active application, battery, sound, clock) and the sound popover
 *   pasek_dolny.c     the bottom bar: Start, the pinned applications, the running ones that are not pinned
 *   menu_start.c      the Start menu
 *   pulpit.c          the desktop layer: icons in a grid (select, drag, double click)
 *   kontekst.c        the menu under the right mouse button (desktop, icon, bottom bar, Start menu)
 *   zapis.c           the desktop arrangement in skarbiec (fd 6)
 *   rozmieszczenie.c  the rules of the arrangement (pure, host tests)
 *
 * Every panel is one surface (P_*) drawn only when it is `brudna`; a module sets that flag on the
 * panels its change affects, main.c redraws them (odswiez). */
#ifndef K1_GUI_H
#define K1_GUI_H

#include <stdint.h>

#include "k1_klient.h"
#include "k1_okno.h"
#include "k1_uslugi.h"
#include "rozmieszczenie.h"

#define TAG "K1-GUI"
#define FD_NADZORCA 4
#define FD_BATERIA 5
#define FD_SKARBIEC 6

/* surfaces of the shell */
#define P_GORA 1
#define P_DOK 2
#define P_MENU 3
#define P_DZWIEK 4
#define P_PULPIT 5    /* the desktop layer (K1_POW_PULPIT) */
#define P_KONTEKST 6  /* the right-click menu, above every other panel (created last) */

#define MARG 16 /* shadow margin inside panel surfaces */

extern struct k1_klient k;
extern struct k1_powierzchnia gora, dok, menu, dzwiek, pulpit, kontekst;
extern int32_t dok_x, dok_y, menu_x, menu_y; /* positions of the panel surfaces on the screen */
extern struct rozmieszczenie rozm;           /* the desktop arrangement (zapis.c keeps it in skarbiec) */

/* The native applications (k1_uslugi.h): name, Start-menu description, icon, on the bar and the desktop at first. */
struct aplikacja_opis {
    const char *nazwa, *opis;
    enum k1_ikona ikona;
    int domyslna;
};
extern const struct aplikacja_opis aplikacje[K1_APL_MAKS + 1];

const struct k1_motyw *motyw(void);
/* Background of a floating panel: shadow, rounded body, hairline border (w×h inside the MARG margin). */
void tlo_panelu(struct k1_obraz *o, float w, float h);
/* The arrangement changed (pin, move, add, remove): save it and redraw the bar and the desktop. */
void rozm_zmienione(const char *co);

/* okna.c */
struct okno {
    uint32_t id;
    int apl, stan;
};
struct okno *okno_aplikacji(int apl);
int aktywna_aplikacja(void);
void zmiana_okna(const struct k1_komunikat *m); /* K1_KOM_OKNO from ekran */
void aplikacja(int apl);                         /* the bottom bar: start it, or show / minimise its window */
void otworz_aplikacje(int apl);                  /* the desktop and menus: start it, or show its window */
void zamknij_aplikacje(int apl);                 /* "Zamknij okno": ekran asks the application to close */
void do_nadzorcy(struct k1_komunikat m);

/* pasek_gorny.c */
void rysuj_gora(void);
void rysuj_dzwiek(void);
void odbierz_baterie(void); /* fd 5: battery and session state from nadzorca */
void wskaznik_gora(const struct k1_komunikat *m);
void pokaz_dzwiek(int tak);
int32_t glosnik_x(void);
#define DZW_SZER 300
#define DZW_WYS 84
extern int wyloguj_mozna; /* 1: K1 is installed here (K1_KOM_SESJA) — the Start menu offers "Wyloguj" */
extern int bat_kanal;     /* fd 5 still open */

/* pasek_dolny.c */
#define EL 44 /* bottom bar item */
#define ODST 4
#define DOK_PAD 8
#define DOK_WYS (EL + 2 * DOK_PAD)
/* the widest bar: every application, pinned or running (the surface; the bar itself is centred in it) */
#define DOK_SZER (DOK_PAD + EL + 13 + K1_APL_MAKS * (EL + ODST) + 9 + DOK_PAD)
void rysuj_dok(void);
void wskaznik_dok(const struct k1_komunikat *m);
void dok_zmiana(void); /* pinned or running applications changed: new positions, logged for the tests */
void dok_gotowy(void); /* "pulpit gotowy" with the positions of the bar (after laying the panels out) */

/* menu_start.c */
#define MENU_SZER 400
#define MENU_WIERSZ 56
#define MENU_NAGL 72
#define MENU_STOPKA 56
#define MENU_WYS (MENU_NAGL + K1_APL_MAKS * MENU_WIERSZ + 8 + MENU_STOPKA)
extern int menu_otwarte;
void rysuj_menu(void);
void wskaznik_menu(const struct k1_komunikat *m);
void pokaz_menu(int tak);

/* pulpit.c */
void pulpit_utworz(int pierwszy); /* the layer in the screen's size (again after a mode change) */
void rysuj_pulpit(void);
void wskaznik_pulpit(const struct k1_komunikat *m);
void pulpit_wszystko(void); /* redraw every icon (theme, arrangement) */
int pulpit_kolumn(void);
int pulpit_wierszy(void);

/* kontekst.c */
enum { K_PULPIT = 1, K_IKONA, K_PASEK, K_MENU };
#define KONT_SZER 232
#define KONT_WYS 168
extern int kontekst_otwarte;
void kontekst_otworz(int cel, int apl, int32_t x, int32_t y); /* x, y: the pointer on the screen */
void kontekst_zamknij(void);
void rysuj_kontekst(void);
void wskaznik_kontekst(const struct k1_komunikat *m);

/* zapis.c */
void rozm_wczytaj(void); /* at start: from skarbiec, else the first-start arrangement */
void rozm_zapisz_teraz(void);
void odbierz_skarbiec(void);
extern int skarb_fd; /* fd 6, -1 without skarbiec */

#endif

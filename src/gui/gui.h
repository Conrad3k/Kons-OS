/* K1 gui — the desktop shell: state shared by its modules (0.7.5 split, AGENTS.md "Czytelność").
 *
 *   main.c         start, the loop, layout of the panels on the screen
 *   okna.c         the window list from ekran, starting / showing / minimising applications
 *   pasek_gorny.c  the top bar (active application, battery, sound, clock) and the sound popover
 *   pasek_dolny.c  the bottom bar (Start + applications)
 *   menu_start.c   the Start menu
 *
 * Every panel is one surface (P_*) drawn only when it is `brudna`; a module sets that flag on the
 * panels its change affects, main.c redraws them (odswiez). */
#ifndef K1_GUI_H
#define K1_GUI_H

#include <stdint.h>

#include "k1_klient.h"
#include "k1_okno.h"
#include "k1_uslugi.h"

#define TAG "K1-GUI"
#define FD_NADZORCA 4
#define FD_BATERIA 5

/* surfaces (panels) of the shell */
#define P_GORA 1
#define P_DOK 2
#define P_MENU 3
#define P_DZWIEK 4

#define MARG 16 /* shadow margin inside panel surfaces */

extern struct k1_klient k;
extern struct k1_powierzchnia gora, dok, menu, dzwiek;
extern int32_t dok_x, dok_y, menu_x, menu_y; /* positions of the panel surfaces on the screen */

/* The native applications (k1_uslugi.h): name, Start-menu description, icon, on the bottom bar. */
struct aplikacja_opis {
    const char *nazwa, *opis;
    enum k1_ikona ikona;
    int pasek;
};
extern const struct aplikacja_opis aplikacje[K1_APL_MAKS + 1];

const struct k1_motyw *motyw(void);
/* Background of a floating panel: shadow, rounded body, hairline border (w×h inside the MARG margin). */
void tlo_panelu(struct k1_obraz *o, float w, float h);

/* okna.c */
struct okno {
    uint32_t id;
    int apl, stan;
};
struct okno *okno_aplikacji(int apl);
int aktywna_aplikacja(void);
void zmiana_okna(const struct k1_komunikat *m); /* K1_KOM_OKNO from ekran */
void aplikacja(int apl);                         /* start it, or show / minimise its window */
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
#define X_PASEK(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek) +(pasek)
enum { DOK_APL = 0 K1_APLIKACJE(X_PASEK) }; /* applications on the bottom bar (all are in the menu) */
#define DOK_SZER (DOK_PAD + EL + 13 + DOK_APL * EL + (DOK_APL - 1) * ODST + DOK_PAD)
void rysuj_dok(void);
void wskaznik_dok(const struct k1_komunikat *m);
int32_t element_x(int i); /* 0 = Start, 1..: applications (relative to the bar) */

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

#endif

/* K1 gui — the desktop shell: state shared by its modules (0.7.5 split, AGENTS.md "Czytelność").
 *
 *   main.c            start, the loop, layout of the panels on the screen
 *   okna.c            the window list from ekran, starting / showing / minimising / closing applications
 *   pasek_gorny.c     the top bar (active application, battery, sound, clock) and the sound popover
 *   pasek_dolny.c     the bottom bar: Start, the pinned applications, the running ones that are not pinned
 *   menu_start.c      the Start menu
 *   pulpit.c          the desktop layer: icons in a grid (select, drag, double click)
 *   pliki.c           the files of the Pulpit folder and Kosz on the desktop (skarbiec, fd 6; 0.8.0)
 *   kontekst.c        the menu under the right mouse button (desktop, icon, bottom bar, Start menu)
 *   sesje.c           "Którą sesję otworzyć?" when the browser opens (0.10.1 step 4), on the panel of kontekst.c
 *   zapis.c           the desktop arrangement in skarbiec (fd 6)
 *   rozmieszczenie.c  the rules of the arrangement (pure, host tests)
 *
 * Every panel is one surface (P_*) drawn only when it is `brudna`; a module sets that flag on the
 * panels its change affects, main.c redraws them (odswiez). */
#ifndef K1_GUI_H
#define K1_GUI_H

#include <stdint.h>

#include "k1_klient.h"
#include "k1_menu.h"
#include "k1_okno.h"
#include "k1_opis_klient.h"
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
    int strefa_w_menu;       /* a zone with a line in the Start menu (K1_STREFY, 0.10.0 step 2: the browser) */
};
/* The items the bar and the window list know: the applications 1..K1_APL_MAKS, then the zones (0.10.0, ADR 0042):
 * zone n is item K1_APL_MAKS + n — its title and icon from K1_STREFY, never pinned (-1); in the Start menu only when
 * its row says so (strefa_w_menu). */
#define POZYCJA_STREFY(n) (K1_APL_MAKS + (n))
#define POZYCJE_MAKS (K1_APL_MAKS + K1_STREFY_MAKS)
extern const struct aplikacja_opis aplikacje[POZYCJE_MAKS + 1];

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
void pokaz_aplikacje(int apl);                    /* nadzorca (POKAZ_APL): its window to the front, if it has one */
void otworz_pliki(uint32_t wpis, int co);         /* Pliki at a file or folder (0: none), at Kosz, a name to change:
                                                    * co = K1_PLIKI_* (0.8.0; K1_PLIKI_NAZWA 0.8.2) */
void zamknij_aplikacje(int apl);                 /* "Zamknij okno": ekran asks the application to close */
void do_nadzorcy(struct k1_komunikat m);

/* pasek_gorny.c */
void rysuj_gora(void);
void rysuj_dzwiek(void);
void odbierz_baterie(void); /* fd 5: battery and session state from nadzorca */
void strefa_stan(const struct k1_komunikat *m); /* fd 5 (0.10.0): a zone did not start — the window that says why */
/* fd 5 (0.10.0 step 5): the sessions as the user set them — their names (Start menu, the zone's window on the bar) */
void strefy_sesje(const struct k1_komunikat *m);
const char *nazwa_sesji(int nr); /* the session's name of zone nr ("" for a zone without one) */
int sesja_pamieta(int nr);        /* zone nr remembers (STREFY_STAN a; 0 without K1-DANE — 0.10.1 step 4) */
int sesja_zamyka_sie(int nr);     /* zone nr is closing in the background (STREFY_STAN e — 0.10.2 step 1) */
/* The name of item `apl` (an application, or a zone: "Przeglądarka · Bank") into `buf`. */
const char *nazwa_pozycji(int apl, char *buf, size_t n);
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
#define DOK_SZER (DOK_PAD + EL + 13 + POZYCJE_MAKS * (EL + ODST) + 9 + DOK_PAD)
void rysuj_dok(void);
void wskaznik_dok(const struct k1_komunikat *m);
void dok_zmiana(void); /* pinned or running applications changed: new positions, logged for the tests */
void dok_gotowy(void); /* "pulpit gotowy" with the positions of the bar (after laying the panels out) */

/* menu_start.c */
#define MENU_SZER 400
#define MENU_WIERSZ 56
#define MENU_NAGL 72
#define MENU_STOPKA 56
/* rows of the Start menu: every application but the windows of K1 itself (column -1 of K1_APLIKACJE, ADR 0030), then
 * the zones whose K1_STREFY row has a menu line (0.10.0 step 2: the browser) */
#define X_W_MENU(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek, ...) +((pasek) >= 0)
#define X_W_MENU_Z(n, nr, profil, uid, obraz, tytul, sesja, pamiec, procesory, kolor, trwalosc, ikona, log, menu) \
    +(sizeof(menu) > 1)
enum { MENU_APL = 0 K1_APLIKACJE(X_W_MENU) K1_STREFY(X_W_MENU_Z) };
#undef X_W_MENU
#undef X_W_MENU_Z
#define MENU_WYS (MENU_NAGL + MENU_APL * MENU_WIERSZ + 8 + MENU_STOPKA)
extern int menu_otwarte;
void rysuj_menu(void);
void wskaznik_menu(const struct k1_komunikat *m);
void pokaz_menu(int tak);

/* pulpit.c */
void pulpit_utworz(int pierwszy); /* the layer in the screen's size (again after a mode change) */
void rysuj_pulpit(void);
void wskaznik_pulpit(const struct k1_komunikat *m);
void pulpit_wszystko(void); /* redraw every icon (theme, arrangement) */
void pulpit_loguj(void);    /* "ikony na pulpicie": where each icon is (the tests click by it) */
int pulpit_kolumn(void);
int pulpit_wierszy(void);

/* pliki.c: what an icon shows and does, whatever it is (an application, Kosz, a file — rozmieszczenie.h keys; and
 * "Jeszcze n" — the files of Pulpit that did not fit, never saved, 0.8.2 step 6) */
#define KLUCZ_WIECEJ 0x2000u
int pliki_ukryte(void);  /* files of Pulpit without an icon (L19) */
void pliki_dopasuj(void); /* the icons follow the files (again after "Ułóż ikony") */
/* A file dropped on Kosz or a folder of Pulpit (0.8.2 step 6): DOK_KOSZ / DOK_PRZENIES; skarbiec's refusal: the window
 * of a question or a refusal at (x, y) on the screen. */
void plik_przenies(uint32_t klucz, uint32_t folder_klucz, int32_t x, int32_t y);
int plik_typ(uint32_t klucz, int *folder);               /* a file's type (0: not a file; *folder: 1 for a folder) */
void plik_otworz_w(uint32_t klucz, int apl);             /* "Otwórz za pomocą": skarbiec gives it the file (ADR 0030) */
void plik_pokaz(uint32_t klucz, int co);                 /* Pliki at this file (K1_PLIKI_MIEJSCE) or its name (_NAZWA) */
void pliki_start(void);                                   /* asks skarbiec for the files of the Pulpit folder */
void pliki_odbierz(const struct k1_komunikat *m, int fd); /* DOK_* from skarbiec on fd 6 */
const char *ikona_nazwa(uint32_t klucz);
enum k1_ikona ikona_rysunek(uint32_t klucz);
void ikona_otworz(uint32_t klucz, int32_t x, int32_t y); /* x, y: where a message goes (no application for the file) */
void plik_do_kosza(uint32_t klucz);
const char *plik_nazwa_folderu(uint32_t klucz); /* a folder of Pulpit: its name (NULL: not one) */
void kosz_oproznij(void);
int kosz_pelny(void);

/* kontekst.c */
enum { K_PULPIT = 1, K_IKONA, K_PASEK, K_MENU, K_KOMUNIKAT, K_OKIENKO, K_WLASNE };
#define KONT_SZER (K1_MENU_SZER + K1_MENU_SZER_POD) /* the panel: the menu and its submenu side by side */
#define KONT_WYS K1_MENU_WYS_MAKS
extern int kontekst_otwarte;
void kontekst_otworz(int cel, uint32_t klucz, int32_t x, int32_t y); /* x, y: the pointer on the screen */
void kontekst_zamknij(void);
/* The window of a question or a refusal after dragging (k1_przeciaganie, 0.8.2 step 6) at (x, y) on the screen: the
 * answer (K1_PO_*) goes to `wybrano`, K1_PO_ANULUJ when it closes otherwise; `rodzaj` names it in the log. */
void kontekst_okienko(const struct k1_menu *m, const char *rodzaj, int32_t x, int32_t y, void (*wybrano)(int));
void rysuj_kontekst(void);
void wskaznik_kontekst(const struct k1_komunikat *m);
/* A window another module draws on this panel (0.10.1): its top left at (x, y) on the screen, what it logs for the tests,
 * the pointer in its coordinates (it closes itself: kontekst_zamknij). It must fit KONT_SZER × KONT_WYS. */
struct okienko_wlasne {
    void (*rysuj)(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y);
    void (*loguj)(int32_t x, int32_t y);
    void (*wskaznik)(int rodzaj, int przycisk, int32_t x, int32_t y);
};
void kontekst_wlasne(const struct okienko_wlasne *w, uint32_t klucz, int32_t x, int32_t y);

/* sesje.c (0.10.1 step 4, D4: sketch B) */
void sesje_wybierz(int apl); /* opening the browser (item apl): the window of its sessions, or the only one at once */

/* pamiec.c (0.10.1 step 3, D3: window A) — nadzorca on fd 5 */
void pamiec_przydzial(const struct k1_komunikat *m); /* STREFA_PRZYDZIAL: a session's share */
void pamiec_malo(const struct k1_komunikat *m);      /* MALO_PAMIECI: the window with the sessions */
void pamiec_fokus(void);                             /* the windows changed: the zone with the focus to nadzorca */

/* wersja.c (0.10.5 step 1, S5) — nadzorca on fd 5 */
void wersja_nieudana(const struct k1_komunikat *m); /* AB_NIEUDANA: the window "Nowa wersja K1 nie uruchomiła się" */

/* zapis.c */
void rozm_wczytaj(void); /* at start: from skarbiec, else the first-start arrangement */
void rozm_zapisz_teraz(void);
void odbierz_skarbiec(void);
extern int skarb_fd; /* fd 6, -1 without skarbiec */

#endif

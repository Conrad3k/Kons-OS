/* K1 gui — the desktop shell: state shared by its modules (0.7.5 split, AGENTS.md "Czytelność").
 *
 *   main.c            start, the loop, layout of the panels on the screen
 *   okna.c            the window list from ekran, starting / showing / minimising / closing applications
 *   pasek_gorny.c     the top bar (active application; network, battery, sound in a pill; clock) and its popovers
 *   bateria.c         the battery: its state from nadzorca, its cell on the top bar and its popover (0.11.8 step 3)
 *   pasek_dolny.c     the bottom bar: Start, the pinned applications, the running ones that are not pinned
 *   menu_start.c      the Start menu (its list scrolls, 0.11.8 step 1)
 *   pulpit.c          the desktop layer: icons in a grid (select, drag, double click)
 *   pliki.c           the files of the Pulpit folder and Kosz on the desktop (skarbiec, fd 6; 0.8.0)
 *   kontekst.c        the menu under the right mouse button (desktop, icon, bottom bar, Start menu)
 *   sesje.c           "Którą sesję otworzyć?" when the browser opens (0.10.1 step 4), on the panel of kontekst.c
 *   przelacznik.c     the window switcher, Alt+Tab (0.10.5 step 6), on the panel of kontekst.c
 *   zapis.c           the desktop arrangement in skarbiec (fd 6)
 *   ochrona.c         K1's windows of protection: the TPM's, a new USB device, "Ustaw hasło" (0.13.0)
 *   zabezpieczenia.c  the security screen of Ctrl+Alt+Del (0.13.0 step 6), haslo.c its password form
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
#define P_OKIENKO_PASKA 4 /* the popovers under the top bar's icons (0.11.8 step 2; until then the sound one only) */
#define P_PULPIT 5    /* the desktop layer (K1_POW_PULPIT) */
#define P_KONTEKST 6  /* the right-click menu, above every other panel (created last) */
#define P_ZABEZPIECZENIA 7 /* the security screen (K1_POW_ZABEZPIECZENIA; 0.13.0 step 6): ekran keeps it above all */

#define MARG K1_PANEL_MARGINES /* shadow margin inside panel surfaces (k1_okno.h: ekran's work area too) */

extern struct k1_klient k;
extern struct k1_powierzchnia gora, dok, menu, okienko_paska, pulpit, kontekst;
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
 * zone n is item K1_APL_MAKS + n — the window of one session, its title and icon from K1_STREFY, never pinned (-1);
 * then (0.10.5 step 6, D12, T17) the Start-menu lines of the zones: item POZYCJA_MENU_STREFY(n) for a zone whose row
 * has one (strefa_w_menu: "Przeglądarka") stands for every session with its image — in the Start menu, pinned on the
 * bar (key ROZM_STREFA + n), an icon on the desktop; opening it asks which session (sesje.c). */
#define POZYCJA_STREFY(n) (K1_APL_MAKS + (n))
#define POZYCJA_MENU_STREFY(n) (K1_APL_MAKS + K1_STREFY_MAKS + (n))
#define POZYCJE_MAKS (K1_APL_MAKS + 2 * K1_STREFY_MAKS)
extern const struct aplikacja_opis aplikacje[POZYCJE_MAKS + 1];
int strefa_pozycji(int apl);       /* the zone of a session's window or of a Start-menu line, else 0 */
int menu_strefy(int apl);          /* 1: `apl` is a zone's Start-menu line (Przeglądarka) */
uint32_t klucz_pozycji(int apl);   /* its key in rozmieszczenie.h (an application, ROZM_STREFA + n), else 0 */
int pozycja_klucza(uint32_t klucz); /* back: the item of an application's or a Start-menu line's key, else 0 */

const struct k1_motyw *motyw(void);
/* Background of a floating panel: shadow, rounded body, hairline border (w×h inside the MARG margin). */
void tlo_panelu(struct k1_obraz *o, float w, float h);
/* The arrangement changed (pin, move, add, remove): save it and redraw the bar and the desktop. */
void rozm_zmienione(const char *co);

/* okna.c */
struct okno {
    uint32_t id;
    int apl, stan;
    unsigned uzyte; /* when it last became active (a counter): the order of Alt+Tab */
};
struct okno *okno_aplikacji(int apl);
/* The window of item `apl`; for a Start-menu line the active one of its sessions, else any of them (0.10.5 step 6). */
const struct okno *okno_pozycji(int apl);
int okna_ostatnio_uzyte(struct okno *wy, int maks); /* the windows, the one used most recently first; how many */
int aktywna_aplikacja(void);
void zmiana_okna(const struct k1_komunikat *m); /* K1_KOM_OKNO from ekran */
void aplikacja(int apl);                         /* the bottom bar: start it, or show / minimise its window */
void otworz_aplikacje(int apl);                  /* the desktop and menus: start it, or show its window
                                                  * (a zone's Start-menu line: which session — sesje.c) */
void pokaz_aplikacje(int apl);                    /* nadzorca (POKAZ_APL): its window to the front, if it has one */
void otworz_pliki(uint32_t wpis, int co);         /* Pliki at a file or folder (0: none), at Kosz, a name to change:
                                                    * co = K1_PLIKI_* (0.8.0; K1_PLIKI_NAZWA 0.8.2) */
void zamknij_aplikacje(int apl);                 /* "Zamknij okno": ekran asks the application to close */
void do_nadzorcy(struct k1_komunikat m);

/* pasek_gorny.c */
void rysuj_gora(void);
void rysuj_okienko_paska(void);
void odbierz_od_nadzorcy(void); /* fd 5: battery, session, network, zones, updates from nadzorca */
void strefa_stan(const struct k1_komunikat *m); /* fd 5 (0.10.0): a zone did not start — the window that says why */
/* fd 5 (0.10.0 step 5): the sessions as the user set them — their names (Start menu, the zone's window on the bar) */
void strefy_sesje(const struct k1_komunikat *m);
const char *nazwa_sesji(int nr); /* the session's name of zone nr ("" for a zone without one) */
int sesja_pamieta(int nr);        /* zone nr remembers (STREFY_STAN a; 0 without K1-DANE — 0.10.1 step 4) */
int sesja_zamyka_sie(int nr);     /* zone nr is closing in the background (STREFY_STAN e — 0.10.2 step 1) */
/* The name of item `apl` (an application, or a zone: "Przeglądarka · Bank") into `buf`. */
const char *nazwa_pozycji(int apl, char *buf, size_t n);
void wskaznik_gora(const struct k1_komunikat *m);
void okienko_paska_zamknij(void); /* the popover under an icon of the bar, if one is open (0.11.8 step 2) */
int32_t dzwiek_srodek(void);       /* the middle of the sound icon on the bar (the tests' log) */
/* dzwiek.c — 0.12.0 step 1 (ADR 0054; D2: B): the speaker on the bar, the popover "Dźwięk", the channel to dzwiek */
int dzwiek_fd(void);            /* the channel to the service, for poll (-1) */
void dzwiek_kanal_nowy(int fd); /* DZWIEK_KANAL from nadzorca (fd 5): the caller closes `fd` */
void dzwiek_odbierz(void);
void dzwiek_wczytaj(void);      /* step 2 (D5): the remembered volume from skarbiec, at the start */
void dzwiek_okresowe(double t); /* the remembered volume written a moment after its last change */
int dzwiek_klawisz(const struct k1_komunikat *m); /* a volume key from ekran: 1 taken (D4: ekran's pill) */
void dzwiek_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem);
#define DZWIEK_OKIENKO_SZER 300
#define DZWIEK_OKIENKO_WYS 238 /* without the list "Grają teraz" (0.12.0 step 3: each session that plays adds a row);
                                * 0.12.3 step 2: with the row "Korektor"; step 4: and the row "Kons Spatial Audio" */
int32_t dzwiek_okienko_wys(void);  /* its height now */
void okienko_paska_wysokosc(void); /* pasek_gorny.c: the sound's popover changed its height while open */
void dzwiek_sesja_wycisz(int nr);  /* step 3: ekran's STREFA_WYCISZ (the speaker on a session's bar was clicked) */
void dzwiek_okienko(struct k1_obraz *o, const struct k1_motyw *m);
void dzwiek_okienko_wskaznik(const struct k1_komunikat *m);
void dzwiek_okienko_otwarte(int tak);
#define OKIENKO_PASKA_SZER 300     /* the largest popover of the bar: the surface's size (the sound's width, */
#define OKIENKO_PASKA_WYS 202      /* the battery's height — bateria.c) */
void wskaznik_okienka_paska(const struct k1_komunikat *m);
extern int zablokuj_mozna; /* 1: a password is set (K1_KOM_SESJA) — the Start menu offers "Zablokuj" (0.11.5) */
extern int bat_kanal;     /* fd 5 still open */

/* bateria.c */
void bateria_komunikat(const struct k1_komunikat *m); /* K1_KOM_BATERIA from nadzorca (fd 5) */
int32_t bateria_szer(void);                           /* the battery's cell on the bar; 0: no battery */
void bateria_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem);
/* 0.11.8 step 3 (D4: A): the popover under the battery — percent, state, time left / to full, power draw, the button
 * "Ustawienia baterii"; its numbers come from nadzorca (K1_KOM_BATERIA_POMIAR on fd 5) */
#define BATERIA_OKIENKO_SZER 272
#define BATERIA_OKIENKO_WYS 202
void bateria_pomiar(const struct k1_komunikat *m);
void bateria_okienko_otwarte(int tak); /* the popover opened / closed: nadzorca reads the battery more often meanwhile */
void bateria_okienko(struct k1_obraz *o, const struct k1_motyw *m);
void bateria_okienko_wskaznik(const struct k1_komunikat *m); /* coordinates of the popover's surface */

/* pasek_dolny.c */
#define EL 44 /* bottom bar item */
#define ODST 4
#define DOK_PAD 8
#define DOK_WYS (EL + 2 * DOK_PAD)
/* the widest bar: every application, pinned or running (the surface; the bar itself is centred in it) — zones with a
 * window only (the download zone of 0.10.5 never has one on the bar) */
#define DOK_SZER (DOK_PAD + EL + 13 + (K1_APL_MAKS + K1_STREFY_Z_OKNEM) * (EL + ODST) + 9 + DOK_PAD)
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
#define X_W_MENU_Z(n, nr, profil, uid, obraz, tytul, sesja, pamiec, procesory, kolor, trwalosc, ikona, log, menu, ...) \
    +(sizeof(menu) > 1)
enum { MENU_APL = 0 K1_APLIKACJE(X_W_MENU) K1_STREFY(X_W_MENU_Z) };
#undef X_W_MENU
#undef X_W_MENU_Z
/* 0.11.8 step 1 (D1: A): the list shows MENU_WIDAC rows, the rest scrolls; on a low screen fewer, MENU_WIDAC_MIN at least */
#define MENU_WIDAC 7
#define MENU_WIDAC_MIN 3
#define MENU_LISTA_MAKS (MENU_APL < MENU_WIDAC ? MENU_APL : MENU_WIDAC)
#define MENU_WYS (MENU_NAGL + MENU_LISTA_MAKS * MENU_WIERSZ + 8 + MENU_STOPKA) /* the surface: the tallest menu */
extern int menu_otwarte;
extern int menu_widac;                         /* rows of the list visible on this screen */
void menu_dopasuj(int32_t wys_ekranu);          /* how many rows fit between the bars (again after a mode change) */
void rysuj_menu(void);
void wskaznik_menu(const struct k1_komunikat *m);
void przewin_menu(const struct k1_komunikat *m); /* K1_KOM_PRZEWIN over the menu: the list scrolls */
void pokaz_menu(int tak);

/* pulpit.c */
void pulpit_utworz(int pierwszy); /* the layer in the screen's size (again after a mode change) */
void rysuj_pulpit(void);
void wskaznik_pulpit(const struct k1_komunikat *m);
/* 0.11.0 step 4 (ADR 0052): dragging between windows, led by ekran */
void pulpit_gdzie(int poza);                            /* PRZECIAGANIE_GDZIE: the file dragged is over another window */
void pulpit_koniec(int cel, uint32_t b);                /* PRZECIAGANIE_KONIEC: dropped on a session's window */
void pulpit_upuszczanie(const struct k1_komunikat *m); /* UPUSZCZANIE_CO, UPUSZCZANIE: files of Pliki over the desktop */
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
void plik_do_sesji(uint32_t klucz, int strefa, int otworz); /* 0.11.0 step 3: "Wyślij do", a session in "Otwórz za pomocą" */
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
/* the panel: the menu and its submenu side by side — or the window switcher (przelacznik.c: PRZEL_KOLUMN tiles in a
 * row, PRZEL_WIERSZY rows), whichever is larger */
#define PRZEL_KAF_W 120
#define PRZEL_KAF_H 104
#define PRZEL_ODST 8
#define PRZEL_PAD 16
#define PRZEL_KOLUMN 6
#define PRZEL_WIERSZY 3
#define PRZEL_SZER (2 * PRZEL_PAD + PRZEL_KOLUMN * PRZEL_KAF_W + (PRZEL_KOLUMN - 1) * PRZEL_ODST)
#define PRZEL_WYS (2 * PRZEL_PAD + PRZEL_WIERSZY * PRZEL_KAF_H + (PRZEL_WIERSZY - 1) * PRZEL_ODST)
#define KONT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define KONT_SZER KONT_MAX(K1_MENU_SZER + K1_MENU_SZER_POD, PRZEL_SZER)
#define KONT_WYS KONT_MAX(K1_MENU_WYS_MAKS, PRZEL_WYS)
extern int kontekst_otwarte;
void kontekst_otworz(int cel, uint32_t klucz, int32_t x, int32_t y); /* x, y: the pointer on the screen */
void kontekst_zamknij(void);
/* The window of a question or a refusal after dragging (k1_przeciaganie, 0.8.2 step 6) at (x, y) on the screen: the
 * answer (K1_PO_*) goes to `wybrano`, K1_PO_ANULUJ when it closes otherwise; `rodzaj` names it in the log. */
void kontekst_okienko(const struct k1_menu *m, const char *rodzaj, int32_t x, int32_t y, void (*wybrano)(int));
void rysuj_kontekst(void);
void wskaznik_kontekst(const struct k1_komunikat *m);
/* A window another module draws on this panel (0.10.1): its top left at (x, y) on the screen, what it logs for the tests,
 * the pointer in its coordinates (it closes itself: kontekst_zamknij), its name in the screen's description (NULL:
 * "Okienko"). It must fit KONT_SZER × KONT_WYS. */
struct okienko_wlasne {
    void (*rysuj)(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y);
    void (*loguj)(int32_t x, int32_t y);
    void (*wskaznik)(int rodzaj, int przycisk, int32_t x, int32_t y);
    const char *nazwa;
};
void kontekst_wlasne(const struct okienko_wlasne *w, uint32_t klucz, int32_t x, int32_t y);
int kontekst_pokazuje(const struct okienko_wlasne *w); /* 1: that window is on the panel now */

/* sesje.c (0.10.1 step 4, D4: sketch B) */
void sesje_wybierz(int apl); /* opening the browser (item apl): the window of its sessions, or the only one at once */

/* przelacznik.c (0.10.5 step 6, D11: sketch A) — ekran gives the shell Tab while Alt is held, Esc, Alt's release */
void przelacznik_klawisz(const struct k1_komunikat *m);

/* pamiec.c (0.10.1 step 3, D3: window A) — nadzorca on fd 5 */
void pamiec_przydzial(const struct k1_komunikat *m); /* STREFA_PRZYDZIAL: a session's share */
void pamiec_malo(const struct k1_komunikat *m);      /* MALO_PAMIECI: the window with the sessions */
void pamiec_fokus(void);                             /* the windows changed: the zone with the focus to nadzorca */

/* wersja.c (0.10.5 step 1, S5) — nadzorca on fd 5 */
void wersja_nieudana(const struct k1_komunikat *m); /* AB_NIEUDANA: the window "Nowa wersja K1 nie uruchomiła się" */
void ochrona_stan(const struct k1_komunikat *m); /* OCHRONA: the TPM's windows, the USB question (ochrona.c, 0.13.0) */
void ochrona_bez_hasla(int bez_hasla);           /* SESJA b (0.13.0 step 5): "Ustaw hasło" on the first start */
/* zabezpieczenia.c (0.13.0 step 6, ADR 0061, D8: A) — Ctrl+Alt+Del: the menu and the password's form */
extern struct k1_powierzchnia zab;
void zab_utworz(int pierwszy);                  /* the panel (hidden); after a mode change: centred again */
void zab_otworz(int na_hasle);                  /* shown — on the menu, or on the password's form */
int zab_otwarty(void);
void rysuj_zab(void);
void zab_klawisz(const struct k1_komunikat *m); /* keys on P_ZABEZPIECZENIA (Ctrl+Alt+Del opens it) */
void wskaznik_zab(const struct k1_komunikat *m);
void zab_wynik_skarbca(const struct k1_komunikat *w); /* SKARB_WYNIK on fd 6: the vault's state, the password's result */
/* haslo.c — the password's form (since 0.13.0 step 6 on the security screen; M5e–0.13.0 step 5 in Ustawienia) */
enum { HF_BRAK, HF_USTAW, HF_ZMIEN, HF_WYLACZ };
struct haslo_stan {
    int znane, tryb, wlaczone, usb; /* the vault as skarbiec said (K1_SKARB_* tryb) */
    int formularz, pole, czeka, blad;
    char komunikat[120];
};
extern struct haslo_stan hasla;
void haslo_info(void);                 /* SKARB_INFO: the state comes as SKARB_WYNIK */
int haslo_pola(const char **nazwy);   /* the fields of the form: how many, their labels */
size_t haslo_znaki(int pole);          /* characters typed in a field (the dots) */
void haslo_formularz(int f);           /* HF_*: a new form, what was typed wiped */
void haslo_wyslij(void);
int haslo_wynik(const struct k1_komunikat *w); /* 1: the password was changed */
int haslo_klawisz(unsigned kod, int wartosc, unsigned mody); /* 1: taken */
/* aktualizacja.c (0.10.5 step 2b): AKT_STAN, AKT_OPIS — the window "Jest nowa wersja K1", the dot at Start */
void akt_komunikat(const struct k1_komunikat *m, int fd);
int akt_czeka(void); /* 1: a new version waits for "Zainstaluj" */

/* pobranie.c (0.11.0 step 2, ADR 0052, D2: A) — nadzorca on fd 5: a file from a session, the window that asks */
void pobranie_komunikat(const struct k1_komunikat *m, int fd); /* POBRANIE (with the name's memfd), POBRANIE_WYNIK */
void pobranie_okresowe(void);                                  /* every loop: a window covered by a menu comes back */

/* zapis.c */
void rozm_wczytaj(void); /* at start: from skarbiec, else the first-start arrangement */
void rozm_zapisz_teraz(void);
void odbierz_skarbiec(void);
extern int skarb_fd; /* fd 6, -1 without skarbiec */
extern int rozm_pierwszy_start; /* 1: skarbiec had no arrangement — the first start (0.13.0 step 5: "Ustaw hasło") */

#endif

/* K1 przybornik — the menu under the right mouse button (0.8.2 step 5, L22): one look and one list of rows for the
 * desktop (gui, its own panel) and Pliki (drawn in its window). The look is the desktop's from 0.7.5 (ADR 0026, sketch
 * A "Kafelki"): a rounded panel in the window colour of the theme, rows of 36 px, a hairline between groups.
 *
 * What a file's menu offers is decided here once (k1_menu_pliku, k1_menu_za_pomoca, k1_menu_bez_aplikacji): the
 * caller only says where the file is and does what the chosen row says (K1_MA_*). Pure functions without system calls
 * or allocation — tested on the host (testy/host/test_menu.c). */
#ifndef K1_MENU_H
#define K1_MENU_H

#include <stdint.h>

#include "k1_komunikat.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

#define K1_MENU_SZER 232     /* the width of a menu */
#define K1_MENU_SZER_POD 280 /* "Otwórz za pomocą": "Zawsze otwieraj tą aplikacją" needs more */
#define K1_MENU_SZER_PYTANIE 300 /* the window of a question when dragging (0.8.2 step 6, k1_przeciaganie) */
#define K1_MENU_WIERSZ 36
#define K1_MENU_KRESKA 9
#define K1_MENU_LINIA 18     /* a line of small text (a hint under a muted row, a message) */
#define K1_MENU_MARG 16      /* room for the shadow around the body */
#define K1_MENU_MAKS 10      /* rows at most */
#define K1_MENU_WYS_MAKS 340 /* the highest menu of a file or message (panels of the desktop are this high) */

/* A file no application opens: the message at its icon on the desktop (double click), and the short one — the
 * footer of Pliki (double click, "Otwórz") and the hint under a muted "Otwórz" in the menu. */
#define K1_MENU_BEZ_APLIKACJI_KROTKO "K1 nie ma jeszcze aplikacji do tego typu pliku"
#define K1_MENU_BEZ_APLIKACJI K1_MENU_BEZ_APLIKACJI_KROTKO ". Możesz go przechowywać, przenosić i usuwać."

/* a row to click / a hairline / text only (small, wrapped; `mocna`: the title — Medium 15, wrapped too) */
enum k1_menu_styl { K1_MS_WIERSZ = 0, K1_MS_KRESKA, K1_MS_TEKST };

struct k1_menu_poz {
    char tekst[256]; /* a question names a folder and a file (0.8.2 step 6) */
    const char *podpowiedz; /* under a muted row: why it is muted (small text, wrapped) */
    int akcja, arg;         /* what the caller does (K1_MA_* for a file), e.g. arg = an application */
    uint8_t styl, wyszarzona, mocna, akcent, podmenu, zaznaczona;
};

struct k1_menu {
    struct k1_menu_poz p[K1_MENU_MAKS];
    int n;
    int32_t szer;
};

void k1_menu_zacznij(struct k1_menu *m, int32_t szer);
/* A row (never NULL: a full menu returns a row nobody sees). */
struct k1_menu_poz *k1_menu_dodaj(struct k1_menu *m, const char *tekst, int akcja, int arg);
void k1_menu_kreska(struct k1_menu *m);
struct k1_menu_poz *k1_menu_tekst(struct k1_menu *m, const char *tekst); /* text only, wrapped to the width */

int32_t k1_menu_wys(const struct k1_menu *m);
int32_t k1_menu_y(const struct k1_menu *m, int i); /* the top of row i in the body */
/* Draws the body with its top left at (x, y); the shadow needs K1_MENU_MARG around it. `najechana`: -1 none. */
void k1_menu_rysuj(struct k1_obraz *o, const struct k1_motyw *mt, const struct k1_menu *m, int32_t x, int32_t y,
                   int najechana);
/* Its rows as elements of the screen's description (0.10.2 step 2; k1_menu_rysuj does it — a caller whose rows hold
 * the user's text, a session's name, draws the menu and then gives a copy with neutral names). */
void k1_menu_el(const struct k1_menu *m, int32_t x, int32_t y);
/* The row to click under (x, y) relative to the body (not a hairline, text, nor a muted row), or -1. */
int k1_menu_pod(const struct k1_menu *m, int32_t x, int32_t y);
/* "Otwórz 120,300; Usuń 120,336": the centre of every row to click, the body at (x, y) — the tests click by it. */
void k1_menu_opis(const struct k1_menu *m, int32_t x, int32_t y, char *buf, size_t n);

/* Where the body goes: at the pointer (px, py), to the left of it / above it when it would leave the area w × h. */
void k1_menu_polozenie(const struct k1_menu *m, int32_t px, int32_t py, int32_t w, int32_t h, int32_t *x, int32_t *y);
/* Where the submenu of row `i` goes (the menu's body at x, y): beside it on the right, or on the left without room. */
void k1_menu_podmenu_polozenie(const struct k1_menu *m, int i, const struct k1_menu *pod, int32_t x, int32_t y,
                               int32_t w, int32_t h, int32_t *px, int32_t *py);

/* ---- the menu of a file or folder (one list for the desktop and Pliki) ---- */
enum k1_menu_akcja {
    K1_MA_OTWORZ = 1, /* the default application of the type, a folder: into it */
    K1_MA_ZA_POMOCA,  /* the submenu (k1_menu_za_pomoca) */
    K1_MA_APLIKACJA,  /* in the submenu: open in application `arg` */
    K1_MA_ZAWSZE,     /* in the submenu: "Zawsze otwieraj tą aplikacją" on / off (the menu stays) */
    K1_MA_POKAZ,      /* the desktop: Pliki at this file */
    K1_MA_NAZWA,
    K1_MA_USUN,       /* to Kosz */
    K1_MA_PRZYWROC,   /* Kosz */
    K1_MA_NA_ZAWSZE,  /* Kosz: "Usuń na zawsze" */
    K1_MA_IMPORTUJ,   /* the USB stick */
};
enum k1_menu_miejsce { K1_MM_PLIKI = 1, K1_MM_PULPIT, K1_MM_KOSZ, K1_MM_PENDRIVE };

/* The rows for an entry (`folder` 1: a folder; `typ`: the file's type) shown in `gdzie`. */
void k1_menu_pliku(struct k1_menu *m, enum k1_menu_miejsce gdzie, int folder, int typ);
/* The menu of many entries selected (0.8.2 step 6, Pliki): "Zaznaczone: 12", then what can be done to all of them —
 * Usuń; in Kosz Przywróć, Usuń na zawsze; on the stick Importuj. */
void k1_menu_wielu(struct k1_menu *m, enum k1_menu_miejsce gdzie, int ile);
/* "Otwórz za pomocą": the applications that open `typ` (K1_OTWIERANIE), the one that opens it now (`ustawienie`:
 * K1_UST_OTWIERANIE of the type) marked; with two or more, "Zawsze otwieraj tą aplikacją" (`zawsze`: ticked). */
void k1_menu_za_pomoca(struct k1_menu *m, int typ, int ustawienie, int zawsze);
/* The same for the applications a[0..n), `teraz` marked (k1_menu_za_pomoca: those of a type). */
void k1_menu_aplikacji(struct k1_menu *m, const int *a, int n, int teraz, int zawsze);
/* "Zawsze otwieraj tą aplikacją" (and Ustawienia → Aplikacje): the K1_KOM_USTAW messages that make `apl` the default
 * application of `typ` — of every type of its family, as one row of Ustawienia shows them (room: K1_TYPY_MAKS). How
 * many; 0 when `apl` does not open `typ`. ekran takes them from Ustawienia, Pliki and the desktop. */
int k1_menu_zawsze(int typ, int apl, struct k1_komunikat *ust);
/* The message for a file no application opens: its type, K1_MENU_BEZ_APLIKACJI, on the desktop "Pokaż w Plikach". */
void k1_menu_bez_aplikacji(struct k1_menu *m, int typ, int pokaz);

/* Text wrapped at spaces to `szer` px (a word longer than the line: between characters): `linia` gets every line and
 * its number (NULL: only counts); returns how many lines. The menu's rows and Pliki's details (0.10.7 step 3). */
int k1_zawin(const char *t, int32_t szer, enum k1_cz cz, void (*linia)(const char *, int, void *), void *kontekst);

#endif

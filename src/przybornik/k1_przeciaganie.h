/* K1 przybornik — dragging files and folders (0.8.2 step 6, L24 phase A; owner's decisions D6, D8 and the sketch of
 * 2026-11-12): ONE table of rules for Pliki and the desktop — what is dragged (the source) onto what (the target) gives
 * an action, a question ("Czy na pewno…?") or a refusal with its text ("Możesz tylko…", "Nie możesz — …"). It is asked
 * while dragging (the sign at the pointer) and once more when the files are dropped (the action, or the window with
 * the question); skarbiec's answer (K1_DOK_BLAD_*) is put in the same words (k1_prz_powod). Dragging between windows
 * (phase B) — after 0.8.2, with the zones of 0.11 (D6).
 *
 * Also here: the window of a question (the look of the menu, k1_menu) and the drawing of what is dragged (a card with
 * the name and a count, the sign under it). Pure functions, no system calls or allocation — host test
 * testy/host/test_przeciaganie.c (every source and target of the table). */
#ifndef K1_PRZECIAGANIE_H
#define K1_PRZECIAGANIE_H

#include <stdint.h>

#include "k1_menu.h"
#include "k1_rysuj.h"

#define K1_PRZ_PROG 8 /* px the pointer moves with the button held before it is dragging, not a click */

/* What is dragged: entries of a user folder (Pliki), files of Pulpit (the desktop), entries in Kosz, files on the USB
 * stick, a standard folder (the places on the left of Pliki). */
enum k1_prz_zrodlo { K1_PZ_FOLDER = 1, K1_PZ_PULPIT, K1_PZ_KOSZ, K1_PZ_PENDRIVE, K1_PZ_STALY, K1_PZ_MAKS = K1_PZ_STALY };
/* Where it is dropped: a folder (standard or the user's), Kosz, the USB stick (the place or its folder). */
enum k1_prz_cel { K1_PC_FOLDER = 1, K1_PC_KOSZ, K1_PC_PENDRIVE, K1_PC_MAKS = K1_PC_PENDRIVE };
enum k1_prz_dzialanie {
    K1_PD_NIC = 0,  /* nothing happens (the folder it is in already, Kosz onto Kosz) — no sign */
    K1_PD_PRZENIES, /* DOK_PRZENIES */
    K1_PD_KOSZ,     /* DOK_KOSZ */
    K1_PD_PRZYWROC, /* DOK_PRZYWROC into the folder */
    K1_PD_IMPORT,   /* a copy from the stick into the folder */
    K1_PD_ODMOWA,   /* not allowed: the text says why */
};
enum k1_prz_znak { K1_ZN_BRAK = 0, K1_ZN_PRZENIES, K1_ZN_KOPIUJ, K1_ZN_NIE }; /* the sign at the pointer */

/* What is dragged where — the caller knows it, the table decides. */
struct k1_prz_co {
    int zrodlo, cel;
    int ile;               /* entries dragged (≥ 1) */
    int folderow;          /* folders among them */
    int w_srodku;          /* entries inside those folders (all levels); -1: unknown (the desktop sees only Pulpit) */
    int ten_sam;           /* the target is the folder they lie in */
    int w_siebie;          /* the target is one of the folders dragged, or inside one */
    int za_gleboko;        /* folders inside folders would be more than K1_DOK_GLEBOKOSC */
    uint64_t bajty;        /* an import: the size of the files */
    int64_t wolne_mb;      /* an import: the room for files left, MiB (-1: unknown) */
    const char *cel_nazwa; /* the target's name ("Obrazy"); NULL for Kosz and the stick */
    const char *nazwa;     /* the name of the first entry dragged (a question about one) */
};

struct k1_prz_ocena {
    int dzialanie, pytaj, znak; /* K1_PD_*, 1: ask first (D8), K1_ZN_* */
    char znak_tekst[96];        /* the sign: "Przenieś do „Obrazy”", "Do Kosza", "Pendrive jest tylko do odczytu" */
    char tekst[256];            /* the question, or the whole refusal ("Nie możesz — …") */
    char opis[160];             /* small text under it (may be empty) */
};

void k1_prz_ocen(const struct k1_prz_co *c, struct k1_prz_ocena *o);

/* The window after the drop: the question (rows K1_PO_TAK "Przenieś do Kosza", K1_PO_ANULUJ) or the refusal (K1_PO_OK). */
enum k1_prz_odpowiedz { K1_PO_TAK = 1, K1_PO_ANULUJ, K1_PO_OK, K1_PO_ZACHOWAJ, K1_PO_POMIN };
void k1_prz_okienko(struct k1_menu *m, const struct k1_prz_ocena *o);
/* A name taken in the target (`zajetych` of `ile` entries moved): "W „Obrazy” jest już „IMG_0001.JPG”." —
 * Zachowaj oba (the one moved becomes "IMG_0001 (2).JPG") · Pomiń (the others go on) · Anuluj. */
void k1_prz_okienko_nazwa(struct k1_menu *m, const char *cel, const char *nazwa, int zajetych, int ile);
/* skarbiec said no (K1_DOK_BLAD_*, K1_SKARB_*): the same words as the table ("Nie możesz — …"). */
const char *k1_prz_powod(int wynik);

/* Drawing: the card at the pointer (x, y) — the icon, the name, the count when more than one — and the sign under it
 * (none for K1_ZN_BRAK). */
void k1_prz_rysuj(struct k1_obraz *o, const struct k1_motyw *mt, int32_t x, int32_t y, enum k1_ikona ik, const char *nazwa,
                  int ile, const struct k1_prz_ocena *oc);
/* Only the sign (the desktop draws its own tile), its top left at (x, y); its width. */
int32_t k1_prz_znak_rysuj(struct k1_obraz *o, int32_t x, int32_t y, const struct k1_prz_ocena *oc);
#define K1_PRZ_ZNAK_WYS 24

#endif

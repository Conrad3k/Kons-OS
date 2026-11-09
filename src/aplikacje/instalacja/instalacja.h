/* K1 Instalator (window) — state shared by the modules (0.7.5 split): main.c (state, nadzorca's messages,
 * buttons, mouse and keys), ekrany.c (drawing of each screen). */
#ifndef K1_INSTALACJA_H
#define K1_INSTALACJA_H

#include <stddef.h>
#include <stdint.h>

#include "k1_aplikacja.h"

#define TAG "K1-INSTALACJA"
#define FD_NADZORCA 4
#define LEWO 28
#define GORA (K1_OKNO_TYTUL + 14)
#define DYSKI_Y (K1_OKNO_TYTUL + 112)
#define DYSK_H 60
#define PRZ_H 36
#define POLE_W 260
#define POLE_H 38

enum { S_SZUKAM, S_LISTA, S_POTWIERDZ, S_POSTEP, S_GOTOWE, S_BLAD, S_SB };

extern struct k1_apl apl;
extern int stan;
struct dysk {
    int32_t mib, rodzaj, k1, sektor, zajety;
    int32_t ab_zapas, ab_slot, ab_fl; /* A/B (0.7.0): the other slot's version, the running slot (0: no A/B), flags */
};
extern struct dysk dyski[K1_INST_DYSKI_MAKS];
extern int32_t sb, sbk, sb_prawdziwy, sb_wynik; /* Secure Boot (SB_STAN); sb = -1: not known yet */
extern int nowe_miejsce;                        /* the update wrote the other slot (A/B) */
extern int n_dyskow, zrodlo, wybrany, tryb, procent, etap, wynik;
extern int32_t wersja_zrodla, zrodlo_podpisane; /* M6a: the stick's manifest (0: none) */
extern char pole[64];                           /* the "USUŃ" field */
extern size_t pole_dl;
extern int hover; /* 0..7 disk, 10 primary button, 11 secondary button, 12 the Secure Boot link */
#define SB_LINK "Secure Boot…"

int32_t wersja_dysku(int i); /* -1 = no K1, 0 = K1 without a manifest (0.5.9–0.6.1), else the version */
int zainstalowany(void);     /* K1 runs from a disk of this computer (0.7.2) */
int starsza(void);           /* an update would go back to an older version */
int potwierdzone(void);      /* the field says "usuń" */
int32_t przyciski_y(void);
void przyciski(struct k1_przycisk p[2]);

/* ekrany.c */
void rysuj(struct k1_obraz *o, const struct k1_motyw *m);

#endif

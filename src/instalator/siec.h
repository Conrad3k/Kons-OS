/* K1 instalator — updates through the network (0.10.5 step 2, ADR 0046 T6, T7): the modes K1_INST_SIEC_* of the writing
 * program. nadzorca starts it with
 *   fd 3  the channel (INST_ZADANIE in; INST_ETAP, INST_SIEC, INST_GOTOWE out)
 *   fd 4  STAN: the announcement from the download zone (a pipe; empty when K1 only looks at the disk); POBIERZ: the
 *         package from the download zone (a pipe); else /dev/null
 *   fd 5  the K1-EFI partition of the disk K1 runs from (read and written; the data partition is never ours)
 *   fd 6  STAN: an empty memfd for the announcement to show (Ustawienia, gui); POBIERZ: the announcement STAN accepted
 * Only this program reads what came from the network; nothing it reads changes what starts before "Zainstaluj". */
#ifndef K1_INST_SIEC_H
#define K1_INST_SIEC_H

#include <stdint.h>

#include "k1_efi.h"
#include "k1_ogloszenie.h"

#define FD_KANAL 3
#define FD_ZRODLO 4
#define FD_CEL 5
#define FD_OGL 6

/* main.c */
void etap(int procent, int nr);

/* siec.c: the mode `tryb` (K1_INST_SIEC_*); `test` = the test of updates (a developer build then takes unsigned
 * files); *nowy = 1 when a slot waits to be tried on the next start. A K1_INST_* result. */
int siec_zadanie(int tryb, int test, int *nowy);

/* What the disk holds (siec.c): the partition, its waiting room and the slots. */
struct siec_dysk {
    struct k1_efi e;
    uint64_t rozmiar, pocz; /* bytes of the partition; where its waiting room begins */
    int dziala, wolne;      /* the slot that runs (k1_slot), the other one */
    uint32_t wersja, inna;  /* their versions (0: none) */
};
int siec_ogl_dobre(const uint8_t *b, size_t n, int test, struct k1_ogloszenie *o);
int siec_man_dobry(const uint8_t *man, int test);
int siec_czeka(const struct siec_dysk *d, int test, struct k1_ogloszenie *o, uint8_t sha[32], uint64_t *bajty);

/* siec_instaluj.c: "Zainstaluj" (the waiting version into the free slot, the boot files, k1_nowy) and "Wróć do
 * poprzedniej wersji" (k1_nowy = the free slot holding the previous, older version). */
int siec_zainstaluj(const struct siec_dysk *d, int test);
int siec_wroc(const struct siec_dysk *d, int test);

#endif

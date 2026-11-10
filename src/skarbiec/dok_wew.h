/* K1 skarbiec — what the modules of the user's files share (0.8.1 split of dokumenty.c, AGENTS.md "Czytelność"):
 *   dokumenty.c     the entries in memory, their objects (entry, content pieces), new files and changes
 *   dok_drzewo.c    looking: entries by number, folders (visible, depth, inside), names taken in a folder
 *   dok_kosz.c      Kosz: in, out, for good, after 30 days
 *   dok_czas.c      the time bound: never a time before this K1's build or before the latest clock seen
 *   dok_wczytaj.c   loading the entries at start, with repairs
 * The public functions: dokumenty.h. Part of skarbiec (skarbiec.h). */
#ifndef K1_DOK_WEW_H
#define K1_DOK_WEW_H

#include <stdint.h>

#include "dokumenty.h"
#include "magazyn.h"

#define WL K1_WL_DOKUMENTY
#define DZIEN 86400
#define K1_KOSZ_OD 1793491200LL /* 2026-11-01: no K1 had Kosz before 0.8.0 — an earlier time is a wrong clock */

/* dokumenty.c: the entries (in no order), the number of the next new one */
extern struct k1_dok_wpis wpisy[K1_DOK_MAKS];
extern int n_wpisow;
extern uint32_t nastepny;

/* dokumenty.c: objects */
void nazwa_wpisu(uint32_t id, char out[K1_MAG_NAZWA]);
void nazwa_kawalka(uint32_t id, uint32_t gen, uint32_t k, char out[K1_MAG_NAZWA]);
uint32_t kawalkow(uint64_t rozmiar);
int zapisz_wpis(const struct k1_dok_wpis *w);
void usun_kawalki(uint32_t id, uint32_t gen, uint32_t ile);
/* `nazwa` made free in `folder` ("nazwa (2)"…), `oprocz` not counted */
int wolna_nazwa(uint32_t folder, const char *nazwa, uint32_t oprocz, char out[K1_DOK_NAZWA]);
int zmienialny(uint32_t id); /* the index of a visible user entry, else -1 */

/* dok_drzewo.c */
int indeks(uint32_t id);       /* the place in wpisy[], -1: none */
int staly(uint32_t id);        /* a standard folder (Pulpit, Dokumenty…) */
int folder_otwarty(uint32_t id); /* a folder that can take entries */
int glebokosc(uint32_t id);    /* user folders above `id` */
int wewnatrz(uint32_t id, uint32_t przodek);
int wysokosc(uint32_t id);     /* levels of folders in `id` and below */
int nazwa_zajeta(uint32_t folder, const char *nazwa, uint32_t oprocz);

/* dok_czas.c */
void dok_czas_wczytaj(void); /* the latest clock seen, from its object (at start) */
int64_t dok_granica(void);   /* the build time or the latest clock seen, whichever is later */

#endif

/* K1 — host test of the user's files, split into files ≤ 600 lines (0.9.0 step 9): test_dokumenty.c (names, types,
 * the entry, content, folders, Kosz, loading; main), test_dokumenty_kanal.c (the rights of the channel, a file after a
 * gesture, the file picker, Podgląd), test_dokumenty_naprawa.c (nothing is lost: repairs, the clock, names, limits),
 * test_dokumenty_zapis.c (a write in progress, the parts for Miejsce). What the parts share. */
#ifndef K1_TEST_DOKUMENTY_H
#define K1_TEST_DOKUMENTY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_memfd.h"
#include "k1_test_host.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

#define T0 4000000000LL /* 2096: always after the real clock, which reaches skarbiec through dok_kanal.c */
#define DZIEN 86400LL
#define ROK_2000 946684800LL

extern struct k1_dok_wpis wybrane[K1_DOK_MAKS];
extern struct k1_komunikat do_nadz; /* what skarbiec last told nadzorca */
extern int kan[2];                  /* a channel: [1] skarbiec's end, [0] the client's */
int nazwa(const char *s);           /* a name a file may have */
/* A new entry (a file with `dl` bytes of `dane`, or a folder) of Pliki at time t: its number, 0 when refused. */
uint32_t nowy(uint32_t folder, int rodzaj, const char *n, const uint8_t *dane, uint64_t dl, int64_t t);
int obiektow(void); /* objects of entries and content */
/* Content in memory for dok_zrodlo / dok_cel (dane: to read, wy: to write; dl: bytes). */
struct bufor {
    const uint8_t *dane;
    uint8_t *wy;
    uint64_t dl;
};
int czytaj_bufor(void *ctx, uint64_t od, void *buf, uint32_t n);
int pisz_bufor(void *ctx, const void *buf, uint32_t n);
int dok_kazdy(const struct k1_dok_wpis *w); /* every entry (dok_wybierz) */
/* Request `m` of owner `wl` on `kan`: the reply (*zmiana: dok_obsluz said "changed", *fd_odp: its descriptor). */
struct k1_komunikat zapytaj(int wl, struct k1_komunikat m, int fd, int *zmiana, int *fd_odp);
/* The same on channel `kanal`. */
struct k1_komunikat na(int *kanal, int wl, struct k1_komunikat m, int fd, int *fd_odp);

/* test_dokumenty_kanal.c */
void test_prawa(void);
void test_dostep(void);
/* test_dokumenty_naprawa.c */
void test_naprawa(void);
void test_zegar(void);
void test_zegar_z_internetu(void);
void test_nazwa_popraw(void);
void test_nazwy_podrobki(void);
void test_nazwy_wolne(void);
void test_licznik_kosza(void);
void test_limit_dostepu(void);
void test_duzo_wpisow(void);
void test_rezerwa(void);
void test_folder_aplikacji(void);
/* test_dokumenty_zapis.c */
void test_zapis_w_toku(void);
void test_podzial(void);

#endif

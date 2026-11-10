/* K1 skarbiec — state shared by the modules of the service (0.8.0 split, AGENTS.md "Czytelność"):
 *
 *   main.c        the loop, the control channel from nadzorca, client channels and their object requests
 *   obiekty.c     objects by (owner, name): the vault on the K1-DANE partition, or memory without one
 *   haslo.c       the password: unlocking (login screen), changing (Ustawienia), the limit of attempts
 *   dokumenty.c   the user's files and folders, Kosz (ADR 0029) — on top of obiekty.c, host tests;
 *                 split into dokumenty.c, dok_drzewo.c, dok_kosz.c, dok_wczytaj.c (dok_wew.h)
 *   dok_kanal.c   K1_KOM_DOK_* requests of clients: the table of rights, memfd in and out
 *   dostep.c      a file for an application after a gesture: "Otwórz w…", the file picker (ADR 0030)
 *
 * Documentation: docs/komponenty/skarbiec.md. */
#ifndef K1_SKARBIEC_H
#define K1_SKARBIEC_H

#include <stddef.h>
#include <stdint.h>

#include "magazyn.h"

#define TAG "K1-SKARBIEC"

#include "k1_komunikat.h"

/* obiekty.c */
extern struct k1_magazyn mag;
extern struct k1_mag_dysk dysk;
extern int trwaly;      /* 1: the vault on the partition is open */
extern int zablokowany; /* 1: a vault with a password, not opened yet (M5e) */
void ob_otworz_magazyn(int fd_dysk);
int ob_tryb(void);      /* K1_SKARB_TRWALY / _RAM / _ZABLOKOWANY */
/* Objects of owner `wl` by name (1..31 bytes): results K1_MAG_*. */
int ob_czytaj(int wl, const char *nazwa, void *buf, uint32_t maks, uint32_t *dl);
int ob_zapisz(int wl, const char *nazwa, const void *dane, uint32_t dl);
int ob_usun(int wl, const char *nazwa);
/* Calls `f` for the name of every object of owner `wl` (the names are copied first: `f` may delete). */
void ob_kazdy(int wl, void (*f)(const char *nazwa, void *ctx), void *ctx);

/* haslo.c */
int haslo_ustawione(void);
int sekund_do_proby(void);
int haslo_odblokuj(int fd, size_t dl);  /* login screen: memfd with the password */
int haslo_zmien(int fd, size_t dl);     /* Ustawienia: memfd "current\0new\0" */
void zglos_stan(void);                  /* SKARB_STAN to nadzorca (main.c) */
void do_nadzorcy(struct k1_komunikat m); /* main.c: a message on the control channel */

/* dok_kanal.c */
/* A K1_KOM_DOK_* request of a client (owner `wl`) on channel `fd`; `fd_in` = attached memfd or -1 (closed here).
 * Returns 1 when files changed (the caller tells every client allowed to list them). */
int dok_obsluz(int fd, int wl, const struct k1_komunikat *m, int fd_in);
int dok_moze_listowac(int wl);
void dok_po_otwarciu(void); /* the vault is readable (opened, unlocked, or memory): load the entries */
int dok_co_godzine(void);    /* Kosz: delete what is older than 30 days; 1 when something went */

/* dostep.c: what applications were given (owner `wl`, file `id`) and the file picker */
int dostep_daj(int wl, uint32_t id); /* 0, or K1_DOK_BLAD_OTWARTE: it has K1_DOK_DOSTEP_APL files already */
int dostep_ma(int wl, uint32_t id);
int dostep_otworz(uint32_t id, int apl);          /* DOK_OTWORZ (allowed already): results 0 / K1_DOK_BLAD_* */
int dostep_wybierz(int fd, int wl, int typ);      /* DOK_WYBIERZ of the client on channel `fd` */
int dostep_wybrano(uint32_t id);                  /* DOK_WYBRANO of the file picker */
void dostep_nowy_klient(int fd, int wl);          /* main.c: a new channel */
void dostep_koniec_klienta(int fd, int wl, int ostatni); /* main.c: a channel closes (`ostatni`: of that owner) */

#endif

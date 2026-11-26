/* K1 skarbiec — state shared by the modules of the service (0.8.0 split, AGENTS.md "Czytelność"):
 *
 *   main.c        the loop, the control channel from nadzorca, client channels and their object requests
 *   obiekty.c     objects by (owner, name): the vault on the K1-DANE partition, or memory without one
 *   haslo.c       the password: unlocking (login screen), changing (Ustawienia), the limit of attempts
 *   dokumenty.c   the user's files and folders, Kosz (ADR 0029) — on top of obiekty.c, host tests;
 *                 split into dokumenty.c, dok_drzewo.c, dok_kosz.c, dok_wczytaj.c (dok_wew.h)
 *   dok_kanal.c   K1_KOM_DOK_* requests of clients: the table of rights, memfd in and out
 *   dostep.c      a file for an application after a gesture: "Otwórz za pomocą", the file picker (ADR 0030)
 *   miejsce.c     the room for files, K1-DANE to the end of its USB stick (0.8.1 step 3, ADR 0033)
 *   przerwy.c     work between requests: compaction in steps (0.8.2, ADR 0036)
 *   strefy.c      the disks of the zones that remember: who may name their pieces, room, deleting (0.10.0 step 5)
 *   zalegle.c     replies a client's full channel did not take yet: they wait and go in order (0.9.0, zalegle.h)
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
/* Bytes of the objects of owner `wl` (their data); *ile grows by their number. */
uint64_t ob_bajty(int wl, uint32_t *ile);
/* Room (0.8.1, ADR 0034): free bytes and the whole (the vault's region, or the memory for objects without one). */
uint64_t ob_wolne(void);
uint64_t ob_pojemnosc(void);
/* The vault takes all of a partition now `bajty` long (k1_mag_powieksz; logged with `kiedy`). */
int ob_powieksz(uint64_t bajty, const char *kiedy);
/* Right after the vault opened or was unlocked: a grow cut off is finished. */
void ob_po_otwarciu(void);
void epoka_otwarta(void); /* epoka.c (0.13.0 step 2): SKARB_EPOKA after the vault opened… */
void epoka_sprawdz(void); /* …and after its first write */
void ob_zwolnij(int fd_dysk); /* SKARB_ZWOLNIJ (0.10.10 step 1): the vault and the partition closed, objects in memory */

/* przerwy.c: work in the quiet moments of the loop (0.8.2, ADR 0036) — compaction in steps of 4 MiB */
double przerwa_czeka(void);    /* seconds without a request before the next piece of work; < 0: none (0: now) */
int przerwa_praca(void);       /* one piece of it; 1 when the room for files changed (clients ask again) */
void przerwa_trwala(double s); /* a request kept the loop busy `s` seconds (the longest goes to the log) */

/* haslo.c */
int haslo_ustawione(void);
int sekund_do_proby(void);
int haslo_odblokuj(int fd, size_t dl);  /* login screen: memfd with the password */
int haslo_zmien(int fd, size_t dl);     /* Ustawienia: memfd "current\0new\0" */
void zglos_stan(void);                  /* SKARB_STAN to nadzorca (main.c) */
/* main.c: a message (+ a memfd the caller keeps) to the client on channel `fd` — when its channel is full it waits and
 * goes later in order (zalegle.h, 0.9.0 step 2); the only way anything goes to a client */
void do_klienta(int fd, struct k1_komunikat m, int przekaz);
void do_nadzorcy(struct k1_komunikat m); /* main.c: a message on the control channel */
void do_nadzorcy_z(struct k1_komunikat m, int fd); /* …with a descriptor */

/* dok_kanal.c */
/* A K1_KOM_DOK_* request of a client (owner `wl`) on channel `fd`; `fd_in` = attached memfd or -1 (closed here).
 * Returns 1 when files changed (the caller tells every client allowed to list them). */
int dok_obsluz(int fd, int wl, const struct k1_komunikat *m, int fd_in);
int dok_moze_listowac(int wl);
void dok_koniec_kanalu(int fd); /* main.c: a client channel closes — its write in progress is abandoned (0.8.1) */
void dok_po_otwarciu(void); /* the vault is readable (opened, unlocked, or memory): load the entries */
int dok_co_godzine(void);    /* Kosz: delete what is older than 30 days; 1 when something went */
void dok_zegar_z_internetu(int64_t skok); /* nadzorca set the clock (0.9.0 step 8): Kosz keeps the age of its files */

/* miejsce.c: the room for files (SKARB_MIEJSCE) and growing K1-DANE (nadzorca does the partition table) */
void miejsce_zapas(int mib);              /* MiB the stick has after K1-DANE (SKARB_KLIENT d) */
struct k1_komunikat miejsce_komunikat(void);
void miejsce_wyslij(int fd);
int miejsce_podzial(int fd, int wlasciciel); /* CZESC for every part (Ustawienia only, 0.8.5); the result for WYNIK */
int miejsce_powieksz(void);               /* Ustawienia asked: 0 = nadzorca asked (POWIEKSZ_DANE), else K1_MAG_ARG */
int miejsce_powiekszono(int wynik);       /* nadzorca's POWIEKSZONO: the vault takes the partition; the result */

/* strefy.c: the disks of the zones (0.10.0 step 5) */
int strefy_wolno(int wl, uint32_t obiekt);           /* 1: owner `wl` may name object `obiekt` (a piece: zones only) */
int strefy_miejsce_na(uint32_t obiekt, uint32_t dl); /* 1: room for it (a piece never takes the files' reserve) */
void strefy_wyczysc(int wl);                         /* SKARB_WYCZYSC: every piece of `wl`, then WYCZYSZCZONO */
uint64_t strefy_bajty(uint32_t *kawalki);            /* what the disks of all zones take (Ustawienia → Miejsce) */

/* dostep.c: what applications were given (owner `wl`, file `id`) and the file picker */
int dostep_daj(int wl, uint32_t id); /* 0, or K1_DOK_BLAD_OTWARTE: it has K1_DOK_DOSTEP_APL files already */
int dostep_ma(int wl, uint32_t id);
int dostep_folder(int wl, uint32_t folder); /* a file of that folder was given to it (0.8.2 step 4: Podgląd, ADR 0037) */
int dostep_do_strefy(uint32_t id, int strefa, int otworz); /* DOK_DO_STREFY (allowed already): 0 / K1_DOK_BLAD_* */
int dostep_otworz(uint32_t id, int apl);          /* DOK_OTWORZ (allowed already): results 0 / K1_DOK_BLAD_* */
int dostep_wybierz(int fd, int wl, int typ, int rodzina); /* DOK_WYBIERZ of the client on channel `fd` */
int dostep_wybrano(uint32_t id);                  /* DOK_WYBRANO of the file picker */
void dostep_nowy_klient(int fd, int wl);          /* main.c: a new channel */
void dostep_koniec_klienta(int fd, int wl, int ostatni); /* main.c: a channel closes (`ostatni`: of that owner) */

#endif

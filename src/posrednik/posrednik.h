/* K1 posrednik — the graphics broker's parts (main.c: the channel, the window and the shared memory; prosby.c: the
 * requests of the bridge, checked and done; plot.c: the fences — when the card finished a batch, hangs; karta.c: the
 * card through its render node; testowy.c: a card that is not there, for the tests). 0.10.9 step 5, ADR 0049 pkt 7; the
 * formats: src/wspolne/k1_posrednik.h; documentation: docs/komponenty/posrednik.md. */
#ifndef K1_POSREDNIK_USLUGA_H
#define K1_POSREDNIK_USLUGA_H

#include <poll.h>
#include <stdint.h>

#include "k1_komunikat.h"
#include "k1_posrednik.h"

#define TAG "K1-POSREDNIK"
#define FD_KLIENT 3 /* the channel to the client (a zone's VMM, nadzorca's question, the test program) */
#define FD_KARTA 4  /* the render node of the card (i915) from nadzorca, or /dev/null (no card, the test engine) */

/* What does the work: the card (karta.c) or, in the test image on request of the test program, nothing (testowy.c).
 * ioctl(): the DRM ioctl `nr` of the render node with argument `arg` — 0 or the kernel's errno (> 0). Every argument it
 * gets was checked (k1_posrednik.c) and holds only pointers into the broker's own memory. */
struct silnik {
    const char *nazwa;
    int (*ioctl)(unsigned long nr, void *arg);
};
extern const struct silnik silnik_karty;   /* karta.c */
extern const struct silnik silnik_testowy; /* testowy.c */
extern const struct silnik *silnik;        /* main.c */

/* karta.c: what the card can do — K1_POSR_KARTA_BRAK / NIE (*powod: K1_POSR_NIE_*) / UMIE (*pci, *rewizja, *wersja). */
int posr_karta(int *powod, int *pci, int *rewizja, int *wersja);

/* main.c */
void wyslij(struct k1_komunikat k, int fd);
extern struct k1_posr_obiekty obiekty;
extern volatile uint8_t *wspolna; /* the shared memory of the bridge (NULL: not yet) */
extern uint32_t wspolna_rozmiar;
extern int zawieszona;            /* a context of ours hung the card: the broker ends (K1_POSR_KONIEC_ZAWIESZENIE) */

/* prosby.c: the stream of `dl` bytes in the window `okno` — copied, then checked and done request by request. Returns 0
 * or the K1_POSR_BLAD_* of the first request that failed; *zrobione: how many were done before it. */
int posr_prosby(const volatile uint8_t *okno, uint32_t dl, int32_t *zrobione);
void posr_ban(uint32_t ctx); /* the card banned context ctx: its bit in the shared memory */

/* plot.c: the fences. */
void plot_nowy(uint32_t pierscien, int fd, uint32_t ctx); /* a batch's out-fence (fd: ours from now on) */
int plot_czekaj(uint32_t pierscien, uint64_t nr);         /* POSR_PLOT: 0 or K1_POSR_BLAD_LIMIT */
int plot_deskryptory(struct pollfd *p, int maks);         /* the waiting fences for poll() */
void plot_gotowe(const struct pollfd *p, int n);          /* after poll(): the done ones answered */

#endif

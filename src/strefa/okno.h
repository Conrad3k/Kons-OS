/* K1 strefa — the window at ekran (role STREFA): the picture of the guest's display (virtio-gpu, gpu.c) copied into
 * the window's surface, ekran's keys and pointer to the guest's input devices (wejscie.c), "close" from ekran. Before
 * the guest's first picture the window shows "Uruchamiam …" drawn by the VMM (D1: S1). Main thread only, except
 * okno_zmiana (under the lock, from a processor thread). */
#ifndef K1_STREFA_OKNO_H
#define K1_STREFA_OKNO_H

#include <stdint.h>

enum { OKNO_NIC = 0, OKNO_ZAMKNIJ = 1 };
enum { OKNO_ETAP_START = 1, OKNO_ETAP_OKNO = 2 }; /* the guest's "etap start" / "etap okno" (vsock.c) */

void okno_start(void);          /* connects (WITAJ), creates the window with "Uruchamiam …" */
int okno_fd(void);              /* the channel to ekran, -1 without a window */
int okno_odbierz(void);         /* messages from ekran: OKNO_ZAMKNIJ when ekran asks to close the zone */
void okno_obraz(void);          /* the guest's changed picture into the window, when ekran allows */
void okno_zamknij_goscia(void); /* asks the guest to end (vsock) */
void okno_schowaj(void);        /* the user closed the zone: its window goes now, the machine ends in the background */
void okno_etap(int etap);       /* OKNO_ETAP_* from the guest (under mz.zamek) */
void okno_pelny(int tak);       /* 0.11.0 step 1: the guest's "pelny 1/0" (under mz.zamek): to ekran, the changes only */
void okno_kopia(const uint8_t *t, uint32_t n); /* 0.11.0 step 5: text copied in the zone, to ekran (SCHOWEK_USTAW) */
int okno_czas_ms(void);         /* how soon the main loop must come back for the start screen's next frame, -1: no need */
void okno_sesja(const char *nazwa); /* the session's name for the start screen (argv[4]; checked here), before okno_start */
/* The guest showed rectangle (x, y, w, h) of its display (under mz.zamek). */
void okno_zmiana(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* The window's size: the guest's display (gpu.c: GET_DISPLAY_INFO). */
void okno_rozmiar(uint32_t *w, uint32_t *h);

/* okno_bufor.c (0.10.11 step 2, ADR 0051): the picture from buffers of the card, without a copy; under mz.zamek */
#include "gpu_most.h"
int okno_bufor_obraz(const struct most_obraz *o, int fd, uint32_t szer, uint32_t wys); /* SET_SCANOUT_BLOB; fd borrowed */
void okno_bufor_koniec(uint32_t res);              /* the buffer is gone */
int okno_bufor_jest(uint32_t res, uint32_t *w, uint32_t *h); /* res is the shown buffer: its size */
int okno_bufor_pokazany(void);                     /* the number of the shown one, 0: own pixels */
void okno_bufor_wylacz(void);                      /* SET_SCANOUT of a 2D resource: own pixels again */
int okno_bufor_wyslij(int fd, uint32_t id, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1); /* to ekran */
void okno_bufor_liczniki(unsigned long *wyslane, unsigned long *odrzucone);
void okno_bufor_start(void);

#endif

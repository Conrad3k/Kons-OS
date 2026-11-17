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
int okno_czas_ms(void);         /* how soon the main loop must come back for the start screen's next frame, -1: no need */
void okno_sesja(const char *nazwa); /* the session's name for the start screen (argv[4]; checked here), before okno_start */
/* The guest showed rectangle (x, y, w, h) of its display (under mz.zamek). */
void okno_zmiana(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* The window's size: the guest's display (gpu.c: GET_DISPLAY_INFO). */
void okno_rozmiar(uint32_t *w, uint32_t *h);

#endif

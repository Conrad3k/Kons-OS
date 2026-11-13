/* K1 skarbiec — what a client has not taken yet (0.9.0 step 2, RYZYKA R64; the same idea as ekran's zalegle.h, 0.8.5).
 * When the channel of a client is full (it is busy and does not read), a reply waits here — with the memfd it carries —
 * and goes, in order, when the channel takes it again (POLLOUT). Nothing is dropped on purpose:
 *   - "files changed" (DOK_ZMIANA) waits at most once (a second one says the same: the client asks for the list after
 *     the first, and gets what is there then);
 *   - everything else waits in the order it was sent;
 *   - while anything waits for a client, skarbiec does not take its requests (main.c): the client is busy, its
 *     requests wait in its channel. So one client's queue holds the replies to one request and what other clients
 *     cause (a change, a file for the picker) — ZAL_MAKS is far more than that. A full queue would be an error (logged).
 * Tested on the host with a real channel (testy/host/test_skarb_zalegle.c). */
#ifndef K1_SKARB_ZALEGLE_H
#define K1_SKARB_ZALEGLE_H

#include "k1_komunikat.h"

#define ZAL_MAKS 32 /* the most replies of one request: SKARB_PODZIAL, a CZESC per part (K1_CZESCI_MAKS) + its WYNIK */

struct zal_wpis {
    struct k1_komunikat m;
    int fd; /* our copy of the memfd it carries, or -1 */
};

struct zalegle {
    struct zal_wpis k[ZAL_MAKS];
    int pocz, n;             /* ring buffer: first and how many */
    unsigned long utracone;  /* replies that did not fit (should stay 0: logged) */
};

void zal_poczatek(struct zalegle *z); /* an empty queue (a new client) */
void zal_wyczysc(struct zalegle *z);  /* the client is gone: what waited goes, with its memfds */
/* Sends m (+ fd, kept by the caller) to `gniazdo` now, or puts it behind what waits when the channel is full or
 * something waits already. 0: sent or waiting; -1: lost (a closed channel, or no room — counted in utracone). */
int zal_wyslij(struct zalegle *z, int gniazdo, const struct k1_komunikat *m, int fd);
/* Sends what waits while the channel takes it. Returns how many still wait; a closed channel: everything goes, 0. */
int zal_oproznij(struct zalegle *z, int gniazdo);

#endif

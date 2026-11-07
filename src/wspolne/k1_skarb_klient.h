/* K1 — client side of skarbiec (M5d): ekran (settings), Notatki (the note). The channel comes from
 * nadzorca; replies must come from skarbiec's UID (SO_PASSCRED on our end) and pass k1_komunikat. */
#ifndef K1_SKARB_KLIENT_H
#define K1_SKARB_KLIENT_H

#include <stddef.h>

/* Reads object `id`, waiting up to `ms` ms. Returns its length (data in `buf`), -1 when it does not
 * exist or failed, -2 on timeout or a closed channel. *tryb = K1_SKARB_TRWALY / K1_SKARB_RAM when known. */
int k1_skarb_czytaj(int fd, int id, void *buf, size_t maks, int ms, int *tryb);

/* Sends a new version of object `id` (does not wait for the result). 0 or -1. */
int k1_skarb_zapisz(int fd, int id, const void *dane, size_t n);

/* Takes one pending reply without waiting: 1 = a result (*wynik: 0 or K1_MAG_* < 0, *tryb),
 * 0 = nothing, -1 = the channel is closed (stop using it). Anything else is dropped. */
int k1_skarb_wynik(int fd, int *wynik, int *tryb);

struct k1_komunikat;
/* Like k1_skarb_wynik, but returns the whole WYNIK (request type, result, mode, password set,
 * seconds until the next password attempt) — Ustawienia → Hasło (M5e). */
int k1_skarb_wynik_pelny(int fd, struct k1_komunikat *k);

#endif

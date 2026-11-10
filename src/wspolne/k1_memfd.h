/* K1 — data handed between services as sealed memfd (M5d: skarbiec and its clients).
 * The receiver accepts only a memfd sealed against shrinking, growing, writing and new seals, of
 * exactly the announced size: the content cannot change after the check. */
#ifndef K1_MEMFD_H
#define K1_MEMFD_H

#include <stddef.h>

/* A new sealed memfd holding `n` bytes of `dane` (n may be 0). Returns the fd or -1 (errno). */
int k1_memfd_z_danymi(const char *nazwa, const void *dane, size_t n);

/* Checks that `fd` is a fully sealed memfd of exactly `n` bytes and reads it into `buf`.
 * Returns 0, or -1 when the descriptor is not acceptable or reading failed. Does not close `fd`. */
int k1_memfd_czytaj(int fd, void *buf, size_t n);

/* For data written piece by piece (0.8.0: a file from skarbiec): a new empty memfd (-1 on error), and the seals
 * that make it final (0, or -1 with the fd closed). */
int k1_memfd_nowy(const char *nazwa);
int k1_memfd_zapieczetuj(int fd);

/* Only the check of k1_memfd_czytaj (fully sealed, exactly `n` bytes): then read it with pread. 0 or -1. */
int k1_memfd_sprawdz(int fd, size_t n);

#endif

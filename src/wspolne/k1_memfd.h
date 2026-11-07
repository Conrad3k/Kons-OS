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

#endif

/* K1 — the clipboard (0.11.0 step 5, ADR 0052, T8; the owner's D5: A): text only, UTF-8, at most K1_SCHOWEK_MAKS bytes,
 * kept by ekran in memory (never in skarbiec, never in a log). Every side that takes a clipboard text checks it with
 * k1_schowek_dobry — it may come from a zone. Pure: host test test_schowek.c, fuzzing fuzz_schowek.c. */
#ifndef K1_SCHOWEK_H
#define K1_SCHOWEK_H

#include <stddef.h>
#include <stdint.h>

#define K1_SCHOWEK_MAKS (1u << 20) /* bytes of the text (1 MiB) */

/* 1 when t[0..n) is a text the clipboard keeps: 1..K1_SCHOWEK_MAKS bytes of valid UTF-8 (shortest forms, no surrogates),
 * no NUL and no control characters but tab, newline and carriage return. */
int k1_schowek_dobry(const uint8_t *t, size_t n);

#endif

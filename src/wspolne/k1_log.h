/* K1 — minimal logging to the console (fd 2).
 *
 * Messages go to the console only (one-way log, SPEC §7): the serial port in the tests, nowhere in the release (0.13.0
 * step 4, ADR 0059: console=ttynull) — nadzorca keeps the last lines in memory for the report. No heap allocation:
 * each message is formatted into a fixed stack buffer and truncated if too long. */
#ifndef K1_LOG_H
#define K1_LOG_H

#include <stddef.h>

/* Writes "<tag>: <message>\n" to fd 2. Never fails (errors are ignored: there is nowhere
 * else to report them). Truncated messages end with "…". */
void k1_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* 0.13.0 step 3 (ADR 0058): when set, every line written (with its '\n') goes here too — nadzorca keeps the last ones in
 * memory for the rescue mode's report (logi.c). */
extern void (*k1_log_kopia)(const char *linia, size_t dl);

/* Makes untrusted text (a log line of a service) safe for a terminal, in place: control
 * characters (C0, DEL and the C1 range U+0080..U+009F), invalid or overlong UTF-8 and surrogates
 * become '?' (one per byte); valid UTF-8 such as Polish letters stays. Returns the number of bytes
 * replaced. */
size_t k1_log_oczysc(char *tekst, size_t dlugosc);

/* Critical error (SPEC §7: controlled handling of critical errors). Logs "<tag>: BŁĄD KRYTYCZNY: <message>", then (0.13.0
 * step 4, ADR 0059) the screen of a critical error: the kernel's log cleared and only K1's description put in it (in
 * ASCII — k1_ascii), and the process ends with K1_AWARIA_KOD. Used by PID 1 only: PID 1 ending is a kernel panic, whose
 * screen (DRM_PANIC, kernel's "kmsg") shows that description until the kernel restarts the computer (panic=N; up to
 * 0.12.3: 5 s on the serial port and a power-off). Never returns. */
#define K1_AWARIA_KOD 1
_Noreturn void k1_awaria(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* 0.13.0 step 4: `tekst` (UTF-8) in printable ASCII into `wyj` (n bytes, ends with '\0'): Polish letters without their
 * marks, „” as ", – — as -, … as ..., → as ->, × as x; every other character (control ones too) one '?'. Cut at a
 * whole character when `wyj` is full. The length written. */
size_t k1_ascii(char *wyj, size_t n, const char *tekst);

/* Fatal error in a service (not PID 1): logs "<tag>: BŁĄD: <message>" and exits with code 1.
 * The supervisor decides what happens next; a failing service must never take the system down. */
_Noreturn void k1_koniec(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif

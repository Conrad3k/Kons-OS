/* K1 — minimal logging to the console (fd 2).
 *
 * Messages go to the serial console only (one-way log, SPEC §7). No heap allocation:
 * each message is formatted into a fixed stack buffer and truncated if too long. */
#ifndef K1_LOG_H
#define K1_LOG_H

#include <stddef.h>

/* Writes "<tag>: <message>\n" to fd 2. Never fails (errors are ignored: there is nowhere
 * else to report them). Truncated messages end with "…". */
void k1_log(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Makes untrusted text (a log line of a service) safe for a terminal, in place: control
 * characters (C0, DEL and the C1 range U+0080..U+009F), invalid or overlong UTF-8 and surrogates
 * become '?' (one per byte); valid UTF-8 such as Polish letters stays. Returns the number of bytes
 * replaced. */
size_t k1_log_oczysc(char *tekst, size_t dlugosc);

/* Critical error (SPEC §7: controlled handling of critical errors). Logs
 * "<tag>: BŁĄD KRYTYCZNY: <message>", waits K1_AWARIA_ZWLOKA_S seconds so that a human can read
 * the serial log, then powers the machine off. Never returns. Used by PID 1 instead of exit():
 * if PID 1 exits, the kernel panics. */
#define K1_AWARIA_ZWLOKA_S 5
_Noreturn void k1_awaria(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Fatal error in a service (not PID 1): logs "<tag>: BŁĄD: <message>" and exits with code 1.
 * The supervisor decides what happens next; a failing service must never take the system down. */
_Noreturn void k1_koniec(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif

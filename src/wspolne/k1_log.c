#include "k1_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>

#define K1_LOG_MAX 512

static void write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
}

static void k1_vlog(const char *tag, const char *prefix, const char *fmt, va_list ap)
    __attribute__((format(printf, 3, 0)));

static void k1_vlog(const char *tag, const char *prefix, const char *fmt, va_list ap)
{
    char buf[K1_LOG_MAX];
    size_t len = 0;
    int saved_errno = errno;
    int n = snprintf(buf, sizeof buf, "%s: %s", tag, prefix);
    if (n < 0)
        return;
    len = (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;

    n = vsnprintf(buf + len, sizeof buf - len, fmt, ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof buf - len) {
        /* Truncated: mark it. 4 bytes = "…" (3 bytes UTF-8) + '\n'. */
        len = sizeof buf - 5;
        memcpy(buf + len, "\xe2\x80\xa6", 3);
        len += 3;
    } else {
        len += (size_t)n;
    }
    if (len >= sizeof buf)
        len = sizeof buf - 1;
    buf[len++] = '\n';
    write_all(2, buf, len);
    errno = saved_errno;
}

void k1_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    k1_vlog(tag, "", fmt, ap);
    va_end(ap);
}

void k1_awaria(const char *tag, const char *fmt, ...)
{
    va_list ap;
    struct timespec ts = { .tv_sec = K1_AWARIA_ZWLOKA_S, .tv_nsec = 0 };

    va_start(ap, fmt);
    k1_vlog(tag, "BŁĄD KRYTYCZNY: ", fmt, ap);
    va_end(ap);
    k1_log(tag, "system zostanie wyłączony za %d s", K1_AWARIA_ZWLOKA_S);

    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
    sync();
    reboot(RB_POWER_OFF);
    /* Only reached if power-off failed (e.g. no permission when run outside PID 1). */
    k1_log(tag, "wyłączenie nie powiodło się (%s) — zatrzymano", strerror(errno));
    for (;;)
        pause();
}

void k1_koniec(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    k1_vlog(tag, "BŁĄD: ", fmt, ap);
    va_end(ap);
    exit(1);
}

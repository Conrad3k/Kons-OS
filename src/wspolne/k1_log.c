#include "k1_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/klog.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define K1_LOG_MAX 512
#ifdef K1_WERSJA
#define K1_WERSJA_AWARII K1_WERSJA
#else
#define K1_WERSJA_AWARII ""
#endif

void (*k1_log_kopia)(const char *linia, size_t dl);

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

/* Length of the valid UTF-8 sequence at t[0..n) that is not a control character, or 0. */
static size_t znak_utf8(const unsigned char *t, size_t n)
{
    size_t dl, i;
    uint32_t kod;
    if (t[0] >= 0x20 && t[0] < 0x7f)
        return 1;
    if (t[0] >= 0xc2 && t[0] <= 0xdf) {
        dl = 2;
        kod = t[0] & 0x1fu;
    } else if (t[0] >= 0xe0 && t[0] <= 0xef) {
        dl = 3;
        kod = t[0] & 0x0fu;
    } else if (t[0] >= 0xf0 && t[0] <= 0xf4) {
        dl = 4;
        kod = t[0] & 0x07u;
    } else {
        return 0;
    }
    if (n < dl)
        return 0;
    for (i = 1; i < dl; i++) {
        if ((t[i] & 0xc0) != 0x80)
            return 0;
        kod = (kod << 6) | (t[i] & 0x3fu);
    }
    if ((dl == 3 && kod < 0x800) || (dl == 4 && kod < 0x10000) || kod > 0x10ffff || (kod >= 0xd800 && kod <= 0xdfff) ||
        (kod >= 0x80 && kod <= 0x9f))
        return 0; /* overlong, out of range, surrogate, C1 control */
    return dl;
}

size_t k1_log_oczysc(char *tekst, size_t dlugosc)
{
    size_t i = 0, zmienione = 0;
    while (i < dlugosc) {
        size_t dl = znak_utf8((const unsigned char *)tekst + i, dlugosc - i);
        if (dl == 0) {
            tekst[i++] = '?';
            zmienione++;
        } else {
            i += dl;
        }
    }
    return zmienione;
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
    if (k1_log_kopia != NULL)
        k1_log_kopia(buf, len);
    errno = saved_errno;
}

void k1_log(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    k1_vlog(tag, "", fmt, ap);
    va_end(ap);
}

/* 0.13.0 step 4 (ADR 0059): Polish letters and K1's typographic signs in ASCII, the rest of non-ASCII as '?'. */
static const struct {
    const char *utf8, *ascii;
} ASCII[] = {
    { "ą", "a" }, { "ć", "c" }, { "ę", "e" }, { "ł", "l" }, { "ń", "n" }, { "ó", "o" }, { "ś", "s" }, { "ź", "z" },
    { "ż", "z" }, { "Ą", "A" }, { "Ć", "C" }, { "Ę", "E" }, { "Ł", "L" }, { "Ń", "N" }, { "Ó", "O" }, { "Ś", "S" },
    { "Ź", "Z" }, { "Ż", "Z" }, { "„", "\"" }, { "”", "\"" }, { "–", "-" }, { "—", "-" }, { "…", "..." },
    { "→", "->" }, { "×", "x" },
};

size_t k1_ascii(char *wyj, size_t n, const char *tekst)
{
    size_t d = 0, i, t;
    if (n == 0)
        return 0;
    while (*tekst != '\0') {
        const char *z = "?";
        size_t dl = 1;
        if ((unsigned char)*tekst >= 0x20 && (unsigned char)*tekst < 0x7f) {
            dl = 1;
            z = NULL;
        } else {
            for (i = 0; i < sizeof ASCII / sizeof ASCII[0]; i++)
                if (strncmp(tekst, ASCII[i].utf8, strlen(ASCII[i].utf8)) == 0) {
                    z = ASCII[i].ascii;
                    dl = strlen(ASCII[i].utf8);
                    break;
                }
            if (i == sizeof ASCII / sizeof ASCII[0]) /* another character: '?' for all its bytes at once */
                while ((tekst[dl] & 0xc0) == 0x80)
                    dl++;
        }
        t = z ? strlen(z) : 1;
        if (d + t >= n)
            break;
        memcpy(wyj + d, z ? z : tekst, t);
        d += t;
        tekst += dl;
    }
    wyj[d] = '\0';
    return d;
}

/* 0.13.0 step 4 (ADR 0059): the screen of a critical error — the kernel's log cleared, then only K1's description in it
 * (/dev/kmsg: the initramfs's devtmpfs, or init's), in ASCII: the kernel's panic screen (DRM_PANIC, "kmsg") draws these
 * lines with its own font, which has no Polish letters. Without /dev/kmsg (before devtmpfs) the panic line stays. */
static void ekran_awarii(const char *tag, const char *opis)
{
    static const char *const kmsg[] = { "/dev/kmsg", "/rdzen/jadro/dev/kmsg" };
    char a[K1_LOG_MAX], w[K1_LOG_MAX + 64];
    const char *linie[] = {
        "<2>K1: ==========================================================",
        "<2>K1: Kons K1 " K1_WERSJA_AWARII " nie moze wystartowac (blad krytyczny).",
        w,
        "<2>K1: Komputer uruchomi sie ponownie sam. Po dwoch nieudanych startach K1 pokaze menu:",
        "<2>K1: wybierz \"poprzednia wersja\" albo \"tryb ratunkowy\" (menu daje tez Esc zaraz po wlaczeniu).",
        "<2>K1: Zrob zdjecie tego ekranu - pomoze znalezc przyczyne.",
        "<2>K1: ==========================================================",
    };
    int fd = -1, n;
    size_t i;
    for (i = 0; i < sizeof kmsg / sizeof kmsg[0] && fd < 0; i++)
        fd = open(kmsg[i], O_WRONLY | O_CLOEXEC | O_NOCTTY);
    if (fd < 0)
        return;
    k1_ascii(a, sizeof a, opis);
    n = snprintf(w, sizeof w, "<2>K1: Powod (%s): %s", tag, a);
    if (n < 0)
        w[0] = '\0';
    klogctl(5, NULL, 0); /* SYSLOG_ACTION_CLEAR: the panic screen starts from here (kmsg_dump_rewind) */
    for (i = 0; i < sizeof linie / sizeof linie[0]; i++)
        write_all(fd, linie[i], strlen(linie[i]));
    close(fd);
}

void k1_awaria(const char *tag, const char *fmt, ...)
{
    va_list ap;
    char opis[K1_LOG_MAX];

    va_start(ap, fmt);
    if (vsnprintf(opis, sizeof opis, fmt, ap) < 0)
        opis[0] = '\0';
    va_end(ap);
    k1_log(tag, "BŁĄD KRYTYCZNY: %s", opis);
    k1_log(tag, "ekran awarii, ponowne uruchomienie");
    ekran_awarii(tag, opis);
    tcdrain(2); /* the console's buffer out first: a panic does not flush it (CI with KVM lost these last lines) */
    sync();
    /* PID 1 ending is a kernel panic: its screen (DRM_PANIC) shows the lines above until panic=N restarts the computer
     * (the release: 60 s; the tests: -1 — at once, QEMU -no-reboot ends). */
    _exit(K1_AWARIA_KOD);
}

void k1_koniec(const char *tag, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    k1_vlog(tag, "BŁĄD: ", fmt, ap);
    va_end(ap);
    exit(1);
}

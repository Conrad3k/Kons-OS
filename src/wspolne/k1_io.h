/* K1 — reading and writing a whole buffer at an offset (block devices and partitions return short counts
 * and EINTR): one copy for nadzorca, the installer and skarbiec. 0, or -1 (errno set; a short device: EIO).
 * And a small text file (sysfs, cgroup) in one read. */
#ifndef K1_IO_H
#define K1_IO_H

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

static inline int k1_czytaj_calosc(int fd, void *buf, size_t n, uint64_t off)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = pread(fd, (char *)buf + got, n - got, (off_t)(off + got));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            if (r == 0)
                errno = EIO;
            return -1;
        }
        got += (size_t)r;
    }
    return 0;
}

static inline int k1_pisz_calosc(int fd, const void *buf, size_t n, uint64_t off)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(fd, (const char *)buf + done, n - done, (off_t)(off + done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            if (r == 0)
                errno = EIO;
            return -1;
        }
        done += (size_t)r;
    }
    return 0;
}

/* A small text file (sysfs attribute, cgroup file) into buf, NUL-terminated: the bytes read (> 0), or -1
 * (missing, empty, a symlink). `dir`: AT_FDCWD or an open directory the path is relative to. */
static inline ssize_t k1_czytaj_plik(int dir, const char *sciezka, char *buf, size_t n)
{
    int fd = openat(dir, sciezka, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t r;
    if (fd < 0)
        return -1;
    r = read(fd, buf, n - 1);
    close(fd);
    if (r <= 0)
        return -1;
    buf[r] = '\0';
    return r;
}

#endif

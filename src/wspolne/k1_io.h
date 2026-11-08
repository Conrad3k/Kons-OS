/* K1 — reading and writing a whole buffer at an offset (block devices and partitions return short counts
 * and EINTR): one copy for nadzorca, the installer and skarbiec. 0, or -1 (errno set; a short device: EIO). */
#ifndef K1_IO_H
#define K1_IO_H

#include <errno.h>
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

#endif

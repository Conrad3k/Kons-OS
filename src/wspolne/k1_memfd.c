/* K1 — sealed memfd for handing data between services (M5d). See k1_memfd.h. */
#include "k1_memfd.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U /* include/uapi/linux/memfd.h */
#endif

#define PIECZECIE (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL)

int k1_memfd_z_danymi(const char *nazwa, const void *dane, size_t n)
{
    size_t zapisane = 0;
    int fd = (int)syscall(SYS_memfd_create, nazwa, MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
    if (fd < 0)
        return -1;
    while (zapisane < n) {
        ssize_t r = write(fd, (const char *)dane + zapisane, n - zapisane);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0) {
            int e = errno;
            close(fd);
            errno = r < 0 ? e : EIO;
            return -1;
        }
        zapisane += (size_t)r;
    }
    if (fcntl(fd, F_ADD_SEALS, PIECZECIE) != 0) {
        int e = errno;
        close(fd);
        errno = e;
        return -1;
    }
    return fd;
}

int k1_memfd_czytaj(int fd, void *buf, size_t n)
{
    struct stat st;
    size_t got = 0;
    int p = fcntl(fd, F_GET_SEALS);
    if (p < 0 || (p & PIECZECIE) != PIECZECIE || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size != (off_t)n || lseek(fd, 0, SEEK_SET) != 0)
        return -1;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        got += (size_t)r;
    }
    return 0;
}

#include "k1_kanal.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int k1_kanal_wyslij(int gniazdo, const void *buf, size_t len, int fd_przekaz)
{
    struct msghdr m;
    struct iovec iov;
    union {
        char bufor[CMSG_SPACE(sizeof(int))];
        struct cmsghdr wyrownanie;
    } ctl;
    ssize_t n;

    memset(&m, 0, sizeof m);
    memset(&ctl, 0, sizeof ctl);
    iov.iov_base = (void *)(uintptr_t)buf;
    iov.iov_len = len;
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    if (fd_przekaz >= 0) {
        struct cmsghdr *c;
        m.msg_control = ctl.bufor;
        m.msg_controllen = sizeof ctl.bufor;
        c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd_przekaz, sizeof(int));
    }
    do {
        n = sendmsg(gniazdo, &m, MSG_NOSIGNAL | MSG_DONTWAIT);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        return -1;
    if ((size_t)n != len) {
        errno = EMSGSIZE;
        return -1;
    }
    return 0;
}

enum k1_kanal_wynik k1_kanal_odbierz(int gniazdo, struct k1_odebrane *o)
{
    struct msghdr m;
    struct iovec iov;
    union {
        char bufor[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(4 * sizeof(int))];
        struct cmsghdr wyrownanie;
    } ctl;
    struct cmsghdr *c;
    ssize_t n;

    memset(o, 0, sizeof *o);
    o->fd = -1;
    memset(&m, 0, sizeof m);
    iov.iov_base = o->dane;
    iov.iov_len = sizeof o->dane;
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = ctl.bufor;
    m.msg_controllen = sizeof ctl.bufor;
    do {
        n = recvmsg(gniazdo, &m, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    } while (n < 0 && errno == EINTR);
    if (n < 0)
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? K1_KANAL_PUSTO : K1_KANAL_BLAD;
    if (n == 0 && m.msg_controllen == 0)
        return K1_KANAL_KONIEC; /* SEQPACKET: zero-length read = peer closed */
    o->dlugosc = (size_t)n;
    if (m.msg_flags & (MSG_TRUNC | MSG_CTRUNC))
        o->obciete = 1;
/* musl's CMSG_NXTHDR macro trips -Wsign-conversion; the arithmetic is the kernel ABI's. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
    for (c = CMSG_FIRSTHDR(&m); c != NULL; c = CMSG_NXTHDR(&m, c)) {
        if (c->cmsg_level != SOL_SOCKET)
            continue;
        if (c->cmsg_type == SCM_RIGHTS && c->cmsg_len >= CMSG_LEN(0)) {
            size_t ile = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int), i;
            for (i = 0; i < ile; i++) {
                int fd;
                memcpy(&fd, CMSG_DATA(c) + i * sizeof(int), sizeof fd);
                if (o->fd < 0 && i == 0) {
                    o->fd = fd;
                } else {
                    close(fd);
                    o->obciete = 1;
                }
            }
        } else if (c->cmsg_type == SCM_CREDENTIALS && c->cmsg_len >= CMSG_LEN(sizeof(struct ucred))) {
            struct ucred u;
            memcpy(&u, CMSG_DATA(c), sizeof u);
            o->ma_nadawce = 1;
            o->pid = u.pid;
            o->uid = u.uid;
            o->gid = u.gid;
        }
    }
#pragma GCC diagnostic pop
    return K1_KANAL_KOMUNIKAT;
}

int k1_kanal_utworz(int pary[2])
{
    int tak = 1;
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pary) != 0)
        return -1;
    if (setsockopt(pary[0], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
        int e = errno;
        close(pary[0]);
        close(pary[1]);
        errno = e;
        return -1;
    }
    return 0;
}

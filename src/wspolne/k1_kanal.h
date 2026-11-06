/* K1 — sending and receiving messages on AF_UNIX SOCK_SEQPACKET channels (ADR 0007).
 *
 * Channels are socketpairs created by the supervisor and handed to services as inherited file
 * descriptors — there are no sockets in the file system. One send = one packet = one message.
 * The receiving end has SO_PASSCRED set (by the supervisor, before any message can arrive), so the
 * kernel attaches the real sender credentials to every packet: k1_kanal_odbierz() returns them and
 * receivers compare the UID with the identity the supervisor assigned to that channel.
 * Unexpected file descriptors are always closed by the receiver, never leaked. */
#ifndef K1_KANAL_H
#define K1_KANAL_H

#include <stddef.h>
#include <sys/types.h>

struct k1_odebrane {
    unsigned char dane[64]; /* larger than any message: oversized packets are detected */
    size_t dlugosc;
    int fd;          /* passed file descriptor, -1 if none */
    int ma_nadawce;  /* credentials present */
    pid_t pid;
    uid_t uid;
    gid_t gid;
    int obciete;     /* packet or control data truncated (MSG_TRUNC / MSG_CTRUNC): reject */
};

enum k1_kanal_wynik {
    K1_KANAL_KOMUNIKAT = 1,
    K1_KANAL_PUSTO = 0,       /* EAGAIN: nothing to read */
    K1_KANAL_KONIEC = -1,     /* peer closed the channel */
    K1_KANAL_BLAD = -2,       /* other error (errno set) */
};

/* Sends `len` bytes (one packet) with an optional file descriptor (fd_przekaz >= 0).
 * Never raises SIGPIPE, never blocks (MSG_DONTWAIT). Returns 0 or -1 (errno set). */
int k1_kanal_wyslij(int gniazdo, const void *buf, size_t len, int fd_przekaz);

/* Receives one packet. At most one descriptor is accepted; any others are closed and the packet
 * is marked `obciete`. */
enum k1_kanal_wynik k1_kanal_odbierz(int gniazdo, struct k1_odebrane *o);

/* Creates a channel: SOCK_SEQPACKET socketpair, both ends close-on-exec, the receiving end
 * (`pary[0]`) with SO_PASSCRED. Returns 0 or -1. */
int k1_kanal_utworz(int pary[2]);

#endif

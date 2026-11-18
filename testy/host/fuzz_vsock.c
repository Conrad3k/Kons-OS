/* libFuzzer harness for Raptor Bridge's path of a packet with its connections' state (0.10.5 step 5, S35): vsock.c,
 * polaczenia.c and lacznik.c together, as the VMM runs them — the guest's packets on the transmit queue (any header:
 * ports that collide, every kind, lengths, credit, addresses), its receive buffers, nadzorca's answers (a socket — one
 * end of a socket pair standing for the TCP connection — or a refusal) and the socket's events (the server's bytes, its
 * end). The input is a list of steps. What must hold: no memory error; every packet the VMM gives the guest is a whole
 * header from host 2 to guest 3 inside one buffer, never more bytes than the buffer; places in use never more than
 * K1_STREFA_POLACZENIA_MAKS. A hostile packet may end the machine (koniec): the input ends there. */
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/virtio_mmio.h>
#include <linux/virtio_vsock.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "okno.h"
#include "polaczenia.h"
#include "strefa.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
extern const struct mmio_ops vsock_ops;

#define PAMIEC 0x80000u
#define Q 64u
#define RX_DESC 0x0u
#define RX_AVAIL 0x1000u
#define RX_USED 0x2000u
#define TX_DESC 0x3000u
#define TX_AVAIL 0x4000u
#define TX_USED 0x5000u
#define RX_BUF 0x10000u /* Q buffers of 4 KiB */
#define TX_BUF 0x50000u /* packets to send, one after another */
#define BUF 4096u

struct maszyna mz;
static uint8_t pam[PAMIEC];
static const struct gosc_pamiec m = { pam, PAMIEC, 0 };
static int skonczona;

/* --- what the VMM takes from elsewhere --- */
void koniec(const char *powod) { (void)powod; skonczona = 1; }
void obudz(void) {}
void zamek_wez(const char *kto) { (void)kto; }
void zamek_oddaj(void) {}
void okno_etap(int etap) { (void)etap; }
void gpu_liczniki(unsigned *n, unsigned *mib) { *n = 0, *mib = 0; }
void balon_cel(uint32_t mib) { (void)mib; }
void balon_stan(struct balon_stan *s) { memset(s, 0, sizeof *s); }
void k1_log(const char *tag, const char *fmt, ...) { (void)tag, (void)fmt; }

/* nadzorca: the requests the VMM sent, answered by the input's steps */
static uint32_t prosby[64];
static int n_prosb;
static int odp_obiekt = -1, odp_typ, odp_fd = -1;
static int serwer[K1_STREFA_POLACZENIA_MAKS + 2], n_serwer; /* our ends of the socket pairs */

int k1_kanal_wyslij(int gniazdo, const void *buf, size_t len, int fd_przekaz)
{
    struct k1_komunikat k;
    (void)gniazdo, (void)fd_przekaz;
    if (len != sizeof k)
        abort();
    memcpy(&k, buf, sizeof k);
    if (k.typ == K1_KOM_STREFA_POLACZ && n_prosb < 64)
        prosby[n_prosb++] = k.obiekt;
    return 0;
}

enum k1_odbior_wynik k1_odbierz_komunikat(int gniazdo, uid_t uid, enum k1_kanal_kierunek kier, struct k1_komunikat *k,
                                          int *fd)
{
    (void)gniazdo, (void)uid, (void)kier;
    if (odp_obiekt < 0)
        return K1_ODB_PUSTO;
    *k = k1_komunikat_nowy(odp_typ, (uint32_t)odp_obiekt, odp_typ == K1_KOM_STREFA_GNIAZDO ? K1_STREFA_TCP : 1, 0, 0, 0, 0);
    *fd = odp_fd;
    odp_obiekt = -1, odp_fd = -1;
    return K1_ODB_JEST;
}

/* --- the guest's side --- */
static struct mmio_urz u;
static uint16_t tx_idx, rx_dane;

static void kolejki(void)
{
    static const struct { uint32_t d, a, us; } k[2] = { { RX_DESC, RX_AVAIL, RX_USED }, { TX_DESC, TX_AVAIL, TX_USED } };
    for (int i = 0; i < 2; i++)
        u.q[i] = (struct virtq){ .rozmiar = Q, .gotowa = 1, .desc = k[i].d, .avail = k[i].a, .used = k[i].us };
    u.status = 15;
}

/* Every receive buffer the VMM filled is checked, then offered again. */
static void bufory_rx(void)
{
    uint16_t uzyte, i;
    memcpy(&uzyte, pam + RX_USED + 2, 2);
    for (; rx_dane != uzyte; rx_dane++) {
        uint32_t id, dl;
        struct virtio_vsock_hdr h;
        memcpy(&id, pam + RX_USED + 4 + 8 * (rx_dane % Q), 4);
        memcpy(&dl, pam + RX_USED + 8 + 8 * (rx_dane % Q), 4);
        memcpy(&h, pam + RX_BUF + id * BUF, sizeof h);
        if (id >= Q || dl < sizeof h || dl > BUF || h.src_cid != 2 || h.dst_cid != 3 || dl != sizeof h + h.len)
            abort();
    }
    for (i = 0; i < Q; i++) { /* all Q buffers available again */
        uint64_t a = RX_BUF + (uint64_t)i * BUF;
        uint32_t dl = BUF;
        uint16_t f = 2 /* VRING_DESC_F_WRITE */, idx = (uint16_t)(uzyte + i);
        memcpy(pam + RX_DESC + 16 * i, &a, 8);
        memcpy(pam + RX_DESC + 16 * i + 8, &dl, 4);
        memcpy(pam + RX_DESC + 16 * i + 12, &f, 2);
        memcpy(pam + RX_AVAIL + 4 + 2 * (idx % Q), &i, 2);
    }
    uzyte = (uint16_t)(uzyte + Q);
    memcpy(pam + RX_AVAIL + 2, &uzyte, 2);
}

/* One packet from the guest, built from the input: its header fields and up to 2 KiB of bytes. */
static size_t pakiet(const uint8_t *d, size_t n)
{
    static const uint16_t op[] = { 0, VIRTIO_VSOCK_OP_REQUEST, VIRTIO_VSOCK_OP_RESPONSE, VIRTIO_VSOCK_OP_RST,
                                   VIRTIO_VSOCK_OP_SHUTDOWN, VIRTIO_VSOCK_OP_RW, VIRTIO_VSOCK_OP_CREDIT_UPDATE,
                                   VIRTIO_VSOCK_OP_CREDIT_REQUEST };
    struct virtio_vsock_hdr h = { .src_cid = 3, .dst_cid = 2, .type = VIRTIO_VSOCK_TYPE_STREAM, .buf_alloc = 65536 };
    size_t dl, w;
    uint64_t a = TX_BUF;
    uint32_t c;
    uint16_t i = (uint16_t)(tx_idx % Q), f = 0;
    if (n < 8)
        return n;
    h.op = op[d[0] & 7];
    h.src_port = 1000u + (d[1] & 15);  /* ports that collide */
    h.dst_port = d[2] & 3;             /* 0..3: control (1), connections (2), nobody */
    h.flags = d[3] & 3;
    dl = (size_t)d[4] << 3;            /* 0..2040 */
    if (d[5] & 0x80)
        h.buf_alloc = (uint32_t)d[6] << 8;
    h.fwd_cnt = (uint32_t)d[7] << 10;
    if (d[5] == 0xFF) /* now and then a header the VMM must refuse */
        h.src_cid = d[6], h.type = d[7];
    d += 8, n -= 8;
    dl = dl < n ? dl : n;
    h.len = (uint32_t)dl;
    if (h.op == VIRTIO_VSOCK_OP_RW && n > dl && (d[dl] & 1))
        h.len += 64; /* a header longer than its bytes */
    memcpy(pam + a, &h, sizeof h);
    memcpy(pam + a + sizeof h, d, dl);
    c = (uint32_t)(sizeof h + dl);
    memcpy(pam + TX_DESC + 16 * i, &a, 8);
    memcpy(pam + TX_DESC + 16 * i + 8, &c, 4);
    memcpy(pam + TX_DESC + 16 * i + 12, &f, 2);
    memcpy(pam + TX_DESC + 16 * i + 14, &f, 2);
    memcpy(pam + TX_AVAIL + 4 + 2 * i, &i, 2);
    tx_idx++;
    memcpy(pam + TX_AVAIL + 2, &tx_idx, 2);
    w = 8 + dl;
    {
        const char *powod = NULL;
        if (vsock_ops.powiadom(&u, 1, &powod) != 0)
            skonczona = 1;
    }
    return w;
}

/* nadzorca answers the oldest request: a socket (a socket pair, its other end ours) or a refusal. */
static void odpowiedz(uint8_t b)
{
    int para[2];
    if (n_prosb == 0)
        return;
    odp_obiekt = (int)prosby[0];
    memmove(prosby, prosby + 1, (size_t)--n_prosb * sizeof prosby[0]);
    odp_typ = (b & 1) ? K1_KOM_STREFA_GNIAZDO : K1_KOM_STREFA_ODMOWA;
    if (odp_typ == K1_KOM_STREFA_GNIAZDO && n_serwer < (int)(sizeof serwer / sizeof serwer[0]) &&
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, para) == 0) {
        odp_fd = para[0];
        serwer[n_serwer++] = para[1];
    } else {
        odp_typ = K1_KOM_STREFA_ODMOWA;
    }
}

/* The main thread's turn: nadzorca's answer, the sockets as they are. */
static void obsluz(void)
{
    struct pollfd p[2 + K1_STREFA_POLACZENIA_MAKS];
    int n, i;
    n = pol_deskryptory(p, (int)(sizeof p / sizeof p[0]));
    p[0].revents = odp_obiekt >= 0 ? POLLIN : 0;
    p[1].revents = 0;
    if (poll(p + 2, (nfds_t)(n - 2), 0) < 0)
        return;
    for (i = 0; i < n; i++)
        if (p[i].fd < 0)
            p[i].revents = 0;
    pol_obsluz(p, n);
}

/* The server's side as the input says (its bytes, its end), then the main thread's turn. */
static void gniazda(uint8_t b)
{
    if (n_serwer > 0) {
        int s = serwer[b % n_serwer];
        if (b & 0x40) {
            static const uint8_t bajty[1500] = { 5, 0 };
            if (write(s, bajty, (size_t)(b & 0x3F) * 20 + 1) < 0) { /* full or gone: as a real socket */
            }
        }
        if ((b & 0xC0) == 0xC0)
            shutdown(s, SHUT_WR);
    }
    obsluz();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct k1_strefa_opis opis = { .okno = 1 };
    size_t i = 0;
    memset(pam, 0, sizeof pam);
    memset(&u, 0, sizeof u);
    mz.opis = &opis;
    mz.tryb = 0;
    skonczona = 0, tx_idx = 0, rx_dane = 0, n_prosb = 0, odp_obiekt = -1;
    u.ops = &vsock_ops, u.pamiec = &m;
    vsock_ops.reset(&u);
    kolejki();
    bufory_rx();
    while (i < size && !skonczona) {
        uint8_t k = data[i++];
        if (k < 0xA0)
            i += pakiet(data + i, size - i);
        else if (k < 0xC0)
            odpowiedz(k);
        else if (k < 0xE0 && i < size)
            gniazda(data[i++]);
        else if (k == 0xFF)
            vsock_ops.reset(&u), kolejki(); /* the guest resets the device */
        if (!skonczona) {
            vsock_wyslij();
            bufory_rx();
        }
        if (pol_zajete() > K1_STREFA_POLACZENIA_MAKS)
            abort();
    }
    if (odp_obiekt >= 0)
        obsluz();
    while (n_prosb > 0) { /* every request answered (a refusal): no place stays reserved for the next input */
        odpowiedz(0);
        obsluz();
    }
    vsock_ops.reset(&u); /* every place free, every socket closed */
    if (odp_fd >= 0)
        close(odp_fd);
    odp_fd = -1;
    for (int s = 0; s < n_serwer; s++)
        close(serwer[s]);
    n_serwer = 0;
    return 0;
}

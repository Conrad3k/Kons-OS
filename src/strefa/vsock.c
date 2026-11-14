/* K1 strefa — virtio-vsock (virtio 1.2, section 5.10), stream sockets only. In 0.10.0 step 1 one connection: the guest's
 * control channel to port VSOCK_STEROWANIE of the host (CID 2), text lines "gotowa" (the guest is up) and "zamykam";
 * the VMM sends "zamknij" when ekran asks to close the zone. Any other connection is refused (RST); step 2 adds the
 * browser's connections. Every header from the guest is checked: its CIDs, type, length within the chain, credit
 * (a guest that sends more than the room we announced is hostile: the machine ends). */
#include <stdio.h>
#include <string.h>

#include <linux/virtio_ids.h>
#include <linux/virtio_vsock.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "strefa.h"

#define CID_HOSTA 2
#define CID_GOSCIA 3
#define VSOCK_STEROWANIE 1
#define NASZ_BUFOR 4096u
#define CZEKAJACE 16
#define DANE_MAKS 64u
#define LINIA_MAKS 64u
#define KOLEJKA_RX 0
#define KOLEJKA_TX 1

_Static_assert(sizeof(struct virtio_vsock_hdr) == 44, "vsock: nagłówek 44 B");

struct pakiet {
    struct virtio_vsock_hdr h;
    uint8_t dane[DANE_MAKS];
};

static struct {
    int polaczony;
    uint32_t port_goscia;
    uint32_t jego_bufor, jego_fwd; /* the guest's room for our data, as it last said */
    uint32_t wyslane;              /* bytes we sent on the connection */
    uint32_t odebrane, zgloszone;  /* bytes we took from the guest; what we last told it */
    char linia[LINIA_MAKS + 1];
    size_t dl;
    struct pakiet czeka[CZEKAJACE];
    int n_czeka;
    unsigned long pakiety, odrzucone;
} v;

static void naglowek(struct virtio_vsock_hdr *h, uint32_t port_goscia, uint16_t op, uint32_t dl)
{
    memset(h, 0, sizeof *h);
    h->src_cid = CID_HOSTA;
    h->dst_cid = CID_GOSCIA;
    h->src_port = VSOCK_STEROWANIE;
    h->dst_port = port_goscia;
    h->len = dl;
    h->type = VIRTIO_VSOCK_TYPE_STREAM;
    h->op = op;
    h->buf_alloc = NASZ_BUFOR;
    h->fwd_cnt = v.odebrane;
}

/* Sends what waits into the guest's receive buffers, as many as it gave. */
static void wyslij_czekajace(void)
{
    struct mmio_urz *u = &mz.urz[URZ_VSOCK];
    int bylo = 0, i;
    const char *powod = NULL;
    while (v.n_czeka > 0 && mmio_dziala(u, KOLEJKA_RX)) {
        struct vq_lancuch l;
        struct pakiet *p = &v.czeka[0];
        uint64_t dl = sizeof p->h + p->h.len;
        int w = vq_wez(&u->q[KOLEJKA_RX], u->pamiec, &l, &powod);
        if (w < 0) {
            koniec(powod);
            return;
        }
        if (w == 0)
            break;
        if (l.dl_zapis < dl) {
            vq_oddaj(&u->q[KOLEJKA_RX], u->pamiec, l.glowa, 0);
            koniec("vsock: bufor odbiorczy gościa za mały");
            return;
        }
        p->h.fwd_cnt = v.odebrane;
        vq_zapisz(&l, 0, p, dl);
        vq_oddaj(&u->q[KOLEJKA_RX], u->pamiec, l.glowa, (uint32_t)dl);
        v.zgloszone = v.odebrane;
        for (i = 1; i < v.n_czeka; i++)
            v.czeka[i - 1] = v.czeka[i];
        v.n_czeka--;
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
}

static void kolejkuj(uint32_t port, uint16_t op, const void *dane, uint32_t dl)
{
    struct pakiet *p;
    if (v.n_czeka == CZEKAJACE || dl > DANE_MAKS) {
        v.odrzucone++;
        return;
    }
    p = &v.czeka[v.n_czeka++];
    naglowek(&p->h, port, op, dl);
    if (dl > 0)
        memcpy(p->dane, dane, dl);
}

static void linia_od_goscia(void)
{
    v.linia[v.dl] = '\0';
    if (strcmp(v.linia, "gotowa") == 0)
        k1_log(TAG, "gość gotowy (kanał sterujący)");
    else if (strcmp(v.linia, "zamykam") == 0)
        k1_log(TAG, "gość się zamyka");
    else
        v.odrzucone++;
    v.dl = 0;
}

static void dane_od_goscia(const struct vq_lancuch *l, uint32_t dl)
{
    uint8_t b[NASZ_BUFOR];
    uint32_t i;
    vq_czytaj(l, sizeof(struct virtio_vsock_hdr), b, dl);
    for (i = 0; i < dl; i++) {
        if (b[i] == '\n')
            linia_od_goscia();
        else if (v.dl < LINIA_MAKS)
            v.linia[v.dl++] = (char)b[i];
    }
    v.odebrane += dl;
    if (v.odebrane - v.zgloszone >= NASZ_BUFOR / 2)
        kolejkuj(v.port_goscia, VIRTIO_VSOCK_OP_CREDIT_UPDATE, NULL, 0);
}

/* One packet from the guest: 0, or -1 (the machine ends, *powod). */
static int pakiet(const struct vq_lancuch *l, const char **powod)
{
    struct virtio_vsock_hdr h;
    int nasz;
    if (vq_czytaj(l, 0, &h, sizeof h) != sizeof h || h.len > l->dl_odczyt - sizeof h || h.len > NASZ_BUFOR) {
        *powod = "vsock: pakiet krótszy niż jego nagłówek albo dane";
        return -1;
    }
    if (h.src_cid != CID_GOSCIA || h.dst_cid != CID_HOSTA || h.type != VIRTIO_VSOCK_TYPE_STREAM) {
        *powod = "vsock: zły adres albo typ pakietu";
        return -1;
    }
    v.pakiety++;
    nasz = v.polaczony && h.src_port == v.port_goscia && h.dst_port == VSOCK_STEROWANIE;
    if (nasz) {
        v.jego_bufor = h.buf_alloc;
        v.jego_fwd = h.fwd_cnt;
    }
    switch (h.op) {
    case VIRTIO_VSOCK_OP_REQUEST:
        if (!v.polaczony && h.dst_port == VSOCK_STEROWANIE) {
            v.polaczony = 1;
            v.port_goscia = h.src_port;
            v.jego_bufor = h.buf_alloc;
            v.jego_fwd = h.fwd_cnt;
            v.wyslane = v.odebrane = v.zgloszone = 0;
            kolejkuj(h.src_port, VIRTIO_VSOCK_OP_RESPONSE, NULL, 0);
        } else {
            kolejkuj(h.src_port, VIRTIO_VSOCK_OP_RST, NULL, 0);
        }
        return 0;
    case VIRTIO_VSOCK_OP_RW:
        if (!nasz) {
            kolejkuj(h.src_port, VIRTIO_VSOCK_OP_RST, NULL, 0);
            return 0;
        }
        if (h.len > NASZ_BUFOR - (v.odebrane - v.zgloszone)) {
            *powod = "vsock: gość przekroczył kredyt";
            return -1;
        }
        dane_od_goscia(l, h.len);
        return 0;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        if (nasz)
            kolejkuj(v.port_goscia, VIRTIO_VSOCK_OP_CREDIT_UPDATE, NULL, 0);
        return 0;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
    case VIRTIO_VSOCK_OP_RST:
        if (nasz) {
            v.polaczony = 0;
            if (h.op == VIRTIO_VSOCK_OP_SHUTDOWN)
                kolejkuj(h.src_port, VIRTIO_VSOCK_OP_RST, NULL, 0);
        }
        return 0;
    default:
        return 0; /* RESPONSE, CREDIT_UPDATE (its credit is taken above), unknown: nothing to do */
    }
}

void vsock_zamknij(void)
{
    static const char z[] = "zamknij\n";
    if (!v.polaczony || v.jego_bufor - (v.wyslane - v.jego_fwd) < sizeof z - 1) {
        k1_log(TAG, "vsock: gość nie słucha — „zamknij” niewysłane");
        return;
    }
    kolejkuj(v.port_goscia, VIRTIO_VSOCK_OP_RW, z, sizeof z - 1);
    v.wyslane += sizeof z - 1;
    wyslij_czekajace();
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w = 0, bylo = 0;
    if (k == KOLEJKA_TX) {
        while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
            if (pakiet(&l, powod) != 0)
                return -1;
            vq_oddaj(&u->q[k], u->pamiec, l.glowa, 0);
            bylo = 1;
        }
        if (bylo)
            mmio_uzyte(u);
    }
    wyslij_czekajace(); /* new receive buffers, or answers to what came */
    return w < 0 ? -1 : 0;
}

static void konfig(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    struct virtio_vsock_config c = { CID_GOSCIA };
    (void)u;
    memcpy(dst, (const uint8_t *)&c + od, n);
}

static void reset(struct mmio_urz *u)
{
    (void)u;
    memset(&v, 0, sizeof v);
}

const struct mmio_ops vsock_ops = {
    .id = VIRTIO_ID_VSOCK,
    .n_kolejek = 3, /* rx, tx, event */
    .rozmiar_konfig = sizeof(struct virtio_vsock_config),
    .konfig_czytaj = konfig,
    .powiadom = powiadom,
    .reset = reset,
};

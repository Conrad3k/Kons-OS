/* K1 strefa — virtio-vsock (virtio 1.2, section 5.10), stream sockets only. Two host ports:
 *   VSOCK_STEROWANIE (1): one connection, the guest's control channel — text lines "gotowa" (the guest is up) and
 *     "zamykam"; the VMM sends "zamknij" when ekran asks to close the zone (0.10.0 step 1);
 *   POL_PORT (2): the browser's connections through the relay in the zone, up to K1_STREFA_POLACZENIA_MAKS, each with
 *     its own credit; their bytes go to and from polaczenia.c (0.10.0 step 2, ADR 0043 T9).
 * Any other connection is refused (RST). Every header from the guest is checked: its CIDs, type, length within the
 * chain, credit (a guest that sends more than the room we announced is hostile: the machine ends). Under mz.zamek. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/virtio_ids.h>
#include <linux/virtio_vsock.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "polaczenia.h"
#include "strefa.h"
#include "znaki_wejscia.h"

#define CID_HOSTA 2
#define CID_GOSCIA 3
#define VSOCK_STEROWANIE 1
#define NASZ_BUFOR 4096u
#define CZEKAJACE 64
#define DANE_MAKS 64u
#define LINIA_MAKS 64u
#define KOLEJKA_RX 0
#define KOLEJKA_TX 1
#define WSZYSTKO (VIRTIO_VSOCK_SHUTDOWN_RCV | VIRTIO_VSOCK_SHUTDOWN_SEND)

_Static_assert(sizeof(struct virtio_vsock_hdr) == 44, "vsock: nagłówek 44 B");

struct pakiet {
    struct virtio_vsock_hdr h;
    uint8_t dane[DANE_MAKS];
};

static struct {
    int polaczony;
    uint32_t port_goscia;
    struct pol_kredyt kr; /* the control connection (our room NASZ_BUFOR) */
    char linia[LINIA_MAKS + 1];
    size_t dl;
    struct pakiet czeka[CZEKAJACE];
    int n_czeka;
    unsigned long pakiety, odrzucone;
} v;
static uint8_t dane[POL_BUFOR]; /* one packet's bytes */

/* The credit of the connection between our port and the guest's; NULL: none. */
static struct pol_kredyt *kredyt(uint32_t nasz, uint32_t jego, int *n)
{
    *n = 0;
    if (nasz == VSOCK_STEROWANIE)
        return v.polaczony && jego == v.port_goscia ? &v.kr : NULL;
    if (nasz == POL_PORT && (*n = pol_znajdz(jego)) > 0)
        return pol_kredyt(*n);
    return NULL;
}

static void naglowek(struct virtio_vsock_hdr *h, uint32_t nasz, uint32_t jego, uint16_t op, uint32_t dl)
{
    int n;
    struct pol_kredyt *k = kredyt(nasz, jego, &n);
    memset(h, 0, sizeof *h);
    h->src_cid = CID_HOSTA;
    h->dst_cid = CID_GOSCIA;
    h->src_port = nasz;
    h->dst_port = jego;
    h->len = dl;
    h->type = VIRTIO_VSOCK_TYPE_STREAM;
    h->op = op;
    h->buf_alloc = nasz == VSOCK_STEROWANIE ? NASZ_BUFOR : POL_BUFOR;
    h->fwd_cnt = k != NULL ? k->przekazane : 0;
    if (k != NULL)
        k->zgloszone = k->przekazane;
}

static void kolejkuj(uint32_t nasz, uint32_t jego, uint16_t op, uint32_t flagi, const void *b, uint32_t dl)
{
    struct pakiet *p;
    if (v.n_czeka == CZEKAJACE || dl > DANE_MAKS) {
        v.odrzucone++;
        return;
    }
    p = &v.czeka[v.n_czeka++];
    naglowek(&p->h, nasz, jego, op, dl);
    p->h.flags = flagi;
    if (dl > 0)
        memcpy(p->dane, b, dl);
}

/* One receive buffer of the guest: 1 taken, 0 none now, -1 the machine ends. */
static int bufor_goscia(struct mmio_urz *u, struct vq_lancuch *l)
{
    const char *powod = NULL;
    int w;
    if (!mmio_dziala(u, KOLEJKA_RX))
        return 0;
    w = vq_wez(&u->q[KOLEJKA_RX], u->pamiec, l, &powod);
    if (w < 0)
        koniec(powod);
    if (w > 0 && l->dl_zapis < sizeof(struct virtio_vsock_hdr) + DANE_MAKS) {
        vq_oddaj(&u->q[KOLEJKA_RX], u->pamiec, l->glowa, 0);
        koniec("vsock: bufor odbiorczy gościa za mały");
        return -1;
    }
    return w;
}

/* Connection n's bytes for the guest, as many as its credit and one buffer take: 1 sent, 0 nothing, -1 the end. */
static int dane_polaczenia(struct mmio_urz *u, int n)
{
    struct pol_kredyt *k = pol_kredyt(n);
    uint32_t miejsce = k->jego_bufor - (k->wyslane - k->jego_fwd);
    struct vq_lancuch l;
    struct virtio_vsock_hdr h;
    size_t ile;
    int koniec_tcp, w;
    if (!pol_stan_dla_vsock(n, &koniec_tcp) || miejsce == 0 || miejsce > k->jego_bufor)
        return 0;
    if ((w = bufor_goscia(u, &l)) <= 0)
        return w;
    ile = l.dl_zapis - sizeof h;
    ile = ile < miejsce ? ile : miejsce;
    ile = pol_do_goscia(n, dane, ile < sizeof dane ? ile : sizeof dane);
    naglowek(&h, POL_PORT, pol_port(n), VIRTIO_VSOCK_OP_RW, (uint32_t)ile);
    vq_zapisz(&l, 0, &h, sizeof h);
    vq_zapisz(&l, sizeof h, dane, ile);
    vq_oddaj(&u->q[KOLEJKA_RX], u->pamiec, l.glowa, (uint32_t)(sizeof h + ile));
    k->wyslane += (uint32_t)ile;
    return 1;
}

/* What waits — the small packets first, then the connections' bytes — into the guest's receive buffers. */
void vsock_wyslij(void)
{
    struct mmio_urz *u = &mz.urz[URZ_VSOCK];
    int bylo = 0, i, n, w;
    for (n = 1; n <= K1_STREFA_POLACZENIA_MAKS; n++) { /* credit back, and the end of a socket */
        struct pol_kredyt *k;
        int koniec_tcp;
        if (pol_port(n) == 0)
            continue;
        k = pol_kredyt(n);
        if (k->przekazane - k->zgloszone >= POL_BUFOR / 2)
            kolejkuj(POL_PORT, pol_port(n), VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, NULL, 0);
        if (!pol_stan_dla_vsock(n, &koniec_tcp) && koniec_tcp && !k->koniec_wyslany) {
            kolejkuj(POL_PORT, pol_port(n), VIRTIO_VSOCK_OP_SHUTDOWN, WSZYSTKO, NULL, 0);
            k->koniec_wyslany = 1;
        }
    }
    while (v.n_czeka > 0) {
        struct vq_lancuch l;
        struct pakiet *p = &v.czeka[0];
        uint64_t dl = sizeof p->h + p->h.len;
        if ((w = bufor_goscia(u, &l)) <= 0)
            break;
        vq_zapisz(&l, 0, p, dl);
        vq_oddaj(&u->q[KOLEJKA_RX], u->pamiec, l.glowa, (uint32_t)dl);
        for (i = 1; i < v.n_czeka; i++)
            v.czeka[i - 1] = v.czeka[i];
        v.n_czeka--;
        bylo = 1;
    }
    for (n = 1; v.n_czeka == 0 && n <= K1_STREFA_POLACZENIA_MAKS; n++)
        while (pol_port(n) != 0 && (w = dane_polaczenia(u, n)) > 0)
            bylo = 1;
    if (bylo)
        mmio_uzyte(u);
}

/* "wejscie <n>": what the compositor did with input — bits of znaki_wejscia.h, logged as yes/no (never a key). */
static void znaki_wejscia(const char *s)
{
    char *k;
    unsigned long z = strtoul(s, &k, 10);
    if (*k != '\0' || z > WEJ_WSZYSTKIE) {
        v.odrzucone++;
        return;
    }
#define TN(b) ((z & (b)) ? "tak" : "nie")
    k1_log(TAG, "gość: mapa klawiatury %s, okno z klawiaturą %s, klawisz z urządzenia %s, gdy okno ma klawiaturę %s, "
           "wysłany do okna %s; przycisk myszy %s; nowa klatka po klawiszu %s", TN(WEJ_MAPA), TN(WEJ_FOKUS), TN(WEJ_KLAWISZ),
           TN(WEJ_KLAWISZ_OKNO), TN(WEJ_KLAWISZ_WYSLANY), TN(WEJ_PRZYCISK), TN(WEJ_KLATKA_PO_KLAWISZU));
#undef TN
}

static void linia_od_goscia(void)
{
    v.linia[v.dl] = '\0';
    if (strcmp(v.linia, "gotowa") == 0)
        k1_log(TAG, "gość gotowy (kanał sterujący)");
    else if (strcmp(v.linia, "zamykam") == 0)
        k1_log(TAG, "gość się zamyka");
    else if (strncmp(v.linia, "wejscie ", 8) == 0 && v.dl <= 11 && v.linia[8] >= '0' && v.linia[8] <= '9')
        znaki_wejscia(v.linia + 8);
    else
        v.odrzucone++;
    v.dl = 0;
}

static void sterowanie_dane(uint32_t dl)
{
    uint32_t i;
    for (i = 0; i < dl; i++) {
        if (dane[i] == '\n')
            linia_od_goscia();
        else if (v.dl < LINIA_MAKS)
            v.linia[v.dl++] = (char)dane[i];
    }
    v.kr.odebrane += dl;
    v.kr.przekazane += dl;
    if (v.kr.przekazane - v.kr.zgloszone >= NASZ_BUFOR / 2)
        kolejkuj(VSOCK_STEROWANIE, v.port_goscia, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, NULL, 0);
}

/* A new connection from the guest to our port. */
static void prosba(const struct virtio_vsock_hdr *h)
{
    struct pol_kredyt k = { .jego_bufor = h->buf_alloc, .jego_fwd = h->fwd_cnt };
    int n;
    if (h->dst_port == VSOCK_STEROWANIE && !v.polaczony) {
        v.polaczony = 1;
        v.port_goscia = h->src_port;
        v.kr = k;
        kolejkuj(VSOCK_STEROWANIE, h->src_port, VIRTIO_VSOCK_OP_RESPONSE, 0, NULL, 0);
    } else if (h->dst_port == POL_PORT && pol_znajdz(h->src_port) == 0 && (n = pol_nowe(h->src_port, &k)) > 0) {
        kolejkuj(POL_PORT, h->src_port, VIRTIO_VSOCK_OP_RESPONSE, 0, NULL, 0);
    } else {
        kolejkuj(h->dst_port, h->src_port, VIRTIO_VSOCK_OP_RST, 0, NULL, 0);
    }
}

/* One packet from the guest: 0, or -1 (the machine ends, *powod). */
static int pakiet(const struct vq_lancuch *l, const char **powod)
{
    struct virtio_vsock_hdr h;
    struct pol_kredyt *k;
    int n;
    if (vq_czytaj(l, 0, &h, sizeof h) != sizeof h || h.len > l->dl_odczyt - sizeof h || h.len > POL_BUFOR) {
        *powod = "vsock: pakiet krótszy niż jego nagłówek albo dane";
        return -1;
    }
    if (h.src_cid != CID_GOSCIA || h.dst_cid != CID_HOSTA || h.type != VIRTIO_VSOCK_TYPE_STREAM) {
        *powod = "vsock: zły adres albo typ pakietu";
        return -1;
    }
    v.pakiety++;
    k = kredyt(h.dst_port, h.src_port, &n);
    if (k != NULL) {
        k->jego_bufor = h.buf_alloc;
        k->jego_fwd = h.fwd_cnt;
    }
    switch (h.op) {
    case VIRTIO_VSOCK_OP_REQUEST:
        prosba(&h);
        return 0;
    case VIRTIO_VSOCK_OP_RW:
        if (k == NULL) {
            kolejkuj(h.dst_port, h.src_port, VIRTIO_VSOCK_OP_RST, 0, NULL, 0);
            return 0;
        }
        if (h.len > (n ? POL_BUFOR : NASZ_BUFOR) - (k->odebrane - k->zgloszone)) { /* the room the guest was told */
            *powod = "vsock: gość przekroczył kredyt";
            return -1;
        }
        vq_czytaj(l, sizeof h, dane, h.len);
        if (n == 0)
            sterowanie_dane(h.len);
        else if (pol_od_goscia(n, dane, h.len) != 0) {
            *powod = "vsock: gość przekroczył kredyt";
            return -1;
        }
        return 0;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        if (k != NULL)
            kolejkuj(h.dst_port, h.src_port, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, NULL, 0);
        return 0;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
    case VIRTIO_VSOCK_OP_RST:
        if (k != NULL && n == 0) {
            v.polaczony = 0;
        } else if (k != NULL) {
            int calkiem = h.op == VIRTIO_VSOCK_OP_RST || (h.flags & WSZYSTKO) == WSZYSTKO;
            if (h.op == VIRTIO_VSOCK_OP_SHUTDOWN && !calkiem && !(h.flags & VIRTIO_VSOCK_SHUTDOWN_SEND))
                return 0; /* it only stops receiving: the socket's bytes go on until their end */
            pol_gosc_konczy(n, calkiem);
        }
        if (k != NULL && h.op == VIRTIO_VSOCK_OP_SHUTDOWN && (h.flags & WSZYSTKO) == WSZYSTKO)
            kolejkuj(h.dst_port, h.src_port, VIRTIO_VSOCK_OP_RST, 0, NULL, 0); /* the close the guest waits for */
        return 0;
    default:
        return 0; /* RESPONSE, CREDIT_UPDATE (its credit is taken above), unknown: nothing to do */
    }
}

void vsock_zamknij(void)
{
    static const char z[] = "zamknij\n";
    if (!v.polaczony || v.kr.jego_bufor - (v.kr.wyslane - v.kr.jego_fwd) < sizeof z - 1) {
        k1_log(TAG, "vsock: gość nie słucha — „zamknij” niewysłane");
        return;
    }
    kolejkuj(VSOCK_STEROWANIE, v.port_goscia, VIRTIO_VSOCK_OP_RW, 0, z, sizeof z - 1);
    v.kr.wyslane += sizeof z - 1;
    vsock_wyslij();
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
    vsock_wyslij(); /* new receive buffers, or answers to what came */
    obudz();        /* the main loop: the sockets of the connections may wait on something new */
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

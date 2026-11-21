/* K1 strefa — virtio-vsock (virtio 1.2, section 5.10), stream sockets only. Two host ports:
 *   VSOCK_STEROWANIE (1): one connection, the guest's control channel — text lines "gotowa" (the guest is up) and
 *     "zamykam"; the VMM sends "zamknij" when ekran asks to close the zone (0.10.0 step 1); since step 3 (S26) also
 *     "koniec pamiec|awaria|sam" (why the browser ended by itself: K1 tells the user) and "pamiec strona" (a page
 *     killed for lack of memory), "etap okno" (the browser's first window: the time to the log, S30; since 0.10.2 the
 *     end of the start screen after "etap start" — okno.c); since 0.10.1
 *     step 1 "liczniki A Z K" (the guest's memory, zram, OOM kills: one line of counters with ours, S13) and "kto P K R"
 *     (who ended and how, S15); since 0.10.11 step 1 "pomiar" and 11 numbers (the compositor's measurement, pomiar.c);
 *     since 0.11.0 step 1 "pelny 1" / "pelny 0" (a window of the guest asks for full screen / leaves it — okno.c, at most
 *     4 changes a second) and from the VMM "pelny 0" (K1 ended the full screen)
 *     — numbers only, checked by linia.c — only these exact lines, anything else is counted and ignored;
 *   POL_PORT (2): the browser's connections through the relay in the zone, up to K1_STREFA_POLACZENIA_MAKS, each with
 *     its own credit; their bytes go to and from polaczenia.c (0.10.0 step 2, ADR 0043 T9).
 * Any other connection is refused (RST). Every header from the guest is checked: its CIDs, type, length within the
 * chain, credit (a guest that sends more than the room we announced is hostile: the machine ends). Under mz.zamek.
 * What the VMM owes a connection (its RESPONSE, a credit update, its SHUTDOWN) is state of that connection, sent when the
 * guest gives a receive buffer — never dropped, and its header (our credit) made at that moment (0.10.5 step 5, S36:
 * a queue of 64 lost RESPONSEs and credit updates under load). Only refusals (RST to a port without a connection) and
 * "zamknij" wait in a small queue; a full one drops a refusal, counted. */
#include <stdio.h>
#include <string.h>

#include <linux/virtio_ids.h>
#include <linux/virtio_vsock.h>

#include "k1_komunikat.h"
#include "k1_log.h"
#include "sterowanie.h"
#include "pliki.h"
#include "polaczenia.h"
#include "strefa.h"

#define CID_HOSTA 2
#define CID_GOSCIA 3
#define VSOCK_STEROWANIE 1
#define NASZ_BUFOR 4096u
#define CZEKAJACE 128
#define DANE_MAKS 64u
#define LINIA_MAKS 160u /* 0.10.11 step 1: "pomiar" with 11 numbers */
#define KOLEJKA_RX 0
#define KOLEJKA_TX 1
#define WSZYSTKO (VIRTIO_VSOCK_SHUTDOWN_RCV | VIRTIO_VSOCK_SHUTDOWN_SEND)

_Static_assert(sizeof(struct virtio_vsock_hdr) == 44, "vsock: nagłówek 44 B");

struct pakiet { /* its header is made when it goes */
    uint32_t nasz, jego, flagi, dl;
    uint16_t op;
    uint8_t dane[DANE_MAKS];
};

static struct {
    int polaczony, odpowiedz; /* the control connection; its RESPONSE owed */
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
    if (nasz == K1_PS_PORT) /* 0.11.0 step 2: the files port (pliki.c) */
        return pliki_kredyt(jego);
    return NULL;
}

/* The room we announce on port `nasz` (buf_alloc). */
static uint32_t bufor_portu(uint32_t nasz)
{
    return nasz == VSOCK_STEROWANIE ? NASZ_BUFOR : nasz == K1_PS_PORT ? K1_PS_BUFOR : POL_BUFOR;
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
    h->buf_alloc = bufor_portu(nasz);
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
    *p = (struct pakiet){ nasz, jego, flagi, dl, op, { 0 } };
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

/* One small packet (its header made now, with our credit as it is now): 1 sent, 0 no buffer, -1 the end. */
static int maly(struct mmio_urz *u, uint32_t nasz, uint32_t jego, uint16_t op, uint32_t flagi, const void *b, uint32_t dl)
{
    struct vq_lancuch l;
    struct virtio_vsock_hdr h;
    int w = bufor_goscia(u, &l);
    if (w <= 0)
        return w;
    naglowek(&h, nasz, jego, op, dl);
    h.flags = flagi;
    vq_zapisz(&l, 0, &h, sizeof h);
    if (dl > 0)
        vq_zapisz(&l, sizeof h, b, dl);
    vq_oddaj(&u->q[KOLEJKA_RX], u->pamiec, l.glowa, (uint32_t)(sizeof h + dl));
    return 1;
}

/* What connection n is owed, the next small packet of it: its RESPONSE, a credit update (we used half of the room we
 * last told it, or it asked), its SHUTDOWN (the socket's end, once its bytes went). 1 sent, 0 nothing (or no buffer),
 * -1 the end. */
static int winny_polaczeniu(struct mmio_urz *u, int n)
{
    struct pol_kredyt *k = pol_kredyt(n);
    uint32_t p = pol_port(n);
    int koniec_tcp, w = 0;
    if (k->odpowiedz) {
        if ((w = maly(u, POL_PORT, p, VIRTIO_VSOCK_OP_RESPONSE, 0, NULL, 0)) > 0)
            k->odpowiedz = 0;
        return w;
    }
    if (k->kredyt_prosba || k->przekazane - k->zgloszone >= POL_BUFOR / 2) {
        if ((w = maly(u, POL_PORT, p, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, NULL, 0)) > 0)
            k->kredyt_prosba = 0;
        return w;
    }
    if (!pol_stan_dla_vsock(n, &koniec_tcp) && koniec_tcp && !k->koniec_wyslany) {
        if ((w = maly(u, POL_PORT, p, VIRTIO_VSOCK_OP_SHUTDOWN, (uint32_t)koniec_tcp, NULL, 0)) > 0)
            k->koniec_wyslany = 1;
        return w;
    }
    return 0;
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
    if (k->odpowiedz || !pol_stan_dla_vsock(n, &koniec_tcp) || miejsce == 0 || miejsce > k->jego_bufor)
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

/* The files port (0.11.0 step 2, pliki.c): its RESPONSE or our RST, a credit update, then our frames as its credit
 * allows: 1 sent, 0 nothing, -1 the end. */
static int plikom(struct mmio_urz *u)
{
    uint32_t port;
    int rst, w;
    struct pol_kredyt *k;
    if (pliki_odpowiedz(&port, &rst)) {
        if ((w = maly(u, K1_PS_PORT, port, rst ? VIRTIO_VSOCK_OP_RST : VIRTIO_VSOCK_OP_RESPONSE, 0, NULL, 0)) > 0)
            pliki_odpowiedziano(rst);
        return w;
    }
    if ((k = pliki_kredyt(port)) == NULL)
        return 0;
    if (k->kredyt_prosba || k->przekazane - k->zgloszone >= K1_PS_BUFOR / 2) {
        if ((w = maly(u, K1_PS_PORT, port, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, NULL, 0)) > 0)
            k->kredyt_prosba = 0;
        return w;
    }
    {
        uint32_t miejsce = k->jego_bufor - (k->wyslane - k->jego_fwd);
        uint8_t b[DANE_MAKS];
        size_t n;
        if (miejsce == 0 || miejsce > k->jego_bufor)
            return 0;
        n = pliki_do_goscia(b, miejsce < sizeof b ? miejsce : sizeof b);
        if (n == 0)
            return 0;
        if ((w = maly(u, K1_PS_PORT, port, VIRTIO_VSOCK_OP_RW, 0, b, (uint32_t)n)) > 0) {
            k->wyslane += (uint32_t)n;
            pliki_wyslane(n);
        }
        return w; /* no buffer now: the frames wait for the next one */
    }
}

/* The control connection's owed packets: its RESPONSE, a credit update. */
static int winny_sterowaniu(struct mmio_urz *u)
{
    int w = 0;
    if (v.polaczony && v.odpowiedz) {
        if ((w = maly(u, VSOCK_STEROWANIE, v.port_goscia, VIRTIO_VSOCK_OP_RESPONSE, 0, NULL, 0)) > 0)
            v.odpowiedz = 0;
    } else if (v.polaczony && v.kr.przekazane - v.kr.zgloszone >= NASZ_BUFOR / 2) {
        w = maly(u, VSOCK_STEROWANIE, v.port_goscia, VIRTIO_VSOCK_OP_CREDIT_UPDATE, 0, NULL, 0);
    }
    return w;
}

/* What waits — the small packets first (the queue, then what each connection is owed), then the connections' bytes —
 * into the guest's receive buffers. */
void vsock_wyslij(void)
{
    struct mmio_urz *u = &mz.urz[URZ_VSOCK];
    int bylo = 0, i, n, w = 1, male = 1;
    while (v.n_czeka > 0) {
        struct pakiet *p = &v.czeka[0];
        if ((w = maly(u, p->nasz, p->jego, p->op, p->flagi, p->dane, p->dl)) <= 0)
            break;
        for (i = 1; i < v.n_czeka; i++)
            v.czeka[i - 1] = v.czeka[i];
        v.n_czeka--;
        bylo = 1;
    }
    while (w > 0 && v.n_czeka == 0 && (w = winny_sterowaniu(u)) > 0)
        bylo = 1;
    if (w == 0 && v.n_czeka == 0)
        w = 1; /* the control connection owed nothing: the files port's turn */
    while (w > 0 && v.n_czeka == 0 && (w = plikom(u)) > 0)
        bylo = 1;
    for (n = 1; w >= 0 && male && n <= K1_STREFA_POLACZENIA_MAKS; n++)
        while (pol_port(n) != 0 && (w = winny_polaczeniu(u, n)) > 0)
            bylo = 1;
    male = v.n_czeka == 0 && w >= 0; /* a buffer was left, or nothing small is owed */
    for (n = 1; male && n <= K1_STREFA_POLACZENIA_MAKS; n++)
        while (pol_port(n) != 0 && (w = dane_polaczenia(u, n)) > 0)
            bylo = 1;
    if (bylo)
        mmio_uzyte(u);
}

static void sterowanie_dane(uint32_t dl)
{
    uint32_t i;
    for (i = 0; i < dl; i++) {
        if (dane[i] == '\n') {
            v.linia[v.dl] = '\0';
            if (!sterowanie_linia(v.linia))
                v.odrzucone++;
            v.dl = 0;
        }
        else if (v.dl < LINIA_MAKS)
            v.linia[v.dl++] = (char)dane[i];
    }
    v.kr.odebrane += dl;
    v.kr.przekazane += dl; /* the credit update: winny_sterowaniu */
}

/* A new connection from the guest to our port. */
static void prosba(const struct virtio_vsock_hdr *h)
{
    struct pol_kredyt k = { .jego_bufor = h->buf_alloc, .jego_fwd = h->fwd_cnt };
    int n;
    if (h->dst_port == VSOCK_STEROWANIE && !v.polaczony) {
        v.polaczony = 1, v.odpowiedz = 1;
        v.port_goscia = h->src_port;
        v.kr = k;
    } else if (h->dst_port == K1_PS_PORT && pliki_polacz(h->src_port, &k)) {
        /* 0.11.0 step 2: the agent's files port — its RESPONSE is owed (plikom) */
    } else if (h->dst_port == POL_PORT && pol_znajdz(h->src_port) == 0 && (n = pol_nowe(h->src_port, &k)) > 0) {
        pol_kredyt(n)->odpowiedz = 1;
    } else {
        if (h->dst_port == POL_PORT && pol_znajdz(h->src_port) != 0)
            pol_ten_sam_port();
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
        if (h.len > bufor_portu(h.dst_port) - (k->odebrane - k->zgloszone)) { /* the room the guest was told */
            *powod = "vsock: gość przekroczył kredyt";
            return -1;
        }
        vq_czytaj(l, sizeof h, dane, h.len);
        if (h.dst_port == K1_PS_PORT) {
            if (pliki_od_goscia(dane, h.len) != 0) {
                *powod = "vsock: gość przekroczył kredyt";
                return -1;
            }
        } else if (n == 0)
            sterowanie_dane(h.len);
        else if (pol_od_goscia(n, dane, h.len) != 0) {
            *powod = "vsock: gość przekroczył kredyt";
            return -1;
        }
        return 0;
    case VIRTIO_VSOCK_OP_CREDIT_REQUEST:
        if (k != NULL && (n > 0 || h.dst_port == K1_PS_PORT))
            k->kredyt_prosba = 1;
        else if (k != NULL)
            v.kr.zgloszone = v.kr.przekazane - NASZ_BUFOR; /* the control connection: an update at once */
        return 0;
    case VIRTIO_VSOCK_OP_SHUTDOWN:
    case VIRTIO_VSOCK_OP_RST:
        if (k != NULL && h.dst_port == K1_PS_PORT) {
            pliki_rozlacz();
        } else if (k != NULL && n == 0) {
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

void vsock_kolejka(int *czeka, unsigned long *odrzucone)
{
    *czeka = v.n_czeka;
    *odrzucone = v.odrzucone;
}

void vsock_linia(const char *l)
{
    uint32_t n = (uint32_t)strlen(l);
    if (!v.polaczony || v.kr.jego_bufor - (v.kr.wyslane - v.kr.jego_fwd) < n) {
        k1_log(TAG, "vsock: gość nie słucha — linia „%.*s” niewysłana", (int)(n > 0 ? n - 1 : 0), l);
        return;
    }
    kolejkuj(VSOCK_STEROWANIE, v.port_goscia, VIRTIO_VSOCK_OP_RW, 0, l, n);
    v.kr.wyslane += n;
    vsock_wyslij();
}

void vsock_zamknij(void)
{
    vsock_linia("zamknij\n");
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
    pliki_wyczysc();
    pol_wyczysc(); /* 0.10.5 step 5: the guest's connections end with its device (their sockets used to stay open) */
}

const struct mmio_ops vsock_ops = {
    .id = VIRTIO_ID_VSOCK,
    .n_kolejek = 3, /* rx, tx, event */
    .rozmiar_konfig = sizeof(struct virtio_vsock_config),
    .konfig_czytaj = konfig,
    .powiadom = powiadom,
    .reset = reset,
};

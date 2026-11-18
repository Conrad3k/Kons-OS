/* K1 strefa — virtio-vsock (virtio 1.2, section 5.10), stream sockets only. Two host ports:
 *   VSOCK_STEROWANIE (1): one connection, the guest's control channel — text lines "gotowa" (the guest is up) and
 *     "zamykam"; the VMM sends "zamknij" when ekran asks to close the zone (0.10.0 step 1); since step 3 (S26) also
 *     "koniec pamiec|awaria|sam" (why the browser ended by itself: K1 tells the user) and "pamiec strona" (a page
 *     killed for lack of memory), "etap okno" (the browser's first window: the time to the log, S30; since 0.10.2 the
 *     end of the start screen after "etap start" — okno.c); since 0.10.1
 *     step 1 "liczniki A Z K" (the guest's memory, zram, OOM kills: one line of counters with ours, S13) and "kto P K R"
 *     (who ended and how, S15) — numbers only, checked by linia.c — only these exact lines, anything else is counted
 *     and ignored;
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
#include "k1_teraz.h"
#include "linia.h"
#include "okno.h"
#include "polaczenia.h"
#include "strefa.h"

#define CID_HOSTA 2
#define CID_GOSCIA 3
#define VSOCK_STEROWANIE 1
#define NASZ_BUFOR 4096u
#define CZEKAJACE 128
#define DANE_MAKS 64u
#define LINIA_MAKS 64u
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

/* The counters (0.10.1 step 1, S13): the guest's numbers from "liczniki" and ours, one line every LICZNIKI_ODSTEP s
 * (the main thread: vsock_liczniki — whatever the guest sends, or does not) and once more at the machine's end
 * (vsock_podsumowanie). */
#define LICZNIKI_ODSTEP 5.0
static struct {
    uint32_t g[3];   /* the guest's: available MiB, zram MiB, OOM kills */
    int sa;          /* the guest sent them at least once */
    double t;        /* the last line (k1_teraz) */
} lz;

static void liczniki_loguj(const char *kiedy)
{
    unsigned zasoby, mib;
    struct balon_stan b;
    char g[80];
    gpu_liczniki(&zasoby, &mib);
    balon_stan(&b); /* 0.10.1 step 2: what K1 holds of the guest's memory, the balloon */
    if (lz.sa)
        snprintf(g, sizeof g, "dostępna %u MiB, zram %u MiB, zabite %u", lz.g[0], lz.g[1], lz.g[2]);
    else
        snprintf(g, sizeof g, "brak");
    k1_log(TAG, "liczniki%s: gość — %s; pamięć w K1 %u MiB, oddane %u MiB, balon %u MiB; grafika: zasoby %u, %u MiB; "
                "połączenia %u; vsock czeka %d, odrzucone %lu", kiedy, g, b.w_k1_mib, b.oddane_mib, b.balon_mib, zasoby,
           mib, pol_zajete(), v.n_czeka, v.odrzucone);
}

/* The main thread, once a second (main.c): the counters' line every LICZNIKI_ODSTEP s — also when the guest sends no
 * numbers of its own (a guest that is busy, a hostile one). */
void vsock_liczniki(void)
{
    if (k1_teraz() - lz.t < LICZNIKI_ODSTEP)
        return;
    lz.t = k1_teraz();
    liczniki_loguj("");
}

void vsock_podsumowanie(void)
{
    liczniki_loguj(" przy końcu");
}

/* "kto <1 browser|2 compositor> <code> <rule>" (0.10.1 step 1, S15): who of the guest's programs ended by itself —
 * numbers only, logged once (the first one counts, as "koniec"). */
static void kto(const uint32_t *w)
{
    static int byl;
    if (byl || w[0] < 1 || w[0] > 2)
        return;
    byl = 1;
    if (w[2] > 0)
        k1_log(TAG, "gość: %s zakończony (kod %u) — kompozytor odrzucił przeglądarkę (reguła %u)",
               w[0] == 1 ? "przeglądarka" : "kompozytor", w[1], w[2]);
    else
        k1_log(TAG, "gość: %s zakończony (kod %u)", w[0] == 1 ? "przeglądarka" : "kompozytor", w[1]);
}

static void linia_od_goscia(void)
{
    uint32_t w[3];
    v.linia[v.dl] = '\0';
    if (linia_liczby(v.linia, "liczniki", w, 3, 1u << 24)) {
        memcpy(lz.g, w, sizeof lz.g);
        lz.sa = 1;
    } else if (linia_liczby(v.linia, "kto", w, 3, 999))
        kto(w);
    else if (strcmp(v.linia, "gotowa") == 0)
        k1_log(TAG, "gość gotowy (kanał sterujący) po %.1f s", k1_teraz() - mz.start_t);
    else if (strcmp(v.linia, "etap start") == 0) { /* 0.10.2 step 1: the start screen stays until "etap okno" */
        okno_etap(OKNO_ETAP_START);
        k1_log(TAG, "gość: program strefy startuje — ekran startu do jego okna");
    } else if (strcmp(v.linia, "etap okno") == 0) { /* S30: how long the browser takes to start */
        okno_etap(OKNO_ETAP_OKNO);
        k1_log(TAG, "gość: okno przeglądarki po %.1f s od startu maszyny", k1_teraz() - mz.start_t);
    }
    else if (strcmp(v.linia, "zamykam") == 0)
        k1_log(TAG, "gość się zamyka");
    else if (strcmp(v.linia, "zamykanie gotowe") == 0 || strcmp(v.linia, "zamykanie sygnal") == 0 ||
             strcmp(v.linia, "zamykanie po czasie") == 0)
        k1_log(TAG, "gość: przeglądarka %s po %.1f s",
               v.linia[10] == 'g' ? "zamknęła się sama (dane sesji zapisane)" :
               v.linia[10] == 's' ? "zamknięta sygnałem (bez zapisu danych sesji)" : "nie zamknęła się w czasie — zatrzymana",
               mz.zamykana_t > 0 ? k1_teraz() - mz.zamykana_t : 0.0);
    else if (strcmp(v.linia, "koniec pamiec") == 0 || strcmp(v.linia, "koniec awaria") == 0 ||
             strcmp(v.linia, "koniec sam") == 0) {
        int kod = v.linia[7] == 'p' ? K1_STREFA_KONIEC_PAMIEC : v.linia[7] == 'a' ? K1_STREFA_KONIEC_AWARIA : 0;
        if (!mz.gosc_powiedzial)
            mz.kod_goscia = kod;
        mz.gosc_powiedzial = 1;
        k1_log(TAG, "gość: program strefy zakończył się sam (%s)",
               kod == K1_STREFA_KONIEC_PAMIEC ? "brak pamięci" : kod == K1_STREFA_KONIEC_AWARIA ? "awaria" : "zamknięty");
    } else if (strcmp(v.linia, "pamiec strona") == 0)
        k1_log(TAG, "gość: strona zakończona — brak pamięci w strefie (%u)", ++mz.strony_bez_pamieci);
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
        if (k != NULL && n > 0)
            k->kredyt_prosba = 1;
        else if (k != NULL)
            v.kr.zgloszone = v.kr.przekazane - NASZ_BUFOR; /* the control connection: an update at once */
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

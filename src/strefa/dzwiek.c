/* K1 strefa — the guest's sound card (0.12.0 step 3, ADR 0054 T5): virtio-snd with one output stream (dzwiek.h), queues:
 * control 0, events 1 (never used: no jacks), transfers 2, input 3 (0.13.5 step 2: only with the microphone). The guest's Firefox plays through ALSA
 * to its kernel's virtio-snd driver; its transfers come here and their samples go — copied, in floats without loss
 * (0.12.3, ADR 0055: the guest's S16, S32 or FLOAT; dzw_na_f32) — into this VMM's F32 ring at K1's sound service (a
 * client STREFA: the channel from nadzorca on fd 7, DZWIEK_KANAL). A transfer goes back to the guest only when its last frame was PLAYED (the service's DZWIEK_ODTWORZONO
 * counts them): the guest's driver moves its clock by the transfers given back, so a transfer given back early would
 * make its whole buffer "played" at once (an underrun every time — the first version did that). At most ZAPAS frames are
 * in the ring and not played yet. Without the service (no channel, no card: the stream refused) the device keeps its own
 * clock and drops the samples — the browser's films run on time, silent.
 * 0.13.5 step 2 (ADR 0065): in a session with the switch "Mikrofon" a second stream — the input (dzwiek_mikrofon.c).
 * Trust boundary: the guest is hostile. Requests copied out of its memory and checked field by field
 * (dzwiek_prosby.c); a transfer with no room for its status, or a request with no room for its answer, ends the machine;
 * anything else wrong is answered BAD_MSG. In the log: numbers only, never a sample. */
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/virtio_ids.h>
#include <linux/virtio_snd.h>

#include "dzwiek.h"
#include "k1_dzwiek.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_teraz.h"
#include "strefa.h"

#define Q_STEROWANIE 0
#define Q_WYJSCIE 2
#define Q_WEJSCIE 3 /* 0.13.5 step 2: the input (dzwiek_mikrofon.c) */
#define ZAPAS 8192u /* frames copied and not played yet at most (~170 ms) */

static struct {
    struct dzw_strumien s[DZW_STRUMIENIE]; /* 0 the output; 1 the input — 0.13.5 step 2, only with mz.mikrofon */
    int kanal;                  /* to the sound service, -1 */
    float *pierscien;           /* our ring of F32 (a sealed memfd the service maps) */
    int pfd;
    int otwarty;                /* the service took our ring and plays it */
    int karta;                  /* the service's card (DZWIEK_STAN a): a ring only while it has one */
    uint32_t wpisane, zagrane;  /* frames copied, frames played (by the service or our own clock), mod 2^32 */
    uint32_t w_pierscieniu;     /* frames written into the ring in all (mod 2^32: its place) */
    double zegar_t;             /* our own clock: when it ticked last */
    struct vq_lancuch l;        /* a transfer partly taken (trzymany) */
    int trzymany;
    uint64_t od;
    struct czekajacy {          /* transfers copied, waiting for their last frame to be played */
        struct vq_lancuch l;
        uint32_t koniec;        /* d.wpisane after it */
    } cz[VQ_MAKS];
    unsigned cz_od, cz_n;
    unsigned long ramki, odrzucone, bez_uslugi, starty, stopy;
    unsigned formaty;           /* the guest's formats said in the log (a bit each: at most three lines a machine) */
} d = { .kanal = -1, .pfd = -1 };

#define PIERSCIEN_BAJTY (K1_DZW_PIERSCIEN * K1_DZW_KANALY * sizeof(float)) /* = k1_dzw_pierscien_bajty(F32) */
_Static_assert(K1_DZW_KANALY * sizeof(float) == 8, "ramka F32");

static void konfig_czytaj(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    struct virtio_snd_config c = { 0, mz.mikrofon ? 2 : 1, 0, 0 }; /* no jacks, the streams, no channel maps, no controls */
    (void)u;
    memset(dst, 0, n);
    if (od < sizeof c)
        memcpy(dst, (const uint8_t *)&c + od, n < sizeof c - od ? n : sizeof c - od);
}

static uint32_t czeka(void)
{
    return d.wpisane - d.zagrane;
}

static void wlasny_zegar(void)
{
    d.otwarty = 0;
    d.zegar_t = k1_teraz();
}

static void wyslij(struct k1_komunikat m, int fd)
{
    if (d.kanal >= 0 && k1_kanal_wyslij(d.kanal, &m, sizeof m, fd) != 0)
        wlasny_zegar(); /* the service is gone or full: our own clock until it says more */
}

/* 0.13.5 step 2: the input's messages to the service (dzwiek_mikrofon.c) */
void dzwiek_wyslij(struct k1_komunikat m, int fd)
{
    if (d.kanal >= 0)
        k1_kanal_wyslij(d.kanal, &m, sizeof m, fd);
}

/* START: our ring to the service (once per channel, while it has a card); else our own clock. */
static void otworz(void)
{
    if (d.otwarty)
        return;
    wlasny_zegar();
    if (d.kanal < 0 || d.karta != K1_DZW_KARTA_JEST)
        return;
    if (d.pierscien == NULL) {
        void *m;
        d.pfd = k1_memfd_nowy("k1-dzwiek-strefy");
        if (d.pfd < 0 || ftruncate(d.pfd, (off_t)PIERSCIEN_BAJTY) != 0 ||
            fcntl(d.pfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0 ||
            (m = mmap(NULL, PIERSCIEN_BAJTY, PROT_READ | PROT_WRITE, MAP_SHARED, d.pfd, 0)) == MAP_FAILED) {
            k1_log(TAG, "dźwięk: pierścień niedostępny — strefa gra bez dźwięku");
            return;
        }
        d.pierscien = m;
    }
    d.otwarty = 1;
    d.w_pierscieniu = 0;
    d.zagrane = d.wpisane; /* what our clock had not played yet is dropped */
    wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, K1_DZW_FORMAT_F32, 0, 0, 0, 0), d.pfd);
}

/* A transfer back to the guest with its status (8 bytes into its writable part, checked when it was taken). */
static void oddaj(struct mmio_urz *u, const struct vq_lancuch *l, uint32_t status)
{
    struct virtio_snd_pcm_status st = { status, czeka() * d.s[DZW_WYJSCIE].ramka };
    vq_zapisz(l, 0, &st, sizeof st);
    vq_oddaj(&u->q[Q_WYJSCIE], u->pamiec, l->glowa, sizeof st);
}

/* The transfers whose frames were all played go back. 1: some did. */
static int zagrane(struct mmio_urz *u)
{
    int bylo = 0;
    while (d.cz_n > 0 && (int32_t)(d.cz[d.cz_od].koniec - d.zagrane) <= 0) {
        oddaj(u, &d.cz[d.cz_od].l, VIRTIO_SND_S_OK);
        d.cz_od = (d.cz_od + 1) % VQ_MAKS;
        d.cz_n--;
        bylo = 1;
    }
    return bylo;
}

/* `ile` frames of the held transfer from its byte d.od: into the ring as floats (or, by our own clock, dropped). */
static void przepisz(uint32_t ile)
{
    if (d.otwarty) {
        static uint8_t kawalek[K1_DZW_OKRES * 8]; /* the guest's bytes of up to a period (8: the largest frame) */
        uint32_t zrobione = 0;
        while (zrobione < ile) {
            uint32_t poz = d.w_pierscieniu % K1_DZW_PIERSCIEN, n = ile - zrobione;
            if (n > K1_DZW_PIERSCIEN - poz)
                n = K1_DZW_PIERSCIEN - poz;
            if (n > K1_DZW_OKRES)
                n = K1_DZW_OKRES;
            vq_czytaj(&d.l, d.od + (uint64_t)zrobione * d.s[DZW_WYJSCIE].ramka, kawalek, (uint64_t)n * d.s[DZW_WYJSCIE].ramka);
            dzw_na_f32(d.pierscien + (size_t)poz * K1_DZW_KANALY, kawalek, n, d.s[DZW_WYJSCIE].format);
            d.w_pierscieniu += n;
            zrobione += n;
        }
        wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_ZAPISANO, 0, (int32_t)ile, 0, 0, 0, 0), -1);
    } else {
        d.bez_uslugi += ile;
    }
    d.wpisane += ile;
    d.od += (uint64_t)ile * d.s[DZW_WYJSCIE].ramka;
    d.ramki += ile;
}

/* The next transfer taken and checked: 1 held, 2 answered at once (BAD_MSG), 0 none, -1 the guest broke the rules. */
static int wez(struct mmio_urz *u, const char **powod)
{
    uint8_t nagl[4];
    int w = vq_wez(&u->q[Q_WYJSCIE], u->pamiec, &d.l, powod);
    if (w <= 0)
        return w;
    if (d.l.dl_zapis < sizeof(struct virtio_snd_pcm_status)) {
        *powod = "dźwięk: transfer bez miejsca na stan";
        return -1;
    }
    if (d.l.dl_odczyt < sizeof nagl ||
        dzw_transfer(nagl, (uint32_t)vq_czytaj(&d.l, 0, nagl, sizeof nagl), d.l.dl_odczyt - sizeof nagl,
                     d.s[DZW_WYJSCIE].ramka, DZW_WYJSCIE) != VIRTIO_SND_S_OK) {
        d.odrzucone++;
        oddaj(u, &d.l, VIRTIO_SND_S_BAD_MSG);
        return 2;
    }
    d.trzymany = 1;
    d.od = sizeof nagl;
    return 1;
}

/* The transfers while the stream plays, copied as far as there is room; then those played go back. 0, or -1 (*powod). */
static int wyjscie(struct mmio_urz *u, const char **powod)
{
    int bylo = zagrane(u);
    while (d.s[DZW_WYJSCIE].stan == DZW_GRA && d.cz_n < VQ_MAKS) {
        uint32_t ile;
        if (!d.trzymany) {
            int w = wez(u, powod);
            if (w < 0)
                return -1;
            if (w == 0)
                break;
            bylo |= w == 2;
            if (w == 2)
                continue;
        }
        ile = (uint32_t)((d.l.dl_odczyt - d.od) / d.s[DZW_WYJSCIE].ramka);
        if (ile > ZAPAS - czeka())
            ile = ZAPAS - czeka();
        if (ile > 0)
            przepisz(ile);
        if (d.od < d.l.dl_odczyt)
            break; /* no room: the rest when some was played */
        d.cz[(d.cz_od + d.cz_n) % VQ_MAKS] = (struct czekajacy){ d.l, d.wpisane };
        d.cz_n++;
        d.trzymany = 0;
    }
    if (bylo)
        mmio_uzyte(u);
    return 0;
}

/* RELEASE: every transfer back to the guest (virtio 5.14.6.6.5 — its driver waits for them), the samples dropped. */
static int zwolnij(struct mmio_urz *u, const char **powod)
{
    int w;
    d.zagrane = d.wpisane;
    zagrane(u);
    if (d.trzymany)
        oddaj(u, &d.l, VIRTIO_SND_S_OK);
    d.trzymany = 0;
    while ((w = wez(u, powod)) != 0) {
        if (w < 0)
            return -1;
        if (w == 1)
            oddaj(u, &d.l, VIRTIO_SND_S_OK);
        d.trzymany = 0;
    }
    mmio_uzyte(u);
    return 0;
}

static int sterowanie(struct mmio_urz *u, const char **powod)
{
    struct vq_lancuch l;
    int w, bylo = 0;
    while ((w = vq_wez(&u->q[Q_STEROWANIE], u->pamiec, &l, powod)) > 0) {
        uint8_t req[DZW_PROSBA_MAKS + 1], odp[DZW_ODPOWIEDZ_MAKS];
        uint32_t n = (uint32_t)(l.dl_odczyt > DZW_PROSBA_MAKS ? DZW_PROSBA_MAKS + 1 : l.dl_odczyt), m;
        int zrob, ktory;
        vq_czytaj(&l, 0, req, n < DZW_PROSBA_MAKS ? n : DZW_PROSBA_MAKS);
        m = dzw_prosba(d.s, mz.mikrofon ? 2 : 1, req, n, odp, (uint32_t)(l.dl_zapis < sizeof odp ? l.dl_zapis : sizeof odp),
                       &zrob, &ktory);
        if (m == 0) {
            *powod = "dźwięk: prośba bez miejsca na odpowiedź";
            return -1;
        }
        vq_zapisz(&l, 0, odp, m);
        vq_oddaj(&u->q[Q_STEROWANIE], u->pamiec, l.glowa, m);
        bylo = 1;
        if (ktory == DZW_WEJSCIE) { /* 0.13.5 step 2: the microphone (dzwiek_mikrofon.c) */
            if (zrob == DZW_ZROB_START)
                mikrofon_start(d.kanal, d.karta);
            if (zrob == DZW_ZROB_STOP || zrob == DZW_ZROB_ZWOLNIJ)
                mikrofon_stop(1);
            if (zrob == DZW_ZROB_ZWOLNIJ)
                mikrofon_zwolnij(u, &d.s[DZW_WEJSCIE], 0);
            if (zrob == DZW_ZROB_START && mikrofon_kolejka(u, &d.s[DZW_WEJSCIE], powod) != 0)
                return -1;
            continue;
        }
        if (d.s[DZW_WYJSCIE].stan == DZW_USTAWIONY && d.s[DZW_WYJSCIE].ramka != 0 && !(d.formaty & (1u << (d.s[DZW_WYJSCIE].format & 31)))) {
            d.formaty |= 1u << (d.s[DZW_WYJSCIE].format & 31); /* only formats of DZW_FORMATY get here: three at most */
            k1_log(TAG, "dźwięk: gość gra w formacie %s", dzw_format_nazwa(d.s[DZW_WYJSCIE].format));
        }
        d.starty += zrob == DZW_ZROB_START;
        d.stopy += zrob == DZW_ZROB_STOP;
        if (zrob == DZW_ZROB_START)
            otworz();
        if ((zrob == DZW_ZROB_START && wyjscie(u, powod) != 0) || (zrob == DZW_ZROB_ZWOLNIJ && zwolnij(u, powod) != 0))
            return -1;
    }
    if (bylo)
        mmio_uzyte(u);
    return w < 0 ? -1 : 0;
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    if (k == Q_STEROWANIE)
        return sterowanie(u, powod);
    if (k == Q_WYJSCIE)
        return wyjscie(u, powod);
    if (k == Q_WEJSCIE && mz.mikrofon) /* 0.13.5 step 2: the input (without the microphone the queue stays unused) */
        return mikrofon_kolejka(u, &d.s[DZW_WEJSCIE], powod);
    return 0; /* events: buffers wait unused (no jacks) */
}

static void reset(struct mmio_urz *u)
{
    mikrofon_stop(1);
    mikrofon_zwolnij(u, &d.s[DZW_WEJSCIE], 1);
    d.s[DZW_WYJSCIE] = d.s[DZW_WEJSCIE] = (struct dzw_strumien){ .stan = DZW_NIC };
    d.trzymany = 0;
    d.cz_n = 0;
    d.zagrane = d.wpisane;
}

const struct mmio_ops dzwiek_ops = {
    .id = VIRTIO_ID_SOUND,
    .n_kolejek = 4,
    .rozmiar_konfig = sizeof(struct virtio_snd_config),
    .konfig_czytaj = konfig_czytaj,
    .powiadom = powiadom,
    .reset = reset,
};

/* ---- the channel to K1's sound service (the main thread; the device under mz.zamek) ---- */

/* Under mz.zamek: it comes with nadzorca's messages (polaczenia.c reads them holding it). */
void dzwiek_kanal(int fd)
{
    int nowy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (nowy < 0)
        return;
    if (d.kanal >= 0)
        close(d.kanal);
    d.kanal = nowy;
    wlasny_zegar();
    mikrofon_stop(0); /* a new service: the input asks again (its STAN) */
    d.karta = K1_DZW_KARTA_SZUKA; /* its first DZWIEK_STAN says */
    k1_log(TAG, "dźwięk: kanał do usługi dźwięku");
}

int dzwiek_fd(void)
{
    return d.kanal;
}

static void dalej(void)
{
    const char *powod = NULL;
    if (wyjscie(&mz.urz[URZ_DZWIEK], &powod) != 0 ||
        (mz.mikrofon && mikrofon_kolejka(&mz.urz[URZ_DZWIEK], &d.s[DZW_WEJSCIE], &powod) != 0))
        koniec(powod);
}

void dzwiek_odbierz(void)
{
    zamek_wez("dźwięk: usługa");
    for (int i = 0; i < 64 && d.kanal >= 0; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(d.kanal, K1_UID_DZWIEK, K1_KIER_OD_DZWIEKU, &m, NULL);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) { /* the service ended: nadzorca gives a new channel with the new one */
            close(d.kanal);
            d.kanal = -1;
            wlasny_zegar();
            mikrofon_stop(0);
            break;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_DZWIEK_STAN) {
            d.karta = m.a;
            if (d.s[DZW_WYJSCIE].stan == DZW_GRA)
                otworz(); /* the card came while the guest plays */
            if (d.s[DZW_WEJSCIE].stan == DZW_GRA)
                mikrofon_start(d.kanal, d.karta); /* 0.13.5 step 2: … or records (nothing when it hears already) */
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_DZWIEK_NAGRANO)
            mikrofon_nagrano(&m);
        if (w != K1_ODB_JEST || m.typ != K1_KOM_DZWIEK_ODTWORZONO || !d.otwarty)
            continue;
        d.zagrane += (uint32_t)m.a < czeka() ? (uint32_t)m.a : czeka();
        if (m.b) /* refused (no card) or ended: our own clock plays what is left */
            wlasny_zegar();
    }
    dalej();
    zamek_oddaj();
}

int dzwiek_czas_ms(void)
{
    int t = !d.otwarty && (d.s[DZW_WYJSCIE].stan == DZW_GRA || d.cz_n > 0) ? 10 : -1;
    return t < 0 ? mikrofon_czas_ms(&d.s[DZW_WEJSCIE]) : t;
}

/* Our own clock (no service): the frames copied "play" at 48 kHz. */
void dzwiek_zegar(void)
{
    double t;
    uint32_t n;
    if (mz.mikrofon && mikrofon_czas_ms(&d.s[DZW_WEJSCIE]) >= 0) { /* 0.13.5 step 2: the input's silence */
        const char *powod = NULL;
        zamek_wez("mikrofon: zegar");
        mikrofon_zegar(&mz.urz[URZ_DZWIEK], &d.s[DZW_WEJSCIE], &powod);
        zamek_oddaj();
    }
    if (d.otwarty || (d.s[DZW_WYJSCIE].stan != DZW_GRA && d.cz_n == 0))
        return;
    zamek_wez("dźwięk: zegar");
    t = k1_teraz();
    n = (uint32_t)((t - d.zegar_t) * K1_DZW_CZESTOTLIWOSC);
    if (n > 0) {
        d.zegar_t += (double)n / K1_DZW_CZESTOTLIWOSC;
        d.zagrane += n < czeka() ? n : czeka();
        if (czeka() == 0)
            d.zegar_t = t; /* nothing to play: the clock does not save time for later */
    }
    dalej();
    zamek_oddaj();
}

void dzwiek_podsumowanie(void)
{
    mikrofon_podsumowanie();
    if (d.ramki > 0 || d.odrzucone > 0)
        k1_log(TAG, "dźwięk: zagrane %.1f s (bez usługi %.1f s), odrzucone transfery %lu, start %lu, stop %lu",
               (double)d.ramki / K1_DZW_CZESTOTLIWOSC, (double)d.bez_uslugi / K1_DZW_CZESTOTLIWOSC, d.odrzucone,
               d.starty, d.stopy);
}

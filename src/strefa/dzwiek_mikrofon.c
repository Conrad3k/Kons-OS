/* K1 strefa — the microphone of the guest's sound card (its input stream) (0.13.5 step 2, ADR 0065 T5). Only in a session with
 * the switch "Mikrofon" (mz.mikrofon, argv[5] from nadzorca): then the card has a second stream (DZW_WEJSCIE, dzwiek.h)
 * and the guest's driver gives buffers on the input queue (3) — a head (stream 1) it wrote, then room for the samples
 * and the status. While the guest's stream runs, this VMM hears K1's microphone: a ring of F32 of its own (a sealed memfd)
 * goes to K1's sound service (DZWIEK_OTWORZ b = SLUCHA — the service lets it only because nadzorca said so,
 * DZWIEK_KLIENT d), which writes 1024 frames into it every period and says so (DZWIEK_NAGRANO a); each buffer of the
 * guest goes back when it is full — the frames turned into the guest's format (dzw_z_f32). The guest's clock is the
 * microphone's. Without the service or its microphone (NAGRANO b = 1) the buffers get silence by our own clock, so the
 * browser never hangs. The session's microphone muted (its sign on the window's bar) — the service writes silence.
 * Trust boundary: the guest is hostile — each buffer checked (dzw_transfer: stream 1, whole frames, at most a period); one
 * with no room for its status ends the machine. In the log: numbers only, never a sample. Part of the VMM (dzwiek.c holds
 * the card; mz.zamek held by the callers). */
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/virtio_snd.h>

#include "dzwiek.h"
#include "k1_dzwiek.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_teraz.h"
#include "strefa.h"

#define Q_WEJSCIE 3
#define TRZYMANE 16 /* the guest's buffers held at once (Linux gives as many as periods: 4..8) */
#define PIERSCIEN_BAJTY (K1_DZW_PIERSCIEN * K1_DZW_KANALY * sizeof(float))

static struct {
    const float *pierscien;     /* our ring of F32: the service writes it, we read it */
    int pfd;
    int slucha;                 /* the service gives us the microphone (we sent OTWORZ, no NAGRANO b = 1 since) */
    uint32_t nagrane, czytane;  /* frames the service wrote in all, frames we took (mod 2^32) */
    double zegar_t;             /* our own clock (silence) when the service does not give it: when it ticked last */
    uint32_t cisza;             /* frames of silence our clock owes */
    struct vq_lancuch l[TRZYMANE];
    unsigned od, n;
    unsigned long ramki, odrzucone, starty;
} w = { .pfd = -1 };

static uint32_t czeka(void)
{
    return w.nagrane - w.czytane;
}

/* START of the input: our ring to the service (when it has a channel and a card), else silence by our own clock. */
void mikrofon_start(int kanal, int karta)
{
    w.starty++;
    w.zegar_t = k1_teraz();
    w.cisza = 0;
    if (w.slucha || kanal < 0 || karta != K1_DZW_KARTA_JEST)
        return;
    if (w.pierscien == NULL) {
        void *m;
        w.pfd = k1_memfd_nowy("k1-mikrofon-strefy");
        if (w.pfd < 0 || ftruncate(w.pfd, (off_t)PIERSCIEN_BAJTY) != 0 ||
            fcntl(w.pfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0 ||
            (m = mmap(NULL, PIERSCIEN_BAJTY, PROT_READ, MAP_SHARED, w.pfd, 0)) == MAP_FAILED) {
            k1_log(TAG, "mikrofon: pierścień niedostępny — gość słyszy ciszę");
            return;
        }
        w.pierscien = m;
    }
    w.slucha = 1;
    w.nagrane = w.czytane = 0;
    dzwiek_wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, K1_DZW_FORMAT_F32, K1_DZW_KIER_SLUCHA, 0, 0, 0), w.pfd);
    k1_log(TAG, "mikrofon: gość słucha (pierścień do usługi dźwięku)");
}

/* STOP, RELEASE, a reset, the service's channel gone: we hear no more (powiedz: tell the service). */
void mikrofon_stop(int powiedz)
{
    if (w.slucha && powiedz)
        dzwiek_wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_ZAMKNIJ, 0, K1_DZW_OD_RAZU, K1_DZW_KIER_SLUCHA, 0, 0, 0), -1);
    w.slucha = 0;
    w.zegar_t = k1_teraz();
}

/* DZWIEK_NAGRANO from the service (checked): new frames in our ring, or the end (then silence by our own clock). */
void mikrofon_nagrano(const struct k1_komunikat *m)
{
    if (!w.slucha)
        return;
    if (m->b) {
        w.slucha = 0;
        w.zegar_t = k1_teraz();
        k1_log(TAG, "mikrofon: usługa dźwięku nie daje mikrofonu — gość słyszy ciszę");
        return;
    }
    w.nagrane += (uint32_t)m->a;
    if (czeka() > K1_DZW_PIERSCIEN - K1_DZW_OKRES) /* the guest took too little: its oldest frames are lost */
        w.czytane = w.nagrane - K1_DZW_PIERSCIEN / 2;
}

/* A held buffer back: its samples (`ile` frames from the ring, or silence), then the status. */
static void oddaj(struct mmio_urz *u, const struct vq_lancuch *l, const struct dzw_strumien *s, int z_pierscienia)
{
    static uint8_t probki[DZW_DANE_MAKS];
    static float f[K1_DZW_OKRES * K1_DZW_KANALY];
    uint32_t dane = (uint32_t)(l->dl_zapis - sizeof(struct virtio_snd_pcm_status)), ile = s->ramka ? dane / s->ramka : 0;
    struct virtio_snd_pcm_status st = { VIRTIO_SND_S_OK, 0 };
    memset(probki, 0, dane);
    for (uint32_t zrobione = 0; z_pierscienia && zrobione < ile;) {
        uint32_t poz = w.czytane % K1_DZW_PIERSCIEN, n = ile - zrobione;
        n = n > K1_DZW_PIERSCIEN - poz ? K1_DZW_PIERSCIEN - poz : n;
        n = n > K1_DZW_OKRES ? K1_DZW_OKRES : n;
        memcpy(f, w.pierscien + (size_t)poz * K1_DZW_KANALY, (size_t)n * K1_DZW_KANALY * sizeof(float)); /* a copy first */
        dzw_z_f32(probki + (size_t)zrobione * s->ramka, f, n, s->format);
        w.czytane += n;
        zrobione += n;
    }
    vq_zapisz(l, 0, probki, dane);
    vq_zapisz(l, dane, &st, sizeof st);
    vq_oddaj(&u->q[Q_WEJSCIE], u->pamiec, l->glowa, dane + (uint32_t)sizeof st);
    w.ramki += ile;
}

/* The guest's new buffers taken and checked (held); a wrong one answered BAD_MSG at once. 1 some went back, 0, -1. */
static int wez(struct mmio_urz *u, const struct dzw_strumien *s, const char **powod)
{
    int bylo = 0;
    while (w.n < TRZYMANE) {
        struct vq_lancuch *l = &w.l[(w.od + w.n) % TRZYMANE];
        uint8_t nagl[4];
        int r = vq_wez(&u->q[Q_WEJSCIE], u->pamiec, l, powod);
        if (r <= 0)
            return r < 0 ? -1 : bylo;
        if (l->dl_zapis < sizeof(struct virtio_snd_pcm_status)) {
            *powod = "mikrofon: bufor bez miejsca na stan";
            return -1;
        }
        if (l->dl_odczyt != sizeof nagl ||
            dzw_transfer(nagl, (uint32_t)vq_czytaj(l, 0, nagl, sizeof nagl),
                         l->dl_zapis - sizeof(struct virtio_snd_pcm_status), s->ramka, DZW_WEJSCIE) != VIRTIO_SND_S_OK) {
            struct virtio_snd_pcm_status st = { VIRTIO_SND_S_BAD_MSG, 0 };
            w.odrzucone++;
            vq_zapisz(l, l->dl_zapis - sizeof st, &st, sizeof st);
            vq_oddaj(&u->q[Q_WEJSCIE], u->pamiec, l->glowa, (uint32_t)l->dl_zapis);
            bylo = 1;
            continue;
        }
        w.n++;
    }
    return bylo;
}

/* The input queue: new buffers, and the held ones that can be filled (the service's frames, or our clock's silence). */
int mikrofon_kolejka(struct mmio_urz *u, const struct dzw_strumien *s, const char **powod)
{
    int bylo = wez(u, s, powod);
    if (bylo < 0)
        return -1;
    while (s->stan == DZW_GRA && w.n > 0) {
        const struct vq_lancuch *l = &w.l[w.od];
        uint32_t ile = (uint32_t)((l->dl_zapis - sizeof(struct virtio_snd_pcm_status)) / s->ramka);
        if (w.slucha ? czeka() < ile : w.cisza < ile)
            break; /* not yet: when the service writes, or our clock ticks */
        if (!w.slucha)
            w.cisza -= ile;
        oddaj(u, l, s, w.slucha);
        w.od = (w.od + 1) % TRZYMANE;
        w.n--;
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
    return 0;
}

/* RELEASE or a reset: every held buffer back (silence) — virtio 5.14.6.6.5: the driver waits for them. */
void mikrofon_zwolnij(struct mmio_urz *u, const struct dzw_strumien *s, int przy_resecie)
{
    for (; w.n > 0; w.n--, w.od = (w.od + 1) % TRZYMANE)
        if (!przy_resecie)
            oddaj(u, &w.l[w.od], s, 0);
    w.od = 0;
    if (!przy_resecie)
        mmio_uzyte(u);
}

/* Our own clock (no microphone from the service): silence at 48 kHz while the guest's input runs. */
int mikrofon_czas_ms(const struct dzw_strumien *s)
{
    return s->stan == DZW_GRA && !w.slucha && w.n > 0 ? 10 : -1;
}

void mikrofon_zegar(struct mmio_urz *u, const struct dzw_strumien *s, const char **powod)
{
    double t = k1_teraz();
    if (s->stan != DZW_GRA || w.slucha)
        return;
    if (w.cisza < K1_DZW_PIERSCIEN) {
        uint32_t n = (uint32_t)((t - w.zegar_t) * K1_DZW_CZESTOTLIWOSC);
        w.zegar_t += (double)n / K1_DZW_CZESTOTLIWOSC;
        w.cisza += n;
    } else {
        w.zegar_t = t; /* no buffers taken: the clock does not save silence for later */
    }
    if (mikrofon_kolejka(u, s, powod) != 0)
        koniec(*powod);
}

void mikrofon_podsumowanie(void)
{
    if (w.ramki > 0 || w.odrzucone > 0)
        k1_log(TAG, "mikrofon: gość dostał %.1f s, odrzucone bufory %lu, starty %lu",
               (double)w.ramki / K1_DZW_CZESTOTLIWOSC, w.odrzucone, w.starty);
}

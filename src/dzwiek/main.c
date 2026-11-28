/* K1 dzwiek — the sound service (0.12.0 step 1, ADR 0054; T1–T4, T8).
 *
 * The only process with the sound card (as ekran with the screen): nadzorca opens the card's playback node and its controls
 * and hands them over on fd 3 (DZWIEK_KARTA), with every client's channel (DZWIEK_KLIENT: which row of K1_DZW_KLIENCI, its
 * UID). A client plays one stream: a ring of samples in a sealed memfd (DZWIEK_OTWORZ) and the number of new frames after
 * every write (DZWIEK_ZAPISANO). Every K1_DZW_OKRES frames the service copies what each stream has out of its ring, mixes
 * them, applies the volume of K1 and writes the period to the card. A steering client (gui) sets the volume and the mute
 * (DZWIEK_GLOSNOSC); every client hears the state (DZWIEK_STAN) and how much of its ring was played (DZWIEK_ODTWORZONO).
 * Nothing plays for K1_DZW_CISZA_S seconds: the card stops and frees its buffer (T8). Step 2: the output — the headphones
 * while something is in their jack (kontrolki.c; the kernel mutes the speakers itself), each output with a volume and a
 * mute of its own (D5: gui remembers them and gives them to every new service). Step 3: a zone's VMM is a client for
 * its guest (virtio-snd) — one per zone; each session has a volume and a mute of its own (DZWIEK_GLOSNOSC d), and the
 * steering clients hear which session plays (DZWIEK_SESJA: gui's list "Grają teraz", the speaker on its window's bar).
 * 0.12.3 step 1 (ADR 0055): the path in floats (k1_dzw_tor.c) — a ring of S16 or F32 (DZWIEK_OTWORZ a), the sum, the
 * volume, the limiter instead of clipping, the card's best format (karta.c: 24 bits when it takes them) with a dither.
 * Step 2: the system equalizer of each output on the sum, before the volume (korektor.c; DZWIEK_KOREKTOR from a steering
 * client, DZWIEK_KOREKTOR_STAN to them all). Step 3: Kons Spatial Audio — the scene of each output after the equalizer
 * (przestrzen.c; DZWIEK_SCENA, DZWIEK_SCENA_STAN in the same way).
 * Trust boundary: the clients are hostile (from step 3 a zone's VMM speaks for the guest): every message checked field by
 * field (k1_komunikat_sprawdz, sender UID), the frames counted by the service (k1_dzw_zapisano), the ring copied before
 * use. Sandbox: UID 111, no paths, seccomp: the base + the ALSA ioctls it uses (KLATKA_DZWIEK). The log: numbers and the
 * card's names only — never a sample. The clients, their streams and the mixing: klienci.c (0.13.5). Documentation:
 * docs/komponenty/dzwiek.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "dzwiek.h"
#include "k1_dzw_tor.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_teraz.h"

int stan_karty = K1_DZW_KARTA_SZUKA;
static struct k1_dzw_ogranicznik ogranicznik;
static struct k1_dzw_dither dither;
static unsigned long ograniczone, zle_probki;  /* periods the limiter lowered; F32 samples not finite or beyond ±1.0 */
static struct {
    double od;
    unsigned long ograniczone, zle_probki;
} minuta;                                       /* what the log said last (once a minute while it plays) */

static void karta_od_nadzorcy(const struct k1_komunikat *m, int fd)
{
    if (m->a == K1_DZW_WEZEL_KONTROLKI) {
        kontrolki_przyjmij(fd);
        return;
    }
    if (karta_przyjmij(fd, m->b, m->c) == 0) {
        stan_karty = K1_DZW_KARTA_JEST;
        cisza_od = k1_teraz();
        klienci_stan_wszystkim();
    }
}

static void od_nadzorcy(void)
{
    for (int i = 0; i < 16; i++) {
        struct k1_komunikat m;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_STEROWANIE, &m, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC)
            k1_koniec(TAG, "kanał od nadzorcy zamknięty");
        if (w != K1_ODB_JEST) {
            if (fd >= 0)
                close(fd);
            continue;
        }
        if (m.typ == K1_KOM_DZWIEK_KARTA && fd >= 0)
            karta_od_nadzorcy(&m, fd);
        else if (m.typ == K1_KOM_DZWIEK_KLIENT && fd >= 0)
            klienci_nowy(&m, fd);
        else if (m.typ == K1_KOM_DZWIEK_BEZ_KARTY && stan_karty == K1_DZW_KARTA_SZUKA) {
            k1_log(TAG, "komputer nie ma karty dźwiękowej");
            stan_karty = K1_DZW_KARTA_BRAK;
            klienci_stan_wszystkim();
        } else if (fd >= 0)
            close(fd);
    }
}

/* One period of every stream, mixed, through the equalizer and the scene, at K1's volume, limited, in the card's format,
 * to the card. */
static void okres(void)
{
    static float suma[K1_DZW_OKRES * K1_DZW_KANALY];
    static uint8_t wyj[K1_DZW_OKRES * K1_DZW_KANALY * KARTA_PROBKA_MAKS];
    int w = kontrolki_wyjscie(), r;
    memset(suma, 0, sizeof suma);
    zle_probki += klienci_zmieszaj(suma);
    korektor_licz(suma, K1_DZW_OKRES, w);
    przestrzen_licz(suma, K1_DZW_OKRES, w);
    k1_dzw_glosnosc(suma, K1_DZW_OKRES, klienci_wzmocnienie(w));
    ograniczone += (unsigned long)k1_dzw_ogranicz(&ogranicznik, suma, K1_DZW_OKRES);
    k1_dzw_na_karte(wyj, suma, K1_DZW_OKRES, karta_bajty(), karta_bity(), &dither);
    r = karta_zapisz(wyj);
    if (r < 0) {
        k1_log(TAG, "karta przestała grać");
        stan_karty = K1_DZW_KARTA_BLAD;
        klienci_zerwij();
        klienci_stan_wszystkim();
    }
}

/* Once a minute while it plays: how often the limiter worked and how many samples of F32 rings were not valid — only
 * when there were any (numbers only). */
static void co_minute(void)
{
    double t = k1_teraz();
    if (t - minuta.od < 60.0)
        return;
    if (ograniczone != minuta.ograniczone || zle_probki != minuta.zle_probki)
        k1_log(TAG, "ostatnia minuta: ogranicznik w %lu okresach, złe próbki od klientów: %lu",
               ograniczone - minuta.ograniczone, zle_probki - minuta.zle_probki);
    korektor_minuta();
    przestrzen_minuta();
    minuta.od = t;
    minuta.ograniczone = ograniczone;
    minuta.zle_probki = zle_probki;
}

/* The card plays while a stream is open and K1_DZW_CISZA_S seconds after the last one ended (silence), then sleeps.
 * `miejsce`: the card has room for a period (POLLOUT: at least avail_min = one period) — one period per wake-up, so that
 * nothing taken from a ring is lost on a full card. */
static void graj(int miejsce)
{
    static int gralo;
    if (karta_fd() < 0)
        return;
    if (klienci_strumienie() == 0 && k1_teraz() - cisza_od >= K1_DZW_CISZA_S) {
        karta_usypiaj();
        k1_log(TAG, "cisza: karta uśpiona (przerwy w dźwięku: %lu, ogranicznik w okresach: %lu, złe próbki: %lu)",
               karta_przerwy(), ograniczone, zle_probki);
        gralo = 0;
        return;
    }
    if (!miejsce)
        return;
    if (!gralo) {
        k1_log(TAG, "karta gra");
        minuta.od = k1_teraz();
    }
    gralo = 1;
    okres();
    co_minute();
}

int main(void)
{
    k1_klatka_w_usludze(TAG);
    klienci_start();
    k1_dzw_ogranicznik_start(&ogranicznik);
    k1_dzw_dither_start(&dither);
    korektor_start();
    przestrzen_start();
    k1_log(TAG, "gotowa (48 kHz, stereo, liczy w liczbach zmiennoprzecinkowych; głośność %d%%)",
           K1_DZW_GLOSNOSC_START);
    for (;;) {
        struct pollfd pfd[3 + KLIENCI_MAKS];
        int n = 0, kfd = karta_fd(), czas = -1;
        pfd[n++] = (struct pollfd){ FD_NADZORCA, POLLIN, 0 };
        pfd[n++] = (struct pollfd){ kfd, POLLOUT, 0 };
        pfd[n++] = (struct pollfd){ kontrolki_fd(), POLLIN, 0 };
        klienci_poll(&pfd[n]);
        n += KLIENCI_MAKS;
        if (kfd >= 0 && klienci_strumienie() == 0)
            czas = 250; /* the silence after the last stream: look whether it is time to sleep */
        czas = klienci_czas(czas);
        for (int j = 0; j < 2; j++) { /* 0.12.3 steps 2–3: the last change of the bands, of a scene, said within 0.5 s */
            int zal = j == 0 ? korektor_zalegle(k1_teraz()) : przestrzen_zalegle(k1_teraz());
            if (zal >= 0 && (czas < 0 || czas > zal))
                czas = zal;
        }
        if (poll(pfd, (nfds_t)n, czas) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR))
            od_nadzorcy();
        if ((pfd[2].revents & (POLLIN | POLLERR)) && kontrolki_zdarzenia()) {
            k1_log(TAG, "wyjście: %s", k1_dzw_wyjscie_nazwa(kontrolki_wyjscie()));
            klienci_stan_wszystkim();
        }
        klienci_obsluz(&pfd[3]);
        if (kfd >= 0)
            graj((pfd[1].revents & (POLLOUT | POLLERR)) != 0);
        klienci_sesje();
    }
}

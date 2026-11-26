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
 * card's names only — never a sample. Documentation: docs/komponenty/dzwiek.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dzwiek.h"
#include "k1_dzw_tor.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"

#define KLIENCI_MAKS 12     /* channels at once (gui, Ustawienia, the test program, a VMM per zone with a window) */
#define ODRZUCONE_MAKS 16   /* a client that sends this many bad messages loses its channel */
#define ZGLOS_CO K1_DZW_OKRES /* DZWIEK_ODTWORZONO after each period taken (or at the end): a zone gives its guest
                               * back each transfer when it was played (step 3) */

struct klient {
    int fd, rodzaj, strefa;   /* strefa: the zone of a client STREFA (1..K1_STREFY_MAKS), else 0 */
    uid_t uid;
    const uint8_t *pierscien; /* mapped read only; NULL: no stream */
    int format;               /* K1_DZW_FORMAT_* of the ring (0.12.3) */
    struct k1_dzw_pierscien p;
    int zamyka;               /* ZAMKNIJ after what was written plays */
    int zerwany;              /* step 3: the card failed while it played — ZAPISANO already on its way is no broken rule */
    uint32_t niezgloszone;    /* taken since the last ODTWORZONO */
    unsigned odrzucone;
};

static struct klient klienci[KLIENCI_MAKS];
static int stan_karty = K1_DZW_KARTA_SZUKA;
static int glosnosc[K1_DZW_WYJSCIE_MAKS + 1] = { 0, K1_DZW_GLOSNOSC_START, K1_DZW_GLOSNOSC_START };
static int wyciszone[K1_DZW_WYJSCIE_MAKS + 1];
static double cisza_od;   /* CLOCK_MONOTONIC when the last stream ended */
static struct sesja {
    int glosnosc, wyciszona; /* the session's own, on top of K1's (step 3) */
    int gra, zgloszona;      /* it plays now; what the steering clients were told (-1: nothing yet) */
    double gral;             /* when frames of it were last taken */
} sesje[K1_STREFY_MAKS + 1];
static struct k1_dzw_ogranicznik ogranicznik;
static struct k1_dzw_dither dither;
static unsigned long ograniczone, zle_probki;  /* periods the limiter lowered; F32 samples not finite or beyond ±1.0 */
static struct {
    double od;
    unsigned long ograniczone, zle_probki;
} minuta;                                       /* what the log said last (once a minute while it plays) */

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void wyslij(struct klient *k, struct k1_komunikat m)
{
    if (k1_kanal_wyslij(k->fd, &m, sizeof m, -1) != 0 && errno != EAGAIN)
        k1_log(TAG, "klient %s: wysłanie nieudane: %s", k1_dzw_klient_nazwa(k->rodzaj), strerror(errno));
}

static struct k1_komunikat stan(void)
{
    int jest = stan_karty == K1_DZW_KARTA_JEST, w = kontrolki_wyjscie();
    return k1_komunikat_nowy(K1_KOM_DZWIEK_STAN, 0, stan_karty, glosnosc[w], wyciszone[w], jest ? w : K1_DZW_WYJSCIE_BRAK, 0);
}

static void stan_wszystkim(void)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        if (klienci[i].fd >= 0)
            wyslij(&klienci[i], stan());
}

/* 0.12.3 steps 2–3: what an output plays (its equalizer: korektor_stan, its scene: przestrzen_stan) of `wyjscie` to every
 * steering client (BRAK: of both outputs to `jeden` only, a new one). */
static void wyjscie_zglos(struct klient *jeden, int wyjscie, struct k1_komunikat (*stan_wyjscia)(int))
{
    for (int i = 0; i < KLIENCI_MAKS; i++) {
        struct klient *k = &klienci[i];
        if (k->fd < 0 || !k1_dzw_klient_steruje(k->rodzaj) || (jeden != NULL && k != jeden))
            continue;
        for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++)
            if (wyjscie == K1_DZW_WYJSCIE_BRAK || w == wyjscie)
                wyslij(k, stan_wyjscia(w));
    }
}

static struct k1_komunikat sesja(int nr)
{
    const struct sesja *z = &sesje[nr];
    return k1_komunikat_nowy(K1_KOM_DZWIEK_SESJA, 0, nr, z->gra, z->glosnosc, z->wyciszona, 0);
}

/* Which session plays (K1_DZW_GRA_S after its last frames), its volume, its mute — to the steering clients when it
 * changed; to one new steering client `nowy`: every session that is not as at the start. */
static void sesje_zglos(struct klient *nowy)
{
    double t = teraz();
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        struct sesja *z = &sesje[nr];
        int stan;
        z->gra = z->gral > 0 && t - z->gral < K1_DZW_GRA_S;
        stan = z->gra | z->wyciszona << 1 | z->glosnosc << 2;
        if (nowy != NULL) {
            if (stan != (K1_DZW_SESJA_START << 2))
                wyslij(nowy, sesja(nr));
            continue;
        }
        if (stan == z->zgloszona)
            continue;
        z->zgloszona = stan;
        for (int i = 0; i < KLIENCI_MAKS; i++)
            if (klienci[i].fd >= 0 && k1_dzw_klient_steruje(klienci[i].rodzaj))
                wyslij(&klienci[i], sesja(nr));
    }
}

static void zamknij_strumien(struct klient *k, int zglos)
{
    if (k->pierscien != NULL)
        munmap((void *)(uintptr_t)k->pierscien, k1_dzw_pierscien_bajty(k->format));
    if (zglos && k->fd >= 0)
        wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_ODTWORZONO, 0, (int32_t)k->niezgloszone, 1, 0, 0, 0));
    k->pierscien = NULL;
    k->zamyka = 0;
    k->niezgloszone = 0;
    memset(&k->p, 0, sizeof k->p);
    cisza_od = teraz();
}

static void usun_klienta(struct klient *k, const char *powod)
{
    k1_log(TAG, "klient %s: koniec (%s)", k1_dzw_klient_nazwa(k->rodzaj), powod);
    zamknij_strumien(k, 0);
    close(k->fd);
    k->fd = -1;
}

/* DZWIEK_OTWORZ: exactly the ring's size in its format, sealed against shrinking and growing (a shrunk ring would end us
 * with SIGBUS). */
static int przyjmij_pierscien(struct klient *k, int fd, int format)
{
    size_t bajty = k1_dzw_pierscien_bajty(format);
    struct stat st;
    int pieczecie = fcntl(fd, F_GET_SEALS);
    void *m;
    if (k->pierscien != NULL || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        bajty == 0 || (uint64_t)st.st_size != bajty || pieczecie < 0 ||
        (pieczecie & (F_SEAL_SHRINK | F_SEAL_GROW)) != (F_SEAL_SHRINK | F_SEAL_GROW))
        return -1;
    m = mmap(NULL, bajty, PROT_READ, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED)
        return -1;
    k->pierscien = m;
    k->format = format;
    memset(&k->p, 0, sizeof k->p);
    return 0;
}

static void otworz(struct klient *k, int fd, int format)
{
    if (fd < 0 || przyjmij_pierscien(k, fd, format) != 0) {
        k->odrzucone++;
        k1_log(TAG, "klient %s: pierścień odrzucony", k1_dzw_klient_nazwa(k->rodzaj));
        wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_ODTWORZONO, 0, 0, 1, 0, 0, 0)); /* no stream: it ended at once */
    } else if (stan_karty != K1_DZW_KARTA_JEST || karta_obudz() != 0) {
        zamknij_strumien(k, 1); /* no card: the stream ends at once, the client hears it */
    } else {
        k1_log(TAG, "klient %s: strumień otwarty (%s)", k1_dzw_klient_nazwa(k->rodzaj), k1_dzw_format_nazwa(format));
    }
}

/* One valid message of a client. Returns 0, or -1 when it broke the rules (it loses its channel). */
static int od_klienta(struct klient *k, const struct k1_komunikat *m, int fd)
{
    switch (m->typ) {
    case K1_KOM_DZWIEK_OTWORZ:
        if (k->pierscien != NULL)
            return -1; /* one stream per client */
        k->zerwany = 0;
        otworz(k, fd, m->a);
        break;
    case K1_KOM_DZWIEK_ZAPISANO:
        if (k->pierscien == NULL && k->zerwany)
            break; /* written before it heard that the card failed and its stream ended */
        if (k->pierscien == NULL || k->zamyka || k1_dzw_zapisano(&k->p, (uint32_t)m->a) != 0)
            return -1; /* more than the ring holds, or no stream: the rules broken */
        break;
    case K1_KOM_DZWIEK_ZAMKNIJ:
        if (k->pierscien == NULL)
            break;
        if (m->a == K1_DZW_OD_RAZU || k1_dzw_czeka(&k->p) == 0)
            zamknij_strumien(k, 1);
        else
            k->zamyka = 1;
        break;
    case K1_KOM_DZWIEK_GLOSNOSC: {
        int w = m->c != K1_DZW_WYJSCIE_BRAK ? m->c : kontrolki_wyjscie();
        if (!k1_dzw_klient_steruje(k->rodzaj))
            return -1;
        if (m->d != 0) { /* step 3: a session's own (no number of it in the log: only that one changed) */
            sesje[m->d].glosnosc = m->a;
            sesje[m->d].wyciszona = m->b;
            k1_log(TAG, "sesja %d: głośność %d%%%s", m->d, m->a, m->b ? ", wyciszona" : "");
            sesje_zglos(NULL);
            break;
        }
        if (m->a == glosnosc[w] && m->b == wyciszone[w])
            break;
        k1_log(TAG, "głośność %d%%%s (%s)", m->a, m->b ? ", wyciszone" : "", k1_dzw_wyjscie_nazwa(w));
        glosnosc[w] = m->a;
        wyciszone[w] = m->b;
        stan_wszystkim();
        break;
    }
    case K1_KOM_DZWIEK_KOREKTOR: { /* 0.12.3 step 2: the bands of an output */
        int r = k1_dzw_klient_steruje(k->rodzaj) ? korektor_ustaw(m, teraz()) : -1;
        if (r < 0)
            return -1;
        if (r > 0)
            wyjscie_zglos(NULL, m->a, korektor_stan);
        break;
    }
    case K1_KOM_DZWIEK_SCENA: { /* 0.12.3 step 3: the scene of an output */
        int r = k1_dzw_klient_steruje(k->rodzaj) ? przestrzen_ustaw(m, teraz()) : -1;
        if (r < 0)
            return -1;
        if (r > 0)
            wyjscie_zglos(NULL, m->a, przestrzen_stan);
        break;
    }
    default:
        return -1;
    }
    return 0;
}

static void obsluz_klienta(struct klient *k)
{
    for (int i = 0; i < 32 && k->fd >= 0; i++) { /* a flood waits its turn: the others are served too */
        struct k1_komunikat m;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(k->fd, k->uid, K1_KIER_DO_DZWIEKU, &m, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            usun_klienta(k, "kanał zamknięty");
            break;
        }
        if (w == K1_ODB_JEST && od_klienta(k, &m, fd) != 0)
            k->odrzucone = ODRZUCONE_MAKS;
        if (fd >= 0)
            close(fd); /* the ring is mapped: the descriptor is not needed */
        if (w == K1_ODB_ODRZUCONY)
            k->odrzucone++;
        if (k->odrzucone >= ODRZUCONE_MAKS)
            usun_klienta(k, "złe komunikaty");
    }
}

static void nowy_klient(const struct k1_komunikat *m, int fd)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        if (klienci[i].fd < 0) {
            memset(&klienci[i], 0, sizeof klienci[i]);
            klienci[i].fd = fd;
            klienci[i].rodzaj = m->a;
            klienci[i].uid = (uid_t)m->b;
            klienci[i].strefa = m->c;
            k1_log(TAG, "klient %s: kanał", k1_dzw_klient_nazwa(m->a));
            wyslij(&klienci[i], stan());
            if (k1_dzw_klient_steruje(m->a)) {
                sesje_zglos(&klienci[i]);
                wyjscie_zglos(&klienci[i], K1_DZW_WYJSCIE_BRAK, korektor_stan);
                wyjscie_zglos(&klienci[i], K1_DZW_WYJSCIE_BRAK, przestrzen_stan);
            }
            return;
        }
    k1_log(TAG, "za dużo klientów: kanał zamknięty");
    close(fd);
}

static void karta_od_nadzorcy(const struct k1_komunikat *m, int fd)
{
    if (m->a == K1_DZW_WEZEL_KONTROLKI) {
        kontrolki_przyjmij(fd);
        return;
    }
    if (karta_przyjmij(fd, m->b, m->c) == 0) {
        stan_karty = K1_DZW_KARTA_JEST;
        cisza_od = teraz();
        stan_wszystkim();
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
            nowy_klient(&m, fd);
        else if (m.typ == K1_KOM_DZWIEK_BEZ_KARTY && stan_karty == K1_DZW_KARTA_SZUKA) {
            k1_log(TAG, "komputer nie ma karty dźwiękowej");
            stan_karty = K1_DZW_KARTA_BRAK;
            stan_wszystkim();
        } else if (fd >= 0)
            close(fd);
    }
}

/* Each stream's period into the sum (its session at its own volume). */
static void zmieszaj(float *suma)
{
    static uint8_t kopia[K1_DZW_OKRES * K1_DZW_RAMKA_MAKS];
    for (int i = 0; i < KLIENCI_MAKS; i++) {
        struct klient *k = &klienci[i];
        float wzm = 1.0f;
        uint32_t n;
        if (k->fd < 0 || k->pierscien == NULL)
            continue;
        /* copied first: the client may write meanwhile; missing frames are silence */
        n = k1_dzw_pobierz(&k->p, k->pierscien, k1_dzw_ramka_bajty(k->format), kopia, K1_DZW_OKRES);
        if (k->strefa != 0) { /* step 3: a session at its own volume */
            struct sesja *z = &sesje[k->strefa];
            wzm = k1_dzw_wzmocnienie(z->glosnosc, z->wyciszona);
            if (n > 0)
                z->gral = teraz();
        }
        zle_probki += k1_dzw_dodaj(suma, kopia, k->format, n, wzm);
        k->niezgloszone += n;
        if (k->zamyka && k1_dzw_czeka(&k->p) == 0)
            zamknij_strumien(k, 1);
        else if (k->niezgloszone >= ZGLOS_CO) {
            wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_ODTWORZONO, 0, (int32_t)k->niezgloszone, 0, 0, 0, 0));
            k->niezgloszone = 0;
        }
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
    zmieszaj(suma);
    korektor_licz(suma, K1_DZW_OKRES, w);
    przestrzen_licz(suma, K1_DZW_OKRES, w);
    k1_dzw_glosnosc(suma, K1_DZW_OKRES, k1_dzw_wzmocnienie(glosnosc[w], wyciszone[w]));
    ograniczone += (unsigned long)k1_dzw_ogranicz(&ogranicznik, suma, K1_DZW_OKRES);
    k1_dzw_na_karte(wyj, suma, K1_DZW_OKRES, karta_bajty(), karta_bity(), &dither);
    r = karta_zapisz(wyj);
    if (r < 0) {
        k1_log(TAG, "karta przestała grać");
        stan_karty = K1_DZW_KARTA_BLAD;
        for (int i = 0; i < KLIENCI_MAKS; i++)
            if (klienci[i].fd >= 0 && klienci[i].pierscien != NULL) {
                zamknij_strumien(&klienci[i], 1);
                klienci[i].zerwany = 1;
            }
        stan_wszystkim();
    }
}

/* Once a minute while it plays: how often the limiter worked and how many samples of F32 rings were not valid — only
 * when there were any (numbers only). */
static void co_minute(void)
{
    double t = teraz();
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

static int strumienie(void)
{
    int n = 0;
    for (int i = 0; i < KLIENCI_MAKS; i++)
        n += klienci[i].fd >= 0 && klienci[i].pierscien != NULL;
    return n;
}

/* The card plays while a stream is open and K1_DZW_CISZA_S seconds after the last one ended (silence), then sleeps.
 * `miejsce`: the card has room for a period (POLLOUT: at least avail_min = one period) — one period per wake-up, so that
 * nothing taken from a ring is lost on a full card. */
static void graj(int miejsce)
{
    static int gralo;
    if (karta_fd() < 0)
        return;
    if (strumienie() == 0 && teraz() - cisza_od >= K1_DZW_CISZA_S) {
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
        minuta.od = teraz();
    }
    gralo = 1;
    okres();
    co_minute();
}

int main(void)
{
    k1_klatka_w_usludze(TAG);
    for (int i = 0; i < KLIENCI_MAKS; i++)
        klienci[i].fd = -1;
    for (int nr = 0; nr <= K1_STREFY_MAKS; nr++)
        sesje[nr] = (struct sesja){ K1_DZW_SESJA_START, 0, 0, -1, 0 };
    k1_dzw_ogranicznik_start(&ogranicznik);
    k1_dzw_dither_start(&dither);
    korektor_start();
    przestrzen_start();
    k1_log(TAG, "gotowa (48 kHz, stereo, liczy w liczbach zmiennoprzecinkowych; głośność %d%%)",
           glosnosc[K1_DZW_WYJSCIE_GLOSNIKI]);
    for (;;) {
        struct pollfd pfd[3 + KLIENCI_MAKS];
        int n = 0, kfd = karta_fd(), czas = -1;
        pfd[n++] = (struct pollfd){ FD_NADZORCA, POLLIN, 0 };
        pfd[n++] = (struct pollfd){ kfd, POLLOUT, 0 };
        pfd[n++] = (struct pollfd){ kontrolki_fd(), POLLIN, 0 };
        for (int i = 0; i < KLIENCI_MAKS; i++)
            pfd[n++] = (struct pollfd){ klienci[i].fd, POLLIN, 0 };
        if (kfd >= 0 && strumienie() == 0)
            czas = 250; /* the silence after the last stream: look whether it is time to sleep */
        for (int nr = 1; nr <= K1_STREFY_MAKS; nr++)
            if (sesje[nr].gra && (czas < 0 || czas > 250))
                czas = 250; /* step 3: a session that stopped playing is said within a quarter of a second */
        for (int j = 0; j < 2; j++) { /* 0.12.3 steps 2–3: the last change of the bands, of a scene, said within 0.5 s */
            int zal = j == 0 ? korektor_zalegle(teraz()) : przestrzen_zalegle(teraz());
            if (zal >= 0 && (czas < 0 || czas > zal))
                czas = zal;
        }
        if (poll(pfd, (nfds_t)n, czas) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (pfd[0].revents & (POLLIN | POLLHUP | POLLERR))
            od_nadzorcy();
        if ((pfd[2].revents & (POLLIN | POLLERR)) && kontrolki_zdarzenia()) {
            k1_log(TAG, "wyjście: %s", k1_dzw_wyjscie_nazwa(kontrolki_wyjscie()));
            stan_wszystkim();
        }
        for (int i = 0; i < KLIENCI_MAKS; i++)
            if (klienci[i].fd >= 0 && (pfd[3 + i].revents & (POLLIN | POLLHUP | POLLERR)))
                obsluz_klienta(&klienci[i]);
        if (kfd >= 0)
            graj((pfd[1].revents & (POLLOUT | POLLERR)) != 0);
        sesje_zglos(NULL);
    }
}

/* K1 dzwiek — the clients (split from main.c before the microphone, 0.13.5 step 1): their channels and messages, each
 * one's stream (its sealed ring, counted by us — k1_dzw_pierscien), the volume and mute of K1's outputs and of each
 * zone's session (step 3 of 0.12.0), what the steering clients hear (DZWIEK_STAN, DZWIEK_SESJA, the equalizer's and the
 * scene's state) and the mixing of every stream's period into the sum. Trust boundary: the clients are hostile — every
 * message is checked field by field (k1_odbierz_komunikat with the client's UID), the ring copied before use; a client
 * that breaks the rules or sends ODRZUCONE_MAKS bad messages loses its channel. See main.c and docs/komponenty/dzwiek.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dzwiek.h"
#include "k1_dzw_tor.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_teraz.h"

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
static int glosnosc[K1_DZW_WYJSCIE_MAKS + 1] = { 0, K1_DZW_GLOSNOSC_START, K1_DZW_GLOSNOSC_START };
static int wyciszone[K1_DZW_WYJSCIE_MAKS + 1];
double cisza_od;          /* CLOCK_MONOTONIC when the last stream ended */
static struct sesja {
    int glosnosc, wyciszona; /* the session's own, on top of K1's (step 3) */
    int gra, zgloszona;      /* it plays now; what the steering clients were told (-1: nothing yet) */
    double gral;             /* when frames of it were last taken */
} sesje[K1_STREFY_MAKS + 1];

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

void klienci_stan_wszystkim(void)
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
    double t = k1_teraz();
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
    cisza_od = k1_teraz();
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
        klienci_stan_wszystkim();
        break;
    }
    case K1_KOM_DZWIEK_KOREKTOR: { /* 0.12.3 step 2: the bands of an output */
        int r = k1_dzw_klient_steruje(k->rodzaj) ? korektor_ustaw(m, k1_teraz()) : -1;
        if (r < 0)
            return -1;
        if (r > 0)
            wyjscie_zglos(NULL, m->a, korektor_stan);
        break;
    }
    case K1_KOM_DZWIEK_SCENA: { /* 0.12.3 step 3: the scene of an output */
        int r = k1_dzw_klient_steruje(k->rodzaj) ? przestrzen_ustaw(m, k1_teraz()) : -1;
        if (r < 0)
            return -1;
        if (r == 1)
            wyjscie_zglos(NULL, m->a, przestrzen_stan);
        if (r == 2) /* step 4: an output's "Własna" */
            wyjscie_zglos(NULL, m->a - K1_DZW_SCENA_WLASNA, przestrzen_wlasna_stan);
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

void klienci_nowy(const struct k1_komunikat *m, int fd)
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
                wyjscie_zglos(&klienci[i], K1_DZW_WYJSCIE_BRAK, przestrzen_wlasna_stan); /* step 4 */
            }
            return;
        }
    k1_log(TAG, "za dużo klientów: kanał zamknięty");
    close(fd);
}


/* Each stream's period into the sum (its session at its own volume); returns the F32 samples that were not valid. */
unsigned long klienci_zmieszaj(float *suma)
{
    static uint8_t kopia[K1_DZW_OKRES * K1_DZW_RAMKA_MAKS];
    unsigned long zle = 0;
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
                z->gral = k1_teraz();
        }
        zle += k1_dzw_dodaj(suma, kopia, k->format, n, wzm);
        k->niezgloszone += n;
        if (k->zamyka && k1_dzw_czeka(&k->p) == 0)
            zamknij_strumien(k, 1);
        else if (k->niezgloszone >= ZGLOS_CO) {
            wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_ODTWORZONO, 0, (int32_t)k->niezgloszone, 0, 0, 0, 0));
            k->niezgloszone = 0;
        }
    }
    return zle;
}

int klienci_strumienie(void)
{
    int n = 0;
    for (int i = 0; i < KLIENCI_MAKS; i++)
        n += klienci[i].fd >= 0 && klienci[i].pierscien != NULL;
    return n;
}

/* The card failed while it played: every stream ends (its client hears it), what is on its way is no broken rule. */
void klienci_zerwij(void)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        if (klienci[i].fd >= 0 && klienci[i].pierscien != NULL) {
            zamknij_strumien(&klienci[i], 1);
            klienci[i].zerwany = 1;
        }
}

float klienci_wzmocnienie(int wyjscie)
{
    return k1_dzw_wzmocnienie(glosnosc[wyjscie], wyciszone[wyjscie]);
}

void klienci_start(void)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        klienci[i].fd = -1;
    for (int nr = 0; nr <= K1_STREFY_MAKS; nr++)
        sesje[nr] = (struct sesja){ K1_DZW_SESJA_START, 0, 0, -1, 0 };
}

/* The clients' channels into the poll set from pfd[0] (KLIENCI_MAKS entries; -1 for a free slot). */
void klienci_poll(struct pollfd *pfd)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        pfd[i] = (struct pollfd){ klienci[i].fd, POLLIN, 0 };
}

void klienci_obsluz(const struct pollfd *pfd)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        if (klienci[i].fd >= 0 && (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
            obsluz_klienta(&klienci[i]);
}

/* Step 3: a session that stopped playing is said within a quarter of a second — the poll's timeout for it (or `czas`). */
int klienci_czas(int czas)
{
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++)
        if (sesje[nr].gra && (czas < 0 || czas > 250))
            czas = 250;
    return czas;
}

void klienci_sesje(void)
{
    sesje_zglos(NULL);
}

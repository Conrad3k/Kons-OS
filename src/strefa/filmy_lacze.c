/* K1 strefa — films by the card: the link between the film contexts of the guest (gpu_filmy.c) and the film decoder of
 * this zone (0.13.2 step 3, ADR 0063 pkt 1, 3, 4; usługa "filmy", docs/komponenty/filmy.md).
 *  - At the start (gpu_lacze.c, a session with a graphics broker): the VMM asks nadzorca once for the decoder
 *    (STREFA_POLACZ c = K1_STREFA_FILMY on fd 7), reads what the card decodes (its first word, FILMY_UMIE) and closes the
 *    channel (the decoder ends by itself) — that is the capset K1_FILMY for the guest; refused or silent: no film context.
 *  - The first film of a context: the decoder again, on request (the gpu thread asks, the main thread gets nadzorca's
 *    answer — filmy_od_nadzorcy — and hands it over); its window (a memfd of K1_FILMY_OKNO, sealed) → FILMY_OKNO. When no
 *    film context is left, the channel closes and the decoder ends (its memory goes back — ADR 0063 pkt 4).
 *  - A command: the context's film → the decoder's (at most K1_FILMY_FILMOW_MAKS of the zone), the request copied from
 *    the context's shared memory into the window (KLATKA), the frame into the zone's buffer of the card (POBIERZ: its
 *    dma-buf with FILMY_OKNO), the answer back into the shared memory. The decoder checks every request; its answers are
 *    checked here field by field (k1_kom_filmy.c, its UID). It ends or stays silent (10 s): K1_KF_BLAD_DEKODER from then on
 *    — the guest's driver gets an error, Firefox plays on the processor, the zone and K1 live.
 * The gpu thread calls everything but filmy_od_nadzorcy (the main thread, under mz.zamek). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "filmy_lacze.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_teraz.h"
#include "polaczenia.h"
#include "strefa.h"

#define CZEKAJ_S 10.0      /* one answer of the decoder, at most */
#define NADZORCA_S 5.0     /* nadzorca's answer, at most */

static struct {
    int kanal;                          /* to the decoder (-1: none now) */
    int padl;                           /* it ended or stayed silent: no more films this session */
    uint8_t *okno;                      /* its window (mapped while the channel lives) */
    uid_t uid;
    struct {                            /* the film contexts */
        uint8_t *wsp;
        uint64_t dl;
    } k[K1_KF_KONTEKSTY + 1];
    uint8_t wlasciciel[K1_FILMY_FILMOW_MAKS + 1][2]; /* decoder's film → (context, its film); 0: free */
    unsigned long polecenia, bledy;
} fl = { .kanal = -1 };

/* nadzorca's answer: the main thread puts it here, the asking thread takes it */
static pthread_mutex_t skrzynka_zamek = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t skrzynka_jest = PTHREAD_COND_INITIALIZER;
static int skrzynka_stan = 3, skrzynka_fd = -1; /* stan: 0 waiting, 1 a channel (fd), 2 refused, 3 nobody asks,
                                                  * 4 busy (the decoder before still ends) */

void filmy_od_nadzorcy(int fd, int powod)
{
    pthread_mutex_lock(&skrzynka_zamek);
    if (skrzynka_stan == 0 && skrzynka_fd < 0) {
        skrzynka_fd = fd >= 0 ? fcntl(fd, F_DUPFD_CLOEXEC, 0) : -1; /* (dup is not in the sandbox) */
        skrzynka_stan = skrzynka_fd >= 0 ? 1 : fd < 0 && powod == K1_STREFA_ODM_LIMIT ? 4 : 2;
    }
    pthread_cond_signal(&skrzynka_jest);
    pthread_mutex_unlock(&skrzynka_zamek);
}

/* Asks nadzorca for the decoder once: the channel, -1 (refused), -2 (busy). `na_starcie`: the main thread itself takes
 * the answers off fd 7 (pol_od_nadzorcy — before the machine runs); else the main thread's loop does and we wait. */
static int popros_raz(int na_starcie)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, 0, 0, 0, K1_STREFA_FILMY, 0, 0);
    double do_kiedy = k1_teraz() + NADZORCA_S;
    struct timespec t;
    int fd;
    pthread_mutex_lock(&skrzynka_zamek);
    skrzynka_stan = 0, skrzynka_fd = -1;
    pthread_mutex_unlock(&skrzynka_zamek);
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        return -1;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += (time_t)NADZORCA_S;
    pthread_mutex_lock(&skrzynka_zamek);
    while (skrzynka_stan == 0 && k1_teraz() < do_kiedy) {
        if (na_starcie) {
            struct pollfd p = { FD_NADZORCA, POLLIN, 0 };
            pthread_mutex_unlock(&skrzynka_zamek);
            if (poll(&p, 1, 100) == 1)
                pol_od_nadzorcy();
            pthread_mutex_lock(&skrzynka_zamek);
        } else if (pthread_cond_timedwait(&skrzynka_jest, &skrzynka_zamek, &t) == ETIMEDOUT) {
            break;
        }
    }
    fd = skrzynka_stan == 1 ? skrzynka_fd : skrzynka_stan == 4 ? -2 : -1;
    skrzynka_stan = 3, skrzynka_fd = -1; /* a late answer is not taken (filmy_od_nadzorcy waits for stan 0) */
    pthread_mutex_unlock(&skrzynka_zamek);
    return fd;
}

/* …and again while the decoder before still ends (it ends at once when we close its channel: 2 s at most) */
static int popros(int na_starcie)
{
    int fd = popros_raz(na_starcie);
    for (int i = 0; fd == -2 && i < 20; i++) {
        poll(NULL, 0, 100);
        fd = popros_raz(na_starcie);
    }
    return fd < 0 ? -1 : fd;
}

/* The decoder's next message within ms: 1 (*k), 0 (none), -1 (it ended). */
static int odbierz(struct k1_komunikat *k, int ms)
{
    struct pollfd p = { fl.kanal, POLLIN, 0 };
    enum k1_odbior_wynik w;
    int fd = -1;
    if (poll(&p, 1, ms) <= 0)
        return 0;
    w = k1_odbierz_komunikat(fl.kanal, fl.uid, K1_KIER_OD_FILMOW, k, &fd);
    if (fd >= 0)
        close(fd);
    return w == K1_ODB_KONIEC ? -1 : w == K1_ODB_JEST;
}

static void zamknij_kanal(void)
{
    if (fl.okno != NULL)
        munmap(fl.okno, K1_FILMY_OKNO);
    if (fl.kanal >= 0)
        close(fl.kanal);
    fl.okno = NULL, fl.kanal = -1;
    memset(fl.wlasciciel, 0, sizeof fl.wlasciciel);
}

/* The decoder's first word (FILMY_UMIE) into *z: 0, or -1. */
static int pierwsze_slowo(struct k1_filmy_zbior *z)
{
    struct k1_komunikat k;
    double do_kiedy = k1_teraz() + CZEKAJ_S;
    int r = 0;
    while (r == 0 && k1_teraz() < do_kiedy)
        r = odbierz(&k, 200);
    if (r <= 0 || k.typ != K1_KOM_FILMY_UMIE || k.b != K1_FILMY_KARTA_UMIE)
        return -1;
    *z = (struct k1_filmy_zbior){ K1_FILMY_CAPSET_WERSJA, (uint32_t)k.a, (uint32_t)k.d, (uint32_t)k.e };
    return 0;
}

int filmy_lacze_start(struct k1_filmy_zbior *z)
{
    memset(z, 0, sizeof *z);
    fl.uid = (uid_t)(K1_UID_FILMY_STREF + mz.opis->nr);
    fl.kanal = popros(1);
    if (fl.kanal < 0 || pierwsze_slowo(z) != 0) {
        k1_log(TAG, "filmy kartą: %s — filmy procesorem", fl.kanal < 0 ? "K1 nie dał dekodera" : "dekoder milczy");
        memset(z, 0, sizeof *z);
        zamknij_kanal();
        return -1;
    }
    zamknij_kanal(); /* the decoder ends; the first film asks again */
    k1_log(TAG, "filmy kartą: karta rozpakowuje (kodeki 0x%x, do %u × %u) — zbiór możliwości filmów dla gościa",
           z->kodeki, z->maks_w, z->maks_h);
    return 0;
}

/* Sends k (with fd) and waits for its FILMY_WYNIK: its b, or K1_KF_BLAD_DEKODER (the decoder is gone). */
static int wynik(struct k1_komunikat k, int fd)
{
    struct k1_komunikat o;
    double do_kiedy = k1_teraz() + CZEKAJ_S;
    if (k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_DO_FILMOW, &o) != 0)
        return K1_FILMY_BLAD_PROSBA; /* a field the decoder would drop without an answer */
    if (k1_kanal_wyslij(fl.kanal, &k, sizeof k, fd) != 0)
        return K1_KF_BLAD_DEKODER;
    while (k1_teraz() < do_kiedy) {
        int r = odbierz(&o, 100);
        if (r < 0)
            break;
        if (r == 1 && o.typ == K1_KOM_FILMY_WYNIK && o.a == k.typ && o.obiekt == k.obiekt)
            return o.b;
    }
    fl.padl = 1;
    k1_log(TAG, "filmy kartą: dekoder filmów zakończył się albo milczy — filmy procesorem do końca sesji");
    zamknij_kanal();
    return K1_KF_BLAD_DEKODER;
}

/* The channel and the window, when there is none (the first film): 0, or K1_KF_BLAD_DEKODER. */
static int kanal(void)
{
    struct k1_filmy_zbior z;
    int okno;
    if (fl.kanal >= 0)
        return 0;
    if (fl.padl || (fl.kanal = popros(0)) < 0 || pierwsze_slowo(&z) != 0) {
        zamknij_kanal();
        return K1_KF_BLAD_DEKODER;
    }
    okno = k1_memfd_nowy("strefa-filmy");
    if (okno >= 0 && (ftruncate(okno, K1_FILMY_OKNO) != 0 || fcntl(okno, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) != 0))
        close(okno), okno = -1;
    fl.okno = okno >= 0 ? mmap(NULL, K1_FILMY_OKNO, PROT_READ | PROT_WRITE, MAP_SHARED, okno, 0) : MAP_FAILED;
    if (fl.okno == MAP_FAILED)
        fl.okno = NULL;
    if (fl.okno == NULL || wynik(k1_komunikat_nowy(K1_KOM_FILMY_OKNO, 0, 0, 0, 0, 0, 0), okno) != 0) {
        if (okno >= 0)
            close(okno);
        zamknij_kanal();
        return K1_KF_BLAD_DEKODER;
    }
    close(okno);
    return 0;
}

/* The decoder's film of (context k, its film f); `nowy`: a free one for it. 0: none. */
static uint32_t film(uint32_t k, uint32_t f, int nowy)
{
    for (uint32_t d = 1; d <= K1_FILMY_FILMOW_MAKS; d++)
        if (nowy ? fl.wlasciciel[d][0] == 0 : fl.wlasciciel[d][0] == k && fl.wlasciciel[d][1] == f)
            return d;
    return 0;
}

static int wykonaj(uint32_t k, const struct k1_kf_polecenie *p, int fd)
{
    uint32_t d = film(k, p->film, 0);
    int r;
    if (p->typ != K1_KF_OTWORZ && d == 0)
        return K1_FILMY_BLAD_FILM;
    if ((r = kanal()) != 0)
        return r;
    switch (p->typ) {
    case K1_KF_OTWORZ:
        if (d != 0 || (d = film(k, p->film, 1)) == 0)
            return d != 0 ? K1_FILMY_BLAD_FILM : K1_FILMY_BLAD_PAMIEC; /* the zone's films: K1_FILMY_FILMOW_MAKS */
        r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, d, (int32_t)p->a, (int32_t)p->b, (int32_t)p->c, (int32_t)p->d, 0), -1);
        if (r == 0)
            fl.wlasciciel[d][0] = (uint8_t)k, fl.wlasciciel[d][1] = (uint8_t)p->film;
        return r;
    case K1_KF_KLATKA:
        memcpy(fl.okno, fl.k[k].wsp + K1_KF_PROSBA, p->b); /* k1_kf_sprawdz: b fits the shared memory */
        return wynik(k1_komunikat_nowy(K1_KOM_FILMY_KLATKA, d, (int32_t)p->a, (int32_t)p->b, 0, 0, 0), -1);
    case K1_KF_POBIERZ:
        return wynik(k1_komunikat_nowy(K1_KOM_FILMY_OKNO, d, (int32_t)p->a, 0, 0, 0, 0), fd);
    default: /* K1_KF_ZAMKNIJ */
        r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_ZAMKNIJ, d, 0, 0, 0, 0, 0), -1);
        fl.wlasciciel[d][0] = fl.wlasciciel[d][1] = 0;
        return r;
    }
}

void filmy_lacze_polecenie(uint32_t k, const struct k1_kf_polecenie *p, int fd, int blad)
{
    struct k1_kf_odpowiedz o;
    memset(&o, 0, sizeof o);
    o.numer = p->numer;
    o.wynik = blad != 0 ? blad : fl.k[k].wsp == NULL ? K1_FILMY_BLAD_OKNO : wykonaj(k, p, fd);
    fl.polecenia++, fl.bledy += o.wynik != 0;
    if (fl.k[k].wsp != NULL) {
        memcpy(fl.k[k].wsp, &o, sizeof o); /* before the command is answered: the guest reads it after the fence */
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }
}

int filmy_lacze_wspolna(uint32_t k, uint64_t rozmiar, int *fd)
{
    void *p;
    *fd = k1_memfd_nowy("strefa-filmy-kontekst");
    if (*fd < 0)
        return -1;
    p = ftruncate(*fd, (off_t)rozmiar) == 0 && fcntl(*fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) == 0
            ? mmap(NULL, rozmiar, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0) : MAP_FAILED;
    if (p == MAP_FAILED) {
        close(*fd), *fd = -1;
        return -1;
    }
    if (fl.k[k].wsp != NULL)
        munmap(fl.k[k].wsp, fl.k[k].dl);
    fl.k[k].wsp = p, fl.k[k].dl = rozmiar;
    return 0;
}

void filmy_lacze_koniec(uint32_t k)
{
    int zostaly = 0;
    for (uint32_t d = 1; d <= K1_FILMY_FILMOW_MAKS; d++)
        if (fl.wlasciciel[d][0] == k && fl.kanal >= 0) {
            (void)wynik(k1_komunikat_nowy(K1_KOM_FILMY_ZAMKNIJ, d, 0, 0, 0, 0, 0), -1);
            fl.wlasciciel[d][0] = fl.wlasciciel[d][1] = 0;
        }
    if (fl.k[k].wsp != NULL)
        munmap(fl.k[k].wsp, fl.k[k].dl);
    fl.k[k].wsp = NULL, fl.k[k].dl = 0;
    for (uint32_t i = 1; i <= K1_KF_KONTEKSTY; i++)
        zostaly |= fl.k[i].wsp != NULL;
    if (!zostaly && fl.kanal >= 0)
        zamknij_kanal(); /* no film context left: the decoder ends, its memory goes back (ADR 0063 pkt 4) */
}

void filmy_lacze_podsumowanie(void)
{
    if (fl.polecenia > 0 || fl.padl)
        k1_log(TAG, "filmy kartą: polecenia %lu (błędy %lu)%s", fl.polecenia, fl.bledy,
               fl.padl ? ", dekoder zakończony" : "");
}

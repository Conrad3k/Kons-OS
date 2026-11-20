/* K1 posrednik — the graphics broker (0.10.9 step 5, ADR 0049 pkt 2, 7; plan 0.10.9 T10–T15, S37–S41, S44).
 *
 * Fast graphics: the zone's own driver of the card (Mesa) builds the work; this service takes the requests of the bridge
 * "native context" from ONE client — the VMM of its zone (one broker per zone, started and ended with it — nadzorca
 * posrednik.c; step 6), nadzorca's question at boot ("can the card do it?" — the answer, then the end) or the test
 * program — checks each field by field (k1_posrednik.c) and only then gives it to the card.
 * Arguments: the zone (0: nadzorca's question), "t": the test engine (no card; test image only, on request of the test
 * program). fd 3 = the channel to the client (its UID checked: the zone's VMM, or UID 0 for nadzorca and the test
 * program), fd 4 = the render node of the card (i915; nadzorca gives it only to the card's services) or /dev/null.
 * First message: POSR_UMIE — whether the card can keep the zones apart (full PPGTT, context isolation, pinned
 * addresses, out-fences); without it the broker ends (no fast graphics on that computer — X230). Then: the window and
 * the shared memory (once each), streams of requests, buffers given resource numbers, shared (dma-buf for the VMM),
 * released, fences waited for.
 * Trust boundary: the client is hostile (the zone); the card's driver in the kernel is within its reach only through
 * the checked requests, and the card keeps each client's work in its own address space. A context of ours that hangs
 * the card ends the broker (exit K1_POSR_KONIEC_ZAWIESZENIE): fast graphics of this zone ends, not K1 (T15, S39).
 * Sandbox: UID 110 (the question) or 410 + zone, no paths, seccomp: the base + DRM ioctls only (no open, no memfd, no
 * socket); memory: K1_POSR_PAMIEC for the card's buffers + the code (profile.c).
 * The log: numbers only — never a byte of a request. Documentation: docs/komponenty/posrednik.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <drm/drm.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "posrednik.h"

const struct silnik *silnik;
struct k1_posr_obiekty obiekty;
volatile uint8_t *wspolna;
uint32_t wspolna_rozmiar;
int zawieszona;
static uid_t klient;
static volatile uint8_t *okno;
static unsigned long odrzucone;

void wyslij(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(FD_KLIENT, &k, sizeof k, fd) != 0)
        k1_log(TAG, "wysłanie do klienta nieudane: %s", strerror(errno));
}

/* A memfd of exactly `rozmiar` bytes that cannot shrink or grow (shrunk under us it would end us with SIGBUS). */
static volatile uint8_t *mapuj(int fd, uint32_t rozmiar)
{
    struct stat st;
    int pieczecie = fcntl(fd, F_GET_SEALS);
    void *m;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != rozmiar || pieczecie < 0 ||
        (pieczecie & (F_SEAL_SHRINK | F_SEAL_GROW)) != (F_SEAL_SHRINK | F_SEAL_GROW))
        return NULL;
    m = mmap(NULL, rozmiar, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return m == MAP_FAILED ? NULL : m;
}

static int przyjmij_wspolna(int fd, uint32_t rozmiar)
{
    struct k1_posr_wspolna w = { 0, sizeof w, 0 };
    if (wspolna != NULL || (wspolna = mapuj(fd, rozmiar)) == NULL)
        return K1_POSR_BLAD_OKNO;
    wspolna_rozmiar = rozmiar;
    for (size_t i = 0; i < sizeof w; i++)
        wspolna[i] = ((const uint8_t *)&w)[i];
    return 0;
}

/* POSR_BUFOR: the buffer as a dma-buf for the VMM (read-write, never executable: the VMM maps it so). */
static int bufor(uint32_t res)
{
    struct k1_posr_obiekt *o = k1_posr_ob(&obiekty, res);
    struct drm_prime_handle p = { .fd = -1 };
    if (o == NULL)
        return K1_POSR_BLAD_ZASOB;
    p.handle = o->uchwyt, p.flags = DRM_CLOEXEC | DRM_RDWR;
    if (silnik->ioctl(DRM_IOCTL_PRIME_HANDLE_TO_FD, &p) != 0 || p.fd < 0)
        return K1_POSR_BLAD_KARTA;
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_DMABUF, res, o->mapa, k1_kom_niskie(o->rozmiar), k1_kom_wysokie(o->rozmiar), 0,
                             0),
           p.fd);
    close(p.fd);
    return 0;
}

static int zwolnij(uint32_t res)
{
    struct drm_gem_close z = { .handle = k1_posr_ob_usun(&obiekty, res) };
    if (z.handle == 0)
        return K1_POSR_BLAD_ZASOB;
    silnik->ioctl(DRM_IOCTL_GEM_CLOSE, &z);
    return 0;
}

static void obsluz(const struct k1_komunikat *k, int fd)
{
    int r;
    int32_t zrobione = 0;
    switch (k->typ) {
    case K1_KOM_POSR_OKNO:
        r = okno != NULL || (okno = mapuj(fd, K1_POSR_OKNO)) == NULL ? K1_POSR_BLAD_OKNO : 0;
        break;
    case K1_KOM_POSR_WSPOLNA:
        r = przyjmij_wspolna(fd, (uint32_t)k->a);
        break;
    case K1_KOM_POSR_PROSBY:
        r = okno == NULL ? K1_POSR_BLAD_OKNO : posr_prosby(okno, (uint32_t)k->a, &zrobione);
        break;
    case K1_KOM_POSR_BLOB:
        r = k1_posr_ob_przypnij(&obiekty, (uint32_t)k->a, k->obiekt, k1_kom_u64(k->b, k->c));
        break;
    case K1_KOM_POSR_BUFOR:
        r = bufor(k->obiekt);
        if (r == 0)
            return; /* POSR_DMABUF is the answer */
        break;
    case K1_KOM_POSR_ZWOLNIJ:
        r = zwolnij(k->obiekt);
        break;
    default: /* POSR_PLOT (the checks let no other type of K1_KIER_DO_POSREDNIKA through): answered when done */
        r = plot_czekaj(k->obiekt, k1_kom_u64(k->a, k->b));
        if (r == 0)
            return;
        k1_log(TAG, "za dużo płotów naraz — koniec");
        exit(1);
    }
    if (r != 0 && odrzucone++ % 100 == 0) /* numbers only, at most a line per 100 */
        k1_log(TAG, "prośba %d (zasób %u) odrzucona: %d po %d (odrzuconych: %lu)", k->typ, k->obiekt, r, (int)zrobione,
               odrzucone);
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_WYNIK, k->obiekt, k->typ, r, zrobione, 0, 0), -1);
}

static void petla(void)
{
    struct pollfd p[1 + K1_POSR_PLOTY_MAKS];
    while (!zawieszona) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w;
        int fd = -1, n;
        p[0] = (struct pollfd){ FD_KLIENT, POLLIN, 0 };
        n = plot_deskryptory(p + 1, K1_POSR_PLOTY_MAKS);
        if (poll(p, (nfds_t)(1 + n), -1) < 0) {
            if (errno != EINTR)
                k1_koniec(TAG, "poll: %s", strerror(errno));
            continue;
        }
        plot_gotowe(p + 1, n);
        if (!(p[0].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;
        w = k1_odbierz_komunikat(FD_KLIENT, klient, K1_KIER_DO_POSREDNIKA, &k, &fd);
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "klient zamknął kanał — koniec");
            return;
        }
        if (w == K1_ODB_JEST)
            obsluz(&k, fd);
        else if (w != K1_ODB_PUSTO && odrzucone++ % 100 == 0)
            k1_log(TAG, "odrzucono komunikat od klienta (odrzuconych: %lu)", odrzucone);
        if (fd >= 0)
            close(fd); /* the window and the shared memory stay mapped */
    }
}

int main(int argc, char **argv)
{
    int strefa, test, stan, powod = 0, pci = 0, rewizja = 0, wersja = 0;
    k1_klatka_w_usludze(TAG);
    strefa = argc > 1 ? atoi(argv[1]) : -1;
    test = argc > 2 && strcmp(argv[2], "t") == 0;
    if (strefa < 0 || strefa > K1_STREFY_MAKS || (strefa == 0 && test))
        k1_koniec(TAG, "złe argumenty");
    klient = strefa == 0 || test ? 0 : (uid_t)k1_strefa(strefa)->uid;
    silnik = test ? &silnik_testowy : &silnik_karty;
    stan = posr_karta(&powod, &pci, &rewizja, &wersja);
    k1_log(TAG, "%s, strefa %d: %s (powód %d, karta 0x%04x rewizja %d, i915 %d.%d.%d)", silnik->nazwa, strefa,
           stan == K1_POSR_KARTA_UMIE ? "szybka grafika możliwa" : stan == K1_POSR_KARTA_NIE ? "karta nie oddziela stref"
                                                                                             : "brak karty",
           powod, (unsigned)pci, rewizja, wersja >> 16, wersja >> 8 & 0xff, wersja & 0xff);
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_UMIE, 0, stan, stan == K1_POSR_KARTA_NIE ? powod : 0,
                             stan == K1_POSR_KARTA_UMIE ? pci : 0, stan == K1_POSR_KARTA_UMIE ? rewizja : 0,
                             stan == K1_POSR_KARTA_UMIE ? wersja : 0),
           -1);
    if (strefa == 0 || stan != K1_POSR_KARTA_UMIE)
        return 0; /* nadzorca's question answered, or a card that cannot keep the zones apart */
    petla();
    if (zawieszona) {
        k1_log(TAG, "strefa %d: kontekst zawiesił kartę — koniec szybkiej grafiki tej strefy", strefa);
        return K1_POSR_KONIEC_ZAWIESZENIE;
    }
    return 0; /* the buffers go back to the card with our end (the kernel closes the render node) */
}

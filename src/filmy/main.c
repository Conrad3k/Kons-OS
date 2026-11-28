/* K1 filmy — the film decoder (0.10.9 step 2, ADR 0049 pkt 2, 3; plan 0.10.9 T2, T3, T6, T8).
 *
 * Decodes compressed films with the graphics card (VA-API: libva + i965 + libdrm, statically — scripts/zbuduj-va.sh)
 * for one client: the VMM of its zone (one decoder per zone, started and ended with it — nadzorca filmy.c), nadzorca's
 * question at boot ("what does the card decode?" — the answer, then the end) or the test program.
 * Arguments: the zone (0: nadzorca's question), "t": the test decoder (grey frames, no card; test image only, on request
 * of the test program), "s" (0.13.2 step 3): the same for the test zone's VMM (its client: the zone's UID). fd 3 = the channel to the client (its UID checked: the zone's VMM, or UID 0 for nadzorca and the
 * test program), fd 4 = the render node of the card (i915; nadzorca gives it only to this service) or /dev/null.
 * First message: FILMY_UMIE — what the card decodes. Then the requests (k1_filmy.h): the window (once), a film open,
 * a picture decoded into a surface, a picture back into the window (0.13.2 step 3: or into a buffer of the card of the
 * zone — its dma-buf from the zone's graphics broker), a film closed. Each request out of the window is
 * copied before any check, then checked field by field (k1_filmy_sprawdz) — the card gets only what passed.
 * Trust boundary: the client is hostile (the zone); the card's driver in the kernel and i965 are within its reach only
 * through the checked requests. Sandbox: UID 109 (the question) or 400 + zone, no paths, seccomp: base + getuid/geteuid
 * (libva) and DRM ioctls only; memory: K1_FILMY_PAMIEC for the card's buffers + the code and the window (profile.c).
 * The log: numbers only (sizes, codecs, results) — never a byte of a film. Documentation: docs/komponenty/filmy.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <linux/dma-buf.h>
#include <unistd.h>

#include "filmy.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"

static const struct silnik *silnik;
static uid_t klient;
static struct k1_filmy_film filmy[K1_FILMY_FILMOW_MAKS + 1];
static uint8_t *okno;           /* the shared window (NULL: not yet) */
static uint8_t *prosba;         /* our copy of the last request */
static int32_t umie_bity;
static uint32_t umie_w, umie_h;
static unsigned long odrzucone;

static void wyslij(struct k1_komunikat k)
{
    if (k1_kanal_wyslij(FD_KLIENT, &k, sizeof k, -1) != 0)
        k1_log(TAG, "wysłanie do klienta nieudane: %s", strerror(errno));
}

static void wynik(uint32_t film, int typ, int kod)
{
    wyslij(k1_komunikat_nowy(K1_KOM_FILMY_WYNIK, film, typ, kod, 0, 0, 0));
}

static uint64_t pamiec_filmow(void)
{
    uint64_t s = 0;
    for (int i = 1; i <= K1_FILMY_FILMOW_MAKS; i++)
        if (filmy[i].kodek != 0)
            s += k1_filmy_film_bajty(filmy[i].w, filmy[i].h, filmy[i].klatki);
    return s;
}

/* FILMY_OKNO: exactly K1_FILMY_OKNO bytes that cannot shrink or grow (a shrunk window would end us with SIGBUS). */
static int przyjmij_okno(int fd)
{
    struct stat st;
    int pieczecie = fcntl(fd, F_GET_SEALS);
    void *m;
    if (okno != NULL || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (uint64_t)st.st_size != K1_FILMY_OKNO ||
        pieczecie < 0 || (pieczecie & (F_SEAL_SHRINK | F_SEAL_GROW)) != (F_SEAL_SHRINK | F_SEAL_GROW))
        return K1_FILMY_BLAD_OKNO;
    m = mmap(NULL, K1_FILMY_OKNO, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED)
        return K1_FILMY_BLAD_OKNO;
    okno = m;
    return 0;
}

static int otworz(const struct k1_komunikat *k)
{
    struct k1_filmy_film *f = &filmy[k->obiekt];
    struct k1_filmy_film nowy = { k->a, (uint32_t)k->b, (uint32_t)k->c, (uint32_t)k->d, 0 };
    int r = k1_filmy_otworz_dobre(k->a, nowy.w, nowy.h, nowy.klatki);
    if (f->kodek != 0)
        return K1_FILMY_BLAD_FILM;
    if (r != 0)
        return r;
    if (!((uint32_t)umie_bity & (1u << (k->a - 1))))
        return K1_FILMY_BLAD_KODEK;
    if (nowy.w > umie_w || nowy.h > umie_h)
        return K1_FILMY_BLAD_ROZMIAR;
    if (pamiec_filmow() + k1_filmy_film_bajty(nowy.w, nowy.h, nowy.klatki) > silnik->pamiec)
        return K1_FILMY_BLAD_PAMIEC; /* T8: refused before anything is given — the film plays on the processor */
    r = silnik->otworz((int)k->obiekt, &nowy);
    if (r == 0) {
        *f = nowy;
        k1_log(TAG, "film %u: kodek %d, %u × %u, %u klatek", k->obiekt, k->a, nowy.w, nowy.h, nowy.klatki);
    }
    return r;
}

static int klatka(const struct k1_komunikat *k)
{
    struct k1_filmy_film *f = &filmy[k->obiekt];
    struct k1_filmy_czesci c;
    int r;
    if (f->kodek == 0)
        return K1_FILMY_BLAD_FILM;
    if (okno == NULL)
        return K1_FILMY_BLAD_OKNO;
    memcpy(prosba, okno, (size_t)k->b); /* first a copy: the client may change the window while we look */
    r = k1_filmy_sprawdz(prosba, (size_t)k->b, f, (uint32_t)k->a);
    if (r != 0)
        return r;
    k1_filmy_czesci(prosba, &c);
    r = silnik->klatka((int)k->obiekt, f, (uint32_t)k->a, prosba, &c);
    if (r == 0)
        f->zdekodowane |= 1u << k->a;
    return r;
}

static int pobierz(const struct k1_komunikat *k)
{
    struct k1_filmy_film *f = &filmy[k->obiekt];
    int r;
    if (f->kodek == 0)
        return K1_FILMY_BLAD_FILM;
    if (okno == NULL)
        return K1_FILMY_BLAD_OKNO;
    if ((uint32_t)k->a >= f->klatki || !(f->zdekodowane & (1u << k->a)))
        return K1_FILMY_BLAD_ODNIES;
    r = silnik->pobierz((int)k->obiekt, f, (uint32_t)k->a, okno + K1_FILMY_PROSBA_MAKS,
                        okno + K1_FILMY_PROSBA_MAKS + (size_t)k1_filmy_rzad(f->w) * f->h);
    if (r == 0)
        wyslij(k1_komunikat_nowy(K1_KOM_FILMY_OBRAZ, k->obiekt, k->a, (int32_t)f->w, (int32_t)f->h,
                                 (int32_t)k1_filmy_rzad(f->w), 0));
    return r;
}

/* 0.13.2 step 3 (ADR 0063 pkt 3): FILMY_OKNO with a film — the picture of a surface into a buffer of the card of the
 * zone (its dma-buf, from the zone's graphics broker through the VMM; the test engine's: a memfd). Its size from lseek
 * (a dma-buf has no st_size): at least the frame; mapped for writing only as long as the frame takes, never executable;
 * the card's caches around the processor's writes (DMA_BUF_IOCTL_SYNC — a memfd has none: ENOTTY, nothing to do). */
static int do_bufora(const struct k1_komunikat *k, int fd)
{
    struct k1_filmy_film *f = &filmy[k->obiekt];
    struct dma_buf_sync s = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
    uint64_t dl = k1_filmy_bufor_bajty(f->w, f->h);
    off_t rozmiar;
    uint8_t *m;
    int r;
    if (f->kodek == 0)
        return K1_FILMY_BLAD_FILM;
    if ((uint32_t)k->a >= f->klatki || !(f->zdekodowane & (1u << k->a)))
        return K1_FILMY_BLAD_ODNIES;
    rozmiar = fd >= 0 ? lseek(fd, 0, SEEK_END) : -1;
    if (rozmiar < 0 || (uint64_t)rozmiar < dl)
        return K1_FILMY_BLAD_OKNO;
    m = mmap(NULL, (size_t)dl, PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED)
        return K1_FILMY_BLAD_OKNO;
    (void)ioctl(fd, (int)DMA_BUF_IOCTL_SYNC, &s);
    r = silnik->pobierz((int)k->obiekt, f, (uint32_t)k->a, m, m + k1_filmy_bufor_uv(f->w, f->h));
    s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
    (void)ioctl(fd, (int)DMA_BUF_IOCTL_SYNC, &s);
    munmap(m, (size_t)dl);
    return r;
}

static int zamknij(const struct k1_komunikat *k)
{
    struct k1_filmy_film *f = &filmy[k->obiekt];
    if (f->kodek == 0)
        return K1_FILMY_BLAD_FILM;
    silnik->zamknij((int)k->obiekt);
    memset(f, 0, sizeof *f);
    return 0;
}

static void obsluz(const struct k1_komunikat *k, int fd)
{
    int r;
    switch (k->typ) {
    case K1_KOM_FILMY_OKNO:
        r = k->obiekt == 0 ? przyjmij_okno(fd) : do_bufora(k, fd);
        break;
    case K1_KOM_FILMY_OTWORZ:
        r = otworz(k);
        break;
    case K1_KOM_FILMY_KLATKA:
        r = klatka(k);
        break;
    case K1_KOM_FILMY_POBIERZ:
        r = pobierz(k);
        if (r == 0)
            return; /* FILMY_OBRAZ is the answer */
        break;
    default: /* FILMY_ZAMKNIJ (the checks let no other type of K1_KIER_DO_FILMOW through) */
        r = zamknij(k);
        break;
    }
    if (r != 0 && odrzucone++ % 100 == 0) /* numbers only, at most a line per 100 */
        k1_log(TAG, "prośba %d filmu %u odrzucona: %d (odrzuconych: %lu)", k->typ, k->obiekt, r, odrzucone);
    wynik(k->obiekt, k->typ, r);
}

static void petla(void)
{
    for (;;) {
        struct pollfd p = { FD_KLIENT, POLLIN, 0 };
        struct k1_komunikat k;
        enum k1_odbior_wynik w;
        int fd = -1;
        if (poll(&p, 1, -1) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        w = k1_odbierz_komunikat(FD_KLIENT, klient, K1_KIER_DO_FILMOW, &k, &fd);
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "klient zamknął kanał — koniec");
            return;
        }
        if (w == K1_ODB_JEST)
            obsluz(&k, fd);
        else if (w != K1_ODB_PUSTO && odrzucone++ % 100 == 0)
            k1_log(TAG, "odrzucono komunikat od klienta (odrzuconych: %lu)", odrzucone);
        if (fd >= 0)
            close(fd); /* the window stays mapped */
    }
}

int main(int argc, char **argv)
{
    int strefa, test, stan, kod = 0;
    k1_klatka_w_usludze(TAG);
    strefa = argc > 1 ? atoi(argv[1]) : -1;
    test = argc > 2 && (strcmp(argv[2], "t") == 0 || strcmp(argv[2], "s") == 0); /* 0.13.2: "s" — for the test zone's VMM */
    if (strefa < 0 || strefa > K1_STREFY_MAKS || (strefa == 0 && test))
        k1_koniec(TAG, "złe argumenty");
    klient = strefa == 0 || (argc > 2 && strcmp(argv[2], "t") == 0) ? 0 : (uid_t)k1_strefa(strefa)->uid;
    silnik = test ? &silnik_testowy : &silnik_karty;
    stan = silnik->start(&umie_bity, &umie_w, &umie_h, &kod);
    if (stan != K1_FILMY_KARTA_UMIE)
        umie_bity = 0, umie_w = umie_h = 0;
    k1_log(TAG, "%s, strefa %d: %s (kodeki 0x%x, do %u × %u, kod %d)", silnik->nazwa, strefa,
           stan == K1_FILMY_KARTA_UMIE ? "karta rozpakowuje" : stan == K1_FILMY_KARTA_NIC ? "karta nic" : "brak karty",
           (unsigned)umie_bity, umie_w, umie_h, kod);
    wyslij(k1_komunikat_nowy(K1_KOM_FILMY_UMIE, 0, umie_bity, stan, stan == K1_FILMY_KARTA_NIC ? kod & 0xff : 0,
                             (int32_t)umie_w, (int32_t)umie_h));
    if (strefa == 0 || stan != K1_FILMY_KARTA_UMIE)
        return 0; /* nadzorca's question answered, or nothing to decode with */
    prosba = malloc(K1_FILMY_PROSBA_MAKS);
    if (prosba == NULL)
        k1_koniec(TAG, "brak pamięci na prośbę");
    petla();
    for (int i = 1; i <= K1_FILMY_FILMOW_MAKS; i++)
        if (filmy[i].kodek != 0)
            silnik->zamknij(i);
    return 0;
}

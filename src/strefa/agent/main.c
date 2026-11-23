/* K1 agent — the files of a browser zone (0.11.0 step 2, ADR 0052 T2): runs INSIDE the micro machine (started by gosc,
 * as the browser's user), not in K1. It watches the browser's folder of downloads (argv[1]; the locked settings of
 * strefy/przegladarka/k1.js keep Firefox writing there, without asking where) — since 0.11.4 also the browser's home
 * and the user's folders in it, where "Zapisz obraz jako…" may save (foldery.c) — and, for a file that is complete, tells
 * the VMM on vsock port K1_PS_PORT (frames: src/wspolne/k1_pliki_strefy.h): ZGLOS — its name and size. K1 asks the user;
 * after "Zapisz w Pobranych" the VMM says WYSLIJ and the agent sends the bytes (DANE), the file opened again in its
 * folder (openat, O_NOFOLLOW, no ".."), the same file (inode) of the same size — else BRAK. WYNIK ends it; the file stays
 * in the browser's folder either way. Firefox ESR writes a download to "<name>.part" and renames it at the end (its
 * DownloadCore.sys.mjs: IOUtils.move): IN_MOVED_TO is the end; IN_CLOSE_WRITE of a file that is not a ".part" and not
 * empty also counts ("Zapisz stronę"; 0.11.4: after a quiet while — foldery.c). Files that were there before it started
 * are not told (a session that remembers keeps its downloads); a file is told once until it changes (`znane`); a file
 * K1 refused (ODMOWA: too many wait for the user) is told again when another one ends, or after PONOW_MS (0.11.4 — it
 * was forgotten). K1 trusts nothing it says. Our code, static with musl and the flags of SPEC §11.
 * 0.11.0 step 3: files from K1 into the session's folder "Z K1" (argv[2]; odbior.c), "Otwórz w…" through the compositor's
 * FIFO (argv[3]); a changed version of such a file — a download of the same name (Firefox: "name(1).ext") or the file in
 * "Z K1" written again — is told with ZGLOS dotyczy = K1's number of it ("Zapisać zmiany w oryginale?" in K1). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "agent.h"

#define PLIKOW 16
#define ZNANYCH 32
#define PONOW_MS 5000

static struct plik {
    int stan; /* 0: free, 1: to tell, 2: told, 3: refused — told again at ponow_ms */
    int kat;  /* its folder: our own copy of the descriptor (foldery.c closes its own when the folder goes) */
    uint32_t nr, dotyczy;
    uint64_t rozmiar, ponow_ms;
    ino_t ino;
    char nazwa[K1_PS_NAZWA_MAKS + 1];
} pl[PLIKOW];
static struct znany { /* the files told lately: the same file, unchanged, is not told again */
    ino_t ino;
    uint64_t rozmiar;
    int64_t mtime;
} znane[ZNANYCH];
static unsigned n_znanych;
static uint32_t nastepny;
int vs = -1;
static struct k1_ps_czytnik cz;
static uint8_t b[K1_PS_RAMKA_MAKS], we[4096];

uint64_t teraz_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

void loguj(const char *fmt, ...)
{
    char t[160];
    va_list a;
    va_start(a, fmt);
    vsnprintf(t, sizeof t, fmt, a);
    va_end(a);
    dprintf(2, TAG ": %s\n", t);
}

static int pisz(const uint8_t *p, size_t n)
{
    while (n > 0) {
        ssize_t w = write(vs, p, n);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        p += w, n -= (size_t)w;
    }
    return 0;
}

int ramka(const struct k1_ps_ramka *r)
{
    size_t n = k1_ps_zapisz(r, 1, b, sizeof b);
    return n > 0 ? pisz(b, n) : -1;
}

void rozlacz(void)
{
    if (vs >= 0)
        close(vs);
    vs = -1;
    odbior_rozlacz();
    for (int i = 0; i < PLIKOW; i++)
        if (pl[i].stan == 2)
            pl[i].stan = 1; /* the VMM forgot it: tell it again */
}

static void polacz(void)
{
    struct sockaddr_vm a = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = K1_PS_PORT };
    vs = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (vs >= 0 && connect(vs, (struct sockaddr *)&a, sizeof a) != 0) {
        close(vs);
        vs = -1;
    }
    memset(&cz, 0, sizeof cz);
}

static void zwolnij(struct plik *p)
{
    close(p->kat);
    p->stan = 0;
}

void plik_gotowy(int kat, const char *nazwa, int przeniesiony, uint32_t dotyczy)
{
    struct stat st;
    size_t dl = strlen(nazwa);
    int i, wolny = -1;
    int64_t mt;
    if (dl == 0 || dl > K1_PS_NAZWA_MAKS || fstatat(kat, nazwa, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode) ||
        (!przeniesiony && st.st_size == 0))
        return;
    mt = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
    for (i = 0; i < ZNANYCH; i++)
        if (znane[i].ino == st.st_ino && znane[i].rozmiar == (uint64_t)st.st_size && znane[i].mtime == mt)
            return; /* told already and unchanged (the same file closed twice, lost events looked at again) */
    for (i = 0; i < PLIKOW; i++) {
        if (pl[i].stan != 0 && pl[i].stan != 2 && pl[i].ino == st.st_ino && strcmp(pl[i].nazwa, nazwa) == 0) {
            pl[i].rozmiar = (uint64_t)st.st_size; /* not told yet: told at its new size */
            znane[n_znanych++ % ZNANYCH] = (struct znany){ st.st_ino, (uint64_t)st.st_size, mt };
            return;
        }
        if (pl[i].stan == 0 && wolny < 0)
            wolny = i;
    }
    if (wolny < 0 || (kat = fcntl(kat, F_DUPFD_CLOEXEC, 3)) < 0) {
        loguj("za dużo plików czeka — pomijam jeden");
        return;
    }
    znane[n_znanych++ % ZNANYCH] = (struct znany){ st.st_ino, (uint64_t)st.st_size, mt };
    loguj("plik do zgłoszenia: %llu B%s", (unsigned long long)st.st_size, dotyczy != 0 ? " (zmiana pliku od K1)" : "");
    pl[wolny] = (struct plik){ 1, kat, 0, dotyczy, (uint64_t)st.st_size, 0, st.st_ino, { 0 } };
    memcpy(pl[wolny].nazwa, nazwa, dl);
}

/* The files to tell now (new ones, refused ones whose time came): ZGLOS, each time with a new number. */
static void zglos(void)
{
    uint64_t t = teraz_ms();
    for (int i = 0; vs >= 0 && i < PLIKOW; i++)
        if (pl[i].stan == 1 || (pl[i].stan == 3 && pl[i].ponow_ms <= t)) {
            struct k1_ps_ramka r = { .typ = K1_PS_ZGLOS, .rozmiar = pl[i].rozmiar, .dodatek = pl[i].dotyczy,
                                     .nl = (uint8_t)strlen(pl[i].nazwa) };
            nastepny = nastepny >= K1_PS_NR_MAKS ? 1 : nastepny + 1;
            pl[i].nr = r.nr = nastepny;
            memcpy(r.nazwa, pl[i].nazwa, r.nl);
            if (ramka(&r) != 0)
                rozlacz();
            else
                pl[i].stan = 2;
        }
}

/* ms until a refused file is told again; -1: none is refused */
static int ponow_czekaj(void)
{
    uint64_t t = teraz_ms(), min = UINT64_MAX;
    for (int i = 0; i < PLIKOW; i++)
        if (pl[i].stan == 3 && pl[i].ponow_ms < min)
            min = pl[i].ponow_ms;
    return min == UINT64_MAX ? -1 : min <= t ? 0 : (int)(min - t);
}

/* WYNIK of told file p: refused — told again later; else its end, and room in K1 for the refused ones at once. */
static void wynik(struct plik *p, uint32_t w)
{
    loguj("plik %u: wynik %u", p->nr, w);
    if (w == K1_PS_W_ODMOWA) {
        p->stan = 3;
        p->ponow_ms = teraz_ms() + PONOW_MS;
        return;
    }
    zwolnij(p);
    for (int i = 0; i < PLIKOW; i++)
        if (pl[i].stan == 3)
            pl[i].ponow_ms = 0;
}

/* WYSLIJ: the bytes of file p, frame after frame; BRAK when it is not the same file any more. */
static void wyslij(struct plik *p)
{
    static uint8_t dane[K1_PS_DANE_MAKS];
    struct k1_ps_ramka r = { .typ = K1_PS_DANE, .nr = p->nr, .dane = dane };
    struct stat st;
    uint64_t wyslane = 0;
    int fd = openat(p->kat, p->nazwa, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 || fstat(fd, &st) != 0 || st.st_ino != p->ino || (uint64_t)st.st_size != p->rozmiar) {
        if (fd >= 0)
            close(fd);
        r = (struct k1_ps_ramka){ .typ = K1_PS_BRAK, .nr = p->nr };
        if (ramka(&r) != 0)
            rozlacz();
        return;
    }
    while (wyslane < p->rozmiar) {
        uint64_t zostalo = p->rozmiar - wyslane;
        ssize_t n = pread(fd, dane, zostalo < sizeof dane ? (size_t)zostalo : sizeof dane, (off_t)wyslane);
        if (n <= 0) { /* shorter than it was: the browser changed it */
            r = (struct k1_ps_ramka){ .typ = K1_PS_BRAK, .nr = p->nr };
            break;
        }
        r.dl = (uint32_t)n;
        if (ramka(&r) != 0) {
            close(fd);
            rozlacz();
            return;
        }
        wyslane += (uint64_t)n;
    }
    close(fd);
    if (r.typ == K1_PS_BRAK && ramka(&r) != 0)
        rozlacz();
}

static void od_vmm(void)
{
    ssize_t n = read(vs, we, sizeof we);
    size_t od = 0, u;
    struct k1_ps_ramka r;
    if (n <= 0) {
        rozlacz();
        return;
    }
    while (od < (size_t)n) {
        int w = k1_ps_czytaj(&cz, we + od, (size_t)n - od, &u, &r);
        od += u;
        if (w < 0) {
            loguj("zła ramka od VMM");
            rozlacz();
            return;
        }
        /* 0.11.0 steps 3, 4: a file from K1, a drop on the window (odbior.c) */
        if (w > 0 && (r.typ == K1_PS_WKLEJ || r.typ == K1_PS_WKLEJ_TEKST)) { /* step 5: K1's text for a Ctrl+V */
            schowek_ramka(&r);
            continue;
        }
        if (w > 0 && (r.typ == K1_PS_PLIK || r.typ == K1_PS_BAJTY || r.typ == K1_PS_UPUSC)) {
            odbior_ramka(&r);
            if (vs < 0)
                return;
            continue;
        }
        for (int i = 0; w > 0 && i < PLIKOW; i++)
            if (pl[i].stan == 2 && pl[i].nr == r.nr) {
                if (r.typ == K1_PS_WYSLIJ) {
                    wyslij(&pl[i]);
                    if (vs < 0)
                        return;
                } else {
                    wynik(&pl[i], r.wynik);
                }
            }
    }
}

static int krocej(int x, int y)
{
    return x < 0 ? y : y < 0 || x < y ? x : y;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    if (argc < 4 || odbior_start(argv[2], argv[3]) != 0 || foldery_start(argv[1], argv[2]) != 0) {
        loguj("foldery: %s", strerror(errno));
        return 1;
    }
    if (argc > 4)
        schowek_start(argv[4]); /* 0.11.0 step 5: the compositor's FIFO to us */
    loguj("gotowy");
    for (;;) {
        struct pollfd p[3] = { { foldery_fd(), POLLIN, 0 }, { -1, POLLIN, 0 }, { schowek_fd(), POLLIN, 0 } };
        int czas;
        if (vs < 0)
            polacz();
        zglos();
        p[1].fd = vs;
        czas = krocej(foldery_czekaj(), ponow_czekaj());
        if (vs < 0 || p[2].fd < 0)
            czas = krocej(czas, 2000);
        if (poll(p, 3, czas) < 0 && errno != EINTR)
            return 1;
        if (p[0].revents & POLLIN)
            foldery_czytaj();
        foldery_czas();
        if (vs >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR)))
            od_vmm();
        if (p[2].fd >= 0 && (p[2].revents & POLLIN))
            schowek_czytaj();
    }
}

/* K1 agent — the files of a browser zone (0.11.0 step 2, ADR 0052 T2): runs INSIDE the micro machine (started by gosc,
 * as the browser's user), not in K1. It watches the browser's folder of downloads (argv[1]; the locked settings of
 * strefy/przegladarka/k1.js keep Firefox writing there, without asking where) and, for a file that is complete, tells
 * the VMM on vsock port K1_PS_PORT (frames: src/wspolne/k1_pliki_strefy.h): ZGLOS — its name and size. K1 asks the user;
 * after "Zapisz w Pobranych" the VMM says WYSLIJ and the agent sends the bytes (DANE), the file opened again in its
 * folder (openat, O_NOFOLLOW, no ".."), the same file (inode) of the same size — else BRAK. WYNIK ends it; the file stays
 * in the browser's folder either way. Firefox ESR writes a download to "<name>.part" and renames it at the end (its
 * DownloadCore.sys.mjs: IOUtils.move): IN_MOVED_TO is the end; IN_CLOSE_WRITE of a file that is not a ".part" and not
 * empty also counts ("Zapisz stronę"). Files that were there before it started are not told (a session that remembers
 * keeps its downloads). K1 trusts nothing it says. Our code, static with musl and the flags of SPEC §11. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "k1_pliki_strefy.h"

#define TAG "K1-AGENT"
#define PLIKOW 16

static struct plik {
    int stan; /* 0: free, 1: to tell, 2: told */
    uint32_t nr;
    uint64_t rozmiar;
    ino_t ino;
    char nazwa[K1_PS_NAZWA_MAKS + 1];
} pl[PLIKOW];
static uint32_t nastepny;
static int katalog = -1, vs = -1;
static struct k1_ps_czytnik cz;
static uint8_t b[K1_PS_RAMKA_MAKS], we[4096];

static void loguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void loguj(const char *fmt, ...)
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

static int ramka(const struct k1_ps_ramka *r)
{
    size_t n = k1_ps_zapisz(r, 1, b, sizeof b);
    return n > 0 ? pisz(b, n) : -1;
}

static void rozlacz(void)
{
    if (vs >= 0)
        close(vs);
    vs = -1;
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

/* A name in the folder as inotify gave it: a file to tell K1 of (not a ".part", a regular file, not one told yet). */
static void nowy(const char *nazwa, int przeniesiony)
{
    struct stat st;
    size_t dl = strlen(nazwa);
    int i, wolny = -1;
    if (dl == 0 || dl > K1_PS_NAZWA_MAKS || (dl > 5 && strcmp(nazwa + dl - 5, ".part") == 0) ||
        fstatat(katalog, nazwa, &st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(st.st_mode) ||
        (!przeniesiony && st.st_size == 0))
        return;
    for (i = 0; i < PLIKOW; i++) {
        if (pl[i].stan != 0 && pl[i].ino == st.st_ino && strcmp(pl[i].nazwa, nazwa) == 0)
            return;
        if (pl[i].stan == 0 && wolny < 0)
            wolny = i;
    }
    if (wolny < 0) {
        loguj("za dużo plików czeka — pomijam jeden");
        return;
    }
    pl[wolny] = (struct plik){ 1, nastepny = nastepny >= K1_PS_NR_MAKS ? 1 : nastepny + 1, (uint64_t)st.st_size,
                               st.st_ino, { 0 } };
    memcpy(pl[wolny].nazwa, nazwa, dl);
}

static void zdarzenia(int in)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n = read(in, buf, sizeof buf);
    for (ssize_t o = 0; n > 0 && o + (ssize_t)sizeof(struct inotify_event) <= n;) {
        const struct inotify_event *e = (const struct inotify_event *)(buf + o);
        if (e->len > 0 && (e->mask & (IN_MOVED_TO | IN_CLOSE_WRITE)))
            nowy(e->name, (e->mask & IN_MOVED_TO) != 0);
        o += (ssize_t)sizeof *e + e->len;
    }
}

/* WYSLIJ: the bytes of file p, frame after frame; BRAK when it is not the same file any more. */
static void wyslij(struct plik *p)
{
    static uint8_t dane[K1_PS_DANE_MAKS];
    struct k1_ps_ramka r = { .typ = K1_PS_DANE, .nr = p->nr, .dane = dane };
    struct stat st;
    uint64_t wyslane = 0;
    int fd = openat(katalog, p->nazwa, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
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
        for (int i = 0; w > 0 && i < PLIKOW; i++)
            if (pl[i].stan == 2 && pl[i].nr == r.nr) {
                if (r.typ == K1_PS_WYSLIJ) {
                    wyslij(&pl[i]);
                    if (vs < 0)
                        return;
                } else {
                    loguj("plik %u: wynik %u", r.nr, r.wynik);
                    pl[i].stan = 0;
                }
            }
    }
}

int main(int argc, char **argv)
{
    int in;
    signal(SIGPIPE, SIG_IGN);
    if (argc < 2 || (katalog = open(argv[1], O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0 ||
        (in = inotify_init1(IN_CLOEXEC | IN_NONBLOCK)) < 0 || inotify_add_watch(in, argv[1], IN_MOVED_TO | IN_CLOSE_WRITE) < 0) {
        loguj("folder pobranych: %s", strerror(errno));
        return 1;
    }
    loguj("gotowy");
    for (;;) {
        struct pollfd p[2] = { { in, POLLIN, 0 }, { vs, POLLIN, 0 } };
        if (vs < 0)
            polacz();
        for (int i = 0; vs >= 0 && i < PLIKOW; i++)
            if (pl[i].stan == 1) {
                struct k1_ps_ramka r = { .typ = K1_PS_ZGLOS, .nr = pl[i].nr, .rozmiar = pl[i].rozmiar,
                                         .nl = (uint8_t)strlen(pl[i].nazwa) };
                memcpy(r.nazwa, pl[i].nazwa, r.nl);
                if (ramka(&r) != 0)
                    rozlacz();
                else
                    pl[i].stan = 2;
            }
        p[1].fd = vs;
        if (poll(p, 2, vs < 0 ? 2000 : -1) < 0 && errno != EINTR)
            return 1;
        if (p[0].revents & POLLIN)
            zdarzenia(in);
        if (vs >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR)))
            od_vmm();
    }
}

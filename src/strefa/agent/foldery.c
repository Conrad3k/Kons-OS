/* K1 agent — the folders it watches (0.11.4; main.c). Until 0.11.3 only the folder of downloads was watched, and only
 * for IN_MOVED_TO / IN_CLOSE_WRITE told at once: a picture saved with "Zapisz obraz jako…" into another folder (the
 * window of GTK lets the user go anywhere, and Firefox opens it where the last save went) gave no window in K1. Now:
 *   - the folder of downloads, the browser's home (its parent) and their folders the user sees (not hidden, not the
 *     profile, not "Z K1"), GLEBOKOSC levels down; folders made later (also in the save window) are watched at once;
 *   - "Z K1": only a file K1 gave, written again (odbior_dotyczy);
 *   - a file renamed into a folder (Firefox's ".part" at the end of a download) is ready at once; a file written in
 *     place (IN_CLOSE_WRITE) is ready only after CISZA_MS without another write — a program that writes a file in
 *     several opens is told once, at its whole size;
 *   - IN_Q_OVERFLOW (events lost): the files of every folder changed in the last NIEDAWNO_S seconds are looked at
 *     again (main.c does not tell the same file twice).
 * Files that were there before the agent started are not told (a session that remembers keeps its downloads). Pure
 * of the port: plik_gotowy (main.c) takes a ready file. Host test: testy/host/test_agent_foldery.c. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "agent.h"

#define KATALOGI 32
#define GLEBOKOSC 3
#define CZEKAJACE 16
#define CISZA_MS 700
#define NIEDAWNO_S 120
#define SCIEZKA 512
#ifndef IN_MASK_CREATE
#define IN_MASK_CREATE 0x10000000 /* linux/inotify.h, since Linux 4.18 */
#endif
#define MASKA_PLIKOW (IN_CLOSE_WRITE | IN_MOVED_TO | IN_MODIFY | IN_CREATE | IN_MOVED_FROM | IN_DELETE)

static struct katalog {
    int wd, fd, gleb, z_k1, dom; /* wd -1: a free place; dom: the browser's home */
    char sciezka[SCIEZKA];
} kat[KATALOGI];
static struct czekajacy {
    int jest, fd, z_k1;
    uint64_t gotowy_ms;
    char nazwa[K1_PS_NAZWA_MAKS + 1];
} cz[CZEKAJACE];
static int in = -1;
static char pomin[2][256]; /* names of the home's folders that are not the user's: the profile, "Z K1" */

static struct katalog *wedlug_wd(int wd)
{
    for (int i = 0; i < KATALOGI; i++)
        if (kat[i].wd == wd && wd >= 0)
            return &kat[i];
    return NULL;
}

/* A file name the agent may tell: not hidden, not a download in progress (".part"), of a length the port takes. */
static int nazwa_pliku(const char *n)
{
    size_t dl = strlen(n);
    return dl > 0 && dl <= K1_PS_NAZWA_MAKS && n[0] != '.' && !(dl > 5 && strcmp(n + dl - 5, ".part") == 0);
}

/* Watches folder `sciezka` (depth gleb) and, below GLEBOKOSC, the folders in it — skipping hidden ones and, in the
 * home (gleb 0, not the downloads), the profile and "Z K1". 0, or -1 when it cannot be watched. */
static int dodaj(const char *sciezka, int gleb, int z_k1, int dom)
{
    int i, wolne = -1, wd, fd;
    DIR *d;
    struct dirent *e;
    for (i = 0; i < KATALOGI; i++)
        if (kat[i].wd < 0 && wolne < 0)
            wolne = i;
    if (wolne < 0 || strlen(sciezka) >= SCIEZKA) {
        loguj("za dużo folderów — jednego nie obserwuję");
        return -1;
    }
    /* IN_MASK_CREATE: a folder watched already (the downloads, again from the home) keeps its mask */
    wd = inotify_add_watch(in, sciezka, (z_k1 ? IN_CLOSE_WRITE : MASKA_PLIKOW) | IN_ONLYDIR | IN_DONT_FOLLOW | IN_MASK_CREATE);
    if (wd < 0)
        return errno == EEXIST ? 0 : -1;
    fd = open(sciezka, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        inotify_rm_watch(in, wd);
        return -1;
    }
    kat[wolne] = (struct katalog){ wd, fd, gleb, z_k1, dom, { 0 } };
    strcpy(kat[wolne].sciezka, sciezka);
    if (z_k1 || gleb >= GLEBOKOSC || (d = fdopendir(dup(fd))) == NULL)
        return 0;
    while ((e = readdir(d)) != NULL) {
        char p[SCIEZKA];
        if (e->d_type != DT_DIR || e->d_name[0] == '.' ||
            (dom && (strcmp(e->d_name, pomin[0]) == 0 || strcmp(e->d_name, pomin[1]) == 0)) ||
            snprintf(p, sizeof p, "%s/%s", sciezka, e->d_name) >= (int)sizeof p)
            continue;
        dodaj(p, gleb + 1, 0, 0);
    }
    closedir(d);
    return 0;
}

int foldery_start(const char *pobrane, const char *z_k1)
{
    char dom[SCIEZKA];
    const char *ukosnik = strrchr(pobrane, '/');
    for (int i = 0; i < KATALOGI; i++)
        kat[i].wd = -1;
    in = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (in < 0 || ukosnik == NULL || (size_t)(ukosnik - pobrane) >= sizeof dom || strrchr(z_k1, '/') == NULL ||
        strlen(strrchr(z_k1, '/')) > sizeof pomin[1])
        return -1;
    memcpy(dom, pobrane, (size_t)(ukosnik - pobrane));
    dom[ukosnik - pobrane] = 0;
    strcpy(pomin[0], AGENT_PROFIL);
    strcpy(pomin[1], strrchr(z_k1, '/') + 1);
    if (dodaj(pobrane, 0, 0, 0) != 0 || dodaj(z_k1, 0, 1, 0) != 0)
        return -1;
    if (dodaj(dom, 0, 0, 1) != 0) /* the downloads are watched anyway */
        loguj("katalog domowy: %s", strerror(errno));
    return 0;
}

int foldery_fd(void)
{
    return in;
}

static struct czekajacy *czekajacy(int fd, const char *nazwa)
{
    for (int i = 0; i < CZEKAJACE; i++)
        if (cz[i].jest && cz[i].fd == fd && strcmp(cz[i].nazwa, nazwa) == 0)
            return &cz[i];
    return NULL;
}

/* A file written in place: ready CISZA_MS after its last write (when there is no room to wait: at once). */
static void czekaj(int fd, const char *nazwa, int z_k1)
{
    struct czekajacy *c = czekajacy(fd, nazwa);
    for (int i = 0; c == NULL && i < CZEKAJACE; i++)
        if (!cz[i].jest)
            c = &cz[i];
    if (c == NULL) {
        plik_gotowy(fd, nazwa, 0, odbior_dotyczy(nazwa, z_k1));
        return;
    }
    c->jest = 1, c->fd = fd, c->z_k1 = z_k1;
    c->gotowy_ms = teraz_ms() + CISZA_MS;
    memcpy(c->nazwa, nazwa, strlen(nazwa) + 1);
}

/* Lost events: every file of the folders changed lately waits again as if just written. */
static void przegladaj(void)
{
    time_t od = time(NULL) - NIEDAWNO_S;
    loguj("utracone zdarzenia folderów — przeglądam je");
    for (int i = 0; i < KATALOGI; i++) {
        DIR *d;
        struct dirent *e;
        struct stat st;
        if (kat[i].wd < 0 || kat[i].z_k1 || (d = fdopendir(dup(kat[i].fd))) == NULL)
            continue;
        while ((e = readdir(d)) != NULL)
            if (nazwa_pliku(e->d_name) && fstatat(kat[i].fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISREG(st.st_mode) && st.st_mtime >= od)
                czekaj(kat[i].fd, e->d_name, 0);
        closedir(d);
    }
}

static void zdarzenie(const struct inotify_event *e)
{
    struct katalog *k = wedlug_wd(e->wd);
    struct czekajacy *c;
    char p[SCIEZKA];
    if (k == NULL)
        return;
    if (e->mask & IN_IGNORED) { /* the folder is gone (main.c holds its own copies of the descriptor) */
        for (int i = 0; i < CZEKAJACE; i++)
            if (cz[i].jest && cz[i].fd == k->fd)
                cz[i].jest = 0;
        close(k->fd);
        k->wd = -1;
        return;
    }
    if (e->len == 0)
        return;
    if (e->mask & IN_ISDIR) { /* a new folder: watched, when the user can see it */
        if (!k->z_k1 && (e->mask & (IN_CREATE | IN_MOVED_TO)) && k->gleb < GLEBOKOSC && e->name[0] != '.' &&
            snprintf(p, sizeof p, "%s/%s", k->sciezka, e->name) < (int)sizeof p &&
            !(k->dom && (strcmp(e->name, pomin[0]) == 0 || strcmp(e->name, pomin[1]) == 0)))
            dodaj(p, k->gleb + 1, 0, 0);
        return;
    }
    if (!nazwa_pliku(e->name))
        return;
    c = czekajacy(k->fd, e->name);
    if (k->z_k1) { /* only a file K1 gave, written again */
        if ((e->mask & IN_CLOSE_WRITE) && odbior_dotyczy(e->name, 1) != 0)
            czekaj(k->fd, e->name, 1);
    } else if (e->mask & IN_MOVED_TO) { /* renamed into place: whole (Firefox's end of a download) */
        if (c != NULL)
            c->jest = 0;
        plik_gotowy(k->fd, e->name, 1, odbior_dotyczy(e->name, 0));
    } else if (e->mask & (IN_MOVED_FROM | IN_DELETE)) {
        if (c != NULL)
            c->jest = 0;
    } else if (e->mask & IN_CLOSE_WRITE) {
        czekaj(k->fd, e->name, 0);
    } else if (c != NULL && (e->mask & IN_MODIFY)) { /* written again before it was ready: wait longer */
        c->gotowy_ms = teraz_ms() + CISZA_MS;
    }
}

void foldery_czytaj(void)
{
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n;
    while ((n = read(in, buf, sizeof buf)) > 0)
        for (ssize_t o = 0; o + (ssize_t)sizeof(struct inotify_event) <= n;) {
            const struct inotify_event *e = (const struct inotify_event *)(buf + o);
            if (e->mask & IN_Q_OVERFLOW)
                przegladaj();
            else
                zdarzenie(e);
            o += (ssize_t)sizeof *e + e->len;
        }
}

int foldery_czekaj(void)
{
    uint64_t t = teraz_ms(), min = UINT64_MAX;
    for (int i = 0; i < CZEKAJACE; i++)
        if (cz[i].jest && cz[i].gotowy_ms < min)
            min = cz[i].gotowy_ms;
    return min == UINT64_MAX ? -1 : min <= t ? 0 : (int)(min - t);
}

void foldery_czas(void)
{
    uint64_t t = teraz_ms();
    for (int i = 0; i < CZEKAJACE; i++)
        if (cz[i].jest && cz[i].gotowy_ms <= t) {
            cz[i].jest = 0;
            plik_gotowy(cz[i].fd, cz[i].nazwa, 0, odbior_dotyczy(cz[i].nazwa, cz[i].z_k1));
        }
}

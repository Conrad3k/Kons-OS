/* K1 kompozytor — the clipboard in the zone (0.11.0 step 5, ADR 0052 T8; the owner's D5: A). The zone keeps no clipboard
 * of its own that a page could read later: K1's ekran keeps it.
 *   - The browser copies (wl_data_device.set_selection with a source that offers text): the compositor asks for the text
 *     (wl_data_source.send into a pipe), reads it (at most K1_SCHOWEK_MAKS, checked) and gives it to the agent on its FIFO
 *     ("kopia <n>\n" and the bytes) — the agent sends it to K1, which takes it only soon after the user's gesture.
 *   - Ctrl+V in the window: K1 gives the text first (the agent: "wklej <nr> <n>\n" and the bytes on otworz's FIFO); the
 *     compositor makes it the selection of the client with the keyboard (a wl_data_offer of ours, wl_data_device.selection),
 *     cancels the browser's own source, and says "wklejone <nr>" — then the VMM lets the key go.
 * Writes never block the compositor: what a pipe does not take now waits in `wy` (POLLOUT in the loop). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_schowek.h"
#include "kompozytor.h"

#define WYJSC 6
#define UZYTKOWNIK 1000

static int fifo = -1;                       /* to the agent: "kopia", "wklejone" */
static struct { int fd, zamknij; char *b; size_t n, od; } wy[WYJSC];
static struct { int fd; uint8_t *b; size_t n; int za_duzo; } od_zrodla = { -1, NULL, 0, 0 };
static struct obiekt *zrodlo;                /* the browser's source of the selection (cancelled when K1's comes) */
static const char *const TEKSTOWE[] = { "text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "TEXT", "STRING" };

int schowek_typ_tekstu(const char *mime)
{
    for (size_t i = 0; i < sizeof TEKSTOWE / sizeof TEKSTOWE[0]; i++)
        if (strcmp(mime, TEKSTOWE[i]) == 0)
            return 1;
    return 0;
}

void schowek_start(const char *gniazdo)
{
    char s[256];
    const char *k = strrchr(gniazdo, '/');
    if (k == NULL || (size_t)(k - gniazdo) + 9 >= sizeof s)
        return;
    snprintf(s, sizeof s, "%.*s/schowek", (int)(k - gniazdo), gniazdo);
    unlink(s);
    if (mkfifo(s, 0640) != 0 || chown(s, 0, UZYTKOWNIK) != 0 || chmod(s, 0640) != 0 ||
        (fifo = open(s, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) { /* O_RDWR: no SIGPIPE, no end while the agent restarts */
        kloguj("schowek: kolejka %s nieudana: %s", s, strerror(errno));
        return;
    }
    if (fcntl(fifo, F_SETPIPE_SZ, (int)(2 * K1_SCHOWEK_MAKS)) < 0)
        kloguj("schowek: kolejka bez dużego bufora (%s)", strerror(errno));
}

/* b (n bytes) into fd now, the rest later (kept); zamknij: fd closes at the end. -1: no room (nothing queued). */
static int pisz(int fd, const void *b, size_t n, int zamknij)
{
    ssize_t w = 0;
    size_t juz;
    int czeka = 0;
    for (int i = 0; i < WYJSC; i++)
        czeka |= wy[i].b != NULL && wy[i].fd == fd; /* what waits goes first: nothing past it */
    if (!czeka)
        w = write(fd, b, n);
    juz = w > 0 ? (size_t)w : 0;
    if (juz == n) {
        if (zamknij)
            close(fd);
        return 0;
    }
    for (int i = 0; i < WYJSC; i++)
        if (wy[i].b == NULL && (wy[i].b = malloc(n - juz)) != NULL) {
            memcpy(wy[i].b, (const char *)b + juz, n - juz);
            wy[i].fd = fd, wy[i].n = n - juz, wy[i].od = 0, wy[i].zamknij = zamknij;
            return 0;
        }
    if (zamknij)
        close(fd);
    return -1;
}

int schowek_fd(unsigned i, short *zdarzenia)
{
    if (i < WYJSC) {
        *zdarzenia = POLLOUT;
        return wy[i].b != NULL ? wy[i].fd : -1;
    }
    *zdarzenia = POLLIN;
    return i == WYJSC ? od_zrodla.fd : -2;
}

static void do_agenta(const char *naglowek, const void *b, size_t n)
{
    if (fifo < 0 || pisz(fifo, naglowek, strlen(naglowek), 0) != 0 || (n > 0 && pisz(fifo, b, n, 0) != 0))
        kloguj("schowek: agent nie przyjął");
}

static void koniec_odczytu(void)
{
    char t[40];
    close(od_zrodla.fd);
    od_zrodla.fd = -1;
    if (!od_zrodla.za_duzo && k1_schowek_dobry(od_zrodla.b, od_zrodla.n)) {
        snprintf(t, sizeof t, "kopia %zu\n", od_zrodla.n);
        do_agenta(t, od_zrodla.b, od_zrodla.n);
        kloguj("schowek: przeglądarka skopiowała %zu B — do K1", od_zrodla.n);
    } else {
        kloguj("schowek: tekst przeglądarki pominięty (pusty, za długi albo nie UTF-8)");
    }
}

void schowek_obsluz(unsigned i)
{
    if (i < WYJSC && wy[i].b != NULL) {
        ssize_t w = write(wy[i].fd, wy[i].b + wy[i].od, wy[i].n - wy[i].od);
        if (w > 0)
            wy[i].od += (size_t)w;
        if (wy[i].od == wy[i].n || (w < 0 && errno != EAGAIN)) {
            if (wy[i].zamknij)
                close(wy[i].fd);
            free(wy[i].b);
            wy[i].b = NULL;
        }
    } else if (i == WYJSC && od_zrodla.fd >= 0) {
        ssize_t r = read(od_zrodla.fd, od_zrodla.b + od_zrodla.n, K1_SCHOWEK_MAKS - od_zrodla.n);
        if (r > 0)
            od_zrodla.n += (size_t)r;
        if (r > 0 && od_zrodla.n == K1_SCHOWEK_MAKS)
            od_zrodla.za_duzo = 1, koniec_odczytu();
        else if (r == 0 || (r < 0 && errno != EAGAIN))
            koniec_odczytu();
    }
}

/* set_selection of the browser: a source with text — its text is read now (one at a time; a newer replaces it). */
void schowek_zrodlo(struct obiekt *src, const char *mime)
{
    int p[2];
    zrodlo = src;
    if (src == NULL || mime == NULL)
        return;
    if (od_zrodla.b == NULL && (od_zrodla.b = malloc(K1_SCHOWEK_MAKS)) == NULL)
        return;
    if (od_zrodla.fd >= 0)
        close(od_zrodla.fd);
    od_zrodla.fd = -1;
    if (pipe2(p, O_CLOEXEC | O_NONBLOCK) != 0)
        return;
    zdarzenie(src, 1, "sh", mime, p[1]); /* wl_data_source.send */
    close(p[1]);
    od_zrodla.fd = p[0], od_zrodla.n = 0, od_zrodla.za_duzo = 0;
}

void schowek_zrodlo_znika(struct obiekt *src)
{
    if (zrodlo == src)
        zrodlo = NULL;
}

/* K1's text for a Ctrl+V (otworz.c): the selection of the client with the keyboard, then "wklejone". */
void schowek_wklej(uint32_t nr, const uint8_t *t, size_t n)
{
    char a[32];
    if (!k1_schowek_dobry(t, n) || dane_zaznaczenie(t, n) != 0) {
        kloguj("schowek: tekst z K1 nie trafił do okna");
        return;
    }
    if (zrodlo != NULL) {
        zdarzenie(zrodlo, 2, ""); /* wl_data_source.cancelled: the browser's own selection is not the one any more */
        zrodlo = NULL;
    }
    snprintf(a, sizeof a, "wklejone %u\n", nr);
    do_agenta(a, NULL, 0);
    kloguj("schowek: tekst z K1 (%zu B) jest zaznaczeniem okna", n);
}

/* receive of our offer of text: its bytes into the client's pipe (it closes at the end). */
void schowek_daj(int fd, const char *b, size_t n)
{
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0 || pisz(fd, b, n, 1) != 0)
        kloguj("schowek: przeglądarka nie odebrała tekstu");
}

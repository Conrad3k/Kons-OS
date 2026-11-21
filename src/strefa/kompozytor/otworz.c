/* K1 kompozytor — "Otwórz w…" (0.11.0 step 3, ADR 0052, P16): the agent of the zone's files asks to open a file K1 just
 * gave the session — one line "file:///home/k1/Z%20K1/<name, percent-encoded>" on a FIFO next to the Wayland socket
 * (made here: the browser's group may write, nobody else reads). Firefox in the zone runs with --no-remote and has no
 * D-Bus, so no other program can tell it to open a tab: the compositor types it — Ctrl+L, the address, Alt+Enter (a new
 * tab) — into the window with the keyboard focus, as keys of its keymap. Only that folder, only a name of plain
 * characters (the agent encodes the rest), at most one a second. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kompozytor.h"

#define PRZEDROSTEK "file:///home/k1/Z%20K1/"
#define ADRES_MAKS 900
#define UZYTKOWNIK 1000 /* the browser's (gosc/przegladarka.c) */

static int fifo = -1;
static char linia[ADRES_MAKS + 2];
static size_t dl;
static uint32_t ostatni;

void otworz_start(const char *gniazdo)
{
    char s[256];
    const char *k = strrchr(gniazdo, '/');
    if (k == NULL || (size_t)(k - gniazdo) + 8 >= sizeof s)
        return;
    snprintf(s, sizeof s, "%.*s/otworz", (int)(k - gniazdo), gniazdo);
    unlink(s);
    if (mkfifo(s, 0620) != 0 || chown(s, 0, UZYTKOWNIK) != 0 || chmod(s, 0620) != 0 ||
        (fifo = open(s, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) /* O_RDWR: no end of file when the agent goes */
        kloguj("otwórz w: kolejka %s nieudana: %s", s, strerror(errno));
}

int otworz_fd(void)
{
    return fifo;
}

static int hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

/* 1 when the address may be typed: the folder "Z K1", then letters, digits, ".-_" and "%XX" only. */
static int dobry(const char *a)
{
    size_t p = strlen(PRZEDROSTEK), n = strlen(a);
    if (n <= p || n > ADRES_MAKS || strncmp(a, PRZEDROSTEK, p) != 0)
        return 0;
    for (size_t i = p; i < n; i++) {
        char c = a[i];
        if (c == '%' && i + 2 < n && hex(a[i + 1]) && hex(a[i + 2]))
            i += 2;
        else if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
                   c == '_'))
            return 0;
    }
    return 1;
}

void otworz_czytaj(void)
{
    char b[256];
    ssize_t n = read(fifo, b, sizeof b);
    for (ssize_t i = 0; i < n; i++) {
        if (b[i] != '\n') {
            if (dl <= ADRES_MAKS)
                linia[dl++] = b[i];
            continue;
        }
        linia[dl <= ADRES_MAKS ? dl : 0] = '\0';
        if (!dobry(linia) || teraz_ms() - ostatni < 1000) {
            kloguj("otwórz w: prośba odrzucona");
        } else {
            ostatni = teraz_ms();
            kloguj("otwórz w: nowa karta z plikiem z K1");
            wejscie_wpisz(linia);
        }
        dl = 0;
    }
}

/* K1 kompozytor — "Otwórz w…" (0.11.0 step 3, ADR 0052, P16): the agent of the zone's files asks to open a file K1 just
 * gave the session — one line "file:///home/k1/Z%20K1/<name, percent-encoded>" on a FIFO next to the Wayland socket
 * (made here: the browser's group may write, nobody else reads). Firefox in the zone runs with --no-remote and has no
 * D-Bus, so no other program can tell it to open a tab: the compositor types it — Ctrl+L, the address, Alt+Enter (a new
 * tab) — into the window with the keyboard focus, as keys of its keymap. Only that folder, only a name of plain
 * characters (the agent encodes the rest), at most one a second. 0.11.0 step 4: "upusc X Y <address> …" — files the
 * user dropped on the window in K1 (the agent put them in "Z K1"): the same addresses only, at most
 * K1_PRZ_DO_STREFY_MAKS, X and Y inside the display — the drop is played there (dane.c). 0.11.0 step 5: "wklej <nr> <n>"
 * and then n bytes — K1's clipboard text for a Ctrl+V (schowek.c). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_dokument.h"
#include "k1_schowek.h"
#include "kompozytor.h"

#define PRZEDROSTEK "file:///home/k1/Z%20K1/"
#define ADRES_MAKS 900
#define UZYTKOWNIK 1000 /* the browser's (gosc/przegladarka.c) */

#define LINIA_MAKS (32 + K1_PRZ_DO_STREFY_MAKS * (ADRES_MAKS + 1))

static int fifo = -1;
static char linia[LINIA_MAKS + 2];
static size_t dl;
static uint32_t ostatni;
static struct { uint32_t nr; size_t n, jest; uint8_t *b; } tekst; /* "wklej": its bytes coming (n > 0) */

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
    else if (fcntl(fifo, F_SETPIPE_SZ, (int)(2 * K1_SCHOWEK_MAKS)) < 0) /* 0.11.0 step 5: a whole text of K1 fits */
        kloguj("otwórz w: kolejka bez dużego bufora (%s)", strerror(errno));
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

/* "upusc X Y a1 a2 …": the addresses as text/uri-list for dane.c; 0, or -1 (not one we take). */
static int upusc(char *l)
{
    static char lista[LINIA_MAKS + 2 * K1_PRZ_DO_STREFY_MAKS];
    char *k, *a;
    long x = strtol(l + 6, &k, 10), y;
    size_t n = 0, ile = 0;
    if (k == l + 6 || *k != ' ' || x < 0 || x >= ekran.szer)
        return -1;
    y = strtol(k + 1, &a, 10);
    if (a == k + 1 || *a != ' ' || y < 0 || y >= ekran.wys)
        return -1;
    for (char *t = strtok(a + 1, " "); t != NULL; t = strtok(NULL, " ")) {
        size_t d = strlen(t);
        if (++ile > K1_PRZ_DO_STREFY_MAKS || !dobry(t) || n + d + 2 >= sizeof lista)
            return -1;
        memcpy(lista + n, t, d);
        memcpy(lista + n + d, "\r\n", 2);
        n += d + 2;
    }
    if (ile == 0)
        return -1;
    lista[n] = '\0';
    kloguj("upuszczenie: %zu plików z K1 w punkcie okna", ile);
    dane_upusc((int32_t)x, (int32_t)y, lista);
    return 0;
}

/* "wklej <nr> <n>": the text follows (0), or -1. */
static int wklej(const char *l)
{
    char *k;
    unsigned long nr = strtoul(l + 6, &k, 10), n;
    if (k == l + 6 || *k != ' ' || nr == 0 || nr > 0x7fffffffu)
        return -1;
    n = strtoul(k + 1, &k, 10);
    if (*k != '\0' || n == 0 || n > K1_SCHOWEK_MAKS || (tekst.b == NULL && (tekst.b = malloc(K1_SCHOWEK_MAKS)) == NULL))
        return -1;
    tekst.nr = (uint32_t)nr, tekst.n = n, tekst.jest = 0;
    return 0;
}

void otworz_czytaj(void)
{
    static char b[65536];
    ssize_t n = read(fifo, b, sizeof b);
    for (ssize_t i = 0; i < n; i++) {
        if (tekst.n > 0) { /* the bytes of "wklej" */
            size_t ile = (size_t)(n - i) < tekst.n - tekst.jest ? (size_t)(n - i) : tekst.n - tekst.jest;
            memcpy(tekst.b + tekst.jest, b + i, ile);
            tekst.jest += ile;
            i += (ssize_t)ile - 1;
            if (tekst.jest == tekst.n) {
                schowek_wklej(tekst.nr, tekst.b, tekst.n);
                tekst.n = 0;
            }
            continue;
        }
        if (b[i] != '\n') {
            if (dl <= LINIA_MAKS)
                linia[dl++] = b[i];
            continue;
        }
        linia[dl <= LINIA_MAKS ? dl : 0] = '\0';
        if (strncmp(linia, "wklej ", 6) == 0) {
            if (wklej(linia) != 0)
                kloguj("schowek: prośba odrzucona");
        } else if (strncmp(linia, "upusc ", 6) == 0) {
            if (teraz_ms() - ostatni < 1000 || upusc(linia) != 0)
                kloguj("upuszczenie: prośba odrzucona");
            else
                ostatni = teraz_ms();
        } else if (!dobry(linia) || teraz_ms() - ostatni < 1000) {
            kloguj("otwórz w: prośba odrzucona");
        } else {
            ostatni = teraz_ms();
            kloguj("otwórz w: nowa karta z plikiem z K1");
            wejscie_wpisz(linia);
        }
        dl = 0;
    }
}

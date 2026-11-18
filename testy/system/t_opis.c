/* K1 in-system test — the screen's description for the tests (0.10.2 step 2, ADR 0045; format src/wspolne/k1_opis.h).
 *
 * t_opis (with the other checks, test-m1): a channel of role TEST asks ekran what is on the screen and gets a valid
 * description with the shell's bar and its "Start"; NEGATIVE: the same question on a channel of another role (an
 * application) gets nothing — ekran refuses it (fail closed, T3).
 * t_opis_scenariusz (scripts/test-m4.sh: a disk with the serial number "k1-test-opis"): no checks — the description
 * twice a second, logged when it changed, for the script to click by name (testy/qemu/k1qemu.py: q.ekran()):
 *   K1-TEST: ekran <n> <part>/<parts>: <record> | <record> | …
 *   record: <kind> <window> <x>,<y>,<w>,<h> <state> <name>   (kind: O window, P panel, B button, W row, K tile,
 *   I icon, Z tab, F field; window: ekran's number, 0 for a panel; state: K1_EL_* bits) */
#include <poll.h>

#include "k1_memfd.h"
#include "k1_opis.h"
#include "k1_test.h"

#define CZESC 380 /* bytes of records in one log line (K1_LOG_MAX 512 with the tag and the prefix) */

static struct k1_opis_el el[K1_OPIS_EKRAN_MAKS];

/* One question on channel fd: the number of records in el (checked as the test program's own: k1_opis_sprawdz), or
 * -1 (no answer within `ms`, a wrong one), -2 (the channel closed). */
static int zapytaj_ekran(int fd, int ms)
{
    static uint8_t buf[sizeof(struct k1_opis_naglowek) + sizeof el];
    struct k1_komunikat q = k1_komunikat_nowy(K1_KOM_OPIS_PYTANIE, 0, 0, 0, 0, 0, 0);
    if (k1_kanal_wyslij(fd, &q, sizeof q, -1) != 0)
        return -2;
    for (int t = 0; t < ms / 50; t++) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        enum k1_kanal_wynik w = k1_kanal_odbierz(fd, &o);
        if (w == K1_KANAL_PUSTO) {
            struct pollfd p = { fd, POLLIN, 0 };
            poll(&p, 1, 50);
            continue;
        }
        if (w != K1_KANAL_KOMUNIKAT)
            return -2;
        if (k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_EKRANU, &m) == 0 && m.typ == K1_KOM_OPIS_EKRANU &&
            o.fd >= 0) {
            size_t dl = sizeof(struct k1_opis_naglowek) + (size_t)m.a * sizeof el[0];
            int n = k1_memfd_czytaj(o.fd, buf, dl) == 0 ? k1_opis_sprawdz(buf, dl, K1_OPIS_OD_EKRANU, 0, 0, el) : -1;
            close(o.fd);
            return n == m.a ? n : -1;
        }
        if (o.fd >= 0)
            close(o.fd);
    }
    return -1;
}

static int jest(int n, int rodzaj, const char *nazwa)
{
    for (int i = 0; i < n; i++)
        if (el[i].rodzaj == rodzaj && strcmp(el[i].nazwa, nazwa) == 0)
            return 1;
    return 0;
}

void t_opis(void)
{
    int fd = polacz(K1_ROLA_TEST, 0, 0), n = -1, i, inny;
    for (i = 0; fd >= 0 && i < 20 && !(n > 0 && jest(n, K1_EL_IKONA, "Start")); i++) { /* gui draws again first */
        n = zapytaj_ekran(fd, 2000);
        msleep(250);
    }
    check(n > 0 && jest(n, K1_EL_PANEL, "Pasek") && jest(n, K1_EL_IKONA, "Start"),
          "opis ekranu: klient testowy dostaje opis (pasek pulpitu i „Start”)", "fd %d, rekordy %d", fd, n);
    inny = polacz(K1_ROLA_APLIKACJA, K1_APL_ZEGAR, 0);
    check(inny >= 0 && zapytaj_ekran(inny, 1500) < 0,
          "NEGATYWNY: opis ekranu tylko dla roli testowej — aplikacja nie dostaje odpowiedzi", "fd %d", inny);
    if (inny >= 0)
        close(inny);
    if (fd >= 0)
        close(fd); /* the description goes off: clients stop sending */
}

/* ---- scripts/test-m4.sh ---- */

int znak_dysku(const char *serial)
{
    DIR *d = opendir("/rdzen/jadro/sys/block");
    struct dirent *de;
    int jest_ = 0;
    while (d != NULL && !jest_ && (de = readdir(d)) != NULL) {
        char p[300], b[64];
        snprintf(p, sizeof p, "/rdzen/jadro/sys/block/%s/serial", de->d_name);
        jest_ = de->d_name[0] != '.' && read_file(p, b, sizeof b) == 0 && strncmp(b, serial, strlen(serial)) == 0;
    }
    if (d != NULL)
        closedir(d);
    return jest_;
}

int tryb_opisu(void)
{
    return znak_dysku("k1-test-opis");
}

static size_t rekord(char *b, size_t n, const struct k1_opis_el *e)
{
    static const char kody[] = "?.OPBWKIZFM";
    char nazwa[K1_OPIS_NAZWA];
    int w;
    memcpy(nazwa, e->nazwa, sizeof nazwa);
    for (char *c = nazwa; *c; c++)
        if (*c == '|')
            *c = '/';
    w = snprintf(b, n, "%c %u %d,%d,%d,%d %u %s", kody[e->rodzaj <= K1_EL_MAKS ? e->rodzaj : 0], (unsigned)e->okno,
                 e->x, e->y, e->w, e->h, (unsigned)e->stan, nazwa);
    return w > 0 && (size_t)w < n ? (size_t)w : 0;
}

/* The description as log lines (records never cut between lines). */
static void loguj(unsigned nr, int n)
{
    static char tekst[K1_OPIS_EKRAN_MAKS * 96];
    size_t dl = 0, czesci = 1, od = 0, i;
    for (int r = 0; r < n && dl < sizeof tekst - 100; r++) {
        if (r > 0)
            dl += (size_t)snprintf(tekst + dl, sizeof tekst - dl, " | ");
        dl += rekord(tekst + dl, sizeof tekst - dl, &el[r]);
    }
    for (i = 0, od = 0; i < dl; i++) /* parts: count them first */
        if (i - od >= CZESC && strncmp(tekst + i, " | ", 3) == 0)
            czesci++, od = i + 3;
    for (size_t c = 1, p = 0; c <= czesci; c++) {
        size_t k = p;
        while (k < dl && !(k - p >= CZESC && strncmp(tekst + k, " | ", 3) == 0))
            k++;
        k1_log(TAG, "ekran %u %zu/%zu: %.*s", nr, c, czesci, (int)(k - p), tekst + p);
        p = k + 3;
    }
}

int t_opis_scenariusz(void)
{
    static struct k1_opis_el ostatni[K1_OPIS_EKRAN_MAKS];
    int fd = -1, n_ost = -1, i;
    unsigned nr = 0;
    for (i = 0; i < 600 && find_process("gui") <= 0; i++)
        msleep(100);
    k1_log(TAG, "opis ekranu dla testów: start");
    for (;;) {
        int n;
        if (fd < 0 && (fd = polacz(K1_ROLA_TEST, 0, 0)) < 0) { /* ekran restarted: a new channel */
            msleep(1000);
            continue;
        }
        n = zapytaj_ekran(fd, 2000);
        if (n == -2) {
            close(fd);
            fd = -1;
        } else if (n >= 0 && (n != n_ost || memcmp(el, ostatni, (size_t)n * sizeof el[0]) != 0)) {
            memcpy(ostatni, el, (size_t)n * sizeof el[0]);
            n_ost = n;
            loguj(++nr, n);
        }
        msleep(500);
    }
}

/* K1 agent — files from K1 into the session (0.11.0 step 3, ADR 0052, D4: A): "Wyślij do…" / "Otwórz w…" in Pliki. The
 * VMM sends PLIK (the name K1 gives it, the size, "otworz") and BAJTY; the bytes go to a hidden file in "Z K1"
 * (".k1-<nr>", O_EXCL, O_NOFOLLOW), renamed at the end to the name — "name (2).ext"… when it is taken — and ODEBRANY
 * tells K1 (0 saved, 1 not). "otworz": one line on the compositor's FIFO, the file's address percent-encoded (the
 * compositor types it into a new tab: kompozytor/otworz.c). What K1 gave stays known (its number, the name it got here):
 * a changed version of it goes back as ZGLOS with dotyczy (main.c). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "agent.h"

#define ODEBRANYCH 32
#define Z_K1_ADRES "file:///home/k1/Z%20K1/"

static int katalog = -1, fifo = -1;
static const char *fifo_sciezka;
static struct {
    int jest, otworz, fd;
    uint32_t nr;
    uint64_t rozmiar, jest_bajtow;
    char nazwa[K1_PS_NAZWA_MAKS + 1], tymczasowa[32];
} od;
static struct {
    uint32_t nr;
    char nazwa[K1_PS_NAZWA_MAKS + 1];
} odebrane[ODEBRANYCH];
static unsigned n_odebranych;

int odbior_start(const char *z_k1, const char *fifo_)
{
    fifo_sciezka = fifo_;
    katalog = open(z_k1, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    return katalog >= 0 ? 0 : -1;
}

int odbior_katalog(void)
{
    return katalog;
}

static void wynik(uint32_t nr, uint32_t w)
{
    struct k1_ps_ramka r = { .typ = K1_PS_ODEBRANY, .nr = nr, .wynik = w };
    if (ramka(&r) != 0)
        rozlacz();
}

static void porzuc(void)
{
    if (!od.jest)
        return;
    close(od.fd);
    unlinkat(katalog, od.tymczasowa, 0);
    od.jest = 0;
}

void odbior_rozlacz(void)
{
    porzuc();
}

/* A name K1 gave may be a name here: not ".", "..", hidden, no "/". */
static int nazwa_dobra(const char *n)
{
    return n[0] != '\0' && n[0] != '.' && strchr(n, '/') == NULL;
}

/* The free name in "Z K1": `nazwa`, else "base (2).ext"… (up to 99); 0, or -1. */
static int wolna(const char *nazwa, char *out, size_t n)
{
    const char *kropka = strrchr(nazwa, '.');
    size_t baza = kropka != NULL && kropka != nazwa ? (size_t)(kropka - nazwa) : strlen(nazwa);
    snprintf(out, n, "%s", nazwa);
    for (int i = 2; i < 100 && faccessat(katalog, out, F_OK, AT_SYMLINK_NOFOLLOW) == 0; i++)
        snprintf(out, n, "%.*s (%d)%s", (int)baza, nazwa, i, nazwa + baza);
    return faccessat(katalog, out, F_OK, AT_SYMLINK_NOFOLLOW) == 0 ? -1 : 0;
}

/* "Otwórz w…": the file's address (everything but letters, digits and ".-_" as %XX) to the compositor. */
static void otworz(const char *nazwa)
{
    char a[1024];
    size_t d = (size_t)snprintf(a, sizeof a, "%s", Z_K1_ADRES);
    for (const unsigned char *c = (const unsigned char *)nazwa; *c && d + 5 < sizeof a; c++) {
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || strchr(".-_", *c) != NULL)
            a[d++] = (char)*c;
        else
            d += (size_t)snprintf(a + d, sizeof a - d, "%%%02X", *c);
    }
    a[d++] = '\n';
    if (fifo < 0)
        fifo = open(fifo_sciezka, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fifo < 0 || write(fifo, a, d) != (ssize_t)d)
        loguj("otwórz w: kompozytor nie przyjął prośby");
}

static void koniec(void)
{
    char nazwa[K1_PS_NAZWA_MAKS + 16];
    uint32_t w = 1;
    close(od.fd);
    od.jest = 0;
    if (wolna(od.nazwa, nazwa, sizeof nazwa) == 0 && strlen(nazwa) <= K1_PS_NAZWA_MAKS &&
        renameat(katalog, od.tymczasowa, katalog, nazwa) == 0) {
        w = 0;
        odebrane[n_odebranych % ODEBRANYCH].nr = od.nr;
        snprintf(odebrane[n_odebranych % ODEBRANYCH].nazwa, sizeof odebrane[0].nazwa, "%s", nazwa);
        n_odebranych++;
        loguj("plik %u z K1 w „Z K1”%s", od.nr, od.otworz ? " — otwieram" : "");
        if (od.otworz)
            otworz(nazwa);
    } else {
        unlinkat(katalog, od.tymczasowa, 0);
    }
    wynik(od.nr, w);
}

void odbior_ramka(const struct k1_ps_ramka *r)
{
    if (r->typ == K1_PS_PLIK) {
        porzuc(); /* a new file: one that did not end is gone */
        snprintf(od.tymczasowa, sizeof od.tymczasowa, ".k1-%u", r->nr);
        unlinkat(katalog, od.tymczasowa, 0);
        od.fd = nazwa_dobra(r->nazwa) ? openat(katalog, od.tymczasowa, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)
                                     : -1;
        if (od.fd < 0) {
            wynik(r->nr, 1);
            return;
        }
        od.jest = 1, od.nr = r->nr, od.otworz = r->dodatek == 1;
        od.rozmiar = r->rozmiar, od.jest_bajtow = 0;
        snprintf(od.nazwa, sizeof od.nazwa, "%s", r->nazwa);
        if (od.rozmiar == 0)
            koniec();
        return;
    }
    if (!od.jest || r->nr != od.nr || r->dl > od.rozmiar - od.jest_bajtow ||
        pwrite(od.fd, r->dane, r->dl, (off_t)od.jest_bajtow) != (ssize_t)r->dl) {
        if (od.jest && r->nr == od.nr) {
            porzuc();
            wynik(r->nr, 1);
        }
        return;
    }
    od.jest_bajtow += r->dl;
    if (od.jest_bajtow == od.rozmiar)
        koniec();
}

/* "name(N).ext" (Firefox's next free name of a download) of `oryg`: 1. */
static int kolejna_nazwa(const char *n, const char *oryg)
{
    const char *k = strrchr(oryg, '.');
    size_t baza = k != NULL && k != oryg ? (size_t)(k - oryg) : strlen(oryg), i = baza + 1;
    if (strncmp(n, oryg, baza) != 0 || n[baza] != '(' || n[i] < '1' || n[i] > '9')
        return 0;
    while (n[i] >= '0' && n[i] <= '9')
        i++;
    return n[i] == ')' && strcmp(n + i + 1, oryg + baza) == 0;
}

uint32_t odbior_dotyczy(const char *nazwa, int z_k1)
{
    unsigned ile = n_odebranych < ODEBRANYCH ? n_odebranych : ODEBRANYCH;
    for (unsigned k = 0; k < ile; k++) { /* the newest first */
        unsigned j = (n_odebranych - 1 - k) % ODEBRANYCH;
        if (strcmp(odebrane[j].nazwa, nazwa) == 0 || (!z_k1 && kolejna_nazwa(nazwa, odebrane[j].nazwa)))
            return odebrane[j].nr;
    }
    return 0;
}

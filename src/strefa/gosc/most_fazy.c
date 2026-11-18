/* K1 gosc — the phases of the test of Raptor Bridge (most.h; 0.10.5 step 5, T15). Each phase opens its connections at
 * once, drives them to their end and says in one line how many ended as expected ("most: <phase>: ok X/Y"); the script
 * (scripts/test-most.sh) checks X = Y, compares the totals with its server's and with K1's counters, and reads the
 * times (the bridge's throughput and delay go to the description of Raptor Bridge). Counts only in the log. */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gosc.h"
#include "most.h"

#define PRZEKAZNIK "/usr/lib/k1/przekaznik" /* in the test zone's image too (scripts/zbuduj-obraz.sh) */
#define CZAS_FAZY 240.0 /* s; under emulation (no KVM) a phase of megabytes takes tens of seconds */

static struct {
    unsigned tcp;            /* connections the server should have taken (SOCKS reply 0 to the router) */
    unsigned odmowy_d3;      /* refused by nadzorca (D3: the home network) */
    unsigned odmowy_lacznik; /* refused by the connector itself (loopback: never an address to reach) */
    unsigned long long w_dol, w_gore;
} suma;

static struct zad *do_routera(const char *linia)
{
    struct zad *a = most_nowe(0);
    if (a != NULL)
        most_socks_ip(a, MOST_ROUTER, MOST_PORT, linia);
    return a;
}

static void d(uint64_t n, uint64_t przerwij_po)
{
    char l[32];
    struct zad *a;
    snprintf(l, sizeof l, "D %llu\n", (unsigned long long)n);
    if ((a = do_routera(l)) != NULL)
        a->rodzaj = MOST_D, a->do_wyslania = 0, a->przerwij_po = przerwij_po, a->blad = (int)n; /* blad: n until end */
}

static void e(uint64_t n)
{
    struct zad *a = do_routera("E\n");
    if (a != NULL)
        a->rodzaj = MOST_E, a->do_wyslania = n;
}

static void h(uint64_t n, uint64_t w_gore)
{
    char l[32];
    struct zad *a;
    snprintf(l, sizeof l, "H %llu\n", (unsigned long long)n);
    if ((a = do_routera(l)) != NULL)
        a->rodzaj = MOST_H, a->do_wyslania = w_gore, a->blad = (int)n;
}

static void c(uint64_t n)
{
    char l[32];
    struct zad *a;
    snprintf(l, sizeof l, "C %llu\n", (unsigned long long)n);
    if ((a = do_routera(l)) != NULL)
        a->rodzaj = MOST_C, a->blad = (int)n;
}

static void odmowa(uint32_t ip, int kto) /* kto: -1 nadzorca, -3 the connector */
{
    struct zad *a = most_nowe(0);
    if (a != NULL)
        most_socks_ip(a, ip, MOST_PORT, "D 1\n"), a->rodzaj = kto;
}

static void nazwa(const char *n)
{
    struct zad *a = most_nowe(0);
    if (a != NULL)
        most_socks_nazwa(a, n, MOST_PORT), a->rodzaj = -2;
}

static void pusty(void) /* to the relay and away, without a byte (its place in the VMM must come back) */
{
    struct zad *a = most_nowe(0);
    if (a != NULL)
        a->rodzaj = -4, a->odp_dl = 0;
}

/* Whether task a ended as its kind expects (`oczekiwane` was stored in a->blad before the run). */
static int dobrze(struct zad *a, uint64_t n)
{
    if (a->socks == 0 && a->rodzaj >= 0)
        suma.tcp++;
    suma.w_dol += a->odebrane;
    suma.w_gore += a->wyslane;
    switch (a->rodzaj) {
    case MOST_D:
        return a->socks == 0 && a->zle == 0 &&
               (a->przerwij_po ? a->koniec == MOST_PRZERWANE : a->koniec == MOST_EOF && a->odebrane == n);
    case MOST_E:
        return a->socks == 0 && a->zle == 0 && a->koniec == MOST_EOF && a->odebrane == a->do_wyslania &&
               a->wyslane == a->do_wyslania;
    case MOST_H: /* the server ended its side first; the client sent its bytes after that */
        return a->socks == 0 && a->zle == 0 && a->koniec == MOST_EOF && a->odebrane == n &&
               a->wyslane == a->do_wyslania;
    case MOST_C: /* a reset in the middle: fewer bytes, all of them right, and an end (EOF or ECONNRESET) */
        return a->socks == 0 && a->zle == 0 && a->odebrane < n && (a->koniec == MOST_EOF || a->koniec == MOST_BLAD);
    case -1: /* D3: nadzorca refuses (SOCKS 2) */
    case -3: /* loopback: the connector refuses before asking nadzorca */
        if (a->rodzaj == -1)
            suma.odmowy_d3 += a->socks == 2;
        else
            suma.odmowy_lacznik += a->socks == 2;
        return a->socks == 2 && a->odebrane == 0;
    case -4:
        return a->koniec == MOST_PRZERWANE;
    default: /* a name: not found (4) or not a name we look up (1) */
        return (a->socks == 4 || a->socks == 1) && a->odebrane == 0;
    }
}

/* Runs the tasks from `od`, checks them, says the phase's line; 1 when all went as expected. */
static int faza(const char *nazwa_fazy, int od)
{
    uint64_t n[MOST_MAKS];
    unsigned ok = 0, wszystkie = 0;
    unsigned long long bajty = 0;
    double t;
    int i;
    for (i = od; i < most_liczba(); i++) { /* the expected size travels in blad until the run */
        n[i] = (uint64_t)(unsigned)most_zad(i)->blad;
        most_zad(i)->blad = 0;
    }
    t = most_uruchom(od, CZAS_FAZY);
    for (i = od; i < most_liczba(); i++) {
        struct zad *a = most_zad(i);
        int w = dobrze(a, n[i]);
        ok += (unsigned)w;
        wszystkie++;
        bajty += a->odebrane + a->wyslane;
        if (!w)
            loguj("most: %s: połączenie %d — rodzaj %d, socks %d, koniec %d (%s), w dół %llu, w górę %llu, złe %lu",
                  nazwa_fazy, i - od, a->rodzaj, a->socks, a->koniec, a->blad ? strerror(a->blad) : "-",
                  (unsigned long long)a->odebrane, (unsigned long long)a->wyslane, a->zle);
    }
    loguj("most: %s: ok %u/%u, %.2f s, %.2f MiB/s", nazwa_fazy, ok, wszystkie, t, (double)bajty / (1 << 20) / t);
    return ok == wszystkie;
}

/* The connector from the wrong side: straight to the VMM's port (no relay), bytes the relay would never send. */
static void zly_socks(const char *opis, const uint8_t *b, size_t n, uint8_t kod)
{
    struct zad *a = most_nowe(1);
    if (a == NULL)
        return;
    most_surowe(a, b, n, kod == 0xFF ? 2 : 12);
    a->rodzaj = 100 + kod;
    (void)opis;
}

static int faza_zla(void)
{
    static const uint8_t v4[] = { 4, 1, 0, 0, 80, 10, 0, 2, 2, 0 },
                         bind[] = { 5, 1, 0, 5, 2, 0, 1, 10, 0, 2, 2, 0x22, 0x56 },
                         ipv6[] = { 5, 1, 0, 5, 1, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 80 },
                         port0[] = { 5, 1, 0, 5, 1, 0, 1, 10, 0, 2, 2, 0, 0 },
                         zla_nazwa[] = { 5, 1, 0, 5, 1, 0, 3, 4, 'a', ' ', 'b', '!', 0, 80 },
                         rsv[] = { 5, 1, 0, 5, 1, 7, 1, 10, 0, 2, 2, 0, 80 };
    int od = most_liczba(), i;
    unsigned ok = 0, wszystkie = 0;
    zly_socks("SOCKS4", v4, sizeof v4, 0xFF);
    zly_socks("BIND", bind, sizeof bind, 7);
    zly_socks("IPv6", ipv6, sizeof ipv6, 8);
    zly_socks("port 0", port0, sizeof port0, 1);
    zly_socks("zła nazwa", zla_nazwa, sizeof zla_nazwa, 1);
    zly_socks("bajt zarezerwowany", rsv, sizeof rsv, 1);
    most_uruchom(od, CZAS_FAZY);
    for (i = od; i < most_liczba(); i++) {
        struct zad *a = most_zad(i);
        int kod = a->rodzaj - 100,
            w = kod == 0xFF ? a->n_odp == 2 && a->odp[0] == 5 && a->odp[1] == 0xFF
                            : a->n_odp == 12 && a->odp[1] == 0 && a->odp[3] == kod;
        w = w && a->koniec == MOST_EOF && a->odebrane == 0;
        ok += (unsigned)w;
        wszystkie++;
        if (!w)
            loguj("most: zły SOCKS5 %d: odpowiedź %zu B (%d %d), koniec %d", kod, a->n_odp, a->odp[1], a->odp[3],
                  a->koniec);
    }
    loguj("most: zły SOCKS5 wprost do VMM: ok %u/%u", ok, wszystkie);
    return ok == wszystkie;
}

/* More connections than the VMM has places for, all at once (straight, a greeting each): exactly
 * K1_STREFA_POLACZENIA_MAKS get the connector's reply, the rest are refused (RST) — none hangs. */
static int faza_miejsca(void)
{
    static const uint8_t powitanie[] = { 5, 1, 0 };
    int od = most_liczba(), i, n = 140;
    unsigned odp = 0, odmowy = 0, inne = 0;
    for (i = 0; i < n; i++) {
        struct zad *a = most_nowe(1);
        if (a != NULL)
            most_surowe(a, powitanie, sizeof powitanie, 2), a->przerwij_po = 0, a->rodzaj = 99;
    }
    /* each waits for its 2 bytes; the connector then waits for a request that never comes: the task ends at the
     * phase's limit — so the limit is short, and the places are all held at that moment */
    most_uruchom(od, 20.0);
    for (i = od; i < most_liczba(); i++) {
        struct zad *a = most_zad(i);
        if (a->n_odp == 2 && a->odp[0] == 5 && a->odp[1] == 0)
            odp++;
        else if (a->koniec == MOST_BLAD && (a->blad == ECONNRESET || a->blad == ECONNREFUSED))
            odmowy++;
        else
            inne++;
    }
    loguj("most: więcej połączeń niż miejsc: odpowiedź %u, odmowa %u, inne %u (z %d)", odp, odmowy, inne, n);
    return odp == 128 && odmowy == (unsigned)n - 128 && inne == 0;
}

/* The relay as in the browser's zone (the loopback up, the relay listening on 127.0.0.1:1080); 0 when it is ready. */
static int przekaznik(void)
{
    char *argv[] = { "przekaznik", NULL }, *env[] = { NULL };
    pid_t p;
    int i;
    petla_zwrotna();
    p = fork();
    if (p == 0) {
        execve(PRZEKAZNIK, argv, env);
        _exit(127);
    }
    for (i = 0; p > 0 && i < 100; i++) {
        struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(1080), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0), w = fd >= 0 ? connect(fd, (struct sockaddr *)&a, sizeof a) : -1;
        if (fd >= 0)
            close(fd);
        if (w == 0)
            return 0;
        poll(NULL, 0, 100);
    }
    loguj("most: przekaźnik nie ruszył");
    return -1;
}

void most(void)
{
    int dobrze_wszystko = 1, i, od;
    double t = most_teraz();
    loguj("most: start");
    if (przekaznik() != 0)
        return;
    /* 1. one at a time: the smallest, a few MiB down, an echo of 1 MiB (throughput of one connection) */
    od = most_liczba(), d(1, 0);
    dobrze_wszystko &= faza("jeden bajt", od);
    od = most_liczba(), d(3u << 20, 0);
    dobrze_wszystko &= faza("3 MiB w dół", od);
    od = most_liczba(), e(1u << 20);
    dobrze_wszystko &= faza("echo 1 MiB", od);
    /* 2. many at once: 48 connections of 1 B … 1 MiB and echoes */
    od = most_liczba();
    for (i = 0; i < 40; i++)
        d((uint64_t[]){ 1, 1000, 65536, 300000, 1u << 20 }[i % 5], 0);
    for (i = 0; i < 8; i++)
        e(200000);
    dobrze_wszystko &= faza("48 naraz", od);
    /* 3. half-closes both ways */
    od = most_liczba(), e(100000), e(100000), h(70000, 50000), h(1, 300000);
    dobrze_wszystko &= faza("półzamknięcia", od);
    /* 4. ended in the middle: the client, the server (reset) */
    od = most_liczba();
    for (i = 0; i < 4; i++)
        d(4u << 20, 256u << 10), c(2u << 20), pusty();
    dobrze_wszystko &= faza("przerwane w połowie", od);
    /* 5. refusals of nadzorca (D3) next to good ones; names next to addresses */
    od = most_liczba();
    for (i = 0; i < 6; i++)
        d(10000, 0), odmowa(0x0A00024Du /* 10.0.2.77: the home network */, -1), odmowa(0x7F000001u, -3);
    nazwa("nie-ma.invalid"), nazwa("zla nazwa"), d(10000, 0);
    dobrze_wszystko &= faza("odmowy i nazwy", od);
    most_zwolnij();
    /* 6. hostile from inside the zone, without the relay */
    dobrze_wszystko &= faza_zla();
    most_zwolnij();
    dobrze_wszystko &= faza_miejsca();
    most_zwolnij();
    /* 7. the places came back: the relay works as before */
    od = most_liczba(), d(100000, 0), d(100000, 0);
    dobrze_wszystko &= faza("po złych", od);
    loguj("most: wynik %s: połączenia do serwera %u, odmowy D3 %u, odmowy łącznika %u, w dół %llu B, w górę %llu B "
          "(%.1f s)", dobrze_wszystko ? "OK" : "BŁĄD", suma.tcp, suma.odmowy_d3, suma.odmowy_lacznik, suma.w_dol,
          suma.w_gore, most_teraz() - t);
}

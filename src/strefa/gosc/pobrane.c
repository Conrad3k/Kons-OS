/* K1 gosc — the test zone as the agent of a browser's files (0.11.0 step 2, ADR 0052; scripts/test-pobrane.sh), on
 * the VMM's files port (frames: k1_pliki_strefy.h). TRYB_POBRANIE: four files told at once — "raport (test).txt" of
 * POB_DUZY bytes of a pattern (three pieces of K1), "zły/plik\x01.pdf" (a name K1 must repair), "zostaw.bin" (the test
 * clicks "Zostaw w przeglądarce") and "pusty.txt" (0 bytes); each sent on WYSLIJ, the machine ends after the last
 * WYNIK. TRYB_POBRANIE_ZLE: hostile — bytes without WYSLIJ, more bytes than the size, a frame of no type (each: the VMM
 * closes the files port, the zone lives), a flood of 20 files (K1_PS_CZEKA taken, the rest refused), a file of 5 GiB
 * (too big), BRAK in the middle of the bytes, and the machine's end in the middle of the bytes (the test program says
 * "Zapisz" to every file). Each line of the log says what the guest saw; scripts/test-pobrane.sh checks them. */
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "gosc.h"
#include "k1_pliki_strefy.h"

#define POB_DUZY (2u * 1048576u + 4321u) /* scripts/test-pobrane.sh and t_pobrane.c: the same */
#define CZEKAJ_MS 300000

static int vs = -1;
static struct k1_ps_czytnik cz;
static uint8_t b[K1_PS_RAMKA_MAKS], we[256];
static size_t we_n, we_od;

uint8_t pobrane_wzor(uint64_t i)
{
    return (uint8_t)((i * 7 + (i >> 9) + 13) & 255);
}

static int polacz(void)
{
    struct sockaddr_vm a = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = K1_PS_PORT };
    if (vs >= 0)
        close(vs);
    vs = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    memset(&cz, 0, sizeof cz);
    we_n = we_od = 0;
    if (vs < 0 || connect(vs, (struct sockaddr *)&a, sizeof a) != 0) {
        loguj("pobrane: port plików: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int pisz(const void *p, size_t n)
{
    const uint8_t *c = p;
    while (n > 0) {
        ssize_t w = write(vs, c, n);
        if (w <= 0)
            return -1;
        c += w, n -= (size_t)w;
    }
    return 0;
}

static int ramka(const struct k1_ps_ramka *r)
{
    size_t n = k1_ps_zapisz(r, 1, b, sizeof b);
    return n > 0 ? pisz(b, n) : -1;
}

static int zglos(uint32_t nr, const char *nazwa, uint64_t rozmiar)
{
    struct k1_ps_ramka r = { .typ = K1_PS_ZGLOS, .nr = nr, .rozmiar = rozmiar, .nl = (uint8_t)strlen(nazwa) };
    memcpy(r.nazwa, nazwa, r.nl);
    return ramka(&r);
}

/* The next frame from K1 within ms: 1, 0 none in time, -1 the port closed (or a bad frame). */
static int czytaj(struct k1_ps_ramka *r, int ms)
{
    for (;;) {
        size_t u;
        int w;
        struct pollfd p = { vs, POLLIN, 0 };
        if (we_n > we_od) {
            w = k1_ps_czytaj(&cz, we + we_od, we_n - we_od, &u, r);
            we_od += u;
            if (w != 0)
                return w;
        }
        if (poll(&p, 1, ms) <= 0)
            return 0;
        ssize_t n = read(vs, we, sizeof we);
        if (n <= 0)
            return -1;
        we_n = (size_t)n, we_od = 0;
    }
}

/* `ile` bytes of file nr from byte `od` of the pattern, in frames of 64 KiB. */
static int bajty(uint32_t nr, uint64_t od, uint64_t ile)
{
    static uint8_t d[K1_PS_DANE_MAKS];
    struct k1_ps_ramka r = { .typ = K1_PS_DANE, .nr = nr, .dane = d };
    while (ile > 0) {
        r.dl = ile < sizeof d ? (uint32_t)ile : (uint32_t)sizeof d;
        for (uint32_t i = 0; i < r.dl; i++)
            d[i] = pobrane_wzor(od + i);
        if (ramka(&r) != 0)
            return -1;
        od += r.dl, ile -= r.dl;
    }
    return 0;
}

static const struct {
    uint32_t nr;
    const char *nazwa;
    uint32_t rozmiar;
} uczciwe[] = { { 11, "raport (test).txt", POB_DUZY }, { 12, "zły/plik\x01.pdf", 300 }, { 13, "zostaw.bin", 1000 },
                { 14, "pusty.txt", 0 } };
#define UCZCIWYCH (int)(sizeof uczciwe / sizeof uczciwe[0])

static void pobranie(void)
{
    static const char pdf[] = "%PDF-1.4\n% K1 test\n";
    struct k1_ps_ramka r;
    int koniec = 0, i;
    if (polacz() != 0)
        return;
    for (i = 0; i < UCZCIWYCH; i++)
        if (zglos(uczciwe[i].nr, uczciwe[i].nazwa, uczciwe[i].rozmiar) != 0)
            return;
    loguj("pobrane: zgłoszone %d pliki", UCZCIWYCH);
    while (koniec < UCZCIWYCH && czytaj(&r, CZEKAJ_MS) > 0)
        for (i = 0; i < UCZCIWYCH; i++) {
            if (r.nr != uczciwe[i].nr)
                continue;
            if (r.typ == K1_PS_WYNIK) {
                loguj("pobrane: plik %u — wynik %u", r.nr, r.wynik);
                koniec++;
            } else if (r.nr == 12) { /* a PDF: the type K1 recognises from the content */
                static uint8_t t[300];
                struct k1_ps_ramka d = { .typ = K1_PS_DANE, .nr = 12, .dane = t, .dl = sizeof t };
                memset(t, ' ', sizeof t);
                memcpy(t, pdf, sizeof pdf - 1);
                loguj("pobrane: plik %u — wysyłam", r.nr);
                if (ramka(&d) != 0)
                    return;
            } else {
                loguj("pobrane: plik %u — wysyłam", r.nr);
                if (bajty(r.nr, 0, uczciwe[i].rozmiar) != 0)
                    return;
            }
        }
    loguj("pobrane: koniec (%d z %d)", koniec, UCZCIWYCH);
}

/* After a broken rule: the VMM must close the port (EOF within 10 s). */
static void zamknieta(const char *co)
{
    struct k1_ps_ramka r;
    int w;
    while ((w = czytaj(&r, 10000)) > 0)
        ;
    loguj("pobrane zły %s: %s", co, w < 0 ? "port zamknięty" : "BŁĄD: port otwarty");
}

/* The first WYSLIJ (or WYNIK) for file nr; 1: WYSLIJ came. */
static int czekaj_wyslij(uint32_t nr, uint32_t *wynik)
{
    struct k1_ps_ramka r;
    while (czytaj(&r, 60000) > 0)
        if (r.nr == nr) {
            *wynik = r.wynik;
            return r.typ == K1_PS_WYSLIJ;
        }
    *wynik = 99;
    return 0;
}

static void zalew(void)
{
    struct k1_ps_ramka r;
    int odmowy = 0, wyniki = 0;
    for (uint32_t nr = 20; nr < 40; nr++)
        zglos(nr, "zalew.bin", 10);
    while (wyniki < 20 && czytaj(&r, 60000) > 0) {
        if (r.typ == K1_PS_WYSLIJ) { /* the ones K1 took: gone already */
            struct k1_ps_ramka brak = { .typ = K1_PS_BRAK, .nr = r.nr };
            ramka(&brak);
        } else {
            wyniki++;
            odmowy += r.wynik == K1_PS_W_ODMOWA;
        }
    }
    loguj("pobrane zły zalew: wyniki %d, odmowy %d", wyniki, odmowy);
}

static void pobranie_zle(void)
{
    static const uint8_t zla[12] = { 9, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0 };
    uint32_t w;
    if (polacz() == 0 && zglos(1, "a.bin", 100) == 0 && bajty(1, 0, 50) == 0)
        zamknieta("bajty bez zgody");
    if (polacz() == 0 && zglos(2, "b.bin", 100) == 0 && czekaj_wyslij(2, &w) && bajty(2, 0, 200) == 0)
        zamknieta("więcej niż rozmiar");
    if (polacz() == 0 && pisz(zla, sizeof zla) == 0)
        zamknieta("zła ramka");
    if (polacz() == 0)
        zalew();
    if (zglos(40, "duzy.iso", 5ull << 30) == 0) {
        czekaj_wyslij(40, &w);
        loguj("pobrane zły za duży: wynik %u", w);
    }
    if (zglos(50, "przerwany.bin", 3u << 20) == 0 && czekaj_wyslij(50, &w) && bajty(50, 0, 3u << 19) == 0) {
        struct k1_ps_ramka brak = { .typ = K1_PS_BRAK, .nr = 50 };
        ramka(&brak);
        czekaj_wyslij(50, &w);
        loguj("pobrane zły brak w trakcie: wynik %u", w);
    }
    if (zglos(60, "koniec.bin", 3u << 20) == 0 && czekaj_wyslij(60, &w) && bajty(60, 0, 3u << 19) == 0)
        loguj("pobrane zły koniec strefy w trakcie: wyłączam");
}

void pobrane(int zle)
{
    if (zle)
        pobranie_zle();
    else
        pobranie();
}

/* K1 gosc — the test zone as the agent receiving files from K1 (0.11.0 step 3, ADR 0052; scripts/test-pobrane.sh):
 * TRYB_ODBIOR. The test program gives the zone three files of Dokumenty ("Wyślij do…"; the third with "Otwórz w…"), each
 * K1_PS_PLIK + BAJTY on the files port: the bytes checked against the pattern of K1 (odbior_wzor), ODEBRANY 0. Then each
 * comes back changed (ZGLOS with dotyczy = K1's number; other bytes, a different size): the test clicks "Zapisz",
 * "Zapisz jako nowy plik", "Nie" in K1's window. Last, NEGATIVE: a changed version of a file K1 never gave (dotyczy 999)
 * — K1 refuses it. 0.11.0 step 4: then a file of the desktop dragged onto the zone's window (scripts/test-pobrane.sh
 * drags it): UPUSC (how many, where — inside the window) and that many PLIK frames with tryb UPUSC, bytes checked. Each
 * line of the log says what the guest saw. */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "gosc.h"
#include "k1_pliki_strefy.h"

#define PLIKOW 3
#define CZEKAJ_MS 300000

static int vs = -1;
static struct k1_ps_czytnik cz;
static uint8_t b[K1_PS_RAMKA_MAKS], we[65536];
static size_t we_n, we_od;

static uint8_t odbior_wzor(uint64_t i) /* the content of the test program's files (testy/system/t_pobrane.c) */
{
    return (uint8_t)((i * 13 + 5) & 255);
}

static int ramka(const struct k1_ps_ramka *r)
{
    size_t n = k1_ps_zapisz(r, 1, b, sizeof b), o = 0;
    while (n > 0 && o < n) {
        ssize_t w = write(vs, b + o, n - o);
        if (w <= 0)
            return -1;
        o += (size_t)w;
    }
    return n > 0 ? 0 : -1;
}

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

/* One file from K1: its PLIK, then its bytes checked; 1 when it came whole and right. */
static int jeden(uint32_t *nr, int *otworz, char *nazwa, size_t nn)
{
    struct k1_ps_ramka r;
    uint64_t rozmiar = 0, jest = 0;
    int dobre = 1;
    if (czytaj(&r, CZEKAJ_MS) <= 0 || r.typ != K1_PS_PLIK)
        return 0;
    *nr = r.nr, *otworz = (int)r.dodatek, rozmiar = r.rozmiar;
    snprintf(nazwa, nn, "%s", r.nazwa);
    while (jest < rozmiar && czytaj(&r, CZEKAJ_MS) > 0 && r.typ == K1_PS_BAJTY && r.nr == *nr) {
        for (uint32_t i = 0; i < r.dl; i++)
            dobre &= r.dane[i] == odbior_wzor(jest + i);
        jest += r.dl;
    }
    r = (struct k1_ps_ramka){ .typ = K1_PS_ODEBRANY, .nr = *nr, .wynik = !(dobre && jest == rozmiar) };
    ramka(&r);
    loguj("odbiór: plik %u z K1 (%llu B, %s)%s", *nr, (unsigned long long)jest, dobre && jest == rozmiar ? "zgodny" : "BŁĄD",
          *otworz == K1_PS_TRYB_OTWORZ ? ", otworzyć" : *otworz == K1_PS_TRYB_UPUSC ? ", upuszczony" : "");
    return dobre && jest == rozmiar;
}

/* A changed version of K1's file `dotyczy` (its name, 777 bytes of 'Z'), sent on WYSLIJ; the result. */
static uint32_t zmiana(uint32_t nr, uint32_t dotyczy, const char *nazwa)
{
    static uint8_t t[777];
    struct k1_ps_ramka r = { .typ = K1_PS_ZGLOS, .nr = nr, .rozmiar = sizeof t, .dodatek = dotyczy, .nl = (uint8_t)strlen(nazwa) };
    memcpy(r.nazwa, nazwa, r.nl);
    memset(t, 'Z', sizeof t);
    if (ramka(&r) != 0)
        return 99;
    while (czytaj(&r, CZEKAJ_MS) > 0)
        if (r.nr == nr && r.typ == K1_PS_WYSLIJ) {
            struct k1_ps_ramka d = { .typ = K1_PS_DANE, .nr = nr, .dane = t, .dl = sizeof t };
            ramka(&d);
        } else if (r.nr == nr && r.typ == K1_PS_WYNIK) {
            return r.wynik;
        }
    return 99;
}

/* 0.11.0 step 4: UPUSC first (nothing comes while the pointer is only over the window), then its files. */
static void upuszczenie(void)
{
    struct k1_ps_ramka r;
    uint32_t nr;
    char nazwa[K1_PS_NAZWA_MAKS + 1];
    int tryb, dobre = 0, z = 0;
    loguj("odbiór: czekam na upuszczenie na okno");
    if (czytaj(&r, CZEKAJ_MS) <= 0 || r.typ != K1_PS_UPUSC) {
        loguj("odbiór: upuszczenie — BŁĄD (ramka %u zamiast UPUSC)", r.typ);
        return;
    }
    loguj("odbiór: upuszczenie %u plików w %u,%u", r.nr, r.x, r.y);
    for (uint32_t i = 0; i < r.nr; i++)
        if (jeden(&nr, &tryb, nazwa, sizeof nazwa))
            dobre++, z += tryb == K1_PS_TRYB_UPUSC;
    loguj("odbiór: upuszczone pliki zgodne: %d, z trybem upuszczenia: %d", dobre, z);
}

void odbior(void)
{
    struct sockaddr_vm a = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = K1_PS_PORT };
    uint32_t nr[PLIKOW];
    char nazwa[PLIKOW][K1_PS_NAZWA_MAKS + 1];
    int otworz, i;
    vs = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (vs < 0 || connect(vs, (struct sockaddr *)&a, sizeof a) != 0) {
        loguj("odbiór: port plików: %s", strerror(errno));
        return;
    }
    cz.od_strefy = 0;
    loguj("odbiór: czekam na pliki z K1");
    for (i = 0; i < PLIKOW && jeden(&nr[i], &otworz, nazwa[i], sizeof nazwa[i]); i++)
        ;
    for (int j = 0; j < i; j++)
        loguj("odbiór: zmiana pliku %u — wynik %u", nr[j], zmiana(100 + (uint32_t)j, nr[j], nazwa[j]));
    loguj("odbiór zły: zmiana pliku, którego K1 nie dał — wynik %u", zmiana(200, 999, "obcy.bin"));
    upuszczenie();
}

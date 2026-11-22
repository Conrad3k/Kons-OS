/* K1 gosc — the test zone as the agent receiving files from K1 (0.11.0 step 3, ADR 0052; scripts/test-pobrane.sh):
 * TRYB_ODBIOR. The test program gives the zone three files of Dokumenty ("Wyślij do…"; the third with "Otwórz w…"), each
 * K1_PS_PLIK + BAJTY on the files port: the bytes checked against the pattern of K1 (odbior_wzor), ODEBRANY 0. Then each
 * comes back changed (ZGLOS with dotyczy = K1's number; other bytes, a different size): the test clicks "Zapisz",
 * "Zapisz jako nowy plik", "Nie" in K1's window. Last, NEGATIVE: a changed version of a file K1 never gave (dotyczy 999)
 * — K1 refuses it. 0.11.0 step 4: then a file of the desktop dragged onto the zone's window (scripts/test-pobrane.sh
 * drags it): UPUSC (how many, where — inside the window) and that many PLIK frames with tryb UPUSC, bytes checked.
 * 0.11.0 step 5: the clipboard as the agent — nothing comes without Ctrl+V; a copy before any gesture (K1 refuses); the
 * text Notatki copied comes at Ctrl+V in the window (checked, WKLEJONE); a copy back ("ze strefy próbnej" — the script
 * pastes it in Notatki); NEGATIVE: a flood of copies, text with a control character, a length past 1 MiB (the port
 * closes). Each line of the log says what the guest saw. */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "gosc.h"
#include "k1_pliki_strefy.h"
#include "k1_schowek.h"

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
    memset(&r, 0, sizeof r);
    if (czytaj(&r, 3 * CZEKAJ_MS) <= 0 || r.typ != K1_PS_UPUSC) { /* the script moves the window and drags first */
        loguj("odbiór: upuszczenie — BŁĄD (ramka %u zamiast UPUSC)", r.typ);
        return;
    }
    loguj("odbiór: upuszczenie %u plików w %u,%u", r.nr, r.x, r.y);
    for (uint32_t i = 0; i < r.nr; i++)
        if (jeden(&nr, &tryb, nazwa, sizeof nazwa))
            dobre++, z += tryb == K1_PS_TRYB_UPUSC;
    loguj("odbiór: upuszczone pliki zgodne: %d, z trybem upuszczenia: %d", dobre, z);
}

/* KOPIA + KOPIA_TEKST of t (n bytes; dl: the length told, else n). */
static int kopia(uint32_t nr, const char *t, size_t n, uint32_t dl)
{
    struct k1_ps_ramka r = { .typ = K1_PS_KOPIA, .nr = nr, .x = dl ? dl : (uint32_t)n };
    if (ramka(&r) != 0)
        return -1;
    r = (struct k1_ps_ramka){ .typ = K1_PS_KOPIA_TEKST, .nr = nr, .dane = (const uint8_t *)t, .dl = (uint32_t)n };
    return ramka(&r);
}

#define Z_NOTATEK "k1 test"        /* what scripts/test-pobrane.sh types in Notatki and copies */
#define ZE_STREFY "tekst-ze-strefy 42"

static void schowek_test(void)
{
    static uint8_t t[K1_SCHOWEK_MAKS];
    struct k1_ps_ramka r;
    uint32_t dl = 0, nr = 0;
    int w = czytaj(&r, 4000);
    loguj("schowek: bez Ctrl+V %s", w == 0 ? "nic nie przyszło" : "BŁĄD — coś przyszło");
    kopia(1, ZE_STREFY, strlen(ZE_STREFY), 0); /* no gesture in the window yet: K1 refuses it */
    loguj("schowek: kopia bez gestu wysłana; czekam na Ctrl+V");
    while ((w = czytaj(&r, 3 * CZEKAJ_MS)) > 0 && r.typ != K1_PS_WKLEJ)
        ;
    if (w > 0) {
        nr = r.nr, dl = r.x;
        for (uint32_t jest = 0; jest < dl && czytaj(&r, CZEKAJ_MS) > 0 && r.typ == K1_PS_WKLEJ_TEKST;) {
            memcpy(t + jest, r.dane, r.dl < dl - jest ? r.dl : dl - jest);
            jest += r.dl;
        }
        r = (struct k1_ps_ramka){ .typ = K1_PS_WKLEJONE, .nr = nr };
        ramka(&r);
    }
    loguj("schowek: tekst z K1 %u B, %s", dl, dl == strlen(Z_NOTATEK) && memcmp(t, Z_NOTATEK, dl) == 0 ? "zgodny" : "BŁĄD");
    kopia(2, ZE_STREFY, strlen(ZE_STREFY), 0); /* right after the Ctrl+V: K1 takes it */
    loguj("schowek: kopia ze strefy wysłana");
    sleep(40);                                 /* the script pastes it in Notatki (slow under emulation) */
    for (uint32_t i = 0; i < 20; i++)
        kopia(10 + i, "zalew", 5, 0);
    sleep(1);
    kopia(40, "zlytekst", 9, 0);
    sleep(1);
    loguj("schowek zły: zalew 20, znak sterujący, za długi — wysłane");
    { /* KOPIA with a length past 1 MiB: k1_ps_zapisz refuses it, so the bytes by hand */
        uint8_t z[20] = { K1_PS_KOPIA, 0, 0, 0, 12, 0, 0, 0, 41, 0, 0, 0 };
        uint32_t za = K1_SCHOWEK_MAKS + 1;
        memcpy(z + 12, &za, 4); /* little-endian, as the guest */
        if (write(vs, z, sizeof z) != (ssize_t)sizeof z)
            loguj("schowek zły: za długi — nie wysłany");
    }
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
    schowek_test(); /* 0.11.0 step 5 */
}

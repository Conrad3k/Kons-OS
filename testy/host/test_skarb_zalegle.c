/* Host tests of skarbiec's queue of replies a client has not taken yet (0.9.0 step 2, src/skarbiec/zalegle.c), on a real
 * channel (k1_kanal): sent at once while the channel takes them; when it is full they wait, in order, with their
 * descriptors; DOK_ZMIANA waits at most once; nothing waiting is lost; a closed channel or a gone client frees what
 * waited; a full queue counts the loss. ASan/UBSan build. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_test_host.h"
#include "zalegle.h"

static struct k1_komunikat wynik(uint32_t nr)
{
    return k1_komunikat_nowy(K1_KOM_SKARB_WYNIK, nr, K1_KOM_SKARB_INFO, 0, 0, 0, 0);
}

static struct k1_komunikat zmiana(void)
{
    return k1_komunikat_nowy(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0);
}

/* The next message on the client's end into *m (its descriptor into *fd, or -1): 1, or 0 when none waits. */
static int odbierz(int gniazdo, struct k1_komunikat *m, int *fd)
{
    struct k1_odebrane o;
    if (k1_kanal_odbierz(gniazdo, &o) != K1_KANAL_KOMUNIKAT)
        return 0;
    EXPECT(o.dlugosc == sizeof *m && !o.obciete);
    memcpy(m, o.dane, sizeof *m); /* the number of a reply in obiekt goes beyond K1_OB_MAKS: not checked here */
    *fd = o.fd;
    return 1;
}

/* The channel takes them: at once, nothing waits. */
static void od_razu(void)
{
    struct zalegle z;
    struct k1_komunikat m;
    int p[2], fd;
    EXPECT(k1_kanal_utworz(p) == 0);
    zal_poczatek(&z);
    m = wynik(1);
    EXPECT(zal_wyslij(&z, p[0], &m, -1) == 0 && z.n == 0);
    EXPECT(odbierz(p[1], &m, &fd) && m.obiekt == 1 && fd < 0);
    close(p[0]);
    close(p[1]);
}

/* A client that does not read: what does not fit waits, in order, a change once; then it reads, all of it comes. */
static void pelny_kanal(void)
{
    struct zalegle z;
    struct k1_komunikat m;
    int p[2], rura[2], fd, i, wyslane = 0, odebrane = 0, zmiany = 0, w_kolejnosci = 1, z_deskryptorem = 0;
    uint32_t ostatni = 0;
    char c = 0;
    EXPECT(k1_kanal_utworz(p) == 0 && pipe(rura) == 0);
    zal_poczatek(&z);
    for (i = 0; i < 100000 && z.n == 0; i++) { /* until the channel is full */
        m = wynik((uint32_t)++wyslane);
        EXPECT(zal_wyslij(&z, p[0], &m, -1) == 0);
    }
    EXPECT(z.n == 1 && wyslane > 1);
    /* more while it waits: a change twice (waits once), a reply with a descriptor, more replies */
    m = zmiana();
    EXPECT(zal_wyslij(&z, p[0], &m, -1) == 0 && z.n == 2);
    EXPECT(zal_wyslij(&z, p[0], &m, -1) == 0 && z.n == 2);
    m = wynik((uint32_t)++wyslane);
    EXPECT(zal_wyslij(&z, p[0], &m, rura[1]) == 0 && z.n == 3);
    close(rura[1]); /* the queue keeps its own copy */
    for (i = 0; i < 5; i++) {
        m = wynik((uint32_t)++wyslane);
        EXPECT(zal_wyslij(&z, p[0], &m, -1) == 0);
    }
    EXPECT(z.n == 8 && zal_oproznij(&z, p[0]) == 8); /* still full */
    /* the client reads again: everything, in order */
    while (z.n > 0 || odebrane + zmiany < wyslane + 1) {
        int byl = 0;
        while (odbierz(p[1], &m, &fd)) {
            byl = 1;
            if (m.typ == K1_KOM_DOK_ZMIANA) {
                zmiany++;
                continue;
            }
            w_kolejnosci &= m.obiekt == ostatni + 1;
            ostatni = m.obiekt;
            odebrane++;
            if (fd >= 0) {
                z_deskryptorem++;
                EXPECT(write(fd, "k", 1) == 1); /* the write end of the pipe, passed on */
                close(fd);
            }
        }
        if (!byl && z.n == 0)
            break;
        zal_oproznij(&z, p[0]);
    }
    EXPECT(odebrane == wyslane && zmiany == 1 && w_kolejnosci && z_deskryptorem == 1 && z.n == 0 && z.utracone == 0);
    EXPECT(read(rura[0], &c, 1) == 1 && c == 'k');
    close(rura[0]);
    close(p[0]);
    close(p[1]);
}

/* The client is gone (its channel closed) or ends: what waited goes with its descriptors. */
static void koniec_klienta(void)
{
    struct zalegle z;
    struct k1_komunikat m;
    int p[2], rura[2], i;
    char c;
    EXPECT(k1_kanal_utworz(p) == 0 && pipe(rura) == 0);
    zal_poczatek(&z);
    for (i = 0; i < 100000 && z.n == 0; i++) {
        m = wynik((uint32_t)i + 1);
        zal_wyslij(&z, p[0], &m, -1);
    }
    m = wynik(1);
    EXPECT(zal_wyslij(&z, p[0], &m, rura[1]) == 0 && z.n == 2);
    close(rura[1]);
    close(p[1]); /* the client is gone */
    EXPECT(zal_oproznij(&z, p[0]) == 0 && z.n == 0);
    EXPECT(read(rura[0], &c, 1) == 0); /* our copy of the descriptor was closed: end of the pipe */
    m = wynik(2);
    EXPECT(zal_wyslij(&z, p[0], &m, -1) == -1 && z.n == 0); /* nothing waits for a closed channel */
    close(rura[0]);
    close(p[0]);
    /* zal_wyczysc: the same for a client nadzorca closed */
    EXPECT(k1_kanal_utworz(p) == 0 && pipe(rura) == 0);
    zal_poczatek(&z);
    for (i = 0; i < 100000 && z.n == 0; i++) {
        m = wynik((uint32_t)i + 1);
        zal_wyslij(&z, p[0], &m, -1);
    }
    EXPECT(zal_wyslij(&z, p[0], &m, rura[1]) == 0);
    close(rura[1]);
    zal_wyczysc(&z);
    EXPECT(z.n == 0 && read(rura[0], &c, 1) == 0);
    close(rura[0]);
    close(p[0]);
    close(p[1]);
}

/* More than ZAL_MAKS waiting (cannot happen in skarbiec: it does not take requests of a client replies wait for): the
 * new one is lost and counted, what waits stays. */
static void pelna_kolejka(void)
{
    struct zalegle z;
    struct k1_komunikat m;
    int p[2], i;
    EXPECT(k1_kanal_utworz(p) == 0);
    zal_poczatek(&z);
    for (i = 0; i < 100000 && z.n < ZAL_MAKS; i++) {
        m = wynik((uint32_t)i + 1);
        EXPECT(zal_wyslij(&z, p[0], &m, -1) == 0);
    }
    EXPECT(z.n == ZAL_MAKS && z.utracone == 0);
    m = zmiana();
    EXPECT(zal_wyslij(&z, p[0], &m, -1) == -1 && z.utracone == 1 && z.n == ZAL_MAKS);
    zal_wyczysc(&z);
    close(p[0]);
    close(p[1]);
}

int main(void)
{
    od_razu();
    pelny_kanal();
    koniec_klienta();
    pelna_kolejka();
    return K1_TEST_KONIEC("test_skarb_zalegle");
}

/* K1 in-system test — skarbiec and its clients (0.9.0 step 2): a reply never goes lost when a client's channel is full.
 * The test program gets channels to skarbiec from nadzorca (TEST_SKARB) as the owner Pliki, itself (UID 0) the sender. */
#include <poll.h>

#include "k1_memfd.h"
#include "k1_test.h"

#define NAZWA_TESTU "test-zalegle"

/* A channel to skarbiec as owner `wlasciciel`, or -1. */
static int skarb(int wlasciciel)
{
    int fd;
    if (zapytaj(k1_komunikat_nowy(K1_KOM_TEST_SKARB, 0, wlasciciel, 0, 0, 0, 0), &fd, 10) != 0)
        return -1;
    return fd;
}

/* The next checked message from skarbiec within `ms`: 1, 0 when none came, -1 for a wrong one. */
static int od_skarbca(int fd, int ms, struct k1_komunikat *m)
{
    struct k1_odebrane o;
    int i;
    for (i = 0; i <= ms / 10; i++) {
        enum k1_kanal_wynik r = k1_kanal_odbierz(fd, &o);
        if (r == K1_KANAL_PUSTO) {
            if (i < ms / 10)
                msleep(10);
            continue;
        }
        if (o.fd >= 0)
            close(o.fd);
        return r == K1_KANAL_KOMUNIKAT && o.ma_nadawce && o.uid == K1_UID_SKARBIEC &&
                       k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, m) == 0
                   ? 1
                   : -1;
    }
    return 0;
}

/* DOK_WYNIK of request `typ` on `fd` (what else comes on the way — changes, the room — is skipped): its b (the result),
 * its c in *nowy; -100 when none came. */
static int wynik_dok(int fd, int typ, int32_t *nowy)
{
    struct k1_komunikat m;
    int r;
    while ((r = od_skarbca(fd, 10000, &m)) != 0)
        if (r > 0 && m.typ == K1_KOM_DOK_WYNIK && m.a == typ) {
            if (nowy != NULL)
                *nowy = m.c;
            return m.b;
        }
    return -100;
}

/* Sends to skarbiec on `fd`; the channel stays full for `ms` (skarbiec does not take more): 0. */
static int wyslij_czekaj(int fd, struct k1_komunikat m, int przekaz, int ms)
{
    struct pollfd p = { .fd = fd, .events = POLLOUT };
    for (;;) {
        if (k1_kanal_wyslij(fd, &m, sizeof m, przekaz) == 0)
            return 1;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS)
            return 0;
        if (poll(&p, 1, ms) <= 0)
            return 0;
    }
}

/* A folder of the user in Dokumenty, made by client `fd`: its number, or 0. */
static uint32_t nowy_folder(int fd, const char *nazwa)
{
    size_t n = strlen(nazwa);
    int mfd = k1_memfd_z_danymi("k1-test-nazwa", nazwa, n);
    int32_t id = 0;
    int ok = mfd >= 0 && wyslij_czekaj(fd, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER,
                                                             (int32_t)n, 0, 0, 0), mfd, 5000);
    if (mfd >= 0)
        close(mfd);
    return ok && wynik_dok(fd, K1_KOM_DOK_NOWY, &id) == 0 && id > 0 ? (uint32_t)id : 0;
}

/* A client stops reading (its channel fills up), meanwhile files change; when it reads again, it gets the reply to
 * every request it sent and the news that files changed (RYZYKA R64). skarbiec does not take its requests while
 * replies wait for it (it is busy), and serves the other clients as usual. */
void t_skarb_zalegle(void)
{
    struct k1_komunikat m, info = k1_komunikat_nowy(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0);
    int a = skarb(K1_PROFIL_PLIKI), b = skarb(K1_PROFIL_PLIKI), wyslane = 0, zatrzymane = 0, wyniki = 0, miejsca = 0,
        zmiany = 0, zle = 0, r;
    long pid = find_process("skarbiec");
    uint32_t folder = 0;
    if (a < 0 || b < 0) {
        check(0, "NEGATYWNY 0.9.0: skarbiec nie gubi odpowiedzi klienta, który przez chwilę nie odbiera", "kanały: %d %d",
              a, b);
        return;
    }
    /* a is busy: it only sends (each SKARB_INFO: WYNIK + MIEJSCE back), until skarbiec stops taking its requests */
    while (wyslane < 1500) {
        if (!wyslij_czekaj(a, info, -1, 3000)) {
            zatrzymane = 1;
            break;
        }
        wyslane++;
    }
    /* meanwhile b makes a folder: the change waits for a too */
    folder = nowy_folder(b, NAZWA_TESTU);
    /* a reads again */
    while ((r = od_skarbca(a, 3000, &m)) != 0) {
        if (r < 0)
            zle++;
        else if (m.typ == K1_KOM_SKARB_WYNIK && m.a == K1_KOM_SKARB_INFO && m.b == 0)
            wyniki++;
        else if (m.typ == K1_KOM_SKARB_MIEJSCE)
            miejsca++;
        else if (m.typ == K1_KOM_DOK_ZMIANA)
            zmiany++;
        else
            zle++;
    }
    check(zatrzymane && folder != 0 && wyniki == wyslane && miejsca == wyslane && zmiany >= 1 && zle == 0 &&
              find_process("skarbiec") == pid,
          "NEGATYWNY 0.9.0: klient skarbca, który przez chwilę nie odbiera, dostaje potem każdą odpowiedź i „pliki się "
          "zmieniły”; skarbiec wstrzymuje tylko jego prośby, inny klient działa",
          "wysłane=%d (wstrzymane=%d) odpowiedzi=%d miejsce=%d zmiany=%d złe=%d folder_innego=%u skarbiec %ld → %ld",
          wyslane, zatrzymane, wyniki, miejsca, zmiany, zle, folder, pid, find_process("skarbiec"));
    if (folder != 0) { /* the folder goes: into Kosz, then for good */
        wyslij_czekaj(b, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, folder, 0, 0, 0, 0, 0), -1, 5000);
        r = wynik_dok(b, K1_KOM_DOK_KOSZ, NULL);
        wyslij_czekaj(b, k1_komunikat_nowy(K1_KOM_DOK_USUN, folder, 0, 0, 0, 0, 0), -1, 5000);
        if (r != 0 || wynik_dok(b, K1_KOM_DOK_USUN, NULL) != 0)
            k1_log(TAG, "folder testowy %u: nie usunięto", folder);
    }
    close(a);
    close(b);
}

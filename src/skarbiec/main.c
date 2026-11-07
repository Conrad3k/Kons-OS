/* K1 skarbiec — the Blob Vault service (M5d, ADR 0015).
 *
 * The only process with access to the K1-DANE data partition (fd 4, opened by nadzorca; absent →
 * objects are kept in memory until power off). Clients (ekran: settings, Notatki: the note,
 * nadzorca: battery history) get channels from nadzorca, which tells us on the control channel
 * (fd 3, sender UID 0 checked) who the owner of each channel is; a client sees only the objects of
 * its owner. Data travels as sealed memfd (k1_memfd). Nothing of the content reaches the log.
 *
 * Trust boundary: everything from the disk (magazyn.c checks it) and from clients (k1_komunikat,
 * UID of every packet). Sandbox: UID 103, seccomp (base + pread/pwrite/fdatasync on the partition,
 * getrandom, memfd_create, sysinfo), Landlock without any path, no other devices.
 * Documentation: docs/komponenty/skarbiec.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_uslugi.h"
#include "magazyn.h"
#include "monocypher.h"

#define TAG "K1-SKARBIEC"
#define FD_STEROWANIE 3
#define FD_DYSK 4
#define MAKS_KLIENTOW 8
#define MAKS_RAM 32

struct klient {
    int fd, wlasciciel, uid;
};

struct obiekt_ram {
    int wlasciciel, id;
    uint32_t dl;
    uint8_t *dane;
};

static struct klient kl[MAKS_KLIENTOW];
static struct k1_magazyn mag;
static struct k1_mag_dysk dysk;
static int trwaly;     /* 1: the vault on the partition is open */
static int zablokowany; /* 1: a vault with a password, not opened yet (M5e) */
static int zle_proby;   /* wrong passwords in a row */
static double nastepna_proba; /* CLOCK_MONOTONIC: no attempt before this (grows after wrong ones) */
static struct obiekt_ram ram[MAKS_RAM];
static uint8_t bufor[K1_SKARB_MAKS];

static void nazwa(int id, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "o%d", id);
}

/* Argon2id memory: RAM/16, 16..256 MiB (ThinkPad X230 with 8 GB → 256 MiB). */
static uint32_t argon_kib(void)
{
    struct sysinfo si;
    unsigned long long kib;
    if (sysinfo(&si) != 0)
        return 16384;
    kib = (unsigned long long)si.totalram * si.mem_unit / 16 / 1024;
    if (kib < 16384)
        kib = 16384;
    if (kib > 262144)
        kib = 262144;
    return (uint32_t)kib;
}

static void otworz_magazyn(void)
{
    off_t rozmiar;
    int haslo, w;
    if (fcntl(FD_DYSK, F_GETFD) < 0) {
        k1_log(TAG, "brak partycji K1-DANE — dane tylko w pamięci (do wyłączenia komputera)");
        return;
    }
    rozmiar = lseek(FD_DYSK, 0, SEEK_END);
    if (rozmiar <= 0) {
        k1_log(TAG, "partycja K1-DANE: nieznany rozmiar — dane tylko w pamięci");
        return;
    }
    k1_mag_dysk_fd(&dysk, FD_DYSK, (uint64_t)rozmiar);
    w = k1_mag_sprawdz(&dysk, &haslo);
    if (w == K1_MAG_PUSTY) {
        w = k1_mag_utworz(&mag, &dysk, NULL, argon_kib());
        if (w == K1_MAG_OK) {
            trwaly = 1;
            k1_log(TAG, "utworzono nowy magazyn na pustej partycji K1-DANE (%lld MiB, zaszyfrowany, bez hasła)",
                   (long long)(rozmiar >> 20));
        } else {
            k1_log(TAG, "nie udało się utworzyć magazynu: %s — dane tylko w pamięci", k1_mag_opis(w));
        }
        return;
    }
    if (w == K1_MAG_JEST && haslo) {
        zablokowany = 1;
        k1_log(TAG, "magazyn chroniony hasłem — czekam na hasło z ekranu logowania");
        return;
    }
    if (w == K1_MAG_JEST) {
        w = k1_mag_otworz(&mag, &dysk, NULL);
        if (w == K1_MAG_OK) {
            trwaly = 1;
            k1_log(TAG, "magazyn otwarty: %d obiektów", mag.n_wpisow);
        } else {
            k1_log(TAG, "magazyn nie otwarty: %s — partycja zostaje nietknięta, dane tylko w pamięci",
                   k1_mag_opis(w));
        }
        return;
    }
    if (w == K1_MAG_OBCY)
        k1_log(TAG, "partycja K1-DANE zawiera nieznane dane — nie nadpisuję; dane tylko w pamięci");
    else
        k1_log(TAG, "partycja K1-DANE: %s — dane tylko w pamięci", k1_mag_opis(w));
}

static int tryb(void)
{
    return trwaly ? K1_SKARB_TRWALY : (zablokowany ? K1_SKARB_ZABLOKOWANY : K1_SKARB_RAM);
}

static int haslo_ustawione(void)
{
    return zablokowany || (trwaly && mag.haslo);
}

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int sekund_do_proby(void)
{
    double d = nastepna_proba - teraz();
    return d <= 0 ? 0 : (int)d + 1;
}

/* After a wrong password: from the third one on, 5 s, 10 s, 20 s … up to 5 minutes. */
static void zla_proba(void)
{
    int n = ++zle_proby;
    double przerwa = n < 3 ? 0 : 5.0 * (double)(1u << (n - 3 < 6 ? n - 3 : 6));
    if (przerwa > K1_HASLO_PRZERWA)
        przerwa = K1_HASLO_PRZERWA;
    nastepna_proba = teraz() + przerwa;
    k1_log(TAG, "złe hasło (%d. raz z rzędu)%s", n, przerwa > 0 ? " — następna próba za chwilę" : "");
}

/* Tells nadzorca the state of the vault (it starts the login screen or the desktop accordingly). */
static void zglos_stan(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_STAN, 0, tryb(), haslo_ustawione(), 0, 0, 0);
    if (k1_kanal_wyslij(FD_STEROWANIE, &m, sizeof m, -1) != 0)
        k1_log(TAG, "zgłoszenie stanu do nadzorcy: %s", strerror(errno));
}

/* ---- objects in memory (no partition) ---- */

static struct obiekt_ram *ram_znajdz(int wl, int id, int utworz)
{
    int i, wolny = -1;
    for (i = 0; i < MAKS_RAM; i++) {
        if (ram[i].id == id && ram[i].wlasciciel == wl)
            return &ram[i];
        if (ram[i].id == 0 && wolny < 0)
            wolny = i;
    }
    if (!utworz || wolny < 0)
        return NULL;
    ram[wolny].wlasciciel = wl;
    ram[wolny].id = id;
    return &ram[wolny];
}

static int czytaj_obiekt(int wl, int id, uint32_t *dl)
{
    char n[K1_MAG_NAZWA];
    struct obiekt_ram *o;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly) {
        nazwa(id, n);
        return k1_mag_czytaj(&mag, (uint16_t)wl, n, bufor, sizeof bufor, dl);
    }
    o = ram_znajdz(wl, id, 0);
    if (o == NULL)
        return K1_MAG_BRAK;
    memcpy(bufor, o->dane, o->dl);
    *dl = o->dl;
    return K1_MAG_OK;
}

static int zapisz_obiekt(int wl, int id, uint32_t dl)
{
    char n[K1_MAG_NAZWA];
    struct obiekt_ram *o;
    uint8_t *kopia;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly) {
        nazwa(id, n);
        return k1_mag_zapisz(&mag, (uint16_t)wl, n, bufor, dl);
    }
    o = ram_znajdz(wl, id, 1);
    if (o == NULL)
        return K1_MAG_PELNY;
    kopia = malloc(dl ? dl : 1);
    if (kopia == NULL)
        return K1_MAG_PAMIEC;
    memcpy(kopia, bufor, dl);
    free(o->dane);
    o->dane = kopia;
    o->dl = dl;
    return K1_MAG_OK;
}

static int usun_obiekt(int wl, int id)
{
    char n[K1_MAG_NAZWA];
    struct obiekt_ram *o;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly) {
        nazwa(id, n);
        return k1_mag_usun(&mag, (uint16_t)wl, n);
    }
    o = ram_znajdz(wl, id, 0);
    if (o == NULL)
        return K1_MAG_BRAK;
    free(o->dane);
    memset(o, 0, sizeof *o);
    return K1_MAG_OK;
}

/* ---- channels ---- */

static void odpowiedz(struct klient *k, int id, int typ, int wynik)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_WYNIK, (uint32_t)id, typ, wynik, tryb(), haslo_ustawione(),
                                              sekund_do_proby());
    k1_kanal_wyslij(k->fd, &m, sizeof m, -1);
}

static void zamknij_klienta(struct klient *k)
{
    close(k->fd);
    k->fd = -1;
}

static void obsluz_sterowanie(void)
{
    for (int n = 0; n < 16; n++) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        enum k1_kanal_wynik w = k1_kanal_odbierz(FD_STEROWANIE, &o);
        int i;
        if (w == K1_KANAL_PUSTO)
            return;
        if (w != K1_KANAL_KOMUNIKAT)
            k1_koniec(TAG, "nadzorca zamknął kanał sterujący");
        if (o.obciete || !o.ma_nadawce || o.uid != 0 ||
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_STEROWANIE, &m) != 0 || m.typ != K1_KOM_SKARB_KLIENT ||
            o.fd < 0) {
            if (o.fd >= 0)
                close(o.fd);
            k1_log(TAG, "kanał sterujący: odrzucono komunikat (nadawca uid %d)", o.ma_nadawce ? (int)o.uid : -1);
            continue;
        }
        for (i = 0; i < MAKS_KLIENTOW && kl[i].fd >= 0; i++)
            ;
        if (i == MAKS_KLIENTOW) {
            close(o.fd);
            k1_log(TAG, "za dużo klientów");
            continue;
        }
        if (fcntl(o.fd, F_SETFL, O_NONBLOCK) != 0) {
            close(o.fd);
            continue;
        }
        kl[i].fd = o.fd;
        kl[i].wlasciciel = m.a;
        kl[i].uid = m.b;
        k1_log(TAG, "nowy klient: właściciel %d (uid %d)", m.a, m.b);
    }
}

/* A password from a sealed memfd: 1..K1_HASLO_MAKS bytes, no NUL inside → NUL-terminated in `h`. */
static int czytaj_haslo(int fd, size_t dl, char *h)
{
    if (dl < 1 || dl > K1_HASLO_MAKS || k1_memfd_czytaj(fd, h, dl) != 0 || memchr(h, '\0', dl) != NULL)
        return -1;
    h[dl] = '\0';
    return 0;
}

/* Login screen: unlock the vault with a password (rate-limited here, whatever the client does). */
static int odblokuj(int fd, size_t dl)
{
    char h[K1_HASLO_MAKS + 1];
    int r;
    if (!zablokowany)
        return trwaly ? K1_MAG_OK : K1_MAG_ARG;
    if (sekund_do_proby() > 0)
        return K1_MAG_HASLO;
    if (czytaj_haslo(fd, dl, h) != 0)
        return K1_MAG_ARG;
    r = k1_mag_otworz(&mag, &dysk, h);
    crypto_wipe(h, sizeof h);
    if (r == K1_MAG_OK) {
        zablokowany = 0;
        trwaly = 1;
        zle_proby = 0;
        k1_log(TAG, "magazyn odblokowany: %d obiektów", mag.n_wpisow);
        zglos_stan();
    } else if (r == K1_MAG_HASLO) {
        zla_proba();
    } else {
        k1_log(TAG, "odblokowanie: %s", k1_mag_opis(r));
    }
    return r;
}

/* Ustawienia: set, change or remove the password; the current one must be given (if there is one). */
static int zmien_haslo(int fd, size_t dl)
{
    char b[2 * K1_HASLO_MAKS + 2], *nowe;
    size_t n1, n2;
    int r;
    if (!trwaly)
        return zablokowany ? K1_MAG_HASLO : K1_MAG_ARG;
    if (sekund_do_proby() > 0)
        return K1_MAG_HASLO;
    if (dl < 2 || dl > sizeof b || k1_memfd_czytaj(fd, b, dl) != 0 || b[dl - 1] != '\0')
        return K1_MAG_ARG;
    n1 = strnlen(b, dl);
    if (n1 >= dl - 1 || n1 > K1_HASLO_MAKS)
        return K1_MAG_ARG;
    nowe = b + n1 + 1;
    n2 = strnlen(nowe, dl - n1 - 1);
    if (n1 + 1 + n2 + 1 != dl || n2 > K1_HASLO_MAKS || (n2 > 0 && n2 < 4)) {
        crypto_wipe(b, sizeof b);
        return K1_MAG_ARG;
    }
    r = k1_mag_sprawdz_haslo(&mag, n1 ? b : NULL);
    if (r == K1_MAG_OK) {
        zle_proby = 0;
        r = k1_mag_zmien_haslo(&mag, n2 ? nowe : NULL);
        k1_log(TAG, "%s", r != K1_MAG_OK ? "zmiana hasła nieudana" : n2 ? "hasło ustawione" : "hasło wyłączone");
    } else if (r == K1_MAG_HASLO) {
        zla_proba();
    }
    crypto_wipe(b, sizeof b);
    return r;
}

static void obsluz_klienta(struct klient *k)
{
    for (int n = 0; n < 16 && k->fd >= 0; n++) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        enum k1_kanal_wynik w = k1_kanal_odbierz(k->fd, &o);
        uint32_t dl;
        int r;
        if (w == K1_KANAL_PUSTO)
            return;
        if (w != K1_KANAL_KOMUNIKAT) {
            zamknij_klienta(k);
            return;
        }
        if (o.obciete || !o.ma_nadawce || (int)o.uid != k->uid ||
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_DO_SKARBCA, &m) != 0 ||
            (o.fd >= 0) != k1_komunikat_z_deskryptorem(m.typ, K1_KIER_DO_SKARBCA)) {
            if (o.fd >= 0)
                close(o.fd);
            k1_log(TAG, "odrzucono komunikat od klienta (właściciel %d, nadawca uid %d)", k->wlasciciel,
                   o.ma_nadawce ? (int)o.uid : -1);
            continue;
        }
        switch (m.typ) {
        case K1_KOM_SKARB_CZYTAJ:
            r = czytaj_obiekt(k->wlasciciel, (int)m.obiekt, &dl);
            if (r == K1_MAG_OK) {
                int fd = k1_memfd_z_danymi("k1-obiekt", bufor, dl);
                struct k1_komunikat d = k1_komunikat_nowy(K1_KOM_SKARB_DANE, m.obiekt, (int32_t)dl, 0, 0, 0, 0);
                if (fd < 0) {
                    odpowiedz(k, (int)m.obiekt, m.typ, K1_MAG_PAMIEC);
                    break;
                }
                k1_kanal_wyslij(k->fd, &d, sizeof d, fd);
                close(fd);
            } else {
                odpowiedz(k, (int)m.obiekt, m.typ, r);
            }
            break;
        case K1_KOM_SKARB_ZAPISZ:
            if (k1_memfd_czytaj(o.fd, bufor, (size_t)m.a) != 0) {
                close(o.fd);
                k1_log(TAG, "odrzucono zapis: deskryptor nie jest zapieczętowanym memfd o podanym rozmiarze");
                odpowiedz(k, (int)m.obiekt, m.typ, K1_MAG_ARG);
                break;
            }
            close(o.fd);
            r = zapisz_obiekt(k->wlasciciel, (int)m.obiekt, (uint32_t)m.a);
            if (r != K1_MAG_OK)
                k1_log(TAG, "zapis obiektu %u (właściciel %d): %s", m.obiekt, k->wlasciciel, k1_mag_opis(r));
            else
                k1_log(TAG, "zapisano obiekt %u właściciela %d (%d B, %s)", m.obiekt, k->wlasciciel, (int)m.a,
                       trwaly ? "na dysku" : "w pamięci");
            odpowiedz(k, (int)m.obiekt, m.typ, r);
            break;
        case K1_KOM_SKARB_USUN:
            odpowiedz(k, (int)m.obiekt, m.typ, usun_obiekt(k->wlasciciel, (int)m.obiekt));
            break;
        case K1_KOM_SKARB_ODBLOKUJ: /* only the login screen */
            r = k->wlasciciel == K1_PROFIL_LOGOWANIE ? odblokuj(o.fd, (size_t)m.a) : K1_MAG_ARG;
            close(o.fd);
            odpowiedz(k, 1, m.typ, r);
            break;
        case K1_KOM_SKARB_HASLO: /* only Ustawienia */
            r = k->wlasciciel == K1_PROFIL_USTAWIENIA ? zmien_haslo(o.fd, (size_t)m.a) : K1_MAG_ARG;
            close(o.fd);
            odpowiedz(k, 1, m.typ, r);
            break;
        case K1_KOM_SKARB_INFO:
            odpowiedz(k, 1, m.typ, K1_MAG_OK);
            break;
        default:
            break;
        }
    }
}

int main(void)
{
    struct pollfd pfd[1 + MAKS_KLIENTOW];
    int i;

    k1_klatka_w_usludze(TAG);
    if (fcntl(FD_STEROWANIE, F_SETFL, O_NONBLOCK) != 0)
        k1_koniec(TAG, "brak kanału sterującego (fd %d)", FD_STEROWANIE);
    for (i = 0; i < MAKS_KLIENTOW; i++)
        kl[i].fd = -1;
    otworz_magazyn();
    k1_log(TAG, "gotowy (%s)", trwaly ? "dane na dysku" : zablokowany ? "zablokowany hasłem" : "dane tylko w pamięci");
    zglos_stan();
    for (;;) {
        pfd[0].fd = FD_STEROWANIE;
        pfd[0].events = POLLIN;
        for (i = 0; i < MAKS_KLIENTOW; i++) {
            pfd[1 + i].fd = kl[i].fd;
            pfd[1 + i].events = POLLIN;
        }
        if (poll(pfd, 1 + MAKS_KLIENTOW, -1) < 0) {
            if (errno == EINTR)
                continue;
            k1_koniec(TAG, "poll: %s", strerror(errno));
        }
        if (pfd[0].revents)
            obsluz_sterowanie();
        for (i = 0; i < MAKS_KLIENTOW; i++)
            if (kl[i].fd >= 0 && pfd[1 + i].revents)
                obsluz_klienta(&kl[i]);
    }
}

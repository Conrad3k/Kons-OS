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
 * Since 0.8.0 also the user's files and folders (owner K1_WL_DOKUMENTY, ADR 0029: dokumenty.c, dok_kanal.c); since
 * 0.8.1 the room for files and K1-DANE to the end of its stick (miejsce.c, ADR 0033).
 * Modules: skarbiec.h. Documentation: docs/komponenty/skarbiec.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "skarbiec.h"
#include "zalegle.h"

_Static_assert(K1_MAG_HASLO == K1_SKARB_ZLE_HASLO, "K1_SKARB_ZLE_HASLO must match K1_MAG_HASLO");
_Static_assert(K1_MAG_PELNY == K1_SKARB_PELNY, "K1_SKARB_PELNY must match K1_MAG_PELNY");
_Static_assert(K1_MAG_IO == K1_SKARB_IO && K1_MAG_ARG == K1_SKARB_ARG, "K1_SKARB_IO/ARG must match K1_MAG_IO/ARG");

#define FD_STEROWANIE 3
#define FD_DYSK 4
#define MAKS_KLIENTOW 16
#define GODZINA 3600.0

struct klient {
    int fd, wlasciciel, uid;
    struct zalegle zal; /* replies its channel did not take yet (0.9.0 step 2) */
};

static struct klient kl[MAKS_KLIENTOW];
static uint8_t bufor[K1_SKARB_MAKS];
static int na_usb; /* K1-DANE is on a USB stick (from nadzorca's SKARB_KLIENT) */
static struct klient *powieksza; /* the client (Ustawienia) waiting for K1-DANE to grow (0.8.1) */

/* Objects of applications: numbers 1..K1_OB_MAKS, kept under the name "o<number>". */
static void nazwa(int id, char out[K1_MAG_NAZWA])
{
    snprintf(out, K1_MAG_NAZWA, "o%d", id);
}

/* Tells nadzorca the state of the vault (it starts the login screen or the desktop accordingly). */
void zglos_stan(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_STAN, 0, ob_tryb(), haslo_ustawione(), 0, 0, 0);
    if (k1_kanal_wyslij(FD_STEROWANIE, &m, sizeof m, -1) != 0)
        k1_log(TAG, "zgłoszenie stanu do nadzorcy: %s", strerror(errno));
}

void do_nadzorcy(struct k1_komunikat m)
{
    do_nadzorcy_z(m, -1);
}

void do_nadzorcy_z(struct k1_komunikat m, int fd)
{
    if (k1_kanal_wyslij(FD_STEROWANIE, &m, sizeof m, fd) != 0)
        k1_log(TAG, "komunikat do nadzorcy: %s", strerror(errno));
}

/* Every message to a client goes here (dok_kanal.c, dostep.c, miejsce.c too): when its channel is full, it waits in
 * the client's queue (zalegle.h) and goes later, in order — a reply is never lost (0.9.0 step 2, RYZYKA R64). */
void do_klienta(int fd, struct k1_komunikat m, int przekaz)
{
    for (int i = 0; i < MAKS_KLIENTOW; i++)
        if (kl[i].fd == fd && fd >= 0) {
            unsigned long przed = kl[i].zal.utracone;
            if (zal_wyslij(&kl[i].zal, fd, &m, przekaz) != 0 && kl[i].zal.utracone != przed)
                k1_log(TAG, "kolejka klienta pełna (właściciel %d): utracono odpowiedź, razem %lu", kl[i].wlasciciel,
                       kl[i].zal.utracone);
            return;
        }
    k1_kanal_wyslij(fd, &m, sizeof m, przekaz); /* not a client's channel (none such today): as before */
}

static void odpowiedz(struct klient *k, int id, int typ, int wynik)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_WYNIK, (uint32_t)id, typ, wynik, ob_tryb(),
                                              haslo_ustawione() | (trwaly && na_usb ? 2 : 0), sekund_do_proby());
    do_klienta(k->fd, m, -1);
}

static void zamknij_klienta(struct klient *k)
{
    int ostatni = 1;
    for (int i = 0; i < MAKS_KLIENTOW; i++)
        if (&kl[i] != k && kl[i].fd >= 0 && kl[i].wlasciciel == k->wlasciciel)
            ostatni = 0;
    dostep_koniec_klienta(k->fd, k->wlasciciel, ostatni);
    dok_koniec_kanalu(k->fd);
    if (powieksza == k)
        powieksza = NULL;
    zal_wyczysc(&k->zal);
    close(k->fd);
    k->fd = -1;
}

/* Files changed: every client allowed to list them asks again. */
static void oglos_zmiane(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0);
    for (int i = 0; i < MAKS_KLIENTOW; i++)
        if (kl[i].fd >= 0 && dok_moze_listowac(kl[i].wlasciciel))
            do_klienta(kl[i].fd, m, -1); /* waits at most once (zalegle.h) */
}

/* nadzorca's answer to POWIEKSZ_DANE: the vault takes the grown partition, the client that asked is told. */
static void powiekszono(int wynik)
{
    int r = miejsce_powiekszono(wynik);
    if (r == K1_MAG_OK)
        oglos_zmiane(); /* Pliki ask for the list again: the room comes with it */
    if (powieksza != NULL) {
        odpowiedz(powieksza, 1, K1_KOM_SKARB_POWIEKSZ, r);
        miejsce_wyslij(powieksza->fd);
        powieksza = NULL;
    }
}

static void nowy_klient(const struct k1_komunikat *m, int fd)
{
    int i;
    for (i = 0; i < MAKS_KLIENTOW && kl[i].fd >= 0; i++)
        ;
    if (i == MAKS_KLIENTOW || fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        k1_log(TAG, "za dużo klientów");
        return;
    }
    kl[i].fd = fd;
    zal_poczatek(&kl[i].zal);
    kl[i].wlasciciel = m->a;
    kl[i].uid = m->b;
    na_usb = m->c;      /* where the K1-DANE is (nadzorca knows; shown in Ustawienia) */
    miejsce_zapas(m->d); /* and how much more the stick has (0.8.1) */
    k1_log(TAG, "nowy klient: właściciel %d (uid %d)", m->a, m->b);
    dostep_nowy_klient(kl[i].fd, kl[i].wlasciciel);
}

static void obsluz_sterowanie(void)
{
    for (int n = 0; n < 16; n++) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        enum k1_kanal_wynik w = k1_kanal_odbierz(FD_STEROWANIE, &o);
        if (w == K1_KANAL_PUSTO)
            return;
        if (w != K1_KANAL_KOMUNIKAT)
            k1_koniec(TAG, "nadzorca zamknął kanał sterujący");
        if (o.obciete || !o.ma_nadawce || o.uid != 0 ||
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_STEROWANIE, &m) != 0 ||
            (m.typ != K1_KOM_SKARB_KLIENT && m.typ != K1_KOM_POWIEKSZONO &&
             m.typ != K1_KOM_ZEGAR_PRZESUNIETY && m.typ != K1_KOM_SKARB_WYCZYSC && m.typ != K1_KOM_SKARB_ZWOLNIJ) ||
            (o.fd >= 0) != k1_komunikat_z_deskryptorem(m.typ, K1_KIER_STEROWANIE)) {
            if (o.fd >= 0)
                close(o.fd);
            k1_log(TAG, "kanał sterujący: odrzucono komunikat (nadawca uid %d)", o.ma_nadawce ? (int)o.uid : -1);
            continue;
        }
        if (m.typ == K1_KOM_POWIEKSZONO)
            powiekszono(m.a);
        else if (m.typ == K1_KOM_ZEGAR_PRZESUNIETY)
            dok_zegar_z_internetu((int64_t)k1_kom_u64(m.a, m.b));
        else if (m.typ == K1_KOM_SKARB_WYCZYSC) /* 0.10.0 step 5: "Wyczyść dane sesji" */
            strefy_wyczysc(m.a);
        else if (m.typ == K1_KOM_SKARB_ZWOLNIJ) /* 0.10.10 step 1: the Instalator installs anew on this disk */
            ob_zwolnij(FD_DYSK), do_nadzorcy(k1_komunikat_nowy(K1_KOM_SKARB_ZWOLNIONO, 0, 0, 0, 0, 0, 0));
        else
            nowy_klient(&m, o.fd);
    }
}

static void czytaj_obiekt(struct klient *k, uint32_t id)
{
    char n[K1_MAG_NAZWA];
    uint32_t dl;
    int r, fd;
    struct k1_komunikat d;
    nazwa((int)id, n);
    r = ob_czytaj(k->wlasciciel, n, bufor, sizeof bufor, &dl);
    if (r != K1_MAG_OK) {
        odpowiedz(k, (int)id, K1_KOM_SKARB_CZYTAJ, r);
        return;
    }
    fd = k1_memfd_z_danymi("k1-obiekt", bufor, dl);
    if (fd < 0) {
        odpowiedz(k, (int)id, K1_KOM_SKARB_CZYTAJ, K1_MAG_PAMIEC);
        return;
    }
    d = k1_komunikat_nowy(K1_KOM_SKARB_DANE, id, (int32_t)dl, 0, 0, 0, 0);
    do_klienta(k->fd, d, fd);
    close(fd);
}

static void zapisz_obiekt(struct klient *k, const struct k1_komunikat *m, int fd)
{
    char n[K1_MAG_NAZWA];
    int r;
    if (k1_memfd_czytaj(fd, bufor, (size_t)m->a) != 0) {
        k1_log(TAG, "odrzucono zapis: deskryptor nie jest zapieczętowanym memfd o podanym rozmiarze");
        odpowiedz(k, (int)m->obiekt, m->typ, K1_MAG_ARG);
        return;
    }
    nazwa((int)m->obiekt, n);
    r = strefy_miejsce_na(m->obiekt, (uint32_t)m->a) ? ob_zapisz(k->wlasciciel, n, bufor, (uint32_t)m->a) : K1_MAG_PELNY;
    if (r != K1_MAG_OK)
        k1_log(TAG, "zapis obiektu %u (właściciel %d): %s", m->obiekt, k->wlasciciel, k1_mag_opis(r));
    else if (m->obiekt < K1_OB_DYSK) /* the pieces of a zone's disk: too many to log each */
        k1_log(TAG, "zapisano obiekt %u właściciela %d (%d B, %s)", m->obiekt, k->wlasciciel, (int)m->a,
               trwaly ? "na dysku" : "w pamięci");
    odpowiedz(k, (int)m->obiekt, m->typ, r);
}

/* One checked message of a client (its attached descriptor, if any, is closed here). */
static void wykonaj(struct klient *k, const struct k1_komunikat *m, int fd)
{
    char n[K1_MAG_NAZWA];
    if (k1_kom_dok_prosba(m->typ)) {
        if (dok_obsluz(k->fd, k->wlasciciel, m, fd))
            oglos_zmiane();
        return;
    }
    if ((m->typ == K1_KOM_SKARB_CZYTAJ || m->typ == K1_KOM_SKARB_ZAPISZ || m->typ == K1_KOM_SKARB_USUN) &&
        !strefy_wolno(k->wlasciciel, m->obiekt)) { /* a piece of a zone's disk: only a zone's VMM (0.10.0 step 5) */
        if (fd >= 0)
            close(fd);
        k1_log(TAG, "odrzucono: obiekt %u dysku strefy, a właściciel %d nie jest strefą", m->obiekt, k->wlasciciel);
        odpowiedz(k, (int)m->obiekt, m->typ, K1_MAG_ARG);
        return;
    }
    switch (m->typ) {
    case K1_KOM_SKARB_CZYTAJ:
        czytaj_obiekt(k, m->obiekt);
        break;
    case K1_KOM_SKARB_ZAPISZ:
        zapisz_obiekt(k, m, fd);
        break;
    case K1_KOM_SKARB_USUN:
        nazwa((int)m->obiekt, n);
        odpowiedz(k, (int)m->obiekt, m->typ, ob_usun(k->wlasciciel, n));
        break;
    case K1_KOM_SKARB_ODBLOKUJ: /* only the login screen */
        odpowiedz(k, 1, m->typ, k->wlasciciel == K1_PROFIL_LOGOWANIE ? haslo_odblokuj(fd, (size_t)m->a) : K1_MAG_ARG);
        break;
    case K1_KOM_SKARB_HASLO: /* only gui's security screen (0.13.0 step 6, ADR 0061; until then Ustawienia) */
        odpowiedz(k, 1, m->typ, k->wlasciciel == K1_PROFIL_GUI ? haslo_zmien(fd, (size_t)m->a) : K1_MAG_ARG);
        break;
    case K1_KOM_SKARB_INFO:
        odpowiedz(k, 1, m->typ, K1_MAG_OK);
        miejsce_wyslij(k->fd);
        break;
    case K1_KOM_SKARB_PODZIAL: /* only Ustawienia (0.8.5 step 3): numbers, never a name */
        odpowiedz(k, 1, m->typ, miejsce_podzial(k->fd, k->wlasciciel));
        break;
    case K1_KOM_SKARB_POWIEKSZ: /* only Ustawienia: the user clicked "Użyj całego pendrive'a" (ADR 0033) */
        if (k->wlasciciel == K1_PROFIL_USTAWIENIA && powieksza == NULL && miejsce_powieksz() == K1_MAG_OK) {
            powieksza = k;
            break;
        }
        odpowiedz(k, 1, m->typ, K1_MAG_ARG);
        miejsce_wyslij(k->fd);
        break;
    default:
        break;
    }
    if (fd >= 0)
        close(fd);
}

/* Up to 16 requests of a client — none while replies wait for it (it is busy: its requests wait in its channel). */
static void obsluz_klienta(struct klient *k)
{
    for (int n = 0; n < 16 && k->fd >= 0 && k->zal.n == 0; n++) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        enum k1_kanal_wynik w = k1_kanal_odbierz(k->fd, &o);
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
        double t0 = k1_teraz();
        wykonaj(k, &m, o.fd);
        przerwa_trwala(k1_teraz() - t0);
    }
}

int main(void)
{
    struct pollfd pfd[1 + MAKS_KLIENTOW];
    double kosz_kontrola, cisza;
    int i, n;

    k1_klatka_w_usludze(TAG);
    if (fcntl(FD_STEROWANIE, F_SETFL, O_NONBLOCK) != 0)
        k1_koniec(TAG, "brak kanału sterującego (fd %d)", FD_STEROWANIE);
    for (i = 0; i < MAKS_KLIENTOW; i++)
        kl[i].fd = -1;
    ob_otworz_magazyn(FD_DYSK);
    if (!zablokowany)
        dok_po_otwarciu();
    k1_log(TAG, "gotowy (%s)", trwaly ? "dane na dysku" : zablokowany ? "zablokowany hasłem" : "dane tylko w pamięci");
    zglos_stan();
    kosz_kontrola = k1_teraz() + GODZINA;
    for (;;) {
        double t;
        pfd[0].fd = FD_STEROWANIE;
        pfd[0].events = POLLIN;
        for (i = 0; i < MAKS_KLIENTOW; i++) {
            pfd[1 + i].fd = kl[i].fd;
            pfd[1 + i].events = kl[i].zal.n > 0 ? POLLOUT : POLLIN; /* replies wait: only when it takes them */
        }
        t = kosz_kontrola - k1_teraz();
        cisza = przerwa_czeka(); /* compaction and the transition wait for a quiet moment (przerwy.c) */
        if (cisza >= 0 && t > cisza)
            t = cisza;
        n = poll(pfd, 1 + MAKS_KLIENTOW, t <= 0 ? 0 : (int)(t * 1000) + 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            k1_koniec(TAG, "poll: %s", strerror(errno));
        }
        if (cisza >= 0 && (n == 0 || cisza == 0) && przerwa_praca())
            oglos_zmiane(); /* format 2 now: Pliki ask for the list again, the room comes with it */
        if (pfd[0].revents)
            obsluz_sterowanie();
        for (i = 0; i < MAKS_KLIENTOW; i++)
            if (kl[i].fd >= 0 && pfd[1 + i].revents) {
                if (kl[i].zal.n > 0)
                    zal_oproznij(&kl[i].zal, kl[i].fd);
                obsluz_klienta(&kl[i]); /* nothing while replies still wait; a closed channel ends here */
            }
        epoka_sprawdz(); /* 0.13.0 step 2: the first write of this opening, for the TPM */
        if (k1_teraz() >= kosz_kontrola) { /* Kosz: older than 30 days goes for good */
            kosz_kontrola = k1_teraz() + GODZINA;
            if (dok_co_godzine())
                oglos_zmiane();
        }
    }
}

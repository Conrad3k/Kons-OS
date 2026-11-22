/* K1 ekran — clients and their windows: focus, minimising, settings, and the validated messages of the clients (every
 * packet: sender UID, every field, role-dependent policy). Split out of main.c in M4.1; the surfaces themselves (memory,
 * a new one, committing) in powierzchnie.c since 0.9.0 step 9. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "k1_zdarzenie.h"

/* ---- clients ---- */

/* A client busy drawing may not read for a while: when its channel is full, the message waits in zal (merged and,
 * for a client that does not read at all, dropped by the rules of zalegle.h) and goes on POLLOUT, in order. */
void wyslij(int kl, struct k1_komunikat m)
{
    struct klient *k;
    if (kl < 0 || s.kl[kl].fd < 0)
        return;
    k = &s.kl[kl];
    if (k->zal.n == 0 && k1_kanal_wyslij(k->fd, &m, sizeof m, -1) == 0)
        return;
    if (k->zal.n == 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        return; /* the channel is broken: POLLHUP disconnects the client */
    zal_dodaj(&k->zal, &m);
}

void wyslij_zalegle(int kl)
{
    struct klient *k = &s.kl[kl];
    const struct k1_komunikat *m;
    while (k->fd >= 0 && (m = zal_pierwszy(&k->zal)) != NULL && k1_kanal_wyslij(k->fd, m, sizeof *m, -1) == 0)
        zal_zdejmij(&k->zal);
}

void loguj_zalegle(void)
{
    static double t_log;
    double t = k1_teraz();
    int i;
    if (t - t_log < 0.5)
        return;
    for (i = 0; i < MAKS_KLIENTOW; i++) {
        struct klient *k = &s.kl[i];
        if (k->fd >= 0 && k->zal.porzucone + k->zal.wazne != k->zal_log) {
            k1_log(TAG, "klient %s nie odbiera: porzucono %lu ruchów i klawiszy, %lu innych (czeka %d)", nazwa_roli(k),
                   k->zal.porzucone, k->zal.wazne, k->zal.n);
            k->zal_log = k->zal.porzucone + k->zal.wazne;
            t_log = t;
            return;
        }
    }
}

int powloka(void)
{
    int i;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_POWLOKA)
            return i;
    return -1;
}

/* The boot password screen (M5e, role LOGOWANIE): while it is connected it gets every key. */
int logowanie(void)
{
    int i;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_LOGOWANIE)
            return i;
    return -1;
}

int panele(const struct klient *k)
{
    return k->rola == K1_ROLA_POWLOKA || k->rola == K1_ROLA_LOGOWANIE;
}

const char *nazwa_roli(const struct klient *k)
{
    const char *apl = k1_profil_program(k1_apl_profil(k->apl));
    if (k->rola == K1_ROLA_WEJSCIE)
        return "wejście";
    if (k->rola == K1_ROLA_POWLOKA)
        return "powłoka";
    if (k->rola == K1_ROLA_LOGOWANIE)
        return "logowanie";
    if (k->rola == K1_ROLA_STREFA)
        return "strefa";
    if (k->rola == K1_ROLA_TEST)
        return "test";
    return apl != NULL ? apl : "?";
}

void odrzuc(int kl, const char *powod)
{
    struct klient *k = &s.kl[kl];
    k->odrzucone++;
    s.odrzucone++;
    if (k->zalogowane < 8) {
        k->zalogowane++;
        k1_log(TAG, "odrzucono komunikat (%s): %s", nazwa_roli(k), powod);
    }
}

/* Who changes which settings: Ustawienia every one; the default application of a file type also Pliki and the desktop
 * ("Zawsze otwieraj tą aplikacją" in "Otwórz za pomocą", 0.8.2 step 5). */
static int ustawia(const struct klient *k, int32_t klucz)
{
    int otwieranie = klucz >= K1_UST_OTWIERANIE && klucz < K1_UST_OPIS;
    if (k->rola == K1_ROLA_APLIKACJA && k->apl == K1_APL_USTAWIENIA)
        return 1;
    return otwieranie && (k->rola == K1_ROLA_POWLOKA || (k->rola == K1_ROLA_APLIKACJA && k->apl == K1_APL_PLIKI));
}

void zglos_okno(const struct pow *p, int stan)
{
    int sh = powloka();
    if (sh >= 0 && p->okno != 0)
        wyslij(sh, okno_strefy(p) ? k1_komunikat_nowy(K1_KOM_OKNO, p->okno, 0, stan, s.kl[p->kl].apl, 0, 0)
                                  : k1_komunikat_nowy(K1_KOM_OKNO, p->okno, s.kl[p->kl].apl, stan, 0, 0, 0));
}

void ustaw_fokus(int i)
{
    if (i == s.fokus)
        return;
    pelny_fokus(i); /* 0.11.0 step 1: another window — the full screen ends */
    if (s.fokus >= 0 && s.p[s.fokus].uzyta) {
        struct pow *p = &s.p[s.fokus];
        wyslij(p->kl, k1_komunikat_nowy(K1_KOM_FOKUS, p->id, 0, 0, 0, 0, 0));
        if (!p->zmin)
            zglos_okno(p, K1_OKNO_NIEAKTYWNE);
        uszkodz_pow(p);
    }
    s.fokus = i;
    if (i >= 0) {
        struct pow *p = &s.p[i];
        wyslij(p->kl, k1_komunikat_nowy(K1_KOM_FOKUS, p->id, 1, 0, 0, 0, 0));
        zglos_okno(p, K1_OKNO_AKTYWNE);
        uszkodz_pow(p);
    }
}

/* Topmost visible window that could take the focus, or -1. */
static int najwyzsze_okno(int pomin)
{
    int i, best = -1;
    for (i = 0; i < MAKS_POW; i++)
        if (i != pomin && widac(&s.p[i]) && s.p[i].rodzaj == K1_POW_OKNO && (best < 0 || s.p[i].z > s.p[best].z))
            best = i;
    return best;
}

void na_wierzch(int i)
{
    s.p[i].z = ++s.z_licznik;
    uszkodz_pow(&s.p[i]);
}

void aktywuj(int i)
{
    struct pow *p = &s.p[i];
    if (p->zmin) {
        p->zmin = 0;
        k1_log(TAG, "okno %u przywrócone", p->okno);
    }
    na_wierzch(i);
    ustaw_fokus(i);
}

void minimalizuj(int i)
{
    struct pow *p = &s.p[i];
    if (p->zmin)
        return;
    if (pelny_jest(p))
        pelny_koniec("minimalizacja");
    uszkodz_pow(p);
    p->zmin = 1;
    if (s.chwyt == i)
        s.chwyt = -1;
    if (s.ciagniete == i)
        s.ciagniete = -1;
    k1_log(TAG, "okno %u zminimalizowane", p->okno);
    if (s.fokus == i)
        ustaw_fokus(najwyzsze_okno(i));
    zglos_okno(p, K1_OKNO_ZMINIMALIZOWANE);
}

/* The shell's "Zamknij okno" (0.7.5) and the badge of a zone (0.10.0): the client closes its window itself. */
void zamknij_okno(int i)
{
    k1_log(TAG, "okno %u: powłoka prosi aplikację o zamknięcie", s.p[i].okno);
    wyslij(s.p[i].kl, k1_komunikat_nowy(K1_KOM_ZAMKNIJ, s.p[i].id, 0, 0, 0, 0, 0));
}

static void usun_pow(int i)
{
    struct pow *p = &s.p[i];
    if (!p->uzyta)
        return;
    opis_pow_koniec(i);
    uszkodz_pow(p);
    if (p->okno != 0 && p->polowa >= 0) {
        k1_log(TAG, "okno %u zamknięte", p->okno);
        zglos_okno(p, K1_OKNO_ZAMKNIETE);
    }
    if (p->mem != NULL)
        munmap((void *)(uintptr_t)p->mem, p->rozmiar);
    if (p->nowa_mem != NULL) /* 0.10.5 step 4: a new size not shown yet */
        munmap((void *)(uintptr_t)p->nowa_mem, p->nowa_rozmiar);
    strefa_bufory_zwolnij(i); /* 0.10.11 step 2: its buffers of the card (off the layer first) */
    pelny_usun(i);
    przeciaganie_usun(i); /* 0.11.0 step 4 */
    schowek_usun(i);      /* step 5 */
    p->uzyta = 0;
    if (s.pod == i)
        s.pod = -1;
    if (s.chwyt == i)
        s.chwyt = -1;
    if (s.ciagniete == i)
        s.ciagniete = -1;
    if (s.strefa_okno == i)
        s.strefa_okno = -1;
    if (s.fokus == i) {
        s.fokus = -1;
        ustaw_fokus(najwyzsze_okno(i));
    }
}

void rozlacz(int kl)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == kl)
            usun_pow(i);
    if (s.kl[kl].rola != K1_ROLA_WEJSCIE)
        k1_log(TAG, "klient %s rozłączony (odrzucone komunikaty: %lu)", nazwa_roli(&s.kl[kl]), s.kl[kl].odrzucone);
    close(s.kl[kl].fd);
    s.kl[kl].fd = -1;
    opis_klienci();
}

int znajdz_pow(int kl, uint32_t id)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == kl && s.p[i].id == id)
            return i;
    return -1;
}

static int znajdz_okno(uint32_t okno)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].okno == okno && s.p[i].polowa >= 0)
            return i;
    return -1;
}

/* The password screen: one full screen; the shell (its desktop layer and the panels) and an application
 * (two full-screen windows): two. */
void ustawienie(int k, int32_t v)
{
    int i;
    if (k == K1_UST_ROZDZIELCZOSC && v != s.tryb_nr) {
        s.nowy_tryb = v; /* applied between frames (main.c), then broadcast */
        return;
    }
    if (k == K1_UST_TAPETA && v != s.ust[k]) {
        double t0 = k1_teraz();
        rysuj_tlo(v);
        uszkodz(0, 0, s.W, s.H);
        k1_log(TAG, "tapeta %d (%.1f s)", (int)v, k1_teraz() - t0);
    }
    s.ust[k] = v;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola != K1_ROLA_WEJSCIE)
            wyslij(i, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, k, v, 0, 0, 0));
    ustawienia_zapisz(); /* M5d: a new version in skarbiec */
}

/* 0.10.2 step 2 (opis.c): the screen's description, and the test client that may only ask for it; a descriptor no
 * other message carries. 1: handled (refused or done). */
static int opis_albo_test(int kl, const struct k1_komunikat *m, int fd)
{
    const char *powod;
    if (m->typ == K1_KOM_OPIS || m->typ == K1_KOM_OPIS_PYTANIE)
        powod = opis_komunikat(kl, m, fd);
    else if (s.kl[kl].rola == K1_ROLA_TEST) { /* no surfaces, nothing else */
        if (fd >= 0)
            close(fd);
        powod = "klient testowy tylko pyta o opis ekranu";
    } else if (m->typ != K1_KOM_POWIERZCHNIA && m->typ != K1_KOM_TAPETA_ZDJECIE && m->typ != K1_KOM_STREFA_BUFOR &&
               m->typ != K1_KOM_SCHOWEK_USTAW && fd >= 0) {
        close(fd);
        powod = "niespodziewany deskryptor";
    } else {
        return 0;
    }
    if (powod != NULL)
        odrzuc(kl, powod);
    return 1;
}

/* The wallpaper photo (0.8.0 step 4: zdjecie.c) and a zone's picture from buffers of the card (0.10.11 step 2:
 * strefa_bufor.c). 1: handled (refused or done). */
static int obraz_albo_tapeta(int kl, const struct k1_komunikat *m, int fd)
{
    const char *powod;
    if (m->typ == K1_KOM_TAPETA_ZDJECIE)
        powod = zdjecie_przyjmij(kl, m, fd);
    else if (m->typ == K1_KOM_STREFA_BUFOR)
        powod = strefa_bufor_przyjmij(kl, m, fd);
    else if (m->typ == K1_KOM_STREFA_OBRAZ)
        powod = strefa_obraz(kl, m);
    else
        return 0;
    if (powod != NULL)
        odrzuc(kl, powod);
    return 1;
}

static void komunikat(int kl, const struct k1_komunikat *m, int fd)
{
    struct klient *k = &s.kl[kl];
    int i = -1;
    if (opis_albo_test(kl, m, fd) || obraz_albo_tapeta(kl, m, fd))
        return;
    if (m->typ == K1_KOM_POWIERZCHNIA)
        { nowa_pow(kl, m, fd); return; }
    if (m->typ == K1_KOM_ZATWIERDZ)
        { zatwierdz(kl, m); return; }
    if (m->typ == K1_KOM_AKTYWUJ_OKNO || m->typ == K1_KOM_MINIMALIZUJ_OKNO || m->typ == K1_KOM_ZAMKNIJ_OKNO) {
        if (k->rola != K1_ROLA_POWLOKA)
            { odrzuc(kl, "zarządzanie cudzymi oknami tylko dla powłoki"); return; }
        i = znajdz_okno(m->obiekt);
        if (i < 0)
            return;
        if (m->typ == K1_KOM_AKTYWUJ_OKNO)
            aktywuj(i);
        else if (m->typ == K1_KOM_MINIMALIZUJ_OKNO)
            minimalizuj(i);
        else
            zamknij_okno(i);
        return;
    }
    if (m->typ == K1_KOM_USTAW) {
        if (!ustawia(k, m->a))
            { odrzuc(kl, "tego ustawienia ten klient nie zmienia"); return; }
        if (m->a == K1_UST_ROZDZIELCZOSC && m->b > s.n_trybow)
            { odrzuc(kl, "nieznany tryb ekranu"); return; }
        if (m->a == K1_UST_TAPETA && m->b == K1_TAPETA_ZDJECIE && !s.ust[K1_UST_ZDJECIE])
            { odrzuc(kl, "brak zdjęcia na tapetę"); return; }
        k1_log(TAG, "ustawienie %d=%d", (int)m->a, (int)m->b);
        { ustawienie(m->a, m->b); return; }
    }
    i = znajdz_pow(kl, m->obiekt);
    if (i < 0)
        { odrzuc(kl, "nieznana powierzchnia"); return; }
    switch (m->typ) {
    case K1_KOM_POKAZ:
    case K1_KOM_PRZESUN:
        if (s.p[i].rodzaj != K1_POW_PANEL)
            { odrzuc(kl, "pokazywanie i przesuwanie tylko dla paneli"); return; }
        uszkodz_pow(&s.p[i]);
        if (m->typ == K1_KOM_POKAZ && m->a && !s.p[i].pokaz)
            pelny_koniec("panel powłoki"); /* 0.11.0 step 1: the shell shows something — over the full screen */
        if (m->typ == K1_KOM_POKAZ) {
            s.p[i].pokaz = m->a;
            s.p[i].widoczna = m->a && s.p[i].polowa >= 0;
        } else {
            s.p[i].x = m->a;
            s.p[i].y = m->b;
        }
        uszkodz_pow(&s.p[i]);
        return;
    case K1_KOM_PRZECIAGNIJ:
        if (s.p[i].rodzaj != K1_POW_OKNO || s.chwyt != i || !(s.przyciski & (1 << K1_PRZYCISK_LEWY)))
            { odrzuc(kl, "przeciąganie bez wciśniętego przycisku na tym oknie"); return; }
        s.ciagniete = i;
        s.ciag_dx = s.x - s.p[i].x;
        s.ciag_dy = s.y - s.p[i].y;
        s.ciag_klatki = s.klatki;
        s.ciag_cpu = czas_procesora();
        return;
    case K1_KOM_MINIMALIZUJ:
        if (s.p[i].rodzaj != K1_POW_OKNO)
            { odrzuc(kl, "minimalizacja tylko okien"); return; }
        { minimalizuj(i); return; }
    case K1_KOM_USUN: { usun_pow(i); return; }
    case K1_KOM_PRZECIAGANIE: case K1_KOM_PRZECIAGANIE_CEL: /* 0.11.0 step 4 */
        { przeciaganie_komunikat(kl, i, m); return; }
    case K1_KOM_SCHOWEK_USTAW: case K1_KOM_SCHOWEK_DAJ: /* step 5 */
        { schowek_komunikat(kl, i, m, fd); return; }
    case K1_KOM_PELNY_EKRAN: /* 0.11.0 step 1 */
        if (!okno_strefy(&s.p[i]))
            { odrzuc(kl, "pełny ekran tylko dla okna strefy"); return; }
        { pelny_prosba(i, m->a); return; }
    default:
        { odrzuc(kl, "nieobsługiwany typ"); return; }
    }
}

int odbierz_klienta(int kl)
{
    int n;
    for (n = 0; n < 64; n++) {
        struct klient *k = &s.kl[kl];
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(k->fd, &o);
        if (w == K1_KANAL_PUSTO)
            return 1;
        if (w != K1_KANAL_KOMUNIKAT)
            return 0;
        if (!o.ma_nadawce || o.uid != k->uid) {
            if (o.fd >= 0)
                close(o.fd);
            if (s.obcy++ < 3)
                k1_log(TAG, "komunikat od obcego nadawcy (uid %d, oczekiwano %d) — odrzucony",
                       o.ma_nadawce ? (int)o.uid : -1, (int)k->uid);
            odrzuc(kl, "obcy nadawca");
        } else if (k->rola == K1_ROLA_WEJSCIE) {
            struct k1_zdarzenie z;
            if (o.fd >= 0)
                close(o.fd); /* input messages never carry descriptors */
            if (o.fd >= 0 || o.obciete || k1_zdarzenie_sprawdz(o.dane, o.dlugosc, &z) != 0)
                odrzuc(kl, "błędne zdarzenie wejścia");
            else if (!wylaczanie_trwa()) /* 0.10.7 step 6: K1 powers off — no input for the windows any more */
                zastosuj(&z);
        } else {
            struct k1_komunikat m;
            if (o.obciete || k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_DO_EKRANU, &m) != 0) {
                if (o.fd >= 0)
                    close(o.fd);
                odrzuc(kl, "błędny komunikat");
            } else {
                komunikat(kl, &m, o.fd);
            }
        }
        if (s.kl[kl].odrzucone >= MAKS_BLEDOW) {
            k1_log(TAG, "klient %s: za dużo błędnych komunikatów — rozłączony", nazwa_roli(&s.kl[kl]));
            return 0;
        }
    }
    return 1;
}


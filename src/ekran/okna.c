/* K1 ekran — clients and their surfaces: sealed memfd surfaces, windows and panels, focus,
 * minimising, settings, and the validated messages of the clients (every packet: sender UID,
 * every field, role-dependent policy). Split out of main.c in M4.1. */
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

void wyslij(int kl, struct k1_komunikat m)
{
    if (kl >= 0 && s.kl[kl].fd >= 0)
        k1_kanal_wyslij(s.kl[kl].fd, &m, sizeof m, -1); /* a client that does not read loses messages */
}

int powloka(void)
{
    int i;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_POWLOKA)
            return i;
    return -1;
}

const char *nazwa_roli(const struct klient *k)
{
    static const char *const apl[K1_APL_MAKS + 1] = { "?", "zegar", "notatki", "ustawienia", "zasoby" };
    if (k->rola == K1_ROLA_WEJSCIE)
        return "wejście";
    if (k->rola == K1_ROLA_POWLOKA)
        return "powłoka";
    return k->apl >= 1 && k->apl <= K1_APL_MAKS ? apl[k->apl] : "?";
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

void zglos_okno(const struct pow *p, int stan)
{
    int sh = powloka();
    if (sh >= 0 && p->okno != 0)
        wyslij(sh, k1_komunikat_nowy(K1_KOM_OKNO, p->okno, s.kl[p->kl].apl, stan, 0, 0, 0));
}

static void ustaw_fokus(int i)
{
    if (i == s.fokus)
        return;
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

static void na_wierzch(int i)
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

static void minimalizuj(int i)
{
    struct pow *p = &s.p[i];
    if (p->zmin)
        return;
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

static void usun_pow(int i)
{
    struct pow *p = &s.p[i];
    if (!p->uzyta)
        return;
    uszkodz_pow(p);
    if (p->okno != 0 && p->polowa >= 0) {
        k1_log(TAG, "okno %u zamknięte", p->okno);
        zglos_okno(p, K1_OKNO_ZAMKNIETE);
    }
    if (p->mem != NULL)
        munmap((void *)(uintptr_t)p->mem, p->rozmiar);
    p->uzyta = 0;
    if (s.pod == i)
        s.pod = -1;
    if (s.chwyt == i)
        s.chwyt = -1;
    if (s.ciagniete == i)
        s.ciagniete = -1;
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
}

static int znajdz_pow(int kl, uint32_t id)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == kl && s.p[i].id == id)
            return i;
    return -1;
}

int znajdz_okno(uint32_t okno)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].okno == okno && s.p[i].polowa >= 0)
            return i;
    return -1;
}

static size_t budzet_klienta(const struct klient *k)
{
    size_t ekran = (size_t)s.W * (size_t)s.H * 4u * 2u;
    return k->rola == K1_ROLA_POWLOKA ? ekran : 2u * ekran;
}

static void nowa_pow(int kl, const struct k1_komunikat *m, int fd)
{
    struct klient *k = &s.kl[kl];
    struct stat st;
    int i, wolny = -1, okien = 0, uszczelnienia;
    size_t rozmiar = (size_t)m->a * (size_t)m->b * 4u * 2u, zajete = 0;
    void *mem;
    struct pow *p;

    if (fd < 0)
        { odrzuc(kl, "powierzchnia bez deskryptora"); return; }
    if (znajdz_pow(kl, m->obiekt) >= 0) {
        close(fd);
        { odrzuc(kl, "powierzchnia o tym numerze już istnieje"); return; }
    }
    if ((m->c == K1_POW_PANEL) != (k->rola == K1_ROLA_POWLOKA)) {
        close(fd);
        { odrzuc(kl, m->c == K1_POW_PANEL ? "panele tylko dla powłoki" : "okna tylko dla aplikacji"); return; }
    }
    if (m->a > s.W || m->b > s.H || (m->c == K1_POW_OKNO && m->b > s.H - PASEK_GORNY)) {
        close(fd);
        { odrzuc(kl, "powierzchnia większa niż ekran"); return; }
    }
    for (i = 0; i < MAKS_POW; i++) {
        if (!s.p[i].uzyta && wolny < 0)
            wolny = i;
        if (s.p[i].uzyta && s.p[i].kl == kl) {
            okien++;
            zajete += s.p[i].rozmiar;
        }
    }
    if (wolny < 0 || okien >= (k->rola == K1_ROLA_POWLOKA ? K1_POW_MAKS : MAKS_OKIEN_KLIENTA)) {
        close(fd);
        { odrzuc(kl, "za dużo powierzchni"); return; }
    }
    /* Every mapped surface costs ekran memory when it is composed: at most what two full-screen
     * windows need per client (shell: one), so that clients cannot exhaust ekran's cgroup. */
    if (zajete + rozmiar > budzet_klienta(k)) {
        close(fd);
        { odrzuc(kl, "przekroczony limit pamięci powierzchni"); return; }
    }
    /* Only a sealed memfd of exactly the right size: it can never shrink under our mapping. */
    uszczelnienia = fcntl(fd, F_GET_SEALS);
    if (uszczelnienia < 0) {
        close(fd);
        { odrzuc(kl, "deskryptor nie jest memfd"); return; }
    }
    if ((uszczelnienia & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)) != (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)) {
        close(fd);
        { odrzuc(kl, "memfd bez pieczęci SHRINK/GROW/SEAL"); return; }
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != (off_t)rozmiar) {
        close(fd);
        { odrzuc(kl, "zły rozmiar memfd"); return; }
    }
    mem = mmap(NULL, rozmiar, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED)
        { odrzuc(kl, "mmap powierzchni nieudany"); return; }
    p = &s.p[wolny];
    memset(p, 0, sizeof *p);
    p->uzyta = 1;
    p->kl = kl;
    p->id = m->obiekt;
    p->rodzaj = m->c;
    p->w = m->a;
    p->h = m->b;
    p->mem = mem;
    p->rozmiar = rozmiar;
    p->polowa = -1;
    p->pokaz = 1;
    p->z = ++s.z_licznik;
    if (p->rodzaj == K1_POW_PANEL) {
        p->x = m->d;
        p->y = m->e;
    } else {
        int32_t nr = (int32_t)(s.nastepne_okno % 6);
        p->okno = ++s.nastepne_okno;
        p->x = (s.W - p->w) / 2 + nr * 28 - 70;
        p->y = PASEK_GORNY + (s.H - PASEK_GORNY - 80 - p->h) / 2 + nr * 28 - 40;
        if (p->x < 0)
            p->x = 0;
        if (p->y < PASEK_GORNY + 8)
            p->y = PASEK_GORNY + 8;
    }
}

static void zatwierdz(int kl, const struct k1_komunikat *m)
{
    int i = znajdz_pow(kl, m->obiekt);
    struct pow *p;
    if (i < 0)
        { odrzuc(kl, "zatwierdzenie nieznanej powierzchni"); return; }
    p = &s.p[i];
    p->polowa = m->a;
    p->czeka_ramka = 1;
    if (!p->widoczna) {
        p->widoczna = p->pokaz;
        uszkodz_pow(p);
        if (p->rodzaj == K1_POW_OKNO) {
            struct klient *k = &s.kl[kl];
            k1_log(TAG, "okno %u (%s) %dx%d w %d,%d", p->okno, nazwa_roli(k), (int)p->w, (int)p->h, (int)p->x,
                   (int)p->y);
            zglos_okno(p, K1_OKNO_NOWE);
            na_wierzch(i);
            /* Focus stealing protection: a new window gets the keyboard only if nothing else has
             * it, the focused window is the client's own, or it is the first window of a client
             * that has just been started (by the user, through the shell). Otherwise it opens
             * just below the focused window, so what is on top is always what receives the keys. */
            if (s.fokus < 0 || s.p[s.fokus].kl == kl || (!k->mial_okno && teraz() - k->od < CZAS_PIERWSZEGO_OKNA)) {
                ustaw_fokus(i);
            } else {
                k1_log(TAG, "okno %u bez fokusu (ochrona klawiatury)", p->okno);
                na_wierzch(s.fokus);
                zglos_okno(p, K1_OKNO_NIEAKTYWNE);
            }
            k->mial_okno = 1;
        }
        return;
    }
    {
        /* Damage only inside the surface itself. */
        struct prost r = { 0, 0, p->w, p->h }, d = { m->b, m->c, m->d, m->e };
        if (m->d > 0 && m->e > 0 && !przetnij(&d, &r))
            return;
        if (m->d > 0 && m->e > 0)
            r = d;
        uszkodz(p->x + r.x, p->y + r.y, r.w, r.h);
    }
}

void ustawienie(int k, int32_t v)
{
    int i;
    if (k == K1_UST_ROZDZIELCZOSC && v != s.tryb_nr) {
        s.nowy_tryb = v; /* applied between frames (main.c), then broadcast */
        return;
    }
    if (k == K1_UST_TAPETA && v != s.ust[k]) {
        double t0 = teraz();
        k1_rysuj_tlo(&s.tlo, v);
        uszkodz(0, 0, s.W, s.H);
        k1_log(TAG, "tapeta %d (%.1f s)", (int)v, teraz() - t0);
    }
    s.ust[k] = v;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola != K1_ROLA_WEJSCIE)
            wyslij(i, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, k, v, 0, 0, 0));
}

static void komunikat(int kl, const struct k1_komunikat *m, int fd)
{
    struct klient *k = &s.kl[kl];
    int i = -1;
    if (m->typ != K1_KOM_POWIERZCHNIA && fd >= 0) {
        close(fd);
        { odrzuc(kl, "niespodziewany deskryptor"); return; }
    }
    if (m->typ == K1_KOM_POWIERZCHNIA)
        { nowa_pow(kl, m, fd); return; }
    if (m->typ == K1_KOM_ZATWIERDZ)
        { zatwierdz(kl, m); return; }
    if (m->typ == K1_KOM_AKTYWUJ_OKNO || m->typ == K1_KOM_MINIMALIZUJ_OKNO) {
        if (k->rola != K1_ROLA_POWLOKA)
            { odrzuc(kl, "zarządzanie cudzymi oknami tylko dla powłoki"); return; }
        i = znajdz_okno(m->obiekt);
        if (i < 0)
            return;
        if (m->typ == K1_KOM_AKTYWUJ_OKNO)
            aktywuj(i);
        else
            minimalizuj(i);
        return;
    }
    if (m->typ == K1_KOM_USTAW) {
        if (k->rola != K1_ROLA_APLIKACJA || k->apl != K1_APL_USTAWIENIA)
            { odrzuc(kl, "zmiana ustawień tylko dla aplikacji Ustawienia"); return; }
        if (m->a == K1_UST_ROZDZIELCZOSC && m->b > s.n_trybow)
            { odrzuc(kl, "nieznany tryb ekranu"); return; }
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
        return;
    case K1_KOM_MINIMALIZUJ:
        if (s.p[i].rodzaj != K1_POW_OKNO)
            { odrzuc(kl, "minimalizacja tylko okien"); return; }
        { minimalizuj(i); return; }
    case K1_KOM_USUN:
        { usun_pow(i); return; }
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
            else
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


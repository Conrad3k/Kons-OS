/* K1 skarbiec — requests about the user's files from clients (K1_KOM_DOK_*, 0.8.0, ADR 0029): who may do what
 * (one row per client in `prawa`), names and content in sealed memfd, the replies. The owner of a channel comes
 * from nadzorca, so an application cannot pretend to be Pliki. Part of skarbiec (skarbiec.h). */
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_io.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

enum {
    P_LISTA = 1 << 0, P_CZYTAJ = 1 << 1, P_NOWY = 1 << 2, P_ZAPISZ = 1 << 3, P_NAZWA = 1 << 4, P_PRZENIES = 1 << 5,
    P_KOSZ = 1 << 6, P_PRZYWROC = 1 << 7, P_USUN = 1 << 8, P_OPROZNIJ = 1 << 9, P_WSZYSTKO = (1 << 10) - 1,
};
enum { Z_WSZYSTKO = 1, Z_PULPIT = 2 }; /* which entries the client sees */

/* The table of rights: a client not listed may do nothing with files. */
static const struct prawa {
    int wlasciciel, prawa, zakres;
} prawa[] = {
    { K1_PROFIL_PLIKI, P_WSZYSTKO, Z_WSZYSTKO },
    /* the desktop: the files of the Pulpit folder (names, to draw them) and Kosz — no content */
    { K1_PROFIL_GUI, P_LISTA | P_KOSZ | P_OPROZNIJ, Z_PULPIT },
};

static uint8_t lista[K1_DOK_LISTA_MAKS];
static struct k1_dok_wpis wybrane[K1_DOK_MAKS];

static const struct prawa *prawa_klienta(int wl)
{
    for (size_t i = 0; i < sizeof prawa / sizeof prawa[0]; i++)
        if (prawa[i].wlasciciel == wl)
            return &prawa[i];
    return NULL;
}

int dok_moze_listowac(int wl)
{
    const struct prawa *p = prawa_klienta(wl);
    return p != NULL && (p->prawa & P_LISTA);
}

static int64_t teraz(void)
{
    return (int64_t)time(NULL);
}

void dok_po_otwarciu(void)
{
    dok_wczytaj(teraz());
}

int dok_co_godzine(void)
{
    int n = dok_przeterminowane(teraz());
    if (n > 0)
        k1_log(TAG, "Kosz: %d usuniętych na zawsze (po %d dniach)", n, K1_KOSZ_DNI);
    return n > 0;
}

/* What Pliki shows: visible entries and those in Kosz (not what lies inside a folder in Kosz). */
static int dla_plikow(const struct k1_dok_wpis *w)
{
    return w->kosz != 0 || dok_widoczny(w->id);
}

static int dla_pulpitu(const struct k1_dok_wpis *w)
{
    return w->rodzic == K1_FOLDER_PULPIT && w->kosz == 0;
}

static int w_zakresie(const struct prawa *p, uint32_t id)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    return w != NULL && (p->zakres == Z_WSZYSTKO || dla_pulpitu(w));
}

static void wynik(int fd, uint32_t id, int typ, int r, uint32_t nowy)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_WYNIK, id, typ, r, (int32_t)nowy, 0, 0);
    k1_kanal_wyslij(fd, &m, sizeof m, -1);
}

static void wyslij_liste(int fd, const struct prawa *p)
{
    int n = dok_wybierz(p->zakres == Z_WSZYSTKO ? dla_plikow : dla_pulpitu, wybrane);
    size_t dl = k1_dok_lista_zapisz(wybrane, n, dok_w_koszu(), lista);
    int m_fd = k1_memfd_z_danymi("k1-lista", lista, dl);
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_LISTA, 0, (int32_t)dl, n, 0, 0, 0);
    if (m_fd < 0) {
        wynik(fd, 0, K1_KOM_DOK_SPIS, K1_MAG_PAMIEC, 0);
        return;
    }
    k1_kanal_wyslij(fd, &m, sizeof m, m_fd);
    close(m_fd);
}

/* Content into a new memfd, piece after piece. */
struct do_memfd {
    int fd;
    uint64_t od;
};
static int do_memfd(void *ctx, const void *buf, uint32_t n)
{
    struct do_memfd *d = ctx;
    if (k1_pisz_calosc(d->fd, buf, n, d->od) != 0)
        return -1;
    d->od += n;
    return 0;
}

static void wyslij_tresc(int fd, uint32_t id, uint64_t maks)
{
    int m_fd = k1_memfd_nowy("k1-plik"), r;
    struct do_memfd d = { m_fd, 0 };
    struct dok_cel c = { do_memfd, &d };
    const struct k1_dok_wpis *w = dok_wpis(id);
    uint64_t ile = 0;
    struct k1_komunikat m;
    if (m_fd < 0) {
        wynik(fd, id, K1_KOM_DOK_CZYTAJ, K1_MAG_PAMIEC, 0);
        return;
    }
    r = dok_czytaj_tresc(id, maks, &c, &ile);
    if (r == 0 && k1_memfd_zapieczetuj(m_fd) != 0) {
        wynik(fd, id, K1_KOM_DOK_CZYTAJ, K1_MAG_PAMIEC, 0);
        return;
    }
    if (r != 0) {
        close(m_fd);
        wynik(fd, id, K1_KOM_DOK_CZYTAJ, r, 0);
        return;
    }
    m = k1_komunikat_nowy(K1_KOM_DOK_TRESC, id, (int32_t)ile, (int32_t)w->rozmiar, 0, 0, 0);
    k1_kanal_wyslij(fd, &m, sizeof m, m_fd);
    close(m_fd);
}

/* Content from a client's memfd, from byte `od`. */
struct z_memfd {
    int fd;
    uint64_t od;
};
static int z_memfd(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct z_memfd *z = ctx;
    return k1_czytaj_calosc(z->fd, buf, n, z->od + od);
}

/* The name at the start of a client's memfd of `calosc` bytes (checked: sealed, exactly that size). */
static int czytaj_nazwe(int fd, size_t calosc, size_t nl, char nazwa[K1_DOK_NAZWA])
{
    memset(nazwa, 0, K1_DOK_NAZWA);
    if (fd < 0 || nl >= K1_DOK_NAZWA || k1_memfd_sprawdz(fd, calosc) != 0 || k1_czytaj_calosc(fd, nazwa, nl, 0) != 0)
        return K1_MAG_ARG;
    return 0;
}

/* NOWY: obiekt=folder, a=kind, b=name length, c=content length. */
static int nowy(int wl, const struct k1_komunikat *m, int fd_in, uint32_t *id)
{
    char nazwa[K1_DOK_NAZWA];
    struct z_memfd z = { fd_in, (uint64_t)m->b };
    struct dok_zrodlo zr = { z_memfd, &z, (uint64_t)m->c };
    int r = czytaj_nazwe(fd_in, (size_t)m->b + (size_t)m->c, (size_t)m->b, nazwa);
    if (r != 0)
        return r;
    return dok_nowy(m->obiekt, m->a, nazwa, (size_t)m->b, m->a == K1_DOK_PLIK ? &zr : NULL, wl, K1_POCH_K1, teraz(), id);
}

static int zapisz(const struct k1_komunikat *m, int fd_in)
{
    struct z_memfd z = { fd_in, 0 };
    struct dok_zrodlo zr = { z_memfd, &z, (uint64_t)m->a };
    if (k1_memfd_sprawdz(fd_in, (size_t)m->a) != 0)
        return K1_MAG_ARG;
    return dok_zapisz_tresc(m->obiekt, &zr, teraz());
}

static int nazwa(const struct k1_komunikat *m, int fd_in)
{
    char n[K1_DOK_NAZWA];
    int r = czytaj_nazwe(fd_in, (size_t)m->a, (size_t)m->a, n);
    return r != 0 ? r : dok_zmien_nazwe(m->obiekt, n, (size_t)m->a, teraz());
}

/* A change request already allowed: its result. */
static int zmiana(int wl, const struct k1_komunikat *m, int fd_in, uint32_t *nowy_id)
{
    switch (m->typ) {
    case K1_KOM_DOK_NOWY:
        return nowy(wl, m, fd_in, nowy_id);
    case K1_KOM_DOK_ZAPISZ:
        return zapisz(m, fd_in);
    case K1_KOM_DOK_NAZWA:
        return nazwa(m, fd_in);
    case K1_KOM_DOK_PRZENIES:
        return dok_przenies(m->obiekt, (uint32_t)m->a, teraz());
    case K1_KOM_DOK_KOSZ:
        return dok_do_kosza(m->obiekt, teraz());
    case K1_KOM_DOK_PRZYWROC:
        return dok_przywroc(m->obiekt);
    case K1_KOM_DOK_USUN:
        return dok_usun_na_zawsze(m->obiekt);
    default:
        return dok_oproznij_kosz();
    }
}

static int potrzebne_prawo(int typ)
{
    static const int p[] = { [K1_KOM_DOK_SPIS - K1_KOM_DOK_SPIS] = P_LISTA, [K1_KOM_DOK_CZYTAJ - K1_KOM_DOK_SPIS] = P_CZYTAJ,
                             [K1_KOM_DOK_NOWY - K1_KOM_DOK_SPIS] = P_NOWY, [K1_KOM_DOK_ZAPISZ - K1_KOM_DOK_SPIS] = P_ZAPISZ,
                             [K1_KOM_DOK_NAZWA - K1_KOM_DOK_SPIS] = P_NAZWA, [K1_KOM_DOK_PRZENIES - K1_KOM_DOK_SPIS] = P_PRZENIES,
                             [K1_KOM_DOK_KOSZ - K1_KOM_DOK_SPIS] = P_KOSZ, [K1_KOM_DOK_PRZYWROC - K1_KOM_DOK_SPIS] = P_PRZYWROC,
                             [K1_KOM_DOK_USUN - K1_KOM_DOK_SPIS] = P_USUN, [K1_KOM_DOK_OPROZNIJ - K1_KOM_DOK_SPIS] = P_OPROZNIJ };
    return p[typ - K1_KOM_DOK_SPIS];
}

/* Whether the entries a request names are within the client's sight. */
static int widzi(const struct prawa *p, const struct k1_komunikat *m)
{
    if (p->zakres == Z_WSZYSTKO || m->typ == K1_KOM_DOK_SPIS || m->typ == K1_KOM_DOK_OPROZNIJ)
        return 1;
    if (m->typ == K1_KOM_DOK_NOWY)
        return m->obiekt == K1_FOLDER_PULPIT;
    return w_zakresie(p, m->obiekt) && (m->typ != K1_KOM_DOK_PRZENIES || m->a == K1_FOLDER_PULPIT);
}

int dok_obsluz(int fd, int wl, const struct k1_komunikat *m, int fd_in)
{
    const struct prawa *p = prawa_klienta(wl);
    uint32_t nowy_id = 0;
    int r;
    if (p == NULL || !(p->prawa & potrzebne_prawo(m->typ)) || !widzi(p, m)) {
        r = p == NULL || !(p->prawa & potrzebne_prawo(m->typ)) ? K1_DOK_BLAD_PRAWA : K1_DOK_BLAD_BRAK;
        k1_log(TAG, "pliki: odmowa (właściciel %d, komunikat %d)", wl, m->typ);
        wynik(fd, m->obiekt, m->typ, r, 0);
    } else if (zablokowany) {
        wynik(fd, m->obiekt, m->typ, K1_MAG_HASLO, 0);
        r = K1_MAG_HASLO;
    } else if (m->typ == K1_KOM_DOK_SPIS) {
        wyslij_liste(fd, p);
        r = 1; /* not a change */
    } else if (m->typ == K1_KOM_DOK_CZYTAJ) {
        wyslij_tresc(fd, m->obiekt, (uint64_t)m->a);
        r = 1;
    } else {
        r = zmiana(wl, m, fd_in, &nowy_id);
        wynik(fd, m->obiekt, m->typ, r, r == 0 ? nowy_id : 0);
        if (r != 0)
            k1_log(TAG, "pliki: komunikat %d (właściciel %d): %s", m->typ, wl, r <= K1_DOK_BLAD_NAZWA ? "odmowa" : k1_mag_opis(r));
    }
    if (fd_in >= 0)
        close(fd_in);
    return r == 0;
}

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
    P_KOSZ = 1 << 6, P_PRZYWROC = 1 << 7, P_USUN = 1 << 8, P_OPROZNIJ = 1 << 9, P_OTWORZ = 1 << 10,
    P_WYBIERZ = 1 << 11, P_WYBRANO = 1 << 12, P_WSZYSTKO = (1 << 11) - 1, /* all but the file picker's */
    P_POCHODZENIE = 1 << 13, /* a new file with an origin other than K1: an import from a USB stick (ADR 0031), and
                              * the time it came with (DOK_CZAS, 0.8.2) */
};
/* Which entries the client sees: all (with Kosz), the Pulpit folder, the visible ones (no Kosz), or its own files
 * (made by it) and the files it was given (ADR 0030). */
enum { Z_WSZYSTKO = 1, Z_PULPIT, Z_WIDOCZNE, Z_WLASNE };

/* The table of rights: a client not listed may do nothing with files. */
static const struct prawa {
    int wlasciciel, prawa, zakres;
    const char *folder; /* Z_WLASNE: its folder in Dokumenty, where its new files go */
} prawa[] = {
    { K1_PROFIL_PLIKI, P_WSZYSTKO | P_POCHODZENIE, Z_WSZYSTKO, NULL },
    /* the desktop: the files of the Pulpit folder (names, to draw them) and Kosz — no content */
    { K1_PROFIL_GUI, P_LISTA | P_KOSZ | P_OPROZNIJ | P_OTWORZ, Z_PULPIT, NULL },
    /* Notatki: its notes and the text files the user opened in it; Kosz only for its own */
    { K1_PROFIL_NOTATKI, P_LISTA | P_CZYTAJ | P_NOWY | P_ZAPISZ | P_NAZWA | P_KOSZ | P_WYBIERZ, Z_WLASNE, "Notatki" },
    /* the file picker: names to choose from (no content), the user's choice */
    { K1_PROFIL_WYBOR, P_LISTA | P_WYBRANO, Z_WIDOCZNE, NULL },
};

static uint8_t lista[K1_DOK_LISTA_MAKS];
static struct k1_dok_wpis wybrane[K1_DOK_MAKS];
static uint8_t kawalek[K1_DOK_KAWALEK];

/* A write in progress of each client channel (0.8.1 step 4): at most one; a closed channel abandons it. */
#define KANALOW 16
static struct {
    int fd;
    struct dok_zapis z;
} zapisy[KANALOW];

static struct dok_zapis *zapis_kanalu(int fd, int nowy)
{
    int wolny = -1;
    for (int i = 0; i < KANALOW; i++) {
        if (zapisy[i].z.jest && zapisy[i].fd == fd)
            return &zapisy[i].z;
        if (!zapisy[i].z.jest && wolny < 0)
            wolny = i;
    }
    if (!nowy || wolny < 0)
        return NULL;
    zapisy[wolny].fd = fd;
    return &zapisy[wolny].z;
}

void dok_koniec_kanalu(int fd)
{
    struct dok_zapis *z = zapis_kanalu(fd, 0);
    if (z != NULL) {
        k1_log(TAG, "pliki: kanał zamknięty w trakcie zapisu — porzucony (%u kawałków)", z->kawalkow);
        dok_zapis_porzuc(z);
    }
}

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

/* The time for entries: the clock, never before the bound (dok_czas.c). */
static int64_t teraz(void)
{
    return dok_czas((int64_t)time(NULL));
}

void dok_po_otwarciu(void)
{
    dok_wczytaj((int64_t)time(NULL));
}

int dok_co_godzine(void)
{
    int n = dok_przeterminowane((int64_t)time(NULL));
    if (n > 0)
        k1_log(TAG, "Kosz: %d usuniętych na zawsze (po %d dniach)", n, K1_KOSZ_DNI);
    return n > 0;
}

/* What Pliki shows: visible entries and those in Kosz (not what lies inside a folder in Kosz). */
static int dla_plikow(const struct k1_dok_wpis *w)
{
    return k1_dok_w_koszu(w) || dok_widoczny(w->id);
}

static int dla_pulpitu(const struct k1_dok_wpis *w)
{
    return w->rodzic == K1_FOLDER_PULPIT && w->kosz == 0;
}

static int widoczny(const struct k1_dok_wpis *w)
{
    return dok_widoczny(w->id);
}

static int wl_filtra; /* the owner for dla_wlasnych (dok_wybierz takes a function of the entry only) */
static int dla_wlasnych(const struct k1_dok_wpis *w)
{
    return w->rodzaj == K1_DOK_PLIK && dok_widoczny(w->id) && (w->tworca == wl_filtra || dostep_ma(wl_filtra, w->id));
}

static int (*const filtry[])(const struct k1_dok_wpis *) = {
    [Z_WSZYSTKO] = dla_plikow, [Z_PULPIT] = dla_pulpitu, [Z_WIDOCZNE] = widoczny, [Z_WLASNE] = dla_wlasnych
};

static int w_zakresie(const struct prawa *p, uint32_t id)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    wl_filtra = p->wlasciciel;
    return w != NULL && filtry[p->zakres](w);
}

static void wynik(int fd, uint32_t id, int typ, int r, uint32_t nowy)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_WYNIK, id, typ, r, (int32_t)nowy, 0, 0);
    k1_kanal_wyslij(fd, &m, sizeof m, -1);
}

static void wyslij_liste(int fd, const struct prawa *p)
{
    int n;
    wl_filtra = p->wlasciciel;
    n = dok_wybierz(filtry[p->zakres], wybrane);
    size_t dl = k1_dok_lista_zapisz(wybrane, n, dok_w_koszu(), lista);
    int m_fd = k1_memfd_z_danymi("k1-lista", lista, dl);
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_LISTA, 0, (int32_t)dl, n, 0, 0, 0);
    if (m_fd < 0) {
        wynik(fd, 0, K1_KOM_DOK_SPIS, K1_MAG_PAMIEC, 0);
        return;
    }
    k1_kanal_wyslij(fd, &m, sizeof m, m_fd);
    close(m_fd);
    miejsce_wyslij(fd); /* Pliki say when little room is left (0.8.1) */
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

static void wyslij_tresc(int fd, uint32_t id, uint32_t od_kawalka, uint64_t maks)
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
    r = dok_czytaj_tresc(id, od_kawalka, maks, &c, &ile);
    if (r == 0 && k1_memfd_zapieczetuj(m_fd) != 0) {
        wynik(fd, id, K1_KOM_DOK_CZYTAJ, K1_MAG_PAMIEC, 0);
        return;
    }
    if (r != 0) {
        close(m_fd);
        wynik(fd, id, K1_KOM_DOK_CZYTAJ, r, 0);
        return;
    }
    m = k1_komunikat_nowy(K1_KOM_DOK_TRESC, id, (int32_t)ile, (int32_t)od_kawalka, k1_kom_niskie(w->rozmiar),
                          k1_kom_wysokie(w->rozmiar), 0);
    k1_kanal_wyslij(fd, &m, sizeof m, m_fd);
    close(m_fd);
}

/* The name at the start of a client's memfd of `calosc` bytes (checked: sealed, exactly that size). */
static int czytaj_nazwe(int fd, size_t calosc, size_t nl, char nazwa[K1_DOK_NAZWA])
{
    memset(nazwa, 0, K1_DOK_NAZWA);
    if (fd < 0 || nl >= K1_DOK_NAZWA || k1_memfd_sprawdz(fd, calosc) != 0 || k1_czytaj_calosc(fd, nazwa, nl, 0) != 0)
        return K1_MAG_ARG;
    return 0;
}

/* The first piece of a write in progress from a client's memfd (from byte `od`, `dl` bytes). */
static int pierwszy(struct dok_zapis *z, int fd_in, size_t od, size_t dl)
{
    int r = dl == 0 ? 0 : k1_czytaj_calosc(fd_in, kawalek, dl, (uint64_t)od) == 0 ? dok_zapis_kawalek(z, kawalek, (uint32_t)dl)
                                                                              : K1_MAG_ARG;
    if (r != 0)
        dok_zapis_porzuc(z);
    return r;
}

/* NOWY: obiekt=folder, a=kind, b=name length, c=content length, e=1 more pieces follow (a write in progress). A client
 * with its own folder: files go there. */
static int nowy(int fd, const struct prawa *p, const struct k1_komunikat *m, int fd_in, uint32_t *id)
{
    char nazwa[K1_DOK_NAZWA];
    struct dok_zapis *z;
    uint32_t folder = m->obiekt;
    int r = czytaj_nazwe(fd_in, (size_t)m->b + (size_t)m->c, (size_t)m->b, nazwa);
    if (r == 0 && p->folder != NULL)
        r = dok_folder_aplikacji(p->folder, p->wlasciciel, teraz(), &folder);
    if (r != 0)
        return r;
    if (m->a == K1_DOK_FOLDER)
        return dok_nowy(folder, m->a, nazwa, (size_t)m->b, NULL, p->wlasciciel, K1_POCH_K1, teraz(), id);
    if ((z = zapis_kanalu(fd, 1)) == NULL || z->jest)
        return K1_DOK_BLAD_ZAPIS; /* this channel has a write in progress already */
    if (!dok_miejsce_na((uint64_t)m->c))
        return K1_DOK_BLAD_BRAK_MIEJSCA;
    if ((r = dok_zapis_nowy(z, folder, nazwa, (size_t)m->b, p->wlasciciel, m->d != 0 ? m->d : K1_POCH_K1)) != 0 ||
        (r = pierwszy(z, fd_in, (size_t)m->b, (size_t)m->c)) != 0)
        return r;
    return m->e ? 0 : dok_zapis_koniec(z, teraz(), id);
}

static int zapisz(int fd, const struct k1_komunikat *m, int fd_in)
{
    struct dok_zapis *z;
    uint32_t id;
    int r;
    if (k1_memfd_sprawdz(fd_in, (size_t)m->a) != 0)
        return K1_MAG_ARG;
    if ((z = zapis_kanalu(fd, 1)) == NULL || z->jest)
        return K1_DOK_BLAD_ZAPIS;
    if ((r = dok_zapis_tresci(z, m->obiekt)) != 0)
        return r;
    if (!dok_miejsce_na((uint64_t)m->a)) {
        dok_zapis_porzuc(z);
        return K1_DOK_BLAD_BRAK_MIEJSCA;
    }
    if ((r = pierwszy(z, fd_in, 0, (size_t)m->a)) != 0)
        return r;
    return m->b ? 0 : dok_zapis_koniec(z, teraz(), &id);
}

/* DOK_KAWALEK, DOK_KONIEC: the client's write in progress goes on, ends or is abandoned. */
static int dalej(int fd, const struct k1_komunikat *m, int fd_in, uint32_t *id)
{
    struct dok_zapis *z = zapis_kanalu(fd, 0);
    if (z == NULL)
        return K1_DOK_BLAD_ZAPIS;
    if (m->typ == K1_KOM_DOK_KAWALEK) {
        if (k1_memfd_sprawdz(fd_in, (size_t)m->a) != 0) {
            dok_zapis_porzuc(z);
            return K1_MAG_ARG;
        }
        return pierwszy(z, fd_in, 0, (size_t)m->a);
    }
    if (m->a == 1) {
        dok_zapis_porzuc(z);
        return 0;
    }
    return dok_zapis_koniec(z, teraz(), id);
}

static int nazwa(const struct k1_komunikat *m, int fd_in)
{
    char n[K1_DOK_NAZWA];
    int r = czytaj_nazwe(fd_in, (size_t)m->a, (size_t)m->a, n);
    return r != 0 ? r : dok_zmien_nazwe(m->obiekt, n, (size_t)m->a, m->b, teraz());
}

/* A change request already allowed: its result. */
static int zmiana(int fd, const struct prawa *p, const struct k1_komunikat *m, int fd_in, uint32_t *nowy_id)
{
    switch (m->typ) {
    case K1_KOM_DOK_NOWY:
        return nowy(fd, p, m, fd_in, nowy_id);
    case K1_KOM_DOK_ZAPISZ:
        return zapisz(fd, m, fd_in);
    case K1_KOM_DOK_KAWALEK:
    case K1_KOM_DOK_KONIEC:
        return dalej(fd, m, fd_in, nowy_id);
    case K1_KOM_DOK_NAZWA:
        return nazwa(m, fd_in);
    case K1_KOM_DOK_CZAS:
        return dok_ustaw_czas(m->obiekt, (int64_t)k1_kom_u64(m->a, m->b));
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

static int potrzebne_prawo(const struct k1_komunikat *m)
{
    switch (m->typ) {
    case K1_KOM_DOK_SPIS: return P_LISTA;
    case K1_KOM_DOK_CZYTAJ: return P_CZYTAJ;
    case K1_KOM_DOK_NOWY: return m->d != 0 ? P_NOWY | P_POCHODZENIE : P_NOWY;
    case K1_KOM_DOK_ZAPISZ: return P_ZAPISZ;
    case K1_KOM_DOK_NAZWA: return P_NAZWA;
    case K1_KOM_DOK_PRZENIES: return P_PRZENIES;
    case K1_KOM_DOK_KOSZ: return P_KOSZ;
    case K1_KOM_DOK_PRZYWROC: return P_PRZYWROC;
    case K1_KOM_DOK_USUN: return P_USUN;
    case K1_KOM_DOK_OPROZNIJ: return P_OPROZNIJ;
    case K1_KOM_DOK_OTWORZ: return P_OTWORZ;
    case K1_KOM_DOK_WYBIERZ: return P_WYBIERZ;
    case K1_KOM_DOK_KAWALEK:
    case K1_KOM_DOK_KONIEC: return 0; /* the rights were checked when the write started (NOWY, ZAPISZ) */
    case K1_KOM_DOK_CZAS: return P_POCHODZENIE;
    default: return P_WYBRANO;
    }
}

/* Whether the entries a request names are within the client's sight. */
static int widzi(const struct prawa *p, const struct k1_komunikat *m)
{
    const struct k1_dok_wpis *w = dok_wpis(m->obiekt);
    if (p->zakres == Z_WSZYSTKO)
        return 1;
    switch (m->typ) {
    case K1_KOM_DOK_SPIS:
    case K1_KOM_DOK_OPROZNIJ:
    case K1_KOM_DOK_WYBIERZ:
    case K1_KOM_DOK_WYBRANO: /* the file itself is checked in dostep.c */
    case K1_KOM_DOK_KAWALEK:
    case K1_KOM_DOK_KONIEC: /* the client's own write in progress */
        return 1;
    case K1_KOM_DOK_NOWY: /* the desktop: into Pulpit; an application with its folder: a file "into Dokumenty" = there */
        return p->zakres == Z_PULPIT ? m->obiekt == K1_FOLDER_PULPIT
                                     : p->folder != NULL && m->obiekt == K1_FOLDER_DOKUMENTY && m->a == K1_DOK_PLIK;
    case K1_KOM_DOK_KOSZ: /* an application: only what it made itself */
        return w_zakresie(p, m->obiekt) && (p->zakres != Z_WLASNE || w->tworca == p->wlasciciel);
    case K1_KOM_DOK_PRZENIES:
        return w_zakresie(p, m->obiekt) && m->a == K1_FOLDER_PULPIT;
    default:
        return w_zakresie(p, m->obiekt);
    }
}

/* DOK_OTWORZ, DOK_WYBIERZ, DOK_WYBRANO (allowed already): dostep.c. */
static int wybor(int fd, const struct prawa *p, const struct k1_komunikat *m)
{
    if (m->typ == K1_KOM_DOK_OTWORZ)
        return dostep_otworz(m->obiekt, m->a);
    if (m->typ == K1_KOM_DOK_WYBIERZ)
        return dostep_wybierz(fd, p->wlasciciel, m->a);
    return dostep_wybrano(m->obiekt);
}

int dok_obsluz(int fd, int wl, const struct k1_komunikat *m, int fd_in)
{
    const struct prawa *p = prawa_klienta(wl);
    uint32_t nowy_id = 0;
    int r;
    int prawo = potrzebne_prawo(m), ma = p != NULL && (p->prawa & prawo) == prawo;
    if (!ma || !widzi(p, m)) {
        r = !ma ? K1_DOK_BLAD_PRAWA : K1_DOK_BLAD_BRAK;
        k1_log(TAG, "pliki: odmowa (właściciel %d, komunikat %d)", wl, m->typ);
        wynik(fd, m->obiekt, m->typ, r, 0);
    } else if (zablokowany) {
        wynik(fd, m->obiekt, m->typ, K1_MAG_HASLO, 0);
        r = K1_MAG_HASLO;
    } else if (m->typ == K1_KOM_DOK_SPIS) {
        wyslij_liste(fd, p);
        r = 1; /* not a change */
    } else if (m->typ == K1_KOM_DOK_CZYTAJ) {
        wyslij_tresc(fd, m->obiekt, (uint32_t)m->b, (uint64_t)m->a);
        r = 1;
    } else if (m->typ >= K1_KOM_DOK_OTWORZ && m->typ <= K1_KOM_DOK_WYBRANO) {
        r = wybor(fd, p, m);
        if (r != 0 || m->typ != K1_KOM_DOK_WYBIERZ) /* WYBIERZ: the answer is DOK_WYBRANY, later */
            wynik(fd, m->obiekt, m->typ, r, 0);
        if (r != 0)
            k1_log(TAG, "pliki: komunikat %d (właściciel %d): odmowa (%d)", m->typ, wl, r);
        r = 1; /* no file changed */
    } else {
        r = zmiana(fd, p, m, fd_in, &nowy_id);
        wynik(fd, m->obiekt, m->typ, r, r == 0 ? nowy_id : 0);
        if (r != 0)
            k1_log(TAG, "pliki: komunikat %d (właściciel %d): %s", m->typ, wl, r <= K1_DOK_BLAD_NAZWA ? "odmowa" : k1_mag_opis(r));
        if (r == 0 && (m->typ == K1_KOM_DOK_KAWALEK || (m->typ == K1_KOM_DOK_KONIEC && m->a == 1) ||
                       (m->typ == K1_KOM_DOK_NOWY && m->e) || (m->typ == K1_KOM_DOK_ZAPISZ && m->b)))
            r = 1; /* a write in progress: nothing to see until it is done */
    }
    if (fd_in >= 0)
        close(fd_in);
    return r == 0;
}

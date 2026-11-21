/* K1 — host test of the user's files: the rights of the channel (dok_kanal.c over a real socketpair), a file for an application
 * after a gesture, the file picker, Podgląd (dostep.c, ADR 0030, 0037) (test_dokumenty.h). */
#define K1_TEST_CZESC
#include "test_dokumenty.h"

/* ---- the channel: rights ---- */

int kan[2];

static int na_pulpicie(const struct k1_dok_wpis *w)
{
    return w->rodzic == K1_FOLDER_PULPIT && w->kosz == 0;
}

struct k1_komunikat zapytaj(int wl, struct k1_komunikat m, int fd, int *zmiana, int *fd_odp)
{
    struct k1_odebrane o;
    struct k1_komunikat k;
    *zmiana = dok_obsluz(kan[1], wl, &m, fd);
    memset(&k, 0, sizeof k);
    *fd_odp = -1;
    if (k1_kanal_odbierz(kan[0], &o) == K1_KANAL_KOMUNIKAT &&
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &k) == 0)
        *fd_odp = o.fd;
    else if (o.fd >= 0)
        close(o.fd);
    if (k.typ == K1_KOM_DOK_LISTA) { /* 0.8.1 step 3: the room for files right after every list */
        struct k1_komunikat mk;
        EXPECT(k1_kanal_odbierz(kan[0], &o) == K1_KANAL_KOMUNIKAT && o.fd < 0 &&
               k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &mk) == 0 && mk.typ == K1_KOM_SKARB_MIEJSCE &&
               mk.c == dok_ile());
    }
    return k;
}

void test_prawa(void)
{
    struct k1_komunikat k;
    int zm, fd, mfd;
    uint32_t plik, pulpit;
    EXPECT(k1_kanal_utworz(kan) == 0);
    dok_wczytaj(T0);
    plik = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "tajne.txt", (const uint8_t *)"sekret", 6, T0);
    pulpit = nowy(K1_FOLDER_PULPIT, K1_DOK_PLIK, "notatka.txt", (const uint8_t *)"hej", 3, T0);
    /* Pliki: everything */
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_LISTA && k.b >= 2 && fd >= 0 && !zm);
    close(fd);
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, plik, 100, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_TRESC && k.a == 6 && k.b == 0 && k1_kom_u64(k.c, k.d) == 6 && fd >= 0);
    close(fd);
    mfd = k1_memfd_z_danymi("t", "Nowy folder", 11);
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_FOLDER, 11, 0, 0, 0), mfd, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == 0 && k.c >= K1_DOK_PIERWSZY && zm);
    /* an import (0.8.0 step 3): Pliki may give the origin "z pendrive'a"; a taken name gets " (2)" before the extension */
    mfd = k1_memfd_z_danymi("t", "tajne.txtABC", 12);
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 9, 3, K1_POCH_PENDRIVE, 0),
                mfd, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == 0 && zm && dok_wpis((uint32_t)k.c) != NULL &&
           dok_wpis((uint32_t)k.c)->pochodzenie == K1_POCH_PENDRIVE && strcmp(dok_wpis((uint32_t)k.c)->nazwa, "tajne (2).txt") == 0);
    { /* 0.8.2 step 2: the time it had on the stick — only for a file with another origin, only Pliki */
        uint32_t z = (uint32_t)k.c;
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZAS, z, 1700000000, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_CZAS && k.b == 0 && zm && dok_wpis(z)->zmieniono == 1700000000);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZAS, plik, 1700000000, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_MIEJSCE && !zm && dok_wpis(plik)->zmieniono == T0); /* made in K1: its own time */
        k = zapytaj(K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_CZAS, z, 1600000000, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_PRAWA && !zm && dok_wpis(z)->zmieniono == 1700000000);
        k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_CZAS, z, 1600000000, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_PRAWA && !zm);
        EXPECT(dok_ustaw_czas(z, 0) == K1_DOK_BLAD_MIEJSCE && dok_ustaw_czas(z, K1_DOK_CZAS_MAKS + 1) == K1_DOK_BLAD_MIEJSCE &&
               dok_ustaw_czas(999999, 5) == K1_DOK_BLAD_BRAK);
    }
    mfd = k1_memfd_z_danymi("t", "Nowy folder", 11); /* the size does not match the message */
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_FOLDER, 10, 0, 0, 0), mfd, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_MAG_ARG && !zm);
    /* the desktop: only the Pulpit folder, no content */
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_LISTA && k.b == dok_wybierz(na_pulpicie, wybrane) && fd >= 0);
    if (fd >= 0) {
        static uint8_t buf[K1_DOK_LISTA_MAKS];
        uint32_t kosz;
        int n, jest = 0;
        EXPECT(k1_memfd_czytaj(fd, buf, (size_t)k.a) == 0 &&
               (n = k1_dok_lista_czytaj(buf, (size_t)k.a, wybrane, K1_DOK_MAKS, &kosz)) == k.b);
        for (int i = 0; i < k.b; i++) {
            EXPECT(wybrane[i].rodzic == K1_FOLDER_PULPIT); /* never a name from another folder */
            jest |= wybrane[i].id == pulpit;
        }
        EXPECT(jest);
        close(fd);
    }
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, pulpit, 100, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_DOK_BLAD_PRAWA && fd < 0);
    { /* 0.8.2 step 6: the desktop moves a file of Pulpit into a folder of Pulpit (dropped on it) — nowhere else */
        uint32_t fp = nowy(K1_FOLDER_PULPIT, K1_DOK_FOLDER, "Na pulpicie", NULL, 0, T0);
        uint32_t fd2 = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "W dokumentach", NULL, 0, T0);
        uint32_t pp = nowy(K1_FOLDER_PULPIT, K1_DOK_PLIK, "do folderu.txt", NULL, 0, T0);
        EXPECT(fp != 0 && fd2 != 0 && pp != 0);
        k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, pp, (int32_t)fd2, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_BRAK && !zm && dok_wpis(pp)->rodzic == K1_FOLDER_PULPIT);
        k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, pp, pp, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_BRAK && !zm); /* a file is no folder */
        k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, plik, (int32_t)fp, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_BRAK && !zm); /* a file it does not see */
        k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, pp, (int32_t)fp, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == 0 && zm && dok_wpis(pp)->rodzic == fp);
        EXPECT(dok_usun_na_zawsze(pp) == K1_DOK_BLAD_BRAK && dok_do_kosza(pp, T0) == 0 && dok_do_kosza(fp, T0) == 0 &&
               dok_do_kosza(fd2, T0) == 0);
        /* Pliki: back from Kosz into the folder it was dropped on (DOK_PRZYWROC a) */
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_PRZYWROC, pp, K1_FOLDER_MUZYKA, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == 0 && zm && dok_wpis(pp)->rodzic == K1_FOLDER_MUZYKA && dok_widoczny(pp));
        EXPECT(dok_usun_na_zawsze(fp) == 0 && dok_usun_na_zawsze(fd2) == 0);
    }
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, plik, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_DOK_BLAD_BRAK && !zm && dok_widoczny(plik));
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, pulpit, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == 0 && zm && !dok_widoczny(pulpit));
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_PRZYWROC, pulpit, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && !zm);
    { /* 0.11.0 step 2 (ADR 0052): nadzorca's channel for the zones — only a new file in Pobrane with a zone's origin */
        const int z = K1_POCH_STREFA_OD + K1_STREFA_OGOLNA;
        mfd = k1_memfd_z_danymi("t", "plik.pdf%PDF-1.4", 16);
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_POBRANE, K1_DOK_PLIK, 8, 8, z, 0), mfd, &zm, &fd);
        EXPECT(k.b == 0 && zm && dok_wpis((uint32_t)k.c) != NULL && dok_wpis((uint32_t)k.c)->pochodzenie == z &&
               dok_wpis((uint32_t)k.c)->rodzic == K1_FOLDER_POBRANE && dok_wpis((uint32_t)k.c)->typ == K1_TYP_PDF &&
               dok_wpis((uint32_t)k.c)->tworca == k1_strefa(K1_STREFA_OGOLNA)->profil &&
               strcmp(k1_poch_nazwa(dok_wpis((uint32_t)k.c)->pochodzenie), "z przeglądarki · Ogólna") == 0);
        mfd = k1_memfd_z_danymi("t", "plik.pdfabc", 11); /* another folder */
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 8, 3, z, 0), mfd, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_BRAK && !zm);
        mfd = k1_memfd_z_danymi("t", "plik.pdfabc", 11); /* the origin of K1 */
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_POBRANE, K1_DOK_PLIK, 8, 3, 0, 0), mfd, &zm, &fd);
        EXPECT(k.b != 0 && !zm);
        mfd = k1_memfd_z_danymi("t", "Folder", 6);
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_POBRANE, K1_DOK_FOLDER, 6, 0, 0, 0), mfd, &zm, &fd);
        EXPECT(k.b != 0 && !zm);
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_DOK_BLAD_PRAWA && fd < 0);
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, plik, 100, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_PRAWA && fd < 0);
        k = zapytaj(K1_WL_POBRANIA, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, plik, 0, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_PRAWA && !zm);
        mfd = k1_memfd_z_danymi("t", "plik.pdfabc", 11); /* Pliki may not give a zone's origin */
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_POBRANE, K1_DOK_PLIK, 8, 3, z, 0), mfd, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_BRAK && !zm);
        EXPECT(!dok_moze_listowac(K1_WL_POBRANIA));
    }
    /* anyone else: nothing at all */
    k = zapytaj(K1_PROFIL_KALKULATOR, k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_DOK_BLAD_PRAWA && fd < 0);
    k = zapytaj(K1_PROFIL_ZEGAR, k1_komunikat_nowy(K1_KOM_DOK_OPROZNIJ, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && dok_w_koszu() == 1);
    EXPECT(dok_moze_listowac(K1_PROFIL_PLIKI) && dok_moze_listowac(K1_PROFIL_GUI) && !dok_moze_listowac(K1_PROFIL_EKRAN));
    close(kan[0]);
    close(kan[1]);
}

/* ---- a file for an application after a gesture, the file picker (ADR 0030) ---- */

/* The request on channel `kanal`; the reply read from it (typ 0: none). */
struct k1_komunikat na(int *kanal, int wl, struct k1_komunikat m, int fd, int *fd_odp)
{
    struct k1_odebrane o;
    struct k1_komunikat k;
    dok_obsluz(kanal[1], wl, &m, fd);
    memset(&k, 0, sizeof k);
    *fd_odp = -1;
    if (k1_kanal_odbierz(kanal[0], &o) == K1_KANAL_KOMUNIKAT && k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &k) == 0)
        *fd_odp = o.fd;
    else if (o.fd >= 0)
        close(o.fd);
    if (k.typ == K1_KOM_DOK_LISTA) { /* 0.8.1 step 3: the room for files right after every list */
        struct k1_komunikat mk;
        EXPECT(k1_kanal_odbierz(kan[0], &o) == K1_KANAL_KOMUNIKAT && o.fd < 0 &&
               k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &mk) == 0 && mk.typ == K1_KOM_SKARB_MIEJSCE &&
               mk.c == dok_ile());
    }
    return k;
}

/* The next message waiting on a channel (typ 0: none). */
static struct k1_komunikat odbierz(int *kanal)
{
    struct k1_odebrane o;
    struct k1_komunikat k;
    memset(&k, 0, sizeof k);
    if (k1_kanal_odbierz(kanal[0], &o) == K1_KANAL_KOMUNIKAT)
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &k);
    if (o.fd >= 0)
        close(o.fd);
    return k;
}

/* How many entries the client sees (its list), -1 when refused. */
static int widzi(int *kanal, int wl, uint32_t szukany, int *jest)
{
    static uint8_t buf[K1_DOK_LISTA_MAKS];
    uint32_t kosz;
    int fd, n;
    struct k1_komunikat k = na(kanal, wl, k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), -1, &fd);
    *jest = 0;
    if (k.typ != K1_KOM_DOK_LISTA || fd < 0)
        return -1;
    n = k1_memfd_czytaj(fd, buf, (size_t)k.a) == 0 ? k1_dok_lista_czytaj(buf, (size_t)k.a, wybrane, K1_DOK_MAKS, &kosz) : -1;
    close(fd);
    for (int i = 0; i < n; i++)
        *jest |= wybrane[i].id == szukany;
    return n;
}

static void test_notatki(int *kn, uint32_t obcy, uint32_t *wlasny)
{
    struct k1_komunikat k;
    const struct k1_dok_wpis *w;
    int fd, jest, mfd;
    EXPECT(widzi(kn, K1_PROFIL_NOTATKI, obcy, &jest) == 0 && !jest); /* nothing of anyone else */
    mfd = k1_memfd_z_danymi("t", "Zakupymleko", 11);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 6, 5, 0, 0), mfd, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == 0 && k.c >= K1_DOK_PIERWSZY);
    *wlasny = (uint32_t)k.c;
    w = dok_wpis(*wlasny);
    EXPECT(w != NULL && w->tworca == K1_PROFIL_NOTATKI && dok_wpis(w->rodzic) != NULL &&
           strcmp(dok_wpis(w->rodzic)->nazwa, "Notatki") == 0 && dok_wpis(w->rodzic)->rodzic == K1_FOLDER_DOKUMENTY);
    mfd = k1_memfd_z_danymi("t", "Zakupyjajka", 11); /* the second note: the same folder, the name gets (2) */
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 6, 5, 0, 0), mfd, &fd);
    EXPECT(k.b == 0 && dok_wpis((uint32_t)k.c) != NULL && dok_wpis((uint32_t)k.c)->rodzic == w->rodzic &&
           strcmp(dok_wpis((uint32_t)k.c)->nazwa, "Zakupy (2)") == 0);
    mfd = k1_memfd_z_danymi("t", "Zakupy", 6); /* renamed to a taken name: (2)… when asked for, else refused */
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NAZWA, (uint32_t)k.c, 6, 0, 0, 0, 0), mfd, &fd);
    EXPECT(k.b == K1_DOK_BLAD_ZAJETA);
    mfd = k1_memfd_z_danymi("t", "Zakupy", 6);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NAZWA, k.obiekt, 6, 1, 0, 0, 0), mfd, &fd);
    EXPECT(k.b == 0 && strcmp(dok_wpis(k.obiekt)->nazwa, "Zakupy (2)") == 0);
    EXPECT(widzi(kn, K1_PROFIL_NOTATKI, *wlasny, &jest) == 2 && jest);
    /* negative: a folder, another folder, someone else's file */
    mfd = k1_memfd_z_danymi("t", "Inne", 4);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, 4, 0, 0, 0), mfd, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK);
    mfd = k1_memfd_z_danymi("t", "Inne", 4);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 4, 0, 0, 0), mfd, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK);
    /* only Pliki gives an origin other than K1 (an import from a stick) */
    mfd = k1_memfd_z_danymi("t", "Inne", 4);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 4, 0, K1_POCH_PENDRIVE, 0),
           mfd, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, obcy, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_DOK_BLAD_BRAK && fd < 0);
    mfd = k1_memfd_z_danymi("t", "x", 1);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_ZAPISZ, obcy, 1, 0, 0, 0, 0), mfd, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && dok_wpis(obcy)->rozmiar == 6);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, *wlasny, K1_FOLDER_PULPIT, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, *wlasny, K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA); /* only Pliki and the desktop open files in applications */
}

static void test_otworz(int *kn, uint32_t obcy, uint32_t na_pulpicie_id)
{
    struct k1_komunikat k;
    int fd, jest;
    memset(&do_nadz, 0, sizeof do_nadz);
    k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, obcy, K1_APL_KALKULATOR, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && do_nadz.typ == 0); /* Kalkulator does not open text */
    k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, K1_FOLDER_DOKUMENTY, K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && do_nadz.typ == 0); /* a standard folder is no file (the message check refuses it too) */
    k = na(kn, K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, obcy, K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && do_nadz.typ == 0); /* the desktop: only files of the Pulpit folder */
    k = na(kn, K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, na_pulpicie_id, K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == 0 && do_nadz.typ == K1_KOM_SKARB_URUCHOM && do_nadz.obiekt == na_pulpicie_id && do_nadz.a == K1_APL_NOTATKI);
    k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, obcy, K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == 0 && do_nadz.obiekt == obcy);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, obcy, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == K1_KOM_DOK_TRESC && k.a == 6 && fd >= 0); /* given: Notatki may read it now */
    if (fd >= 0)
        close(fd);
    EXPECT(widzi(kn, K1_PROFIL_NOTATKI, obcy, &jest) == 4 && jest);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, obcy, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && dok_widoczny(obcy)); /* Kosz: only its own files */
    EXPECT(!dostep_ma(K1_PROFIL_ZEGAR, obcy));
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1); /* Notatki closed: what it was given is gone */
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, obcy, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && fd < 0);
}

static void test_wybor(int *kn, uint32_t obcy, uint32_t folder)
{
    int kw[2], kn2[2], fd;
    struct k1_komunikat k;
    EXPECT(k1_kanal_utworz(kw) == 0 && k1_kanal_utworz(kn2) == 0);
    memset(&do_nadz, 0, sizeof do_nadz);
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, obcy, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && !dostep_ma(K1_PROFIL_NOTATKI, obcy)); /* nobody asked */
    k = na(kn, K1_PROFIL_KALKULATOR, k1_komunikat_nowy(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && do_nadz.typ == 0);
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_WYBIERZ, 0, K1_TYP_TEKST, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == 0 && do_nadz.typ == K1_KOM_SKARB_URUCHOM && do_nadz.a == K1_APL_WYBOR && do_nadz.obiekt == 0);
    k = na(kn2, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_CZEKA); /* one request at a time */
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, obcy, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA); /* only the picker answers */
    dostep_nowy_klient(kw[1], K1_PROFIL_WYBOR); /* the picker started: it learns who asks */
    k = odbierz(kw);
    EXPECT(k.typ == K1_KOM_DOK_PROSBA && k.a == K1_APL_NOTATKI && k.b == K1_TYP_TEKST);
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, folder, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && odbierz(kn).typ == 0); /* a folder is not a file */
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, obcy, 10, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && fd < 0); /* names only, never content */
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, obcy, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == 0);
    k = odbierz(kn);
    EXPECT(k.typ == K1_KOM_DOK_WYBRANY && k.obiekt == obcy && dostep_ma(K1_PROFIL_NOTATKI, obcy));
    /* asked again, the picker closes without an answer: cancelled */
    k = na(kn, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(odbierz(kw).typ == K1_KOM_DOK_PROSBA); /* the picker still runs: it gets the request at once */
    dostep_koniec_klienta(kw[1], K1_PROFIL_WYBOR, 1);
    k = odbierz(kn);
    EXPECT(k.typ == K1_KOM_DOK_WYBRANY && k.obiekt == 0);
    k = na(kn2, K1_PROFIL_NOTATKI, k1_komunikat_nowy(K1_KOM_DOK_WYBIERZ, 0, K1_TYP_PNG, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == 0); /* free again */
    dostep_nowy_klient(kw[1], K1_PROFIL_WYBOR);
    EXPECT(odbierz(kw).b == K1_TYP_PNG);
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, obcy, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && odbierz(kn2).typ == 0); /* text, but PNG was asked for */
    dostep_koniec_klienta(kn2[1], K1_PROFIL_NOTATKI, 0); /* the one that asked is gone: the request with it */
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, 0, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK);
    dostep_koniec_klienta(kw[1], K1_PROFIL_WYBOR, 1);
    close(kw[0]);
    close(kw[1]);
    close(kn2[0]);
    close(kn2[1]);
}

/* 0.8.2 step 4 (ADR 0037): Podgląd sees the pictures of the folders of the files the user opened in it — read only, and
 * Kosz (D2) — nothing else: no text beside them, no other folder, nothing without a gesture, nothing after it closes. */
static void test_podglad(int *kn, uint32_t tekst, uint32_t folder)
{
    static const uint8_t png[16] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    int kw[2], fd, jest;
    struct k1_komunikat k;
    uint32_t a = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "a.png", png, sizeof png, T0);
    uint32_t b = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "b.png", png, sizeof png, T0);
    uint32_t t = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "opis.txt", (const uint8_t *)"Morze", 5, T0);
    uint32_t c = nowy(folder, K1_DOK_PLIK, "c.png", png, sizeof png, T0);
    uint32_t d = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "d.png", png, sizeof png, T0);
    EXPECT(a && b && t && c && d && dok_do_kosza(d, T0) == 0 && dok_wpis(a)->typ == K1_TYP_PNG);
    EXPECT(k1_dok_domyslna(K1_TYP_PNG) == K1_APL_PODGLAD && k1_dok_domyslna(K1_TYP_WEBP) == K1_APL_PODGLAD &&
           k1_dok_otwiera(K1_APL_PODGLAD, K1_TYP_GIF) && !k1_dok_otwiera(K1_APL_PODGLAD, K1_TYP_TEKST) &&
           !k1_dok_otwiera(K1_APL_PODGLAD, K1_TYP_PDF));
    /* no gesture yet: nothing */
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, a, &jest) == 0 && !jest);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, a, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && fd < 0);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, a, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && dok_wpis(a)->kosz == 0);
    /* a double click on a.png in Pliki: Podgląd gets it, and the pictures of Obrazy around it */
    memset(&do_nadz, 0, sizeof do_nadz);
    k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, t, K1_APL_PODGLAD, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && do_nadz.typ == 0); /* Podgląd does not open text */
    k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, a, K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && do_nadz.typ == 0); /* Notatki do not open pictures */
    k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, a, K1_APL_PODGLAD, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == 0 && do_nadz.typ == K1_KOM_SKARB_URUCHOM && do_nadz.obiekt == a && do_nadz.a == K1_APL_PODGLAD);
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, b, &jest) == 2 && jest); /* a and b: not the text, not d in Kosz, not c */
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, t, &jest) == 2 && !jest);
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, c, &jest) == 2 && !jest);
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, d, &jest) == 2 && !jest);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, b, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == K1_KOM_DOK_TRESC && k.a == (int32_t)sizeof png && fd >= 0);
    if (fd >= 0)
        close(fd);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, t, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && fd < 0); /* the text beside them */
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, c, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && fd < 0); /* another folder */
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, d, 100, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && fd < 0); /* Kosz */
    /* read only: no change but Kosz */
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, b, K1_FOLDER_PULPIT, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_OPROZNIJ, 0, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && dok_wpis(d) != NULL);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_USUN, d, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && dok_wpis(d) != NULL);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, b, K1_APL_PODGLAD, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, t, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && dok_wpis(t)->kosz == 0);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, b, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == 0 && dok_wpis(b)->kosz != 0); /* D2: Delete puts the picture shown in Kosz */
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, b, &jest) == 1 && !jest);
    /* "Otwórz…": the file picker with pictures only — a text is refused, c.png given with its folder */
    EXPECT(k1_kanal_utworz(kw) == 0);
    k = na(kn, K1_PROFIL_PODGLAD, k1_komunikat_nowy(K1_KOM_DOK_WYBIERZ, 0, 0, K1_RODZINA_OBRAZY, 0, 0, 0), -1, &fd);
    EXPECT(k.typ == 0 && do_nadz.typ == K1_KOM_SKARB_URUCHOM && do_nadz.a == K1_APL_WYBOR);
    dostep_nowy_klient(kw[1], K1_PROFIL_WYBOR);
    k = odbierz(kw);
    EXPECT(k.typ == K1_KOM_DOK_PROSBA && k.a == K1_APL_PODGLAD && k.b == 0 && k.c == K1_RODZINA_OBRAZY);
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, tekst, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == K1_DOK_BLAD_BRAK && odbierz(kn).typ == 0); /* not a picture */
    k = na(kw, K1_PROFIL_WYBOR, k1_komunikat_nowy(K1_KOM_DOK_WYBRANO, c, 0, 0, 0, 0, 0), -1, &fd);
    EXPECT(k.b == 0 && odbierz(kn).typ == K1_KOM_DOK_WYBRANY);
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, c, &jest) == 2 && jest); /* a (Obrazy) and c (its folder) */
    dostep_koniec_klienta(kw[1], K1_PROFIL_WYBOR, 1);
    close(kw[0]);
    close(kw[1]);
    /* Podgląd closed: nothing left */
    dostep_koniec_klienta(kn[1], K1_PROFIL_PODGLAD, 1);
    EXPECT(widzi(kn, K1_PROFIL_PODGLAD, a, &jest) == 0 && !jest);
}

void test_dostep(void)
{
    int kn[2];
    uint32_t obcy, pulpit, wlasny, folder;
    EXPECT(k1_kanal_utworz(kn) == 0);
    dok_wczytaj(T0);
    obcy = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "list.txt", (const uint8_t *)"Droga!", 6, T0);
    pulpit = nowy(K1_FOLDER_PULPIT, K1_DOK_PLIK, "na pulpicie", (const uint8_t *)"hej", 3, T0);
    folder = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Projekt", NULL, 0, T0);
    test_notatki(kn, obcy, &wlasny);
    test_otworz(kn, obcy, pulpit);
    test_wybor(kn, obcy, folder);
    test_podglad(kn, obcy, folder);
    EXPECT(k1_dok_otwiera(K1_APL_NOTATKI, K1_TYP_TEKST) && !k1_dok_otwiera(K1_APL_NOTATKI, K1_TYP_PNG) &&
           k1_dok_otwiera(K1_APL_PLIKI, K1_TYP_PDF) && !k1_dok_otwiera(K1_APL_WYBOR, K1_TYP_TEKST) &&
           !k1_dok_otwiera(K1_APL_PLIKI, 0) && !k1_dok_otwiera(K1_APL_PLIKI, K1_TYPY_MAKS + 1));
    EXPECT(k1_dok_domyslna(K1_TYP_TEKST) == K1_APL_NOTATKI && k1_dok_domyslna(K1_TYP_PDF) == K1_APL_PLIKI);
    close(kn[0]);
    close(kn[1]);
}

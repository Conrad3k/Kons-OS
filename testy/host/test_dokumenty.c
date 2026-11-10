/* K1 — host test of the user's files (0.8.0, ADR 0029): names, types and the entry layout (k1_dokument.c),
 * folders, content in pieces, Kosz with its 30 days (skarbiec dokumenty.c, objects in memory) and the table of
 * rights of the channel (dok_kanal.c over a real socketpair: what Pliki, the desktop and others may do), the access
 * an application gets after a gesture and the file picker (dostep.c, 0.8.0 step 2, ADR 0030). */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_memfd.h"
#include "k1_test_host.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

#define T0 1800000000LL /* 2027 */
#define DZIEN 86400LL

static struct k1_dok_wpis wybrane[K1_DOK_MAKS];
static struct k1_komunikat do_nadz; /* what skarbiec last told nadzorca (main.c sends it on its control channel) */

void do_nadzorcy(struct k1_komunikat m)
{
    do_nadz = m;
}

static int nazwa(const char *s)
{
    return k1_dok_nazwa_poprawna(s, strlen(s));
}

static int z_tekstu(const char *t, const char *oczekiwana)
{
    char n[K1_DOK_NAZWA];
    memset(n, 'X', sizeof n);
    k1_dok_nazwa_z_tekstu(t, strlen(t), n);
    return strcmp(n, oczekiwana) == 0 && k1_dok_nazwa_poprawna(n, strlen(n));
}

/* Notatki (ADR 0030): the name of a note's file follows its first line. */
static void test_nazwa_z_tekstu(void)
{
    char dlugi[200], oczek[60];
    EXPECT(z_tekstu("Lista: zakupy?\nmleko", "Lista zakupy") && z_tekstu("\n\n   Raport  2026  \nx", "Raport 2026"));
    EXPECT(z_tekstu("", "Notatka") && z_tekstu("...", "Notatka") && z_tekstu("  \n\t\n", "Notatka") && z_tekstu("a/b\\c|d", "abcd"));
    EXPECT(z_tekstu("Plan.", "Plan") && z_tekstu("Zażółć K1", "Zażółć K1") && z_tekstu("<>:*?", "Notatka"));
    EXPECT(z_tekstu("zdjecie\xe2\x80\xae" "gpj", "Notatka")); /* a name read backwards: never */
    memset(dlugi, 0, sizeof dlugi);
    for (int i = 0; i < 60; i++)
        memcpy(dlugi + 2 * i, "\xc5\xbc", 2); /* ż: 48 bytes = 24 whole letters */
    memcpy(oczek, dlugi, 48);
    oczek[48] = '\0';
    EXPECT(z_tekstu(dlugi, oczek));
    memset(dlugi, 'a', 47);
    memcpy(dlugi + 47, "\xc5\xbcxyz", 6); /* the 48th byte is half of ż: without it */
    dlugi[53] = '\0';
    memcpy(oczek, dlugi, 47);
    oczek[47] = '\0';
    EXPECT(z_tekstu(dlugi, oczek));
    EXPECT(k1_dok_nazwa_pasuje("Lista", "Lista") && k1_dok_nazwa_pasuje("Lista (2)", "Lista") &&
           k1_dok_nazwa_pasuje("Lista (12)", "Lista"));
    EXPECT(!k1_dok_nazwa_pasuje("Lista (x)", "Lista") && !k1_dok_nazwa_pasuje("Lista 2", "Lista") &&
           !k1_dok_nazwa_pasuje("Lista (2) ", "Lista") && !k1_dok_nazwa_pasuje("Lis", "Lista") &&
           !k1_dok_nazwa_pasuje("Lista ()", "Lista") && !k1_dok_nazwa_pasuje("Listaa", "Lista"));
}

static void test_nazwy(void)
{
    char dluga[200];
    EXPECT(nazwa("Raport 2026.txt") && nazwa("Zażółć gęślą jaźń") && nazwa("a") && nazwa(".ukryty"));
    EXPECT(!nazwa("") && !nazwa("a/b") && !nazwa("a\\b") && !nazwa("c:") && !nazwa("x*") && !nazwa("?") &&
           !nazwa("\"") && !nazwa("<a>") && !nazwa("a|b"));
    EXPECT(!nazwa(" a") && !nazwa("a ") && !nazwa("a.") && !nazwa("a\tb") && !nazwa("a\nb") && !nazwa("\x7f"));
    EXPECT(!nazwa("\xc0\x80") && !nazwa("\xe0\x80\x80") && !nazwa("\xed\xa0\x80") && !nazwa("\xc5") && !nazwa("\xff"));
    EXPECT(!nazwa("zdjecie\xe2\x80\xae" "gpj.exe")); /* U+202E: the name would read backwards */
    EXPECT(!nazwa("a\xc2\x85")); /* C1 control */
    memset(dluga, 'x', sizeof dluga);
    EXPECT(k1_dok_nazwa_poprawna(dluga, 127) && !k1_dok_nazwa_poprawna(dluga, 128));
    EXPECT(k1_dok_nazwy_rowne("Nowy Folder", "nowy folder") && k1_dok_nazwy_rowne("ŁÓDŹ", "łódź") &&
           k1_dok_nazwy_rowne("Ąę", "ąĘ") && !k1_dok_nazwy_rowne("a", "b") && !k1_dok_nazwy_rowne("ab", "a"));
}

static void test_typy(void)
{
    static const uint8_t png[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0 };
    static const uint8_t jpg[] = { 0xff, 0xd8, 0xff, 0xe0 };
    EXPECT(k1_dok_typ(png, sizeof png, 100) == K1_TYP_PNG && k1_dok_typ(jpg, 4, 4) == K1_TYP_JPEG);
    EXPECT(k1_dok_typ((const uint8_t *)"%PDF-1.7", 8, 8) == K1_TYP_PDF);
    EXPECT(k1_dok_typ((const uint8_t *)"PK\x03\x04", 4, 4) == K1_TYP_ZIP);
    EXPECT(k1_dok_typ((const uint8_t *)"Zażółć\n\tgęślą", 17, 17) == K1_TYP_TEKST);
    EXPECT(k1_dok_typ((const uint8_t *)"", 0, 0) == K1_TYP_TEKST);
    EXPECT(k1_dok_typ((const uint8_t *)"ab\0cd", 5, 5) == K1_TYP_INNY);
    EXPECT(k1_dok_typ((const uint8_t *)"ab\xc5", 3, 10) == K1_TYP_TEKST); /* a letter cut by the sample */
    EXPECT(k1_dok_typ((const uint8_t *)"ab\xc5", 3, 3) == K1_TYP_INNY);   /* …or by the end of the file */
    EXPECT(k1_dok_typ((const uint8_t *)"\x1b[2J", 4, 4) == K1_TYP_INNY);  /* terminal codes are not text */
}

static struct k1_dok_wpis przyklad(void)
{
    struct k1_dok_wpis w;
    memset(&w, 0, sizeof w);
    w.id = 100;
    w.rodzic = K1_FOLDER_DOKUMENTY;
    w.rodzaj = K1_DOK_PLIK;
    w.typ = K1_TYP_TEKST;
    w.pochodzenie = K1_POCH_K1;
    w.tworca = K1_PROFIL_PLIKI;
    w.generacja = 3;
    w.rozmiar = 5;
    w.utworzono = T0;
    w.zmieniono = T0 + 1;
    strcpy(w.nazwa, "list.txt");
    return w;
}

static void test_wpis(void)
{
    struct k1_dok_wpis w = przyklad(), r;
    uint8_t b[K1_DOK_WPIS_BAJTY], z[K1_DOK_WPIS_BAJTY];
    static uint8_t lista[K1_DOK_LISTA_NAGL + 3 * K1_DOK_WPIS_BAJTY];
    struct k1_dok_wpis trzy[3], wynik[3];
    uint32_t kosz = 9;
    size_t dl;
    k1_dok_wpis_zapisz(&w, b);
    EXPECT(k1_dok_wpis_czytaj(b, &r) == 0 && memcmp(&r, &w, sizeof w) == 0);
    /* every field out of range is refused */
    static const struct { int off; uint8_t v; } zle[] = { { 0, 5 }, { 4, 0 }, { 4, 50 }, { 8, 3 }, { 9, 0 }, { 9, 99 },
                                                         { 10, 0 }, { 10, 9 }, { 11, 0 }, { 11, 200 }, { 23, 1 }, { 31, 0x80 },
                                                         { 48, 0 }, { 48, '/' }, { 175, 'x' }, { 120, 'x' } };
    for (size_t i = 0; i < sizeof zle / sizeof zle[0]; i++) {
        memcpy(z, b, sizeof z);
        z[zle[i].off] = zle[i].v;
        EXPECT(k1_dok_wpis_czytaj(z, &r) != 0 && r.id == 0);
    }
    w.rodzaj = K1_DOK_FOLDER; /* a folder has no type, size or content */
    k1_dok_wpis_zapisz(&w, b);
    EXPECT(k1_dok_wpis_czytaj(b, &r) != 0);
    w.typ = 0;
    w.rozmiar = 0;
    w.generacja = 0;
    k1_dok_wpis_zapisz(&w, b);
    EXPECT(k1_dok_wpis_czytaj(b, &r) == 0);
    /* the list */
    trzy[0] = trzy[1] = trzy[2] = przyklad();
    trzy[1].id = 101;
    trzy[2].id = 102;
    dl = k1_dok_lista_zapisz(trzy, 3, 2, lista);
    EXPECT(dl == sizeof lista && k1_dok_lista_czytaj(lista, dl, wynik, 3, &kosz) == 3 && kosz == 2 && wynik[2].id == 102);
    EXPECT(k1_dok_lista_czytaj(lista, dl - 1, wynik, 3, &kosz) < 0 && k1_dok_lista_czytaj(lista, dl, wynik, 2, &kosz) < 0);
    trzy[2].id = 100; /* the same number twice */
    k1_dok_lista_zapisz(trzy, 3, 2, lista);
    EXPECT(k1_dok_lista_czytaj(lista, dl, wynik, 3, &kosz) < 0);
    lista[12] = 1;
    EXPECT(k1_dok_lista_czytaj(lista, K1_DOK_LISTA_NAGL, wynik, 3, &kosz) < 0);
}

/* ---- dokumenty.c over objects in memory ---- */

struct bufor {
    const uint8_t *dane;
    uint8_t *wy;
    uint64_t dl;
};
static int czytaj_bufor(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct bufor *b = ctx;
    if (od + n > b->dl)
        return -1;
    memcpy(buf, b->dane + od, n);
    return 0;
}
static int pisz_bufor(void *ctx, const void *buf, uint32_t n)
{
    struct bufor *b = ctx;
    memcpy(b->wy + b->dl, buf, n);
    b->dl += n;
    return 0;
}

static uint32_t nowy(uint32_t folder, int rodzaj, const char *n, const uint8_t *dane, uint64_t dl, int64_t t)
{
    struct bufor b = { dane, NULL, dl };
    struct dok_zrodlo z = { czytaj_bufor, &b, dl };
    uint32_t id = 0;
    int r = dok_nowy(folder, rodzaj, n, strlen(n), rodzaj == K1_DOK_PLIK ? &z : NULL, K1_PROFIL_PLIKI, K1_POCH_K1, t, &id);
    return r == 0 ? id : 0;
}

static void licz(const char *n, void *ctx)
{
    (void)n;
    (*(int *)ctx)++;
}
static int obiektow(void)
{
    int n = 0;
    ob_kazdy(K1_WL_DOKUMENTY, licz, &n);
    return n;
}

static void test_tresc(void)
{
    uint64_t dl = 2 * K1_DOK_KAWALEK + 12345, ile = 0;
    uint8_t *dane = malloc(dl), *wy = malloc(dl);
    struct bufor b = { NULL, wy, 0 };
    struct dok_cel c = { pisz_bufor, &b };
    uint32_t id;
    for (uint64_t i = 0; i < dl; i++)
        dane[i] = (uint8_t)(i * 7 + (i >> 11));
    dane[0] = 0xff, dane[1] = 0xd8, dane[2] = 0xff;
    dok_wczytaj(T0);
    id = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "zdjęcie.jpg", dane, dl, T0);
    EXPECT(id >= K1_DOK_PIERWSZY && dok_wpis(id)->typ == K1_TYP_JPEG && dok_wpis(id)->rozmiar == dl);
    EXPECT(obiektow() == 1 + 3); /* the entry and three pieces */
    EXPECT(dok_czytaj_tresc(id, dl + 99, &c, &ile) == 0 && ile == dl && memcmp(wy, dane, dl) == 0);
    b.dl = 0;
    EXPECT(dok_czytaj_tresc(id, 100, &c, &ile) == 0 && ile == 100 && b.dl == 100); /* a preview: the start only */
    {
        struct bufor nb = { (const uint8_t *)"krótko", NULL, 7 };
        struct dok_zrodlo z = { czytaj_bufor, &nb, 7 };
        EXPECT(dok_zapisz_tresc(id, &z, T0 + 5) == 0 && dok_wpis(id)->typ == K1_TYP_TEKST && dok_wpis(id)->generacja == 2);
        EXPECT(obiektow() == 2); /* the old pieces are gone */
        b.dl = 0;
        EXPECT(dok_czytaj_tresc(id, 1000, &c, &ile) == 0 && ile == 7 && memcmp(wy, "krótko", 7) == 0);
    }
    { /* a source that fails half way: nothing changes, no pieces left behind */
        struct bufor zb = { dane, NULL, dl - 1 };
        struct dok_zrodlo z = { czytaj_bufor, &zb, dl };
        EXPECT(dok_zapisz_tresc(id, &z, T0 + 6) != 0 && dok_wpis(id)->generacja == 2 && obiektow() == 2);
    }
    free(dane);
    free(wy);
}

static void test_foldery(void)
{
    uint32_t a, a2, p1, p2, g[K1_DOK_GLEBOKOSC + 1];
    int i;
    dok_wczytaj(T0);
    a = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Nowy folder", NULL, 0, T0);
    a2 = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "nowy FOLDER", NULL, 0, T0);
    EXPECT(a && a2 && strcmp(dok_wpis(a2)->nazwa, "nowy FOLDER (2)") == 0);
    p1 = nowy(a, K1_DOK_PLIK, "list.txt", (const uint8_t *)"x", 1, T0);
    p2 = nowy(a, K1_DOK_PLIK, "List.txt", (const uint8_t *)"y", 1, T0);
    EXPECT(p1 && p2 && strcmp(dok_wpis(p2)->nazwa, "List (2).txt") == 0);
    EXPECT(dok_zmien_nazwe(p2, "LIST.TXT", 8, 0, T0) == K1_DOK_BLAD_ZAJETA);
    EXPECT(dok_zmien_nazwe(p2, "zły/znak", 10, 0, T0) == K1_DOK_BLAD_NAZWA);
    EXPECT(dok_zmien_nazwe(p2, "Plan.txt", 8, 0, T0) == 0 && strcmp(dok_wpis(p2)->nazwa, "Plan.txt") == 0);
    EXPECT(dok_zmien_nazwe(K1_FOLDER_PULPIT, "X", 1, 0, T0) == K1_DOK_BLAD_BRAK); /* standard folders stay */
    EXPECT(nowy(p1, K1_DOK_PLIK, "w pliku", NULL, 0, T0) == 0);                /* a file is not a folder */
    EXPECT(nowy(77, K1_DOK_PLIK, "nigdzie", NULL, 0, T0) == 0);
    /* moving: never into itself or deeper inside itself */
    g[0] = nowy(a, K1_DOK_FOLDER, "g0", NULL, 0, T0);
    EXPECT(dok_przenies(a, a, T0) == K1_DOK_BLAD_MIEJSCE && dok_przenies(a, g[0], T0) == K1_DOK_BLAD_MIEJSCE);
    EXPECT(dok_przenies(p1, K1_FOLDER_PULPIT, T0) == 0 && dok_wpis(p1)->rodzic == K1_FOLDER_PULPIT);
    EXPECT(dok_przenies(p2, K1_FOLDER_PULPIT, T0) == 0);
    /* depth: a (1) / g0 (2) / … up to 8 levels */
    for (i = 1; i < K1_DOK_GLEBOKOSC - 1; i++)
        g[i] = nowy(g[i - 1], K1_DOK_FOLDER, "g", NULL, 0, T0);
    EXPECT(g[K1_DOK_GLEBOKOSC - 2] != 0);
    EXPECT(nowy(g[K1_DOK_GLEBOKOSC - 2], K1_DOK_FOLDER, "za głęboko", NULL, 0, T0) == 0);
    EXPECT(nowy(g[K1_DOK_GLEBOKOSC - 2], K1_DOK_PLIK, "plik może", NULL, 0, T0) != 0);
    EXPECT(dok_przenies(a2, g[K1_DOK_GLEBOKOSC - 2], T0) == K1_DOK_BLAD_MIEJSCE);
}

static void test_kosz(void)
{
    uint32_t f, w, p, inny;
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Projekt", NULL, 0, T0);
    w = nowy(f, K1_DOK_PLIK, "w środku.txt", (const uint8_t *)"abc", 3, T0);
    p = nowy(K1_FOLDER_PULPIT, K1_DOK_PLIK, "na pulpicie.txt", NULL, 0, T0);
    EXPECT(dok_do_kosza(f, T0 + 10) == 0 && dok_w_koszu() == 1 && !dok_widoczny(f) && !dok_widoczny(w));
    EXPECT(dok_zmien_nazwe(w, "inna", 4, 0, T0) == K1_DOK_BLAD_BRAK); /* inside Kosz: no changes */
    EXPECT(nowy(f, K1_DOK_PLIK, "do kosza", NULL, 0, T0) == 0);
    EXPECT(dok_usun_na_zawsze(p) == K1_DOK_BLAD_BRAK); /* only from Kosz */
    inny = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Projekt", NULL, 0, T0); /* the name is free again */
    EXPECT(inny && strcmp(dok_wpis(inny)->nazwa, "Projekt") == 0);
    EXPECT(dok_przywroc(f) == 0 && dok_widoczny(w) && strcmp(dok_wpis(f)->nazwa, "Projekt (2)") == 0);
    /* restored when its folder is in Kosz: to Dokumenty */
    EXPECT(dok_do_kosza(w, T0) == 0 && dok_do_kosza(f, T0) == 0 && dok_przywroc(w) == 0 &&
           dok_wpis(w)->rodzic == K1_FOLDER_DOKUMENTY && dok_widoczny(w));
    /* 30 days, by the clock; a clock moved back deletes nothing */
    EXPECT(dok_przeterminowane(T0 + 29 * DZIEN) == 0 && dok_wpis(f) != NULL);
    EXPECT(dok_przeterminowane(T0 - 5 * DZIEN) == 0 && dok_wpis(f) != NULL);
    EXPECT(dok_przeterminowane(T0 + 30 * DZIEN) == 1 && dok_wpis(f) == NULL && dok_w_koszu() == 0);
    EXPECT(dok_do_kosza(p, T0) == 0 && dok_do_kosza(inny, T0) == 0 && dok_oproznij_kosz() == 0 && dok_w_koszu() == 0 &&
           dok_wpis(p) == NULL);
}

static int dok_kazdy(const struct k1_dok_wpis *w)
{
    (void)w;
    return 1;
}

static void test_wczytanie(void)
{
    uint8_t smieci[K1_DOK_WPIS_BAJTY];
    uint32_t id, n0;
    int n, i, ten_sam = 1;
    dok_wczytaj(T0);
    n = dok_wybierz(dok_kazdy, wybrane);
    dok_wczytaj(T0); /* loading again changes nothing (the deepest folders and files included) */
    for (i = 0; i < n; i++)
        ten_sam &= dok_wpis(wybrane[i].id) != NULL && dok_wpis(wybrane[i].id)->rodzic == wybrane[i].rodzic;
    EXPECT(n > 10 && ten_sam);
    id = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, "zostaje.txt", (const uint8_t *)"treść", 6, T0);
    n0 = (uint32_t)obiektow();
    memset(smieci, 0x41, sizeof smieci);
    ob_zapisz(K1_WL_DOKUMENTY, "m5000", smieci, sizeof smieci);  /* a damaged entry */
    ob_zapisz(K1_WL_DOKUMENTY, "t4242g1k0", "x", 1);             /* a piece without an entry */
    ob_zapisz(K1_WL_DOKUMENTY, "t100g99k0", "x", 1);             /* a piece of an old generation */
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wpis(id) != NULL && strcmp(dok_wpis(id)->nazwa, "zostaje.txt") == 0 && dok_wpis(5000) == NULL);
    EXPECT((uint32_t)obiektow() == n0 + 1); /* the pieces went, the refused entry stays untouched */
    ob_usun(K1_WL_DOKUMENTY, "m5000");
}

/* ---- the channel: rights ---- */

static int kan[2];

static int na_pulpicie(const struct k1_dok_wpis *w)
{
    return w->rodzic == K1_FOLDER_PULPIT && w->kosz == 0;
}

static struct k1_komunikat zapytaj(int wl, struct k1_komunikat m, int fd, int *zmiana, int *fd_odp)
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
    return k;
}

static void test_prawa(void)
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
    EXPECT(k.typ == K1_KOM_DOK_TRESC && k.a == 6 && k.b == 6 && fd >= 0);
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
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, plik, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_DOK_BLAD_BRAK && !zm && dok_widoczny(plik));
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, pulpit, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == 0 && zm && !dok_widoczny(pulpit));
    k = zapytaj(K1_PROFIL_GUI, k1_komunikat_nowy(K1_KOM_DOK_PRZYWROC, pulpit, 0, 0, 0, 0, 0), -1, &zm, &fd);
    EXPECT(k.b == K1_DOK_BLAD_PRAWA && !zm);
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
static struct k1_komunikat na(int *kanal, int wl, struct k1_komunikat m, int fd, int *fd_odp)
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

static void test_dostep(void)
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
    EXPECT(k1_dok_otwiera(K1_APL_NOTATKI, K1_TYP_TEKST) && !k1_dok_otwiera(K1_APL_NOTATKI, K1_TYP_PNG) &&
           k1_dok_otwiera(K1_APL_PLIKI, K1_TYP_PDF) && !k1_dok_otwiera(K1_APL_WYBOR, K1_TYP_TEKST) &&
           !k1_dok_otwiera(K1_APL_PLIKI, 0) && !k1_dok_otwiera(K1_APL_PLIKI, K1_TYPY_MAKS + 1));
    EXPECT(k1_dok_domyslna(K1_TYP_TEKST) == K1_APL_NOTATKI && k1_dok_domyslna(K1_TYP_JPEG) == K1_APL_PLIKI);
    close(kn[0]);
    close(kn[1]);
}

int main(void)
{
    test_nazwy();
    test_nazwa_z_tekstu();
    test_typy();
    test_wpis();
    test_tresc();
    test_foldery();
    test_kosz();
    test_wczytanie();
    test_prawa();
    test_dostep();
    return K1_TEST_KONIEC("test_dokumenty");
}

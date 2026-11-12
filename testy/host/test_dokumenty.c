/* K1 — host test of the user's files (0.8.0, ADR 0029): names, types and the entry layout (k1_dokument.c),
 * folders, content in pieces, Kosz with its 30 days (skarbiec dokumenty.c, objects in memory) and the table of
 * rights of the channel (dok_kanal.c over a real socketpair: what Pliki, the desktop and others may do), the access
 * an application gets after a gesture and the file picker (dostep.c, 0.8.0 step 2, ADR 0030). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_memfd.h"
#include "k1_test_host.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

#define T0 4000000000LL /* 2096: always after the real clock, which reaches skarbiec through dok_kanal.c */
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

/* Counts the objects of entries and content (m…, t…); skarbiec keeps a few more of its own (the time seen…). */
static void licz(const char *n, void *ctx)
{
    if (n[0] == 'm' || n[0] == 't')
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
    EXPECT(dok_czytaj_tresc(id, 0, dl + 99, &c, &ile) == 0 && ile == dl && memcmp(wy, dane, dl) == 0);
    b.dl = 0;
    EXPECT(dok_czytaj_tresc(id, 0, 100, &c, &ile) == 0 && ile == 100 && b.dl == 100); /* a preview: the start only */
    {
        struct bufor nb = { (const uint8_t *)"krótko", NULL, 7 };
        struct dok_zrodlo z = { czytaj_bufor, &nb, 7 };
        EXPECT(dok_zapisz_tresc(id, &z, T0 + 5) == 0 && dok_wpis(id)->typ == K1_TYP_TEKST && dok_wpis(id)->generacja == 2);
        EXPECT(obiektow() == 2); /* the old pieces are gone */
        b.dl = 0;
        EXPECT(dok_czytaj_tresc(id, 0, 1000, &c, &ile) == 0 && ile == 7 && memcmp(wy, "krótko", 7) == 0);
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
    EXPECT(dok_przenies(a, a, 0, T0) == K1_DOK_BLAD_MIEJSCE && dok_przenies(a, g[0], 0, T0) == K1_DOK_BLAD_MIEJSCE);
    EXPECT(dok_przenies(p1, K1_FOLDER_PULPIT, 0, T0) == 0 && dok_wpis(p1)->rodzic == K1_FOLDER_PULPIT);
    EXPECT(dok_przenies(p2, K1_FOLDER_PULPIT, 0, T0) == 0);
    /* depth: a (1) / g0 (2) / … up to 8 levels */
    for (i = 1; i < K1_DOK_GLEBOKOSC - 1; i++)
        g[i] = nowy(g[i - 1], K1_DOK_FOLDER, "g", NULL, 0, T0);
    EXPECT(g[K1_DOK_GLEBOKOSC - 2] != 0);
    EXPECT(nowy(g[K1_DOK_GLEBOKOSC - 2], K1_DOK_FOLDER, "za głęboko", NULL, 0, T0) == 0);
    EXPECT(nowy(g[K1_DOK_GLEBOKOSC - 2], K1_DOK_PLIK, "plik może", NULL, 0, T0) != 0);
    EXPECT(dok_przenies(a2, g[K1_DOK_GLEBOKOSC - 2], 0, T0) == K1_DOK_BLAD_MIEJSCE);
    /* a taken name (0.8.2 step 6): refused, or "Zachowaj oba" — " (2)" */
    EXPECT(nowy(K1_FOLDER_MUZYKA, K1_DOK_PLIK, "Utwór.txt", NULL, 0, T0) != 0);
    i = (int)nowy(K1_FOLDER_WIDEO, K1_DOK_PLIK, "utwór.TXT", NULL, 0, T0);
    EXPECT(i != 0 && dok_przenies((uint32_t)i, K1_FOLDER_WIDEO, 0, T0) == 0); /* already there: nothing to do */
    EXPECT(dok_przenies((uint32_t)i, K1_FOLDER_MUZYKA, 0, T0) == K1_DOK_BLAD_ZAJETA && dok_wpis((uint32_t)i)->rodzic == K1_FOLDER_WIDEO);
    EXPECT(dok_przenies((uint32_t)i, K1_FOLDER_MUZYKA, 1, T0) == 0 && strcmp(dok_wpis((uint32_t)i)->nazwa, "utwór (2).TXT") == 0 &&
           dok_wpis((uint32_t)i)->rodzic == K1_FOLDER_MUZYKA);
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
    EXPECT(dok_przywroc(f, 0) == 0 && dok_widoczny(w) && strcmp(dok_wpis(f)->nazwa, "Projekt (2)") == 0);
    /* restored when its folder is in Kosz: to Dokumenty */
    EXPECT(dok_do_kosza(w, T0) == 0 && dok_do_kosza(f, T0) == 0 && dok_przywroc(w, 0) == 0 &&
           dok_wpis(w)->rodzic == K1_FOLDER_DOKUMENTY && dok_widoczny(w));
    /* restored where it was dropped (0.8.2 step 6): a visible folder, never one in Kosz, nor inside itself */
    EXPECT(dok_do_kosza(w, T0) == 0 && dok_przywroc(w, f) == K1_DOK_BLAD_MIEJSCE && dok_przywroc(w, 77) == K1_DOK_BLAD_MIEJSCE);
    EXPECT(dok_przywroc(w, K1_FOLDER_WIDEO) == 0 && dok_wpis(w)->rodzic == K1_FOLDER_WIDEO && dok_widoczny(w));
    EXPECT(dok_przywroc(f, f) == K1_DOK_BLAD_MIEJSCE && dok_wpis(f)->kosz != 0);
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
    if (k.typ == K1_KOM_DOK_LISTA) { /* 0.8.1 step 3: the room for files right after every list */
        struct k1_komunikat mk;
        EXPECT(k1_kanal_odbierz(kan[0], &o) == K1_KANAL_KOMUNIKAT && o.fd < 0 &&
               k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &mk) == 0 && mk.typ == K1_KOM_SKARB_MIEJSCE &&
               mk.c == dok_ile());
    }
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
    test_podglad(kn, obcy, folder);
    EXPECT(k1_dok_otwiera(K1_APL_NOTATKI, K1_TYP_TEKST) && !k1_dok_otwiera(K1_APL_NOTATKI, K1_TYP_PNG) &&
           k1_dok_otwiera(K1_APL_PLIKI, K1_TYP_PDF) && !k1_dok_otwiera(K1_APL_WYBOR, K1_TYP_TEKST) &&
           !k1_dok_otwiera(K1_APL_PLIKI, 0) && !k1_dok_otwiera(K1_APL_PLIKI, K1_TYPY_MAKS + 1));
    EXPECT(k1_dok_domyslna(K1_TYP_TEKST) == K1_APL_NOTATKI && k1_dok_domyslna(K1_TYP_PDF) == K1_APL_PLIKI);
    close(kn[0]);
    close(kn[1]);
}


/* ---- 0.8.1 step 1: nothing is lost ---- */

/* The bytes of entry `id` (its object m<id>) changed by `zmien`, written back. */
static void zepsuj_wpis(uint32_t id, void (*zmien)(uint8_t *b))
{
    uint8_t b[K1_DOK_WPIS_BAJTY];
    char n[32];
    uint32_t dl = 0;
    snprintf(n, sizeof n, "m%u", id);
    EXPECT(ob_czytaj(K1_WL_DOKUMENTY, n, b, sizeof b, &dl) == K1_MAG_OK && dl == sizeof b);
    zmien(b);
    EXPECT(ob_zapisz(K1_WL_DOKUMENTY, n, b, sizeof b) == K1_MAG_OK);
}
static uint8_t kopia_wpisu[K1_DOK_WPIS_BAJTY];
static void zly_bajt_nazwy(uint8_t *b) { memcpy(kopia_wpisu, b, sizeof kopia_wpisu); b[48 + 2] = 0xff; }
static void zly_rodzaj(uint8_t *b) { memcpy(kopia_wpisu, b, sizeof kopia_wpisu); b[8] = 7; }
static void przywroc_wpis(uint8_t *b) { memcpy(b, kopia_wpisu, sizeof kopia_wpisu); }
static void zla_nazwa_i_pola(uint8_t *b)
{
    memset(b + 48, 0x01, 20); /* only control characters left */
    b[11] = 200;             /* who made it: no such client */
    b[4] = 50, b[5] = b[6] = b[7] = 0; /* its folder: a number no folder can have */
    b[9] = 99;               /* the type */
}
static void zla_generacja(uint8_t *b) { memcpy(kopia_wpisu, b, sizeof kopia_wpisu); b[12] ^= 0x10; }

/* The content of `id` equals `dane` (dl bytes). */
static int tresc_rowna(uint32_t id, const uint8_t *dane, uint64_t dl)
{
    uint8_t *wy = malloc(dl + 1);
    struct bufor b = { NULL, wy, 0 };
    struct dok_cel c = { pisz_bufor, &b };
    uint64_t ile = 0;
    int ok = dok_czytaj_tresc(id, 0, dl + 1, &c, &ile) == 0 && ile == dl && memcmp(wy, dane, dl) == 0;
    free(wy);
    return ok;
}

/* T2 of the plan: an entry that does not pass the check at start is repaired, never deleted with its content. */
static void test_naprawa(void)
{
    uint64_t dl = 3 * K1_DOK_KAWALEK;
    uint8_t *dane = malloc(dl);
    uint32_t id, id2, inny;
    int n0;
    for (uint64_t i = 0; i < dl; i++)
        dane[i] = (uint8_t)(i * 13 + 1);
    dane[0] = 0xff, dane[1] = 0xd8, dane[2] = 0xff;
    dok_wczytaj(T0);
    inny = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "wa_acje.jpg", (const uint8_t *)"x", 1, T0);
    id = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "wakacje.jpg", dane, dl, T0);
    n0 = obiektow();
    /* one bad byte of the name: the name repaired (taken: " (2)"), the folder and the content stay */
    zepsuj_wpis(id, zly_bajt_nazwy);
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wpis(id) != NULL && dok_wpis(id)->rodzic == K1_FOLDER_OBRAZY && dok_wpis(id)->rozmiar == dl &&
           dok_wpis(id)->typ == K1_TYP_JPEG && strcmp(dok_wpis(id)->nazwa, "wa_acje (2).jpg") == 0);
    EXPECT(obiektow() == n0 && tresc_rowna(id, dane, dl) && dok_wpis(inny) != NULL);
    dok_wczytaj(T0 + 2); /* the repair was written: the next start finds a valid entry */
    EXPECT(dok_wpis(id) != NULL && strcmp(dok_wpis(id)->nazwa, "wa_acje (2).jpg") == 0 && obiektow() == n0);
    /* a kind no version knows: not loaded, but nothing deleted — a later fix (or version) finds it whole */
    zepsuj_wpis(id, zly_rodzaj);
    dok_wczytaj(T0 + 3);
    EXPECT(dok_wpis(id) == NULL && obiektow() == n0);
    EXPECT(nowy(K1_FOLDER_OBRAZY, K1_DOK_FOLDER, "nowszy", NULL, 0, T0) > id); /* never its number */
    n0++;
    zepsuj_wpis(id, przywroc_wpis);
    dok_wczytaj(T0 + 4);
    EXPECT(dok_wpis(id) != NULL && tresc_rowna(id, dane, dl));
    /* a generation whose content is not there: no piece of that file is deleted (nothing is guessed) */
    zepsuj_wpis(id, zla_generacja);
    dok_wczytaj(T0 + 5);
    EXPECT(obiektow() == n0);
    zepsuj_wpis(id, przywroc_wpis);
    dok_wczytaj(T0 + 5);
    EXPECT(dok_wpis(id) != NULL && tresc_rowna(id, dane, dl));
    /* nothing of the name left, no such creator, folder or type: "Odzyskany plik <n>" in Dokumenty, type "Plik" */
    id2 = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, "dokument", (const uint8_t *)"abc", 3, T0);
    zepsuj_wpis(id2, zla_nazwa_i_pola);
    dok_wczytaj(T0 + 6);
    {
        char oczek[64];
        const struct k1_dok_wpis *w = dok_wpis(id2);
        snprintf(oczek, sizeof oczek, "Odzyskany plik %u", id2);
        EXPECT(w != NULL && strcmp(w->nazwa, oczek) == 0 && w->rodzic == K1_FOLDER_DOKUMENTY && w->tworca == K1_PROFIL_PLIKI &&
               w->typ == K1_TYP_INNY && tresc_rowna(id2, (const uint8_t *)"abc", 3) && dok_wpis(id) != NULL);
    }
    free(dane);
}


#define ROK_2000 946684800LL

/* T1/3 of the plan: a wrong clock never empties Kosz early, a time from a wrong clock is set right. */
static void test_zegar(void)
{
    uint32_t a, b2, c;
    ob_usun(K1_WL_DOKUMENTY, "czas"); /* the latest clock seen by the tests before: forgotten */
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    /* a clock at 2000 (a weak battery): the time written is the latest seen, Kosz deletes nothing */
    EXPECT(dok_czas(ROK_2000) >= T0);
    a = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "a.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(a, ROK_2000) == 0); /* as 0.8.0 wrote it */
    EXPECT(dok_przeterminowane(ROK_2000 + 40 * DZIEN) == 0 && dok_wpis(a) != NULL);
    /* the right date again: 30 days from then, not "26 years in Kosz" */
    EXPECT(dok_przeterminowane(T0 + DZIEN) == 0 && dok_wpis(a) != NULL && dok_wpis(a)->kosz == T0 + DZIEN);
    EXPECT(dok_przeterminowane(T0 + 30 * DZIEN) == 0 && dok_przeterminowane(T0 + 31 * DZIEN) == 1 && dok_wpis(a) == NULL);
    /* 5 years ahead when it went to Kosz: 30 days from the right date, not 5 years */
    b2 = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "b.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(b2, T0 + 5 * 365 * DZIEN) == 0);
    EXPECT(dok_przeterminowane(T0 + 32 * DZIEN) == 0 && dok_wpis(b2) != NULL && dok_wpis(b2)->kosz == T0 + 32 * DZIEN);
    EXPECT(dok_przeterminowane(T0 + 62 * DZIEN) == 1 && dok_wpis(b2) == NULL);
    /* the clock moved back after a later time was seen: nothing goes until it is there again */
    c = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "c.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(c, T0 + 62 * DZIEN) == 0);
    EXPECT(dok_czas(T0 + 100 * DZIEN) == T0 + 100 * DZIEN && dok_czas(T0 + 95 * DZIEN) == T0 + 100 * DZIEN);
    EXPECT(dok_przeterminowane(T0 + 95 * DZIEN) == 0 && dok_wpis(c) != NULL);
    /* the latest time survives a restart */
    dok_wczytaj(ROK_2000);
    EXPECT(dok_wpis(c) != NULL && dok_czas(ROK_2000) == T0 + 100 * DZIEN);
    EXPECT(dok_przeterminowane(T0 + 100 * DZIEN) == 1 && dok_wpis(c) == NULL);
    /* a computer that never had a right time: never before this K1's build */
    EXPECT(ob_usun(K1_WL_DOKUMENTY, "czas") == K1_MAG_OK);
    dok_wczytaj(ROK_2000);
    EXPECT(dok_czas(ROK_2000) == K1_CZAS_BUDOWY);
}

static int popraw(const char *s, const char *oczekiwana)
{
    char n[K1_DOK_NAZWA];
    memset(n, 'X', sizeof n);
    k1_dok_nazwa_popraw(s, strlen(s), n);
    return strcmp(n, oczekiwana) == 0 && (n[0] == '\0' || k1_dok_nazwa_poprawna(n, strlen(n)));
}

/* Names repaired (entries at start): "_" for what a name cannot have, whole letters. */
static void test_nazwa_popraw(void)
{
    char dlugi[300], n[K1_DOK_NAZWA];
    EXPECT(popraw("a\xff" "b", "a_b") && popraw("  x y  ", "x y") && popraw("ok.", "ok") && popraw("a/b:c", "a_b_c"));
    EXPECT(popraw("\x01\x02", "__") && popraw("...", "") && popraw("", "") && popraw("Zażółć", "Zażółć"));
    EXPECT(popraw("zdjecie\xe2\x80\xae" "gpj", "zdjecie_gpj") && popraw("\xc5", "_") && popraw("a\xed\xa0\x80" "b", "a___b"));
    memset(dlugi, 0, sizeof dlugi);
    for (int i = 0; i < 140; i++)
        memcpy(dlugi + 2 * i, "\xc5\xbc", 2); /* ż × 140 = 280 bytes */
    k1_dok_nazwa_popraw(dlugi, strlen(dlugi), n);
    EXPECT(strlen(n) == 126 && k1_dok_nazwa_poprawna(n, strlen(n)));
}

/* 0.8.2 step 2 (L9, trial T1/6 of plan 0.8.1): names that look like other names or like nothing are refused —
 * invisible characters, direction marks, spaces of other widths at the edges, a mark on nothing, a letter written as
 * a letter + a mark (one name per look); in the middle a no-break space is a letter like any other. */
static void test_nazwy_podrobki(void)
{
    static const char *const zle[] = {
        "a\xe2\x80\x8b" "b",      /* U+200B zero-width space */
        "a\xc2\xad" "b",          /* U+00AD soft hyphen */
        "a\xe2\x81\xa0" "b",      /* U+2060 word joiner */
        "a\xe2\x80\x8e" "b",      /* U+200E left-to-right mark */
        "a\xe2\x80\x8f" "b",      /* U+200F right-to-left mark */
        "a\xd8\x9c" "b",          /* U+061C Arabic letter mark */
        "\xc2\xa0" "a",           /* U+00A0 at the start */
        "a\xc2\xa0",              /* …at the end */
        "\xe3\x80\x80" "a",       /* U+3000 at the start */
        "a\xe2\x80\xaf",          /* U+202F at the end */
        "\xe3\x85\xa4",           /* U+3164 Hangul filler */
        "a\xf3\xa0\x81\x81",      /* U+E0041 tag */
        "a\xef\xbf\xbe",          /* U+FFFE */
        "a\xf0\x9f\xbf\xbf",      /* U+1FFFF */
        "a\xef\xb7\x90",          /* U+FDD0 */
        "a\xee\x80\x80",          /* U+E000 private use */
        "\xcc\x81" "a",           /* a mark on nothing */
        "a\xcc\xa8.txt",          /* "a" + ogonek: written as "ą" */
        "Zaz\xcc\x87o\xcc\x81\xc5\x82\xc4\x87", /* "Zażółć" from macOS (NFD) */
        "a\xe1\xa0\x8e" "b",      /* U+180E */
    };
    for (size_t i = 0; i < sizeof zle / sizeof zle[0]; i++)
        EXPECT(!nazwa(zle[i]));
    EXPECT(nazwa("a\xc2\xa0" "b") && nazwa("a\xe3\x80\x80" "b")); /* other spaces inside: allowed */
    EXPECT(nazwa("\xc4\x85.txt") && nazwa("q\xcc\xa8") && nazwa("\xc5\x82\xcc\x81")); /* ą; q + ogonek and ł + acute: no one letter */
    EXPECT(nazwa("a\xcc\x81\xcc\x81") == 0 && nazwa("\xc3\xa1\xcc\x81")); /* á + acute: kept as written */
    EXPECT(nazwa("\xf0\x9f\x98\x80") && nazwa("\xe5\x86\x99\xe7\x9c\x9f")); /* an emoji, CJK */
    EXPECT(popraw("a\xcc\xa8.txt", "\xc4\x85.txt") && popraw("Zaz\xcc\x87o\xcc\x81\xc5\x82\xc4\x87", "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"));
    EXPECT(popraw("\xc2\xa0 x\xe3\x80\x80", "x") && popraw("\xcc\x81" "a", "_a") && popraw("a\xe2\x80\x8b" "b", "a_b"));
    EXPECT(popraw("a\xf3\xa0\x81\x81", "a_") && popraw("\xe2\x80\x8b", "_") && popraw("x.\xc2\xa0", "x"));
    /* comparing: Latin-1 and Latin Extended-A capitals */
    EXPECT(k1_dok_nazwy_rowne("\xc3\x84pfel", "\xc3\xa4pfel") && k1_dok_nazwy_rowne("\xc3\x96L", "\xc3\xb6l") &&
           k1_dok_nazwy_rowne("\xc4\x8c\xc5\xa0\xc5\xbd", "\xc4\x8d\xc5\xa1\xc5\xbe") && k1_dok_nazwy_rowne("\xc5\xb8", "\xc3\xbf"));
    EXPECT(!k1_dok_nazwy_rowne("\xc3\x97", "\xc3\xb7") && !k1_dok_nazwy_rowne("\xc4\xb0", "i")); /* × ≠ ÷; İ stays */
    for (uint32_t k = 0x300; k <= 0x328; k++) /* every letter + mark of the table: one letter, valid and different */
        for (uint32_t l = 'A'; l <= 'z'; l++) {
            uint32_t j = k1_dok_jedna_litera(l, k);
            EXPECT(j == 0 || (j >= 0xc0 && j < 0x180 && k1_dok_znak(j) == K1_ZNAK_WSZEDZIE));
        }
    EXPECT(k1_dok_jedna_litera('z', 0x307) == 0x17c && k1_dok_jedna_litera('A', 0x328) == 0x104 &&
           k1_dok_jedna_litera('o', 0x301) == 0xf3 && k1_dok_jedna_litera('x', 0x301) == 0);
}

static const char *nowa_nazwa_wpisu;
static void podmien_nazwe(uint8_t *b)
{
    memset(b + 48, 0, K1_DOK_NAZWA);
    memcpy(b + 48, nowa_nazwa_wpisu, strlen(nowa_nazwa_wpisu));
}

/* L8 (trial T1/2): a free name for a long name with a long "extension" (before: refused as taken); L9: an entry of
 * 0.8.1 whose name is not allowed any more is renamed when the files are read — never refused, nothing lost. */
static void test_nazwy_wolne(void)
{
    char dluga[K1_DOK_NAZWA];
    uint32_t a, b, c, d, e;
    dok_wczytaj(T0);
    memset(dluga, 0, sizeof dluga);
    dluga[0] = 'a', dluga[1] = '.';
    memset(dluga + 2, 'x', 125);
    a = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"1", 1, T0);
    b = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"2", 1, T0);
    EXPECT(a != 0 && b != 0 && strlen(dok_wpis(b)->nazwa) <= 127 && strncmp(dok_wpis(b)->nazwa, "a.xxx", 5) == 0 &&
           strcmp(dok_wpis(b)->nazwa + strlen(dok_wpis(b)->nazwa) - 4, " (2)") == 0);
    memcpy(dluga + 110, ".jpg", 5); /* a long base, a short extension: the base is cut, ".jpg" stays */
    a = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"1", 1, T0);
    b = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"2", 1, T0);
    EXPECT(a != 0 && b != 0 && strcmp(dok_wpis(b)->nazwa + strlen(dok_wpis(b)->nazwa) - 8, " (2).jpg") == 0);
    /* names written by 0.8.1 that are not allowed now */
    c = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "zdjecie.jpg", (const uint8_t *)"z", 1, T0);
    d = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "\xc4\x85.txt", (const uint8_t *)"q", 1, T0);
    e = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "inny.txt", (const uint8_t *)"r", 1, T0);
    nowa_nazwa_wpisu = "zdj\xe2\x80\x8b" "ecie.jpg";
    zepsuj_wpis(c, podmien_nazwe);
    nowa_nazwa_wpisu = "a\xcc\xa8.txt"; /* "ą.txt" from macOS: "ą.txt" is there already */
    zepsuj_wpis(e, podmien_nazwe);
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wpis(c) != NULL && strcmp(dok_wpis(c)->nazwa, "zdj_ecie.jpg") == 0 && tresc_rowna(c, (const uint8_t *)"z", 1));
    EXPECT(dok_wpis(e) != NULL && strcmp(dok_wpis(e)->nazwa, "\xc4\x85 (2).txt") == 0 && dok_wpis(d) != NULL &&
           strcmp(dok_wpis(d)->nazwa, "\xc4\x85.txt") == 0 && tresc_rowna(e, (const uint8_t *)"r", 1));
}

/* T1/7 of the plan: the number of entries in Kosz is what Pliki shows there, a file and its folder each. */
static int pokazany_w_koszu(const struct k1_dok_wpis *w)
{
    return k1_dok_w_koszu(w);
}
static void test_licznik_kosza(void)
{
    uint32_t f, p;
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Wyjazd", NULL, 0, T0);
    p = nowy(f, K1_DOK_PLIK, "bilet.pdf", (const uint8_t *)"%PDF-1", 6, T0);
    EXPECT(dok_do_kosza(p, T0) == 0 && dok_do_kosza(f, T0) == 0);
    EXPECT(dok_w_koszu() == 2 && dok_wybierz(pokazany_w_koszu, wybrane) == 2);
    EXPECT(dok_przywroc(p, 0) == 0 && dok_wpis(p)->rodzic == K1_FOLDER_DOKUMENTY && dok_w_koszu() == 1); /* its folder in Kosz */
}

/* L12 of the plan: an application is given at most 64 files; the 65th is refused with its own result, none taken
 * back in silence (the earliest stays readable). */
static void test_limit_dostepu(void)
{
    int kn[2], fd;
    uint32_t ids[K1_DOK_DOSTEP_APL + 1];
    struct k1_komunikat k;
    char n[32];
    EXPECT(k1_kanal_utworz(kn) == 0);
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1); /* what earlier tests gave it: gone */
    dok_wczytaj(T0);
    for (int i = 0; i <= K1_DOK_DOSTEP_APL; i++) {
        snprintf(n, sizeof n, "plik %d.txt", i);
        ids[i] = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, n, (const uint8_t *)"tekst", 5, T0);
        k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, ids[i], K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
        EXPECT(k.b == (i < K1_DOK_DOSTEP_APL ? 0 : K1_DOK_BLAD_OTWARTE));
    }
    EXPECT(dostep_ma(K1_PROFIL_NOTATKI, ids[0]) && !dostep_ma(K1_PROFIL_NOTATKI, ids[K1_DOK_DOSTEP_APL]));
    EXPECT(dostep_daj(K1_PROFIL_ZEGAR, ids[K1_DOK_DOSTEP_APL]) == 0); /* another application has its own 64 */
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1);
    dostep_koniec_klienta(kn[1], K1_PROFIL_ZEGAR, 1);
    EXPECT(dostep_daj(K1_PROFIL_NOTATKI, ids[K1_DOK_DOSTEP_APL]) == 0); /* closed: room again */
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1);
    close(kn[0]);
    close(kn[1]);
}

/* L4d/L21 of the plan (ADR 0034): 16 384 entries; a tree of thousands deleted at once; loading them again. */
static void test_duzo_wpisow(void)
{
    uint32_t f, g[K1_DOK_GLEBOKOSC], ostatni = 0;
    int n0, n, i, d;
    char nazwa[32];
    double t0;
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    n0 = dok_wybierz(dok_kazdy, wybrane);
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Archiwum", NULL, 0, T0);
    g[0] = f;
    for (d = 1; d < K1_DOK_GLEBOKOSC; d++)
        g[d] = nowy(g[d - 1], K1_DOK_FOLDER, "poziom", NULL, 0, T0);
    for (i = 0, n = n0 + K1_DOK_GLEBOKOSC; n < K1_DOK_MAKS; i++, n++) {
        snprintf(nazwa, sizeof nazwa, "zdjecie %d.jpg", i);
        ostatni = nowy(g[i % K1_DOK_GLEBOKOSC], K1_DOK_PLIK, nazwa, NULL, 0, T0);
        if (ostatni == 0)
            break;
    }
    EXPECT(n == K1_DOK_MAKS && dok_wybierz(dok_kazdy, wybrane) == K1_DOK_MAKS);
    EXPECT(nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "jeszcze jeden", NULL, 0, T0) == 0); /* PELNO */
    t0 = (double)clock() / CLOCKS_PER_SEC;
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wybierz(dok_kazdy, wybrane) == K1_DOK_MAKS && dok_wpis(ostatni) != NULL && dok_widoczny(ostatni));
    EXPECT(dok_do_kosza(f, T0) == 0 && dok_w_koszu() == 1 && !dok_widoczny(ostatni));
    EXPECT(dok_oproznij_kosz() == 0 && dok_wybierz(dok_kazdy, wybrane) == n0 && dok_wpis(ostatni) == NULL);
    fprintf(stderr, "test_dokumenty: 16 384 wpisy — wczytanie i usunięcie drzewa: %.2f s (ASan)\n",
            (double)clock() / CLOCKS_PER_SEC - t0);
    EXPECT((double)clock() / CLOCKS_PER_SEC - t0 < 20);
}

/* L6 of the plan: files never take the last 8 MiB — the system's own objects still save when files are "full". */
static void test_rezerwa(void)
{
    static uint8_t mib[K1_DOK_KAWALEK];
    uint32_t f, id = 1;
    int n = 0;
    dok_wczytaj(T0);
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Duże", NULL, 0, T0);
    while (id != 0 && n < 200) {
        struct bufor b = { mib, NULL, sizeof mib };
        struct dok_zrodlo z = { czytaj_bufor, &b, sizeof mib };
        char nazwa[32];
        snprintf(nazwa, sizeof nazwa, "plik %d", n);
        if (dok_nowy(f, K1_DOK_PLIK, nazwa, strlen(nazwa), &z, K1_PROFIL_PLIKI, K1_POCH_K1, T0, &id) == 0)
            n++;
        else
            id = 0;
    }
    {
        struct bufor b = { mib, NULL, sizeof mib };
        struct dok_zrodlo z = { czytaj_bufor, &b, sizeof mib };
        EXPECT(dok_nowy(f, K1_DOK_PLIK, "ostatni", 7, &z, K1_PROFIL_PLIKI, K1_POCH_K1, T0, &id) == K1_DOK_BLAD_BRAK_MIEJSCA);
    }
    EXPECT(n > 50 && ob_wolne() >= (8u << 20) && ob_wolne() < (8u << 20) + 2 * K1_DOK_KAWALEK);
    {   /* 0.8.1 step 3: what Ustawienia and Pliki are told — without K1-DANE 128 MiB of memory, 8 MiB kept */
        struct k1_komunikat k = miejsce_komunikat(), spr;
        EXPECT(k.typ == K1_KOM_SKARB_MIEJSCE && k.b == 120 && k.a >= 118 && k.a <= 120 && k.c == dok_ile() &&
               k.c > n && k.d == 0); /* entries: these files, their folder and those of earlier tests */
        EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_SKARBCA, &spr) == 0);
    }
    EXPECT(ob_zapisz(K1_PROFIL_EKRAN, "o1", mib, 100000) == K1_MAG_OK); /* settings still save */
    EXPECT(dok_do_kosza(f, T0) == 0 && dok_oproznij_kosz() == 0 && ob_wolne() > 64u << 20); /* deleting frees it */
    ob_usun(K1_PROFIL_EKRAN, "o1");
    EXPECT(dok_rezerwa() == (8u << 20)); /* 128 MiB in memory: 8 MiB (2 % would be less) */
    {   /* growing K1-DANE: never without the vault on a partition, nor for little room on the stick */
        struct k1_komunikat k;
        miejsce_zapas(4000);
        do_nadz.typ = 0;
        EXPECT(miejsce_powieksz() == K1_MAG_ARG && do_nadz.typ == 0); /* data in memory: nothing to grow */
        k = miejsce_komunikat();
        EXPECT(k.d == 0 && k.a < 8 && k.b == 120);                     /* and no button in Ustawienia */
        EXPECT(miejsce_powiekszono(0) == K1_MAG_ARG);                  /* an answer nobody asked for */
        miejsce_zapas(0);
    }
}

/* T1/1 of the plan: the folder of an application is remembered, not looked up by its name. */
static void test_folder_aplikacji(void)
{
    uint32_t f1 = 0, f2 = 0, f3 = 0, f4 = 0;
    dok_wczytaj(T0);
    nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "Kalkulator", (const uint8_t *)"x", 1, T0); /* a file with that name */
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f1) == 0 && dok_wpis(f1) != NULL &&
           dok_wpis(f1)->rodzaj == K1_DOK_FOLDER && strcmp(dok_wpis(f1)->nazwa, "Kalkulator (2)") == 0);
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f2) == 0 && f2 == f1); /* not "(3)" */
    EXPECT(dok_zmien_nazwe(f1, "Rachunki", 8, 0, T0) == 0); /* renamed by the user: still its folder */
    dok_wczytaj(T0 + 1);
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f2) == 0 && f2 == f1);
    EXPECT(dok_przenies(f1, K1_FOLDER_POBRANE, 0, T0) == 0 && dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f2) == 0 &&
           f2 == f1); /* moved, too */
    EXPECT(dok_do_kosza(f1, T0) == 0); /* in Kosz: a new folder, which is remembered from then on */
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f3) == 0 && f3 != f1 && dok_widoczny(f3));
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f4) == 0 && f4 == f3);
    EXPECT(dok_folder_aplikacji("Zegar", K1_PROFIL_ZEGAR, T0, &f4) == 0 && f4 != f3); /* each application its own */
}

/* ---- a write in progress (0.8.1 step 4): pieces of 1 MiB through the channel, the entry after the last ---- */

static struct k1_komunikat kawalek_do(int typ, uint32_t obiekt, const uint8_t *b, size_t n, int32_t c, int32_t e, int *zm)
{
    int fd = -1, mfd = k1_memfd_z_danymi("t", b, n);
    struct k1_komunikat k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(typ, obiekt, (int32_t)n, c, 0, 0, e), mfd, zm, &fd);
    if (fd >= 0)
        close(fd);
    return k;
}

/* NOWY with "more follow": the name and the first piece in one memfd. */
static struct k1_komunikat nowy_w_toku(const char *nazwa, const uint8_t *b, size_t n, int dalej_, int *zm)
{
    static uint8_t buf[K1_DOK_NAZWA + K1_DOK_KAWALEK];
    size_t nl = strlen(nazwa);
    int fd = -1, mfd;
    struct k1_komunikat k;
    memcpy(buf, nazwa, nl);
    memcpy(buf + nl, b, n);
    mfd = k1_memfd_z_danymi("t", buf, nl + n);
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_WIDEO, K1_DOK_PLIK, (int32_t)nl, (int32_t)n,
                                                   K1_POCH_PENDRIVE, dalej_), mfd, zm, &fd);
    if (fd >= 0)
        close(fd);
    return k;
}

/* Reads file `id` piece by piece through the channel into `wy`; the bytes, or -1. */
static int64_t czytaj_kawalkami(uint32_t id, uint8_t *wy)
{
    uint64_t razem = 0;
    for (int32_t k = 0;; k++) {
        int zm, fd;
        struct k1_komunikat t = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, id, (int32_t)K1_DOK_KAWALEK, k, 0, 0, 0),
                                        -1, &zm, &fd);
        if (t.typ != K1_KOM_DOK_TRESC || t.b != k || fd < 0 || k1_memfd_czytaj(fd, wy + razem, (size_t)t.a) != 0)
            return -1;
        close(fd);
        razem += (uint64_t)t.a;
        if (razem >= k1_kom_u64(t.c, t.d))
            return (int64_t)razem;
    }
}

static void test_zapis_w_toku(void)
{
    const uint64_t dl = 2 * K1_DOK_KAWALEK + 12345;
    uint8_t *dane = malloc(dl), *wy = malloc(dl);
    struct k1_komunikat k;
    int zm, przed;
    uint32_t id;
    EXPECT(k1_kanal_utworz(kan) == 0);
    dok_wczytaj(T0);
    for (uint64_t i = 0; i < dl; i++)
        dane[i] = (uint8_t)(i * 13 + (i >> 9));
    dane[0] = 0, dane[1] = 0, dane[2] = 0, dane[3] = 0x18; /* not text, not a picture: "Plik" */
    przed = obiektow();
    k = nowy_w_toku("film.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_NOWY && k.b == 0 && k.c == 0 && !zm); /* nothing to see yet */
    EXPECT(obiektow() == przed + 1); /* its first piece only, no entry */
    k = kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + K1_DOK_KAWALEK, K1_DOK_KAWALEK, 0, 0, &zm);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_KAWALEK && k.b == 0 && !zm);
    k = kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + 2 * K1_DOK_KAWALEK, 12345, 0, 0, &zm);
    EXPECT(k.b == 0);
    k = kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane, 10, 0, 0, &zm); /* NEGATIVE: after a short piece no other may come */
    EXPECT(k.b == K1_DOK_BLAD_ZAPIS && obiektow() == przed); /* …and the whole write is abandoned */
    {   /* the same again, to the end */
        int fd;
        nowy_w_toku("film.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + K1_DOK_KAWALEK, K1_DOK_KAWALEK, 0, 0, &zm);
        kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + 2 * K1_DOK_KAWALEK, 12345, 0, 0, &zm);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        id = (uint32_t)k.c;
        EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_KONIEC && k.b == 0 && id >= K1_DOK_PIERWSZY && zm);
        EXPECT(dok_wpis(id) != NULL && dok_wpis(id)->rozmiar == dl && dok_wpis(id)->rodzic == K1_FOLDER_WIDEO &&
               dok_wpis(id)->typ == K1_TYP_INNY && dok_wpis(id)->pochodzenie == K1_POCH_PENDRIVE && obiektow() == przed + 4);
        EXPECT(czytaj_kawalkami(id, wy) == (int64_t)dl && memcmp(wy, dane, dl) == 0);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_ZAPIS && !zm); /* NEGATIVE: no write in progress to end */
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, id, 100, 5, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_MAG_ARG && fd < 0); /* NEGATIVE: past the end */
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, id, 100, 2, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.typ == K1_KOM_DOK_TRESC && k.a == 100 && k.b == 2 && fd >= 0); /* the start of the last piece */
        close(fd);
    }
    {   /* new content in pieces, the file goes to Kosz meanwhile: nothing changes, its pieces go */
        int fd;
        przed = obiektow();
        k = kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm);
        EXPECT(k.b == 0 && obiektow() == przed + 1);
        EXPECT(dok_zapisz_tresc(id, &(struct dok_zrodlo){ czytaj_bufor, &(struct bufor){ dane, NULL, 5 }, 5 }, T0) ==
               K1_DOK_BLAD_ZAPIS); /* NEGATIVE: one new version at a time */
        EXPECT(dok_do_kosza(id, T0) == 0);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_MIEJSCE && obiektow() == przed && dok_wpis(id)->rozmiar == dl);
        EXPECT(dok_przywroc(id, 0) == 0);
        /* abandoned by the client, and by a closed channel */
        k = kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 1, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == 0 && !zm && obiektow() == przed && dok_wpis(id)->generacja == 1);
        k = kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm);
        k = nowy_w_toku("drugi.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        EXPECT(k.b == K1_DOK_BLAD_ZAPIS); /* NEGATIVE: one write in progress per channel */
        dok_koniec_kanalu(kan[1]);
        EXPECT(obiektow() == przed && dok_wpis(id)->generacja == 1);
    }
    {   /* a power cut in the middle: the pieces of a new file and of new content go at the next start */
        nowy_w_toku("przerwany.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm); /* refused: the channel has one already */
        EXPECT(obiektow() == przed + 1);
        dok_koniec_kanalu(kan[1]); /* (only to forget it here: the start below is what is tested) */
        nowy_w_toku("przerwany.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        EXPECT(obiektow() == przed + 1);
        dok_wczytaj(T0 + 1);
        EXPECT(obiektow() == przed && dok_wpis(id) != NULL && czytaj_kawalkami(id, wy) == (int64_t)dl);
    }
    {   /* at most K1_DOK_KAWALKI_MAKS pieces: 4 GiB */
        struct dok_zapis z;
        EXPECT(dok_zapis_tresci(&z, id) == 0);
        z.kawalkow = K1_DOK_KAWALKI_MAKS;
        z.pelny = 1;
        EXPECT(dok_zapis_kawalek(&z, dane, 1) == K1_DOK_BLAD_ZAPIS);
        z.kawalkow = 0;
        dok_zapis_porzuc(&z);
    }
    close(kan[0]);
    close(kan[1]);
    free(dane);
    free(wy);
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
    test_zapis_w_toku();
    test_dostep();
    test_naprawa();
    test_folder_aplikacji();
    test_nazwa_popraw();
    test_nazwy_podrobki();
    test_nazwy_wolne();
    test_licznik_kosza();
    test_zegar();
    test_limit_dostepu();
    test_duzo_wpisow();
    test_rezerwa();
    return K1_TEST_KONIEC("test_dokumenty");
}

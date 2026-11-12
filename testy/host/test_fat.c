/* Host test of czytnik's FAT reader (src/czytnik/fat.c, nazwy.c; 0.8.0 step 3, ADR 0031) and of the list czytnik
 * sends to Pliki (k1_pendrive.c), ASan/UBSan build. The images come from mtools and sfdisk (testy/host/obrazy_fat.sh):
 * FAT12 without a partition table, FAT16 in an MBR partition, FAT32 in a GPT partition, an NTFS partition. Then the
 * same images damaged on purpose: a loop in a chain, a chain that ends too early, a cluster beyond the volume, a
 * wrong boot sector — every one must end in an error, never in a hang or a read outside the stick.
 * Usage: test_fat KATALOG_Z_OBRAZAMI */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fat.h"
#include "k1_bajty.h"
#include "k1_pendrive.h"
#include "k1_test_host.h"

struct obraz {
    uint8_t *d;
    size_t n;
    unsigned odczytow; /* how many reads (a hang would show as a huge number) */
};

static int czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    struct obraz *o = ctx;
    o->odczytow++;
    if (od > o->n || n > o->n - od) {
        fprintf(stderr, "BŁĄD: odczyt poza obrazem (%llu + %u)\n", (unsigned long long)od, n);
        exit(1);
    }
    memcpy(buf, o->d + od, n);
    return 0;
}

static struct obraz wczytaj(const char *kat, const char *nazwa)
{
    char p[512];
    struct obraz o = { NULL, 0, 0 };
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", kat, nazwa);
    f = fopen(p, "rb");
    if (f == NULL) {
        fprintf(stderr, "brak obrazu %s (testy/host/obrazy_fat.sh)\n", p);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    o.n = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    o.d = malloc(o.n);
    if (o.d == NULL || fread(o.d, 1, o.n, f) != o.n)
        exit(1);
    fclose(f);
    return o;
}

static int otworz(struct fat *f, struct obraz *o)
{
    struct fat_nosnik n = { czytaj, o, o->n };
    return fat_otworz(f, &n);
}

/* ---- a folder into an array ---- */

#define MAKS 3100
static struct fat_wpis spis[MAKS];
static int n_spis;
static int do_spisu(void *ctx, const struct fat_wpis *w)
{
    (void)ctx;
    if (n_spis < MAKS)
        spis[n_spis++] = *w;
    return 0;
}

static int folder(struct fat *f, uint32_t k)
{
    n_spis = 0;
    return fat_folder(f, k, do_spisu, NULL);
}

static const struct fat_wpis *znajdz(const char *nazwa)
{
    for (int i = 0; i < n_spis; i++)
        if (strcmp(spis[i].nazwa, nazwa) == 0)
            return &spis[i];
    return NULL;
}

/* ---- content ---- */

static uint8_t tresc[1 << 20];
static uint32_t n_tresc;
static int do_tresci(void *ctx, const void *buf, uint32_t n)
{
    (void)ctx;
    if (n_tresc + n > sizeof tresc)
        return -1;
    memcpy(tresc + n_tresc, buf, n);
    n_tresc += n;
    return 0;
}

static uint64_t policzone;
static int licz(void *ctx, const void *buf, uint32_t n)
{
    (void)ctx;
    (void)buf;
    policzone += n;
    return 0;
}

/* 0.8.1 step 4: the same file read in pieces from an offset (an odd size: across clusters), the cursor going on
 * through the chain; then backwards (the cursor is behind: from the first cluster again) and another file's cursor. */
static int kawalkami(const struct fat_wpis *w, struct fat *f)
{
    static uint8_t calosc[sizeof tresc];
    struct fat_kursor k = { 0, 0, 0 }, obcy = { w->klaster + 1, 5, w->klaster + 7 };
    uint32_t od;
    memcpy(calosc, tresc, n_tresc);
    n_tresc = 0;
    for (od = 0; od < w->rozmiar; od += 7777)
        if (fat_czytaj_od(f, &k, w->klaster, w->rozmiar, od, 7777, do_tresci, NULL) != 0)
            return 0;
    if (n_tresc != w->rozmiar || memcmp(tresc, calosc, n_tresc) != 0)
        return 0;
    n_tresc = 0;
    if (fat_czytaj_od(f, &k, w->klaster, w->rozmiar, w->rozmiar / 3, 1000, do_tresci, NULL) != 0 ||
        n_tresc != (w->rozmiar - w->rozmiar / 3 < 1000 ? w->rozmiar - w->rozmiar / 3 : 1000) ||
        memcmp(tresc, calosc + w->rozmiar / 3, n_tresc) != 0)
        return 0;
    n_tresc = 0;
    if (fat_czytaj_od(f, &obcy, w->klaster, w->rozmiar, w->rozmiar / 2, 1000, do_tresci, NULL) != 0 ||
        memcmp(tresc, calosc + w->rozmiar / 2, n_tresc) != 0)
        return 0;
    n_tresc = 0; /* past the end: nothing */
    if (fat_czytaj_od(f, &k, w->klaster, w->rozmiar, w->rozmiar + 1, 10, do_tresci, NULL) != 0 || n_tresc != 0)
        return 0;
    memcpy(tresc, calosc, w->rozmiar); /* as the whole read left it (callers look at it) */
    n_tresc = w->rozmiar;
    return 1;
}

/* The file is the pattern of obrazy_fat.sh (after `naglowek` bytes of its own start). */
static int wzor(const struct fat_wpis *w, struct fat *f, uint32_t naglowek)
{
    n_tresc = 0;
    if (w == NULL || fat_czytaj(f, w->klaster, w->rozmiar, w->rozmiar, do_tresci, NULL) != 0 || n_tresc != w->rozmiar)
        return 0;
    for (uint32_t i = naglowek; i < n_tresc; i++)
        if (tresc[i] != (uint8_t)((i * 31 + 7) % 251))
            return 0;
    return kawalkami(w, f);
}

static int czas_bliski(int64_t t)
{
    int64_t teraz = (int64_t)time(NULL);
    return t > teraz - 3 * 86400 && t < teraz + 3 * 86400;
}

static void fat12(const char *kat)
{
    struct obraz o = wczytaj(kat, "fat12.img");
    struct fat f;
    const struct fat_wpis *w;
    EXPECT(otworz(&f, &o) == 0 && f.typ == 12 && f.start == 0);
    EXPECT(folder(&f, 0) == 0);
    EXPECT(n_spis == 3); /* the long name, KROTKA.TXT, DCIM — not the label, not the hidden file */
    w = znajdz("Zdjęcie Łódź.txt");
    EXPECT(w != NULL && !w->folder && w->rozmiar == 4 && czas_bliski(w->czas));
    n_tresc = 0;
    EXPECT(w != NULL && fat_czytaj(&f, w->klaster, w->rozmiar, 100, do_tresci, NULL) == 0 && n_tresc == 4 &&
           memcmp(tresc, "abc\n", 4) == 0);
    EXPECT(wzor(znajdz("KROTKA.TXT"), &f, 0));
    EXPECT(znajdz("UKRYTY.TXT") == NULL);
    w = znajdz("DCIM");
    EXPECT(w != NULL && w->folder && w->rozmiar == 0);
    EXPECT(w != NULL && folder(&f, w->klaster) == 0 && n_spis == 1 && strcmp(spis[0].nazwa, "100K1") == 0 && spis[0].folder);
    EXPECT(n_spis == 1 && folder(&f, spis[0].klaster) == 0 && n_spis == 1 && wzor(znajdz("OBRAZ.PNG"), &f, 8));
    /* the start of a file only (czytnik: its type) */
    n_tresc = 0;
    EXPECT(fat_czytaj(&f, spis[0].klaster, spis[0].rozmiar, 64, do_tresci, NULL) == 0 && n_tresc == 64 &&
           k1_dok_typ(tresc, n_tresc, spis[0].rozmiar) == K1_TYP_PNG);
    /* damaged: the boot sector */
    o.d[11] = 0x01; /* 513 bytes per sector */
    o.d[12] = 0x02;
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    o.d[11] = 0x00;
    o.d[13] = 3; /* sectors per cluster: not a power of 2 */
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    o.d[13] = 1;
    o.d[16] = 0; /* no FAT */
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    o.d[16] = 2;
    EXPECT(otworz(&f, &o) == 0);
    free(o.d);
}

/* The FAT entry of cluster `k` (FAT16) in the image `o`, first FAT. */
static void ustaw_fat16(struct obraz *o, const struct fat *f, uint32_t k, uint16_t v)
{
    k1_z16(o->d + f->start + f->fat_od + k * 2, v);
}

static void fat16(const char *kat)
{
    struct obraz o = wczytaj(kat, "fat16-mbr.img");
    struct fat f;
    const struct fat_wpis *w;
    uint32_t k;
    EXPECT(otworz(&f, &o) == 0 && f.typ == 16 && f.start == 1u << 20);
    EXPECT(folder(&f, 0) == 0 && n_spis == 3);
    EXPECT(wzor(znajdz("duzy.bin"), &f, 0) && wzor(znajdz("b.bin"), &f, 0));
    w = znajdz("frag.bin");
    EXPECT(wzor(w, &f, 0));
    if (w == NULL)
        return;
    k = w->klaster;
    /* a loop: the second cluster points back to the first */
    {
        uint16_t druga = k1_c16(o.d + f.start + f.fat_od + k * 2);
        uint16_t stara = k1_c16(o.d + f.start + f.fat_od + druga * 2);
        unsigned przed;
        ustaw_fat16(&o, &f, druga, (uint16_t)k);
        f.bufor_jest = 0;
        przed = o.odczytow;
        policzone = 0; /* a file "of 4 GiB" in a loop: at most the whole volume is read, then an error */
        EXPECT(fat_czytaj(&f, k, 0xffffffffu, 0xffffffffu, licz, NULL) == FAT_BLAD_USZKODZONY &&
               policzone <= (uint64_t)(f.klastrow + 1) * f.klaster);
        n_tresc = 0;
        EXPECT(fat_czytaj(&f, k, w->rozmiar, w->rozmiar, do_tresci, NULL) == 0); /* the size bounds the reading */
        EXPECT(o.odczytow - przed < 200000);
        ustaw_fat16(&o, &f, druga, stara);
    }
    /* a chain that ends too early */
    ustaw_fat16(&o, &f, k, 0xffff);
    f.bufor_jest = 0;
    n_tresc = 0;
    EXPECT(fat_czytaj(&f, k, w->rozmiar, w->rozmiar, do_tresci, NULL) == FAT_BLAD_USZKODZONY);
    /* a free cluster, a bad one, one beyond the volume in the chain */
    ustaw_fat16(&o, &f, k, 0);
    f.bufor_jest = 0;
    EXPECT(fat_czytaj(&f, k, w->rozmiar, w->rozmiar, do_tresci, NULL) == FAT_BLAD_USZKODZONY);
    ustaw_fat16(&o, &f, k, 0xfff7);
    f.bufor_jest = 0;
    EXPECT(fat_czytaj(&f, k, w->rozmiar, w->rozmiar, do_tresci, NULL) == FAT_BLAD_USZKODZONY);
    ustaw_fat16(&o, &f, k, (uint16_t)(f.klastrow + 2));
    f.bufor_jest = 0;
    EXPECT(fat_czytaj(&f, k, w->rozmiar, w->rozmiar, do_tresci, NULL) == FAT_BLAD_USZKODZONY);
    EXPECT(fat_czytaj(&f, 1, 10, 10, do_tresci, NULL) == FAT_BLAD_USZKODZONY);
    EXPECT(fat_czytaj(&f, f.klastrow + 2, 10, 10, do_tresci, NULL) == FAT_BLAD_USZKODZONY);
    EXPECT(folder(&f, f.klastrow + 2) == FAT_BLAD_USZKODZONY);
    /* the entry of a file pointing beyond the volume: not shown */
    {
        uint8_t *e = o.d + f.start + f.korzen_od;
        for (int i = 0; i < 16; i++, e += 32)
            if (memcmp(e, "B       BIN", 11) == 0)
                k1_z16(e + 26, (uint16_t)(f.klastrow + 5));
        f.bufor_jest = 0;
        EXPECT(folder(&f, 0) == 0 && n_spis == 2 && znajdz("b.bin") == NULL);
    }
    free(o.d);
}

static void fat32(const char *kat)
{
    struct obraz o = wczytaj(kat, "fat32-gpt.img");
    struct fat f;
    const struct fat_wpis *w;
    unsigned przed;
    EXPECT(otworz(&f, &o) == 0 && f.typ == 32 && f.start == 1u << 20 && f.klaster == 512);
    EXPECT(folder(&f, 0) == 0 && n_spis == 2);
    w = znajdz("zdjecie.jpg");
    EXPECT(wzor(w, &f, 4) && n_tresc == 20000 && k1_dok_typ(tresc, 64, 20000) == K1_TYP_JPEG);
    w = znajdz("Duzo");
    EXPECT(w != NULL && w->folder && folder(&f, w->klaster) == 0 && n_spis == 1100);
    EXPECT(znajdz("p0000.txt") != NULL && znajdz("p1099.txt") != NULL);
    /* a loop in the root folder's chain: bounded by the entries of one folder */
    {
        uint8_t *e = o.d + f.start + f.fat_od + (uint64_t)f.korzen_klaster * 4;
        uint32_t stara = k1_c32(e);
        k1_z32(e, f.korzen_klaster);
        f.bufor_jest = 0;
        przed = o.odczytow;
        EXPECT(folder(&f, 0) == 0 && o.odczytow - przed < 70000); /* ends (an end-of-folder entry stops it) */
        k1_z32(e, stara);
    }
    /* the GPT header points beyond the stick: no FAT, no read outside */
    k1_z64(o.d + 512 + 72, (uint64_t)o.n);
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    free(o.d);
}

/* L13 of plan 0.8.2 (trial T6): FAT32 with fewer than 65 525 clusters — from the count alone it would be FAT16 and the
 * stick "not FAT"; its folder of 3000 files (a phone's camera) is read whole. */
static void fat32_maly(const char *kat)
{
    struct obraz o = wczytaj(kat, "fat32-maly.img");
    struct fat f;
    const struct fat_wpis *w;
    EXPECT(otworz(&f, &o) == 0 && f.typ == 32 && f.start == 0 && f.klaster == 4096 && f.klastrow < 65525);
    EXPECT(folder(&f, 0) == 0 && n_spis == 2 && wzor(znajdz("zdjecie.jpg"), &f, 4));
    w = znajdz("Telefon");
    EXPECT(w != NULL && w->folder && folder(&f, w->klaster) == 0 && n_spis == 3000);
    EXPECT(znajdz("IMG_20260000_zdjecie.jpg") != NULL && znajdz("IMG_20262999_zdjecie.jpg") != NULL);
    /* the same boot sector with a 16-bit FAT size: by the count FAT16, but no fixed root folder — not FAT */
    k1_z16(o.d + 22, 1);
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    free(o.d);
}

static void inne(const char *kat)
{
    struct obraz o = wczytaj(kat, "ntfs-mbr.img");
    struct fat f;
    static uint8_t male[100];
    struct obraz m = { male, sizeof male, 0 };
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    EXPECT(otworz(&f, &m) == FAT_BLAD_FORMAT);
    memset(o.d, 0, o.n); /* nothing at all */
    EXPECT(otworz(&f, &o) == FAT_BLAD_FORMAT);
    free(o.d);
}

static void nazwy(void)
{
    char n[K1_DOK_NAZWA];
    uint16_t z[300];
    fat_nazwa_krotka((const uint8_t *)"PHOTO   JPG", 0x18, n);
    EXPECT(strcmp(n, "photo.jpg") == 0);
    fat_nazwa_krotka((const uint8_t *)"README     ", 0, n);
    EXPECT(strcmp(n, "README") == 0);
    fat_nazwa_krotka((const uint8_t *)"\x05" "BC     TXT", 0, n); /* 0x05 = 0xE5: not ASCII → "_" */
    EXPECT(strcmp(n, "_BC.TXT") == 0);
    fat_nazwa_krotka((const uint8_t *)"A:B*    TX?", 0, n);
    EXPECT(strcmp(n, "A_B_.TX_") == 0);
    fat_nazwa_krotka((const uint8_t *)"           ", 0, n);
    EXPECT(strcmp(n, "Plik") == 0);
    /* long names: Polish letters, a pair of surrogates, forbidden characters, direction marks, spaces and dots */
    { const uint16_t t[] = { 0x141, 0xf3, 'd', 0x17a, '.', 'j', 'p', 'g', 0, 0xffff }; fat_nazwa_dluga(t, 10, n); }
    EXPECT(strcmp(n, "Łódź.jpg") == 0);
    { const uint16_t t[] = { 0xd83d, 0xde00, 'x' }; fat_nazwa_dluga(t, 3, n); }
    EXPECT(strcmp(n, "\xf0\x9f\x98\x80x") == 0);
    { const uint16_t t[] = { 0xd83d, 'a', 0xde00, '/', ':', 0x202e, 7, '|' }; fat_nazwa_dluga(t, 8, n); }
    EXPECT(strcmp(n, "_a______") == 0);
    { const uint16_t t[] = { ' ', ' ', 'a', ' ', '.', '.' }; fat_nazwa_dluga(t, 6, n); }
    EXPECT(strcmp(n, "a") == 0);
    { const uint16_t t[] = { ' ', '.', ' ' }; fat_nazwa_dluga(t, 3, n); }
    EXPECT(strcmp(n, "Plik") == 0);
    /* 200 Polish letters (400 bytes) and ".jpg": cut to 127 bytes, whole letters, the extension kept */
    for (int i = 0; i < 200; i++)
        z[i] = 0x105;
    memcpy(z + 200, (const uint16_t[]){ '.', 'j', 'p', 'g' }, 8);
    fat_nazwa_dluga(z, 204, n);
    EXPECT(strlen(n) <= 127 && strcmp(n + strlen(n) - 4, ".jpg") == 0 && k1_dok_nazwa_poprawna(n, strlen(n)));
    EXPECT(strlen(n) == 4 + 122);
    /* 0.8.2 step 2 (L8, L9, trial T4): nothing before the extension → "Plik.jpg", not a hidden ".jpg"; a dot file stays
     * one (hidden); K1's table of names: invisible characters "_", other spaces off the edges, one letter for a + mark */
    { const uint16_t t[] = { ' ', ' ', ' ', ' ', '.', 'j', 'p', 'g' }; fat_nazwa_dluga(t, 8, n); }
    EXPECT(strcmp(n, "Plik.jpg") == 0);
    { const uint16_t t[] = { '.', 'T', 'r', 'a', 's', 'h', 'e', 's' }; fat_nazwa_dluga(t, 8, n); }
    EXPECT(strcmp(n, ".Trashes") == 0);
    { const uint16_t t[] = { 0x200b, '.', 'j', 'p', 'g' }; fat_nazwa_dluga(t, 5, n); }
    EXPECT(strcmp(n, "_.jpg") == 0);
    { const uint16_t t[] = { 0xa0, 'a', 0x200b, 'b', 0x3000, '.', 't', 'x', 't', 0x2007 }; fat_nazwa_dluga(t, 10, n); }
    EXPECT(strcmp(n, "a_b\xe3\x80\x80.txt") == 0);
    { const uint16_t t[] = { 'z', 0x307, 'o', 0x301, 0x142, 'w', 0x328 }; fat_nazwa_dluga(t, 7, n); }
    EXPECT(strcmp(n, "\xc5\xbc\xc3\xb3\xc5\x82w\xcc\xa8") == 0); /* ż, ó, ł; w + ogonek stays two */
    { const uint16_t t[] = { 0x301, 'a', 0xdb40, 0xdc41, 0xe000, 0xfffe }; fat_nazwa_dluga(t, 6, n); }
    EXPECT(strcmp(n, "_a___") == 0);
    { const uint16_t t[] = { 0xad, 0x61c, 0x2060, 0xfeff, 0x3164 }; fat_nazwa_dluga(t, 5, n); }
    EXPECT(strcmp(n, "_____") == 0 && k1_dok_nazwa_poprawna(n, strlen(n)));
    /* two names of 200 letters, different from the 150th: cut to the same name (skarbiec adds " (2)") */
    for (int i = 0; i < 200; i++)
        z[i] = 'a';
    fat_nazwa_dluga(z, 200, n);
    z[150] = 'b';
    {
        char n2[K1_DOK_NAZWA];
        fat_nazwa_dluga(z, 200, n2);
        EXPECT(strcmp(n, n2) == 0 && strlen(n) == 127);
    }
}

/* The list czytnik sends (k1_pendrive.h): written, read back, every damage refused. */
static void lista(void)
{
    static struct k1_pen_wpis w[3];
    static struct k1_dok_wpis d[K1_PEN_WPISOW];
    static uint8_t buf[K1_PEN_LISTA_MAKS + 8];
    size_t dl;
    int wiecej = -1;
    w[0] = (struct k1_pen_wpis){ 5, K1_DOK_FOLDER, 0, 0, 0, "DCIM" };
    w[1] = (struct k1_pen_wpis){ 9, K1_DOK_PLIK, K1_TYP_JPEG, 3000000, 1700000000, "Łódź.jpg" };
    w[2] = (struct k1_pen_wpis){ 12, K1_DOK_PLIK, K1_TYP_INNY, 0xffffffffu, 0, "film.mp4" };
    dl = k1_pen_lista_zapisz(w, 3, 7, 1, buf);
    EXPECT(dl == K1_PEN_LISTA_NAGL + 3 * K1_PEN_WPIS_BAJTY);
    EXPECT(k1_pen_lista_czytaj(buf, dl, 7, d, K1_PEN_WPISOW, &wiecej) == 3 && wiecej == 1);
    EXPECT(d[1].id == 9 && d[1].rodzic == 7 && d[1].typ == K1_TYP_JPEG && d[1].rozmiar == 3000000 &&
           d[1].zmieniono == 1700000000 && d[1].pochodzenie == K1_POCH_PENDRIVE && strcmp(d[1].nazwa, "Łódź.jpg") == 0);
    EXPECT(k1_pen_lista_czytaj(buf, dl, 8, d, K1_PEN_WPISOW, &wiecej) == -1);     /* another folder's list */
    EXPECT(k1_pen_lista_czytaj(buf, dl - 1, 7, d, K1_PEN_WPISOW, &wiecej) == -1); /* length */
    EXPECT(k1_pen_lista_czytaj(buf, dl, 7, d, 2, &wiecej) == -1);                 /* room */
    EXPECT(k1_pen_lista_czytaj(buf, 8, 7, d, K1_PEN_WPISOW, &wiecej) == -1);
#define ZLE(pole, wartosc)                                                                                        \
    do {                                                                                                          \
        uint8_t kopia = buf[pole];                                                                                \
        buf[pole] = (uint8_t)(wartosc);                                                                           \
        EXPECT(k1_pen_lista_czytaj(buf, dl, 7, d, K1_PEN_WPISOW, &wiecej) == -1);                                 \
        buf[pole] = kopia;                                                                                        \
    } while (0)
    ZLE(0, 'X');                                                    /* magic */
    ZLE(12, 2);                                                     /* flags */
    ZLE(K1_PEN_LISTA_NAGL + 4, 3);                                  /* kind */
    ZLE(K1_PEN_LISTA_NAGL + 5, 1);                                  /* a folder with a type */
    ZLE(K1_PEN_LISTA_NAGL + K1_PEN_WPIS_BAJTY + 5, 0);              /* a file without one */
    ZLE(K1_PEN_LISTA_NAGL + K1_PEN_WPIS_BAJTY + 5, K1_TYPY_MAKS + 1);
    ZLE(K1_PEN_LISTA_NAGL + 6, 1);                                  /* padding */
    ZLE(K1_PEN_LISTA_NAGL + 2 * K1_PEN_WPIS_BAJTY + 12, 1);         /* size over 4 GiB */
    ZLE(K1_PEN_LISTA_NAGL + 2 * K1_PEN_WPIS_BAJTY + 23, 0x80);      /* negative time */
    ZLE(K1_PEN_LISTA_NAGL + 24, '/');                               /* a name a K1 file cannot have */
    ZLE(K1_PEN_LISTA_NAGL + 24, 0);                                 /* empty name */
    ZLE(K1_PEN_LISTA_NAGL + 24 + 6, 'x');                           /* bytes after the NUL */
    ZLE(K1_PEN_LISTA_NAGL + K1_PEN_WPIS_BAJTY, 5);                  /* the same id twice */
    ZLE(K1_PEN_LISTA_NAGL, 7);                                      /* the folder itself */
    ZLE(K1_PEN_LISTA_NAGL, 0);                                      /* id 0 */
    ZLE(K1_PEN_LISTA_NAGL + 3, 0x80);                               /* id over K1_PEN_ID_MAKS */
    EXPECT(k1_pen_lista_czytaj(buf, k1_pen_lista_zapisz(w, 0, 0, 0, buf), 0, d, K1_PEN_WPISOW, &wiecej) == 0 && wiecej == 0);
}

int main(int argc, char **argv)
{
    EXPECT(argc == 2);
    if (argc != 2)
        return K1_TEST_KONIEC("test_fat");
    fat12(argv[1]);
    fat16(argv[1]);
    fat32(argv[1]);
    fat32_maly(argv[1]);
    inne(argv[1]);
    nazwy();
    lista();
    return K1_TEST_KONIEC("test_fat");
}

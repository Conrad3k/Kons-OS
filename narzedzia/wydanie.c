/* K1 — the files of a release for updates through the network (0.10.5 step 2, ADR 0046). Runs on the build host,
 * after podpisz (scripts/zbuduj-usb.sh):
 *
 *   wydanie paczka ESP WYJSCIE                  the package (k1_paczka.h) from the K1-EFI image ESP: its slot A (the
 *                                               system and both manifests), the front and the back of the image
 *   wydanie ogloszenie PACZKA OPISY CZAS WYJSCIE [nowa-linia]
 *                                               the announcement (k1_ogloszenie.h) of the package: its version, build
 *                                               id, size and SHA-256, the release time CZAS (s), the descriptions from
 *                                               OPISY (scripts/opisy-wersji.py: "version<TAB>opis<TAB>text" or
 *                                               "version<TAB>poprawki"), signed with the release key
 *
 * Every text is checked as K1 checks it (k1_ogl_tekst_dobry): what K1 would refuse is not signed. The key:
 * K1_KLUCZ_PODPISU (klucz.c); without it the announcement is unsigned (a developer build — K1 takes it only in the
 * test of updates). The same inputs give the same bytes. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_ogloszenie.h"
#include "k1_ogloszenie_podpis.h"
#include "k1_paczka.h"
#include "k1_sha256.h"
#include "klucz.h"
#include "monocypher.h"

static uint8_t buf[1u << 20];

static int czytaj(int fd, void *b, size_t n, uint64_t od)
{
    return pread(fd, b, n, (off_t)od) == (ssize_t)n ? 0 : -1;
}

/* Copies [od, od+n) of `we` to the end of `wy`. */
static int przepisz(int we, uint64_t od, uint64_t n, FILE *wy)
{
    uint64_t i;
    for (i = 0; i < n; i += sizeof buf) {
        size_t ile = n - i < sizeof buf ? (size_t)(n - i) : sizeof buf;
        if (czytaj(we, buf, ile, od + i) != 0 || fwrite(buf, 1, ile, wy) != ile)
            return -1;
    }
    return 0;
}

/* The end of what is not zero in [od, do_) of `fd`, rounded up to 4 KiB (od when all zero). */
static uint64_t koniec_danych(int fd, uint64_t od, uint64_t do_)
{
    uint64_t k = od, i;
    size_t j;
    for (i = od; i < do_; i += sizeof buf) {
        size_t ile = do_ - i < sizeof buf ? (size_t)(do_ - i) : sizeof buf;
        if (czytaj(fd, buf, ile, i) != 0)
            return od;
        for (j = 0; j < ile; j++)
            if (buf[j] != 0)
                k = i + j + 1;
    }
    k = (k + 4095u) & ~4095ull;
    return k < do_ ? k : do_;
}

static int paczka(const char *esp, const char *wyjscie)
{
    static uint8_t przod[K1_PACZKA_PRZOD_MAKS], naglowek[K1_PACZKA_NAGLOWEK];
    struct k1_uklad u;
    struct k1_paczka p;
    uint64_t a, b, tyl_do;
    int fd = open(esp, O_RDONLY);
    off_t rozm = fd < 0 ? -1 : lseek(fd, 0, SEEK_END);
    FILE *wy;
    memset(&p, 0, sizeof p);
    if (rozm <= 0 || czytaj(fd, buf, K1_UKLAD_BAJTY, K1_UKLAD_OFF) != 0 || k1_uklad_czytaj(buf, (uint64_t)rozm, &u) != 0) {
        fprintf(stderr, "wydanie paczka: %s bez bloku układu K1-EFI\n", esp);
        return 1;
    }
    a = u.slot_off[0] < u.slot_off[1] ? u.slot_off[0] : u.slot_off[1];
    b = u.slot_off[0] < u.slot_off[1] ? u.slot_off[1] : u.slot_off[0];
    if (czytaj(fd, p.man_slot, K1_MANIFEST_BAJTY, u.slot_off[0] + K1_SLOT_MANIFEST_OFF) != 0 ||
        czytaj(fd, p.man_start, K1_MANIFEST_BAJTY, u.slot_off[0] + K1_SLOT_START_OFF) != 0 ||
        k1_manifest_czytaj(p.man_slot, &p.slot) != 0 || a > sizeof przod || czytaj(fd, przod, (size_t)a, 0) != 0) {
        fprintf(stderr, "wydanie paczka: %s — manifesty miejsca A albo początek nieczytelne\n", esp);
        return 1;
    }
    tyl_do = koniec_danych(fd, b + K1_SLOT_BAJTY, (uint64_t)rozm);
    p.wersja = p.slot.wersja;
    p.slot_bajty = p.slot.fat_bajty;
    p.efi_bajty = (uint64_t)rozm;
    p.przod_bajty = a;
    p.tyl_od = b + K1_SLOT_BAJTY;
    p.tyl_bajty = tyl_do - p.tyl_od;
    if (k1_paczka_zbuduj(&p, naglowek) != 0 || k1_paczka_uklad(&p, przod, &u) != 0) {
        fprintf(stderr, "wydanie paczka: %s — układ K1-EFI albo manifesty nie pasują do formatu paczki\n", esp);
        return 1;
    }
    wy = fopen(wyjscie, "wb");
    if (wy == NULL || fwrite(naglowek, 1, sizeof naglowek, wy) != sizeof naglowek ||
        przepisz(fd, u.slot_off[0], p.slot_bajty, wy) != 0)
        goto blad;
    memset(buf, 0, sizeof buf);
    if (fwrite(buf, 1, (size_t)(k1_paczka_przod_od(&p) - K1_PACZKA_NAGLOWEK - p.slot_bajty), wy) !=
            (size_t)(k1_paczka_przod_od(&p) - K1_PACZKA_NAGLOWEK - p.slot_bajty) ||
        fwrite(przod, 1, (size_t)a, wy) != (size_t)a || przepisz(fd, p.tyl_od, p.tyl_bajty, wy) != 0 || fclose(wy) != 0)
        goto blad;
    close(fd);
    printf("%s: paczka %s — system %llu MiB, pliki startowe %llu KiB (przód) + %llu KiB (tył), razem %llu B\n", wyjscie,
           p.slot.tekst, (unsigned long long)(p.slot_bajty >> 20), (unsigned long long)(a >> 10),
           (unsigned long long)(p.tyl_bajty >> 10), (unsigned long long)k1_paczka_bajty(&p));
    return 0;
blad:
    fprintf(stderr, "wydanie paczka: zapis %s nieudany: %s\n", wyjscie, strerror(errno));
    return 1;
}

/* The descriptions: lines "version<TAB>opis<TAB>text" or "version<TAB>poprawki", newest first. */
static int opisy(const char *plik, struct k1_ogloszenie *o)
{
    static char w[2048];
    FILE *f = fopen(plik, "r");
    if (f == NULL)
        return -1;
    while (fgets(w, sizeof w, f) != NULL) {
        char *t1 = strchr(w, '\t'), *t2, *nl = strchr(w, '\n');
        struct k1_ogl_opis *d = &o->opis[o->n];
        int32_t v;
        if (nl != NULL)
            *nl = '\0';
        if (w[0] == '\0')
            continue;
        if (t1 == NULL || o->n >= K1_OGL_OPISOW_MAKS)
            goto zle;
        *t1++ = '\0';
        t2 = strchr(t1, '\t');
        if (t2 != NULL)
            *t2++ = '\0';
        v = k1_wersja_z_tekstu(w);
        if (v <= 0)
            goto zle;
        d->wersja = (uint32_t)v;
        if (strcmp(t1, "poprawki") == 0 && t2 == NULL) {
            d->rodzaj = K1_OGL_POPRAWKI;
        } else if (strcmp(t1, "opis") == 0 && t2 != NULL && k1_ogl_tekst_dobry(t2, strlen(t2))) {
            d->rodzaj = K1_OGL_OPIS;
            d->dl = (uint32_t)strlen(t2);
            memcpy(d->tekst, t2, d->dl + 1);
        } else {
            fprintf(stderr, "wydanie ogloszenie: opis wersji %s — tekst niezgodny z zasadami (T18: znaki z tabeli K1, "
                            "jeden akapit do %u bajtów, bez adresów)\n", w, K1_OGL_TEKST_MAKS);
            fclose(f);
            return -1;
        }
        o->n++;
    }
    fclose(f);
    return 0;
zle:
    fprintf(stderr, "wydanie ogloszenie: zły wiersz w %s (albo więcej niż %u wersji)\n", plik, K1_OGL_OPISOW_MAKS);
    fclose(f);
    return -1;
}

static int ogloszenie(const char *plik, const char *plik_opisow, const char *czas, const char *wyjscie, int nowa_linia)
{
    static uint8_t b[K1_OGL_MAKS], naglowek[K1_PACZKA_NAGLOWEK];
    static struct k1_ogloszenie o, r;
    uint8_t tajny[64], pub[32];
    struct k1_paczka p;
    struct k1_sha256 c;
    off_t rozm;
    uint64_t i;
    size_t dl;
    int fd = open(plik, O_RDONLY), jest;
    FILE *wy;
    rozm = fd < 0 ? -1 : lseek(fd, 0, SEEK_END);
    if (rozm <= 0 || czytaj(fd, naglowek, sizeof naglowek, 0) != 0 || k1_paczka_czytaj(naglowek, &p) != 0 ||
        k1_paczka_bajty(&p) != (uint64_t)rozm) {
        fprintf(stderr, "wydanie ogloszenie: %s nie jest paczką K1\n", plik);
        return 1;
    }
    k1_sha256_init(&c);
    for (i = 0; i < (uint64_t)rozm; i += sizeof buf) {
        size_t ile = (uint64_t)rozm - i < sizeof buf ? (size_t)((uint64_t)rozm - i) : sizeof buf;
        if (czytaj(fd, buf, ile, i) != 0)
            return 1;
        k1_sha256_dodaj(&c, buf, ile);
    }
    close(fd);
    memset(&o, 0, sizeof o);
    k1_sha256_koniec(&c, o.sha256);
    o.wersja = p.wersja;
    o.flagi = nowa_linia ? K1_OGL_NOWA_LINIA : 0;
    memcpy(o.wydanie, p.slot.wydanie, sizeof o.wydanie);
    o.paczka_bajty = (uint64_t)rozm;
    o.czas = strtoull(czas, NULL, 10);
    if (opisy(plik_opisow, &o) != 0)
        return 1;
    jest = klucze(tajny, pub);
    if (jest)
        k1_klucz_id(pub, o.klucz_id);
    dl = k1_ogloszenie_zbuduj(&o, b, sizeof b);
    if (dl == 0) {
        fprintf(stderr, "wydanie ogloszenie: opisy niezgodne z zasadami (wersje od najnowszej, nie nowsze od %s)\n",
                p.slot.tekst);
        crypto_wipe(tajny, sizeof tajny);
        return 1;
    }
    k1_ogloszenie_podpisz(b, dl, jest ? tajny : NULL);
    crypto_wipe(tajny, sizeof tajny);
    if (k1_ogloszenie_czytaj(b, dl + 64, &r) != 0 || (jest && k1_ogloszenie_sprawdz_podpis(b, dl + 64, pub) != 0))
        return 1;
    wy = fopen(wyjscie, "wb");
    if (wy == NULL || fwrite(b, 1, dl + 64, wy) != dl + 64 || fclose(wy) != 0) {
        fprintf(stderr, "wydanie ogloszenie: zapis %s nieudany\n", wyjscie);
        return 1;
    }
    printf("%s: ogłoszenie %s (%u opisów, %zu B), %s\n", wyjscie, p.slot.tekst, o.n, dl + 64,
           jest ? "podpisane" : "BEZ podpisu");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[1], "paczka") == 0)
        return paczka(argv[2], argv[3]);
    if ((argc == 6 || (argc == 7 && strcmp(argv[6], "nowa-linia") == 0)) && strcmp(argv[1], "ogloszenie") == 0)
        return ogloszenie(argv[2], argv[3], argv[4], argv[5], argc == 7);
    fprintf(stderr, "użycie: wydanie paczka ESP WYJSCIE | ogloszenie PACZKA OPISY CZAS WYJSCIE [nowa-linia]\n");
    return 2;
}

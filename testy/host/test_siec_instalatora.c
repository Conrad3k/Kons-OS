/* Host test of the writing program's modes for updates through the network (0.10.5 step 2, ADR 0046; src/instalator/
 * siec.c, siec_instaluj.c) on K1-EFI images made like the release's (obrazy_paczki.sh), with a test release key:
 *  - STAN: the disk runs 0.10.9 with 0.10.8 in slot B; the announcement of 0.11.0 is accepted (to download), the one
 *    signed by another key is refused, the one of an older version offers nothing; the announcement to show is written;
 *  - POBIERZ: the package into the waiting room (after the FAT), STAN then says it waits; NEGATIVE: a changed byte in
 *    the system, a cut package, a byte too many, not a package — refused, nothing waits; no waiting room — refused;
 *  - ZAINSTALUJ: the system in slot B, its manifests, grubenv "try B once", the boot files the release's (their digest),
 *    nothing waits after it; NEGATIVE: nothing waiting; a changed byte in the waiting package — grubenv untouched;
 *    while the try is pending everything else waits;
 *  - after a failed try (slot B newer than what runs): 0.11.0 is not offered again, "Wróć" refuses;
 *  - WRÓĆ: slot B with 0.10.8 → grubenv "try B once"; NEGATIVE: slot B changed — refused.
 * Usage: test_siec_instalatora DIR (obrazy_paczki.sh DIR first). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k1_efi.h"
#include "k1_fat32.h"
#include "k1_io.h"
#include "k1_komunikat.h"
#include "k1_paczka.h"
#include "k1_test_host.h"
#include "siec.h"

#define W(a, b, c) ((int32_t)((a) << 16 | (b) << 8 | (c)))

static const char *dir;
static char sciezki[4][512];
static int nast;

void etap(int procent, int nr)
{
    (void)procent;
    (void)nr;
}

static const char *p(const char *nazwa) /* four at once: several in one call */
{
    char *s = sciezki[nast++ % 4];
    snprintf(s, sizeof sciezki[0], "%s/%s", dir, nazwa);
    return s;
}

static void powlok(const char *polecenie)
{
    if (system(polecenie) != 0)
        fprintf(stderr, "nieudane: %s\n", polecenie);
}

static void nowy_dysk(void)
{
    char c[1200];
    snprintf(c, sizeof c, "cp --sparse=always %s/dysk.img %s/d.img", dir, dir);
    powlok(c);
}

static void na(const char *plik, int fd, int flagi)
{
    int f = open(plik, flagi | O_CLOEXEC, 0600);
    if (f < 0 || dup2(f, fd) < 0)
        fprintf(stderr, "nie można otworzyć %s\n", plik);
    if (f >= 0 && f != fd)
        close(f);
}

/* Runs mode `tryb` with fd 4 = `zrodlo`, fd 5 = d.img, fd 6 = `ogl` (STAN: emptied first); INST_SIEC into *s. */
static int uruchom(int tryb, const char *zrodlo, const char *ogl, struct k1_komunikat *s, int *nowy)
{
    int para[2], r, n = 0;
    struct k1_komunikat k;
    char z[512], o[512];
    snprintf(z, sizeof z, "%s", zrodlo);
    snprintf(o, sizeof o, "%s", ogl);
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, para) != 0)
        return -100;
    for (n = 0; n < 2; n++) { /* above fds 3..6, which the program's take */
        int f = fcntl(para[n], F_DUPFD, 20);
        close(para[n]);
        para[n] = f;
    }
    n = 0;
    if (dup2(para[1], FD_KANAL) < 0)
        return -100;
    close(para[1]);
    na(z, FD_ZRODLO, O_RDONLY);
    na(p("d.img"), FD_CEL, O_RDWR);
    na(o, FD_OGL, tryb == K1_INST_SIEC_STAN ? O_RDWR | O_CREAT | O_TRUNC : O_RDONLY);
    r = siec_zadanie(tryb, 0, nowy);
    memset(s, 0, sizeof *s);
    while (recv(para[0], &k, sizeof k, MSG_DONTWAIT) == (ssize_t)sizeof k)
        if (k.typ == K1_KOM_INST_SIEC)
            *s = k, n++;
    close(para[0]);
    close(FD_KANAL);
    close(FD_ZRODLO);
    close(FD_CEL);
    close(FD_OGL);
    return tryb == K1_INST_SIEC_STAN && n != 1 ? -101 : r;
}

static struct k1_efi czytaj_dysk(void)
{
    struct k1_efi e;
    int fd = open(p("d.img"), O_RDONLY);
    memset(&e, 0, sizeof e);
    if (fd < 0 || k1_efi_czytaj(fd, (uint64_t)lseek(fd, 0, SEEK_END), &e) != 0)
        fprintf(stderr, "d.img nieczytelny\n");
    if (fd >= 0)
        close(fd);
    return e;
}

/* Flips one byte of `plik` at `od`. */
static void zmien(const char *plik, uint64_t od)
{
    uint8_t b;
    int fd = open(plik, O_RDWR);
    if (fd >= 0 && k1_czytaj_calosc(fd, &b, 1, od) == 0) {
        b ^= 0x40;
        (void)k1_pisz_calosc(fd, &b, 1, od);
    }
    if (fd >= 0)
        close(fd);
}

static void kopia(const char *z, const char *do_, const char *dopisz)
{
    char c[1400];
    snprintf(c, sizeof c, "cp %s/%s %s/%s%s", dir, z, dir, do_, dopisz);
    powlok(c);
}

static void stan(void)
{
    struct k1_komunikat s;
    int nowy;
    char a[512], b[512];
    nowy_dysk();
    EXPECT(uruchom(K1_INST_SIEC_STAN, p("ogloszenie.bin"), p("pokaz.bin"), &s, &nowy) == 0);
    EXPECT(s.a == W(0, 10, 9) && s.b == 0 && s.c == W(0, 11, 0) && s.e == W(0, 10, 8) && (s.d & K1_SIEC_FL_OGL) &&
           !(s.d & (K1_SIEC_FL_OGL_ZLE | K1_SIEC_FL_BEZ_MIEJSCA | K1_SIEC_FL_PROBA)));
    snprintf(a, sizeof a, "cmp -s %s/ogloszenie.bin %s/pokaz.bin", dir, dir);
    EXPECT(system(a) == 0); /* the announcement to show */
    EXPECT(uruchom(K1_INST_SIEC_STAN, p("obcy.bin"), p("pokaz.bin"), &s, &nowy) == 0);
    EXPECT(s.c == 0 && (s.d & K1_SIEC_FL_OGL_ZLE));      /* NEGATIVE: another key */
    EXPECT(uruchom(K1_INST_SIEC_STAN, p("stare.bin"), p("pokaz.bin"), &s, &nowy) == 0);
    EXPECT(s.c == 0 && (s.d & K1_SIEC_FL_OGL));          /* an older version: nothing to download */
    EXPECT(uruchom(K1_INST_SIEC_STAN, "/dev/null", p("pokaz.bin"), &s, &nowy) == 0);
    EXPECT(s.c == 0 && s.b == 0 && !(s.d & (K1_SIEC_FL_OGL | K1_SIEC_FL_OGL_ZLE)));
    snprintf(b, sizeof b, "truncate -s 300M %s/d.img", dir); /* the stick's K1-EFI: no waiting room */
    powlok(b);
    EXPECT(uruchom(K1_INST_SIEC_STAN, p("ogloszenie.bin"), p("pokaz.bin"), &s, &nowy) == 0 && (s.d & K1_SIEC_FL_BEZ_MIEJSCA) &&
           s.c == W(0, 11, 0)); /* it says there is a newer one (D5: "nagraj nowy pendrive") but cannot take it */
    EXPECT(uruchom(K1_INST_SIEC_POBIERZ, p("paczka.k1a"), p("ogloszenie.bin"), &s, &nowy) == K1_INST_BLAD_MIEJSCE);
}

/* NEGATIVE downloads: each refused, and nothing waits after it. */
static void zle_pobranie(const char *paczka, int oczekiwany)
{
    struct k1_komunikat s;
    int nowy;
    EXPECT(uruchom(K1_INST_SIEC_POBIERZ, paczka, p("ogloszenie.bin"), &s, &nowy) == oczekiwany);
    EXPECT(uruchom(K1_INST_SIEC_STAN, "/dev/null", p("pokaz.bin"), &s, &nowy) == 0 && s.b == 0);
}

static void pobieranie(void)
{
    struct k1_komunikat s;
    int nowy;
    char c[1200];
    nowy_dysk();
    kopia("paczka.k1a", "zla.k1a", "");
    zmien(p("zla.k1a"), K1_PACZKA_NAGLOWEK + 4096); /* a byte of the system */
    zle_pobranie(p("zla.k1a"), K1_INST_BLAD_PODPIS);
    kopia("paczka.k1a", "zla.k1a", "");
    zmien(p("zla.k1a"), 0); /* not a package */
    zle_pobranie(p("zla.k1a"), K1_INST_BLAD_SIEC);
    snprintf(c, sizeof c, "head -c 1500000 %s/paczka.k1a > %s/zla.k1a", dir, dir); /* cut */
    powlok(c);
    zle_pobranie(p("zla.k1a"), K1_INST_BLAD_SIEC);
    snprintf(c, sizeof c, "cp %s/paczka.k1a %s/zla.k1a && printf x >> %s/zla.k1a", dir, dir, dir); /* a byte too many */
    powlok(c);
    zle_pobranie(p("zla.k1a"), K1_INST_BLAD_SIEC);
    EXPECT(uruchom(K1_INST_SIEC_POBIERZ, p("paczka.k1a"), p("obcy.bin"), &s, &nowy) == K1_INST_BLAD_PODPIS);
    /* the real one */
    EXPECT(uruchom(K1_INST_SIEC_POBIERZ, p("paczka.k1a"), p("ogloszenie.bin"), &s, &nowy) == 0 && !nowy);
    EXPECT(uruchom(K1_INST_SIEC_STAN, "/dev/null", p("pokaz.bin"), &s, &nowy) == 0 && s.b == W(0, 11, 0) && s.c == 0);
    snprintf(c, sizeof c, "cmp -s %s/ogloszenie.bin %s/pokaz.bin", dir, dir); /* after a restart: the waiting one's */
    EXPECT(system(c) == 0);
    EXPECT(uruchom(K1_INST_SIEC_STAN, p("ogloszenie.bin"), p("pokaz.bin"), &s, &nowy) == 0 && s.b == W(0, 11, 0) && s.c == 0);
    EXPECT(czytaj_dysk().env.nowy == 0); /* nothing starts otherwise */
}

static int czytaj_fd(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    return k1_czytaj_calosc(*(int *)ctx, buf, n, od);
}

/* The digest of the disk's boot files is the one the release signed. */
static int pliki_startowe_wydania(void)
{
    static struct k1_fat32 f;
    static struct k1_fat32_drzewo t;
    static uint8_t zajete[K1_FAT32_MAPA];
    uint8_t man[K1_MANIFEST_BAJTY], s[32];
    struct k1_manifest m;
    struct k1_efi e = czytaj_dysk();
    int fd = open(p("nowa.img"), O_RDONLY), fd_d = open(p("d.img"), O_RDONLY), ok;
    uint64_t bez[3] = { e.uklad.grubenv_off, e.uklad.slot_off[0], e.uklad.slot_off[1] };
    struct k1_fat32_we we = { czytaj_fd, &fd_d, e.uklad.fat_bajty };
    ok = fd >= 0 && fd_d >= 0 && k1_czytaj_calosc(fd, man, sizeof man, e.uklad.slot_off[0] + K1_SLOT_START_OFF) == 0 &&
         k1_manifest_czytaj(man, &m) == 0;
    if (ok)
        ok = k1_fat32_otworz(&f, &we) == 0 && k1_fat32_drzewo(&f, &t, zajete) == 0 && k1_fat32_skrot(&f, &t, bez, 3, s) == 0 &&
             memcmp(s, m.skrot, 32) == 0;
    if (fd >= 0)
        close(fd);
    if (fd_d >= 0)
        close(fd_d);
    return ok;
}

static void instalacja(void)
{
    struct k1_komunikat s;
    struct k1_efi e;
    int nowy;
    uint8_t g[K1_GRUBENV_BAJTY];
    struct k1_grubenv env;
    int fd;
    nowy_dysk();
    EXPECT(uruchom(K1_INST_SIEC_ZAINSTALUJ, "/dev/null", "/dev/null", &s, &nowy) == K1_INST_BLAD_MIEJSCE && !nowy);
    EXPECT(uruchom(K1_INST_SIEC_POBIERZ, p("paczka.k1a"), p("ogloszenie.bin"), &s, &nowy) == 0);
    e = czytaj_dysk();
    zmien(p("d.img"), k1_pocz_od(e.uklad.fat_bajty) + K1_POCZ_PACZKA_OFF + K1_PACZKA_NAGLOWEK + 777);
    EXPECT(uruchom(K1_INST_SIEC_ZAINSTALUJ, "/dev/null", "/dev/null", &s, &nowy) == K1_INST_BLAD_PODPIS && !nowy);
    e = czytaj_dysk();
    EXPECT(e.env.nowy == 0 && e.slot[1].wersja == (uint32_t)W(0, 10, 8)); /* NEGATIVE: nothing changed */
    zmien(p("d.img"), k1_pocz_od(e.uklad.fat_bajty) + K1_POCZ_PACZKA_OFF + K1_PACZKA_NAGLOWEK + 777); /* back */
    EXPECT(uruchom(K1_INST_SIEC_ZAINSTALUJ, "/dev/null", "/dev/null", &s, &nowy) == 0 && nowy);
    e = czytaj_dysk();
    EXPECT(e.slot[1].wersja == (uint32_t)W(0, 11, 0) && e.env.nowy == 'b' && e.env.slot == 'a' &&
           e.slot[0].wersja == (uint32_t)W(0, 10, 9));
    EXPECT(pliki_startowe_wydania());
    EXPECT(uruchom(K1_INST_SIEC_STAN, "/dev/null", p("pokaz.bin"), &s, &nowy) == 0 && s.b == 0 && (s.d & K1_SIEC_FL_PROBA));
    EXPECT(uruchom(K1_INST_SIEC_WROC, "/dev/null", "/dev/null", &s, &nowy) == K1_INST_BLAD_TRWA);
    EXPECT(uruchom(K1_INST_SIEC_POBIERZ, p("paczka.k1a"), p("ogloszenie.bin"), &s, &nowy) == K1_INST_BLAD_MIEJSCE);
    /* the try failed: GRUB went back to A, nadzorca cleared the try (rozruch.c) — B holds 0.11.0, newer than A */
    env = e.env;
    env.nowy = 0;
    env.start = 'a';
    env.proba = 0;
    k1_grubenv_zbuduj(&env, g);
    fd = open(p("d.img"), O_RDWR);
    EXPECT(fd >= 0 && k1_pisz_calosc(fd, g, sizeof g, e.uklad.grubenv_off) == 0);
    if (fd >= 0)
        close(fd);
    EXPECT(uruchom(K1_INST_SIEC_STAN, p("ogloszenie.bin"), p("pokaz.bin"), &s, &nowy) == 0 && s.c == 0 && s.b == 0 &&
           s.e == 0); /* not offered again; no going "back" to a newer one */
    EXPECT(uruchom(K1_INST_SIEC_WROC, "/dev/null", "/dev/null", &s, &nowy) == K1_INST_BLAD_ARG);
}

static void powrot(void)
{
    struct k1_komunikat s;
    struct k1_efi e;
    int nowy;
    nowy_dysk();
    EXPECT(uruchom(K1_INST_SIEC_WROC, "/dev/null", "/dev/null", &s, &nowy) == 0 && nowy);
    e = czytaj_dysk();
    EXPECT(e.env.nowy == 'b' && e.env.slot == 'a');
    nowy_dysk();
    e = czytaj_dysk();
    zmien(p("d.img"), e.uklad.slot_off[1] + 1000); /* the previous system changed */
    EXPECT(uruchom(K1_INST_SIEC_WROC, "/dev/null", "/dev/null", &s, &nowy) == K1_INST_BLAD_PODPIS && !nowy);
    EXPECT(czytaj_dysk().env.nowy == 0);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    dir = argv[1];
    stan();
    pobieranie();
    instalacja();
    powrot();
    return K1_TEST_KONIEC("test_siec_instalatora");
}

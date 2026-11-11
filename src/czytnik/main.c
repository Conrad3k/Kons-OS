/* K1 czytnik — reads a USB stick for Pliki (0.8.0 step 3, ADR 0031): the FAT file system in a sandbox, so that the
 * kernel never parses a foreign file system (RYZYKA R34) and a crafted stick reaches only this small process.
 *
 * Started by nadzorca when Pliki opens the stick, one process per opening; ends when Pliki closes its channel or
 * the stick is pulled out (nadzorca stops it). fd 3 = the channel to Pliki (its sender checked: UID of Pliki only),
 * fd 4 = the whole stick, opened read only by nadzorca. What it does: the state once (which FAT, or why not),
 * the list of a folder (names, types from the first bytes, sizes, times) and a file's name and content in a sealed
 * memfd — exactly as skarbiec takes a new file (DOK_NOWY), so Pliki passes it on without reading it.
 * Trust boundary: everything on the stick is hostile (fat.c checks it); Pliki is trusted. Sandbox: UID 106, no
 * paths (Landlock), seccomp: base + pread64 (the stick) + memfd_create; no window, no skarbiec, no other device.
 * Names and content never reach the log — only counts. Documentation: docs/komponenty/czytnik.md. */
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fat.h"
#include "k1_czas.h"
#include "k1_indeks.h"
#include "k1_io.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_pendrive.h"
#include "k1_uslugi.h"

#define TAG "K1-CZYTNIK"
#define FD_PLIKI 3
#define FD_NOSNIK 4
#define ZNANYCH (1 << 16) /* entries Pliki was told about (then forgotten all at once) */
#define TYP_BAJTY 64      /* the start of a file read to know its type */

/* What Pliki may ask about: the entries of the lists sent (id = index + 1 + przesuniecie). */
static struct znany {
    uint64_t polozenie;
    uint32_t klaster, rozmiar;
    int folder;
    char nazwa[K1_DOK_NAZWA];
} znane[ZNANYCH];
static int n_znanych;
static uint32_t przesuniecie; /* ids of forgotten entries are never given again */
static struct k1_idx po_polozeniu; /* where on the stick → its place in znane[] (0.8.1, ADR 0034) */
static struct k1_idx_slot sloty[2 * ZNANYCH];
static struct fat fat;
static int fat_jest;
static uint8_t lista[K1_PEN_LISTA_MAKS];
static struct k1_pen_wpis wpisy[K1_PEN_WPISOW];

static int czytaj_nosnik(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    (void)ctx;
    return k1_czytaj_calosc(FD_NOSNIK, buf, n, od);
}

static void wyslij(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(FD_PLIKI, &k, sizeof k, fd) != 0)
        k1_log(TAG, "wysłanie do Plików nieudane: %s", strerror(errno));
}

static void wynik(uint32_t id, int typ, int blad)
{
    wyslij(k1_komunikat_nowy(K1_KOM_PEN_WYNIK, id, typ, blad, 0, 0, 0), -1);
}

static const struct znany *znany(uint32_t id)
{
    if (id <= przesuniecie || id - przesuniecie > (uint32_t)n_znanych)
        return NULL;
    return &znane[id - przesuniecie - 1];
}

static uint32_t skrot(uint64_t polozenie)
{
    return k1_idx_skrot(K1_IDX_START, &polozenie, sizeof polozenie);
}

static int pasuje(const void *ctx, int32_t i)
{
    return znane[i].polozenie == *(const uint64_t *)ctx;
}

/* The id of an entry (the same entry keeps its id while it is remembered). 0 when there is no room. */
static uint32_t zapamietaj(const struct fat_wpis *w)
{
    int32_t i;
    if (po_polozeniu.s == NULL)
        k1_idx_init(&po_polozeniu, sloty, 2 * ZNANYCH);
    i = k1_idx_znajdz(&po_polozeniu, skrot(w->polozenie), pasuje, &w->polozenie);
    if (i >= 0)
        return przesuniecie + (uint32_t)i + 1;
    if (n_znanych == ZNANYCH || przesuniecie + (uint32_t)n_znanych + 1 > K1_PEN_ID_MAKS)
        return 0;
    k1_idx_wstaw(&po_polozeniu, skrot(w->polozenie), n_znanych);
    znane[n_znanych] = (struct znany){ w->polozenie, w->klaster, w->rozmiar, w->folder, "" };
    memcpy(znane[n_znanych].nazwa, w->nazwa, sizeof w->nazwa);
    return przesuniecie + (uint32_t)++n_znanych;
}

/* The stick keeps local time; Pliki shows UTC in Polish time (k1_czas): the time that shows as written. */
static int64_t na_utc(int64_t lokalny)
{
    struct k1_data d;
    if (lokalny <= 0)
        return 0;
    k1_czas_warszawa(lokalny - 3600, &d);
    return lokalny - (d.letni ? 7200 : 3600) < 0 ? 0 : lokalny - (d.letni ? 7200 : 3600);
}

/* ---- the list of a folder ---- */

struct spis {
    int n, wiecej, bez_miejsca;
};

struct poczatek {
    uint8_t b[TYP_BAJTY];
    uint32_t n;
};
static int do_poczatku(void *ctx, const void *buf, uint32_t n)
{
    struct poczatek *p = ctx;
    uint32_t ile = n < TYP_BAJTY - p->n ? n : TYP_BAJTY - p->n;
    memcpy(p->b + p->n, buf, ile);
    p->n += ile;
    return 0;
}

static int do_spisu(void *ctx, const struct fat_wpis *w)
{
    struct spis *s = ctx;
    struct k1_pen_wpis *p = &wpisy[s->n];
    if (s->n == K1_PEN_WPISOW) {
        s->wiecej = 1;
        return 1;
    }
    memset(p, 0, sizeof *p);
    p->id = zapamietaj(w);
    if (p->id == 0) {
        s->bez_miejsca = 1;
        return 1;
    }
    p->rodzaj = w->folder ? K1_DOK_FOLDER : K1_DOK_PLIK;
    p->rozmiar = w->rozmiar;
    p->czas = na_utc(w->czas);
    memcpy(p->nazwa, w->nazwa, sizeof p->nazwa);
    if (!w->folder) { /* the type from the content, as skarbiec does it (never from the name) */
        struct poczatek pc = { { 0 }, 0 };
        if (fat_czytaj(&fat, w->klaster, w->rozmiar, TYP_BAJTY, do_poczatku, &pc) != 0)
            pc.n = 0;
        p->typ = (uint8_t)k1_dok_typ(pc.b, pc.n, w->rozmiar);
        if (w->rozmiar > 0 && pc.n == 0)
            p->typ = K1_TYP_INNY;
    }
    s->n++;
    return 0;
}

static void spis(uint32_t id)
{
    const struct znany *z = id == 0 ? NULL : znany(id);
    struct spis s = { 0, 0, 0 };
    size_t dl;
    int fd, r;
    if (id != 0 && (z == NULL || !z->folder)) {
        wynik(id, K1_KOM_PEN_SPIS, K1_PEN_BLAD_BRAK);
        return;
    }
    r = fat_folder(&fat, z == NULL ? 0 : z->klaster, do_spisu, &s);
    if (s.bez_miejsca) { /* forget everything: the ids sent so far will not be known */
        przesuniecie += (uint32_t)n_znanych;
        n_znanych = 0;
        k1_idx_czysc(&po_polozeniu);
        wynik(id, K1_KOM_PEN_SPIS, K1_PEN_BLAD_BRAK);
        return;
    }
    if (r != 0) {
        k1_log(TAG, "folder nieczytelny (%d)", r);
        wynik(id, K1_KOM_PEN_SPIS, r);
        return;
    }
    dl = k1_pen_lista_zapisz(wpisy, s.n, id, s.wiecej, lista);
    fd = k1_memfd_z_danymi("k1-pendrive", lista, dl);
    if (fd < 0) {
        wynik(id, K1_KOM_PEN_SPIS, K1_PEN_BLAD_PAMIEC);
        return;
    }
    wyslij(k1_komunikat_nowy(K1_KOM_PEN_LISTA, id, (int32_t)dl, s.n, s.wiecej, 0, 0), fd);
    close(fd);
    k1_log(TAG, "folder: %d elementów%s", s.n, s.wiecej ? " (pokazane pierwsze)" : "");
}

/* ---- a file ---- */

static int do_memfd(void *ctx, const void *buf, uint32_t n)
{
    int fd = *(int *)ctx;
    size_t z = 0;
    while (z < n) {
        ssize_t r = write(fd, (const char *)buf + z, n - z);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        z += (size_t)r;
    }
    return 0;
}

/* Piece `kawalek` of file `id` (0.8.1 step 4): the name first in piece 0, then at most K1_DOK_KAWALEK bytes. The
 * position in the file's chain of clusters is kept for the next piece (Pliki ask for them in order). */
static void plik(uint32_t id, uint32_t kawalek)
{
    static struct fat_kursor kursor;
    const struct znany *z = znany(id);
    uint64_t od = (uint64_t)kawalek * K1_DOK_KAWALEK;
    size_t nl;
    int fd, r;
    if (z == NULL || z->folder || od > z->rozmiar) {
        wynik(id, K1_KOM_PEN_CZYTAJ, K1_PEN_BLAD_BRAK);
        return;
    }
    nl = kawalek == 0 ? strlen(z->nazwa) : 0;
    fd = k1_memfd_nowy("k1-pendrive-plik");
    if (fd < 0 || do_memfd(&fd, z->nazwa, (uint32_t)nl) != 0) {
        if (fd >= 0)
            close(fd);
        wynik(id, K1_KOM_PEN_CZYTAJ, K1_PEN_BLAD_PAMIEC);
        return;
    }
    r = fat_czytaj_od(&fat, &kursor, z->klaster, z->rozmiar, (uint32_t)od, K1_DOK_KAWALEK, do_memfd, &fd);
    if (r == 0 && k1_memfd_zapieczetuj(fd) != 0) {
        wynik(id, K1_KOM_PEN_CZYTAJ, K1_PEN_BLAD_PAMIEC); /* the fd is closed */
        return;
    }
    if (r != 0) {
        close(fd);
        k1_log(TAG, "plik nieczytelny (%d)", r);
        wynik(id, K1_KOM_PEN_CZYTAJ, r);
        return;
    }
    {
        uint32_t ile = z->rozmiar - od < K1_DOK_KAWALEK ? (uint32_t)(z->rozmiar - od) : K1_DOK_KAWALEK;
        wyslij(k1_komunikat_nowy(K1_KOM_PEN_PLIK, id, (int32_t)nl, (int32_t)ile, (int32_t)kawalek, k1_kom_niskie(z->rozmiar),
                                 k1_kom_wysokie(z->rozmiar)), fd);
        if (od + ile >= z->rozmiar) /* the last piece: once per file, not per piece (at most 2 lines a second) */
            k1_log(TAG, "plik przeczytany (%u B, kawałków %u)", (unsigned)z->rozmiar, kawalek + 1);
    }
    close(fd);
}

/* ---- start ---- */

static void otworz(void)
{
    struct fat_nosnik n = { czytaj_nosnik, NULL, 0 };
    off_t rozm = lseek(FD_NOSNIK, 0, SEEK_END);
    int r = rozm > 0 ? (n.rozmiar = (uint64_t)rozm, fat_otworz(&fat, &n)) : K1_PEN_BLAD_ODCZYT;
    fat_jest = r == 0;
    if (fat_jest) {
        k1_log(TAG, "pendrive: FAT%d, klaster %u B, %llu MiB", fat.typ, (unsigned)fat.klaster,
               (unsigned long long)(n.rozmiar >> 20));
        wyslij(k1_komunikat_nowy(K1_KOM_PEN_STAN, 0, fat.typ, 0, 0, 0, 0), -1);
    } else {
        k1_log(TAG, "pendrive: %s", r == K1_PEN_BLAD_FORMAT ? "nie FAT (np. exFAT albo NTFS) — nie do odczytu"
                                                            : "błąd odczytu");
        wyslij(k1_komunikat_nowy(K1_KOM_PEN_STAN, 0, 0, r == K1_PEN_BLAD_FORMAT ? r : K1_PEN_BLAD_ODCZYT, 0, 0, 0), -1);
    }
}

int main(void)
{
    k1_klatka_w_usludze(TAG);
    otworz();
    for (;;) {
        struct pollfd p = { FD_PLIKI, POLLIN, 0 };
        struct k1_komunikat k;
        enum k1_odbior_wynik w;
        if (poll(&p, 1, -1) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        w = k1_odbierz_komunikat(FD_PLIKI, K1_UID_PLIKI, K1_KIER_DO_CZYTNIKA, &k, NULL);
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "Pliki zamknęły kanał — koniec");
            return 0;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (!fat_jest)
            wynik(k.obiekt, k.typ, K1_PEN_BLAD_FORMAT);
        else if (k.typ == K1_KOM_PEN_SPIS)
            spis(k.obiekt);
        else if (k.typ == K1_KOM_PEN_CZYTAJ)
            plik(k.obiekt, (uint32_t)k.a);
    }
}

_Static_assert(FAT_BLAD_FORMAT == K1_PEN_BLAD_FORMAT && FAT_BLAD_ODCZYT == K1_PEN_BLAD_ODCZYT &&
                   FAT_BLAD_USZKODZONY == K1_PEN_BLAD_USZKODZONY,
               "czytnik: the results of fat.c travel as K1_PEN_BLAD_*");

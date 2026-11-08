/* K1 nadzorca — A/B after a start (M6c, ADR 0022). Part of nadzorca (see main.c).
 *
 * After an update GRUB starts the new slot once (grubenv: k1_proba = k1_start). When the system came up
 * (the desktop or the password screen for ROZRUCH_OK seconds), nadzorca looks at the K1-EFI of every
 * internal disk: a try that started — and holds the very build that runs now (the build id of the slot
 * manifest = /rdzen/system/wydanie) — becomes the confirmed slot; a try GRUB went back from (k1_start is
 * not k1_proba) is cleared and reported in the Instalator window. Only the 1024 bytes of grubenv are
 * written. A stick (USB) is never touched. */
#include "nadzorca.h"

#define WYDANIE "/rdzen/system/wydanie"
#define ROZRUCH_OK 15.0

static uint8_t wydanie[16];
static int wydanie_jest, ab_zrobione;
static double ab_od;
char ab_nieudana[32]; /* the K1-EFI whose last try did not start (this session; for the Instalator window) */

static int hex(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

void rozruch_wydanie(void)
{
    char t[40];
    int fd = open(WYDANIE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW), i;
    ssize_t n = fd < 0 ? -1 : read(fd, t, sizeof t);
    if (fd >= 0)
        close(fd);
    if (n != 33 || t[32] != '\n') {
        k1_log(TAG, "brak identyfikatora wydania — nowe miejsce A/B nie zostanie potwierdzone automatycznie");
        return;
    }
    for (i = 0; i < 32; i++) {
        int v = hex(t[i]);
        if (v < 0)
            return;
        wydanie[i / 2] = (uint8_t)(i % 2 ? wydanie[i / 2] | v : v << 4);
    }
    wydanie_jest = 1;
}

static int czytaj_w(int fd, void *buf, size_t n, uint64_t off)
{
    return pread(fd, buf, n, (off_t)off) == (ssize_t)n ? 0 : -1;
}

int ab_stan(int fd, struct k1_uklad *u, struct k1_grubenv *e)
{
    uint8_t b[K1_UKLAD_BAJTY], g[K1_GRUBENV_BAJTY];
    off_t rozm = lseek(fd, 0, SEEK_END);
    if (rozm <= 0 || czytaj_w(fd, b, sizeof b, K1_UKLAD_OFF) != 0 || k1_uklad_czytaj(b, (uint64_t)rozm, u) != 0 ||
        czytaj_w(fd, g, sizeof g, u->grubenv_off) != 0 || k1_grubenv_czytaj(g, e) != 0)
        return -1;
    return 0;
}

uint32_t ab_wersja(int fd, const struct k1_uklad *u, int nr, uint8_t wyd[16])
{
    uint8_t man[K1_MANIFEST_BAJTY];
    struct k1_manifest m;
    if (nr < 0 || nr > 1 || czytaj_w(fd, man, sizeof man, u->slot_off[nr] + K1_SLOT_MANIFEST_OFF) != 0 ||
        k1_manifest_czytaj(man, &m) != 0 || m.format != 2)
        return 0;
    if (wyd != NULL)
        memcpy(wyd, m.wydanie, 16);
    return m.wersja;
}

static void sprawdz_dysk(const char *efi)
{
    struct k1_uklad u;
    struct k1_grubenv e;
    uint8_t wyd[16], g[K1_GRUBENV_BAJTY];
    uint32_t w;
    enum k1_ab d;
    int fd = otworz_urzadzenie(efi, O_RDWR | O_EXCL);
    if (fd < 0)
        return;
    if (ab_stan(fd, &u, &e) != 0) {
        close(fd);
        return;
    }
    w = ab_wersja(fd, &u, k1_slot_nr(e.start), wyd);
    d = k1_ab_decyzja(&e, wydanie_jest && w != 0 && memcmp(wyd, wydanie, sizeof wyd) == 0);
    if (d == K1_AB_POTWIERDZ) {
        e.slot = e.start;
        e.proba = 0;
        k1_log(TAG, "A/B: nowa wersja %d.%d.%d wystartowała — potwierdzona (%s, miejsce %c)", (int)(w >> 16),
               (int)(w >> 8 & 0xff), (int)(w & 0xff), efi, e.slot);
    } else if (d == K1_AB_NIEUDANA) {
        k1_log(TAG, "A/B: aktualizacja (miejsce %c) nie wystartowała — działa poprzednia wersja (%s, miejsce %c)", e.proba,
               efi, e.start ? e.start : '?');
        e.proba = 0;
        snprintf(ab_nieudana, sizeof ab_nieudana, "%s", efi);
    }
    if (d != K1_AB_NIC) {
        k1_grubenv_zbuduj(&e, g);
        if (pwrite(fd, g, sizeof g, (off_t)u.grubenv_off) != (ssize_t)sizeof g || fdatasync(fd) != 0)
            k1_log(TAG, "A/B: zapis grubenv nieudany (%s): %s", efi, strerror(errno));
    }
    close(fd);
}

void rozruch_po_starcie(double t)
{
    struct k1_dyski l;
    enum etap_startu e = etap_startu();
    int i;
    if (ab_zrobione || (e != E_PULPIT && e != E_HASLO))
        return;
    if (ab_od == 0) {
        ab_od = t;
        return;
    }
    if (t - ab_od < ROZRUCH_OK || instalator()->pid > 0)
        return;
    ab_zrobione = 1;
    if (k1_dyski_skanuj(SYS, &l) != 0)
        return;
    for (i = 0; i < l.n; i++)
        if (l.d[i].efi[0] && l.d[i].dane[0])
            sprawdz_dysk(l.d[i].efi);
}

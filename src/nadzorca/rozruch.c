/* K1 nadzorca — A/B after a start (M6c, ADR 0022). Part of nadzorca (see main.c).
 *
 * After an update GRUB starts the new slot once (grubenv: k1_proba = k1_start). When the system came up
 * (the desktop or the password screen for ROZRUCH_OK seconds), nadzorca looks at the K1-EFI of every
 * internal disk: a try that started — and holds the very build that runs now (the build id of the slot
 * manifest = /rdzen/system/wydanie) — becomes the confirmed slot; a try GRUB went back from (k1_start is
 * not k1_proba) is cleared and reported in the Instalator window. Only the 1024 bytes of grubenv are
 * written. A stick (USB) is never touched. */
#include "nadzorca.h"
#include "k1_cmdline.h"

#define WYDANIE "/rdzen/system/wydanie"
#define ROZRUCH_OK 15.0

static uint8_t wydanie[16];
static int wydanie_jest, ab_zrobione;
static double ab_od;
char ab_nieudana[32]; /* the K1-EFI whose last try did not start (this session; for the Instalator window) */

void rozruch_wydanie(void)
{
    char t[40];
    if (k1_czytaj_plik(AT_FDCWD, WYDANIE, t, sizeof t) != 33 || t[32] != '\n' || k1_hex_bajty(t, wydanie, sizeof wydanie) != 0) {
        k1_log(TAG, "brak identyfikatora wydania — nowe miejsce A/B nie zostanie potwierdzone automatycznie");
        return;
    }
    wydanie_jest = 1;
}

static void sprawdz_dysk(const char *efi)
{
    struct k1_efi p;
    struct k1_grubenv e;
    const struct k1_manifest *m;
    uint8_t g[K1_GRUBENV_BAJTY];
    char w[16];
    enum k1_ab d;
    int fd = otworz_urzadzenie(efi, O_RDWR | O_EXCL), nr;
    off_t rozm = fd < 0 ? -1 : lseek(fd, 0, SEEK_END);
    if (rozm <= 0 || k1_efi_czytaj(fd, (uint64_t)rozm, &p) != 0 || p.format != 2 || !p.env_ok) {
        if (fd >= 0)
            close(fd);
        return;
    }
    e = p.env;
    nr = k1_slot_nr(e.start);
    m = nr >= 0 ? &p.slot[nr] : NULL;
    d = k1_ab_decyzja(&e, wydanie_jest && m != NULL && m->wersja != 0 && memcmp(m->wydanie, wydanie, sizeof wydanie) == 0);
    if (d == K1_AB_POTWIERDZ) {
        e.slot = e.start;
        e.proba = 0;
        k1_wersja_tekst(m->wersja, w, sizeof w);
        k1_log(TAG, "A/B: nowa wersja %s wystartowała — potwierdzona (%s, miejsce %c)", w, efi, e.slot);
    } else if (d == K1_AB_NIEUDANA) {
        k1_log(TAG, "A/B: aktualizacja (miejsce %c) nie wystartowała — działa poprzednia wersja (%s, miejsce %c)", e.proba,
               efi, e.start ? e.start : '?');
        e.proba = 0;
        snprintf(ab_nieudana, sizeof ab_nieudana, "%s", efi);
    }
    if (d != K1_AB_NIC) {
        k1_grubenv_zbuduj(&e, g);
        if (k1_pisz_calosc(fd, g, sizeof g, p.uklad.grubenv_off) != 0 || fdatasync(fd) != 0)
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

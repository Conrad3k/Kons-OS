/* K1 nadzorca — A/B after a start (M6c, ADR 0022). Part of nadzorca (see main.c).
 *
 * After an update GRUB starts the new slot once (grubenv: k1_proba = k1_start). When the system came up
 * (the desktop or the password screen for ROZRUCH_OK seconds, counted from the moment ekran said its picture is on the
 * screen — 0.10.9 step 2, S3: with i915 a version with a black screen must not be confirmed), nadzorca looks at the K1-EFI of every
 * internal disk: a try that started — and holds the very build that runs now (the build id of the slot
 * manifest = /rdzen/system/wydanie) — becomes the confirmed slot; a try GRUB went back from (k1_start is
 * not k1_proba) is cleared and reported in Ustawienia → Aktualizacje (INST_AB) and, once, in a window of gui (AB_NIEUDANA,
 * 0.10.5 step 1). Only the 1024 bytes of grubenv are
 * written. A stick (USB) is never touched. 0.13.0 step 2 (ADR 0057): at the same moment the TPM learns this K1's
 * generation (tpm.c); a K1 older than the TPM's generation confirms nothing. (0.8.2–0.9.1 then told skarbiec it may move a vault of format 1 to format 2 —
 * gone with format 1 in 0.10.0.) */
#include "nadzorca.h"
#include "k1_cmdline.h"

#define WYDANIE "/rdzen/system/wydanie"
#define ROZRUCH_OK 15.0

static uint8_t wydanie[16];
static int wydanie_jest, ab_zrobione, obraz; /* obraz: ekran said its first frame is on the screen (S3) */
static double ab_od;
static struct k1_komunikat ab_do_gui; /* AB_NIEUDANA waiting for gui (typ 0: none) */
char ab_nieudana[32]; /* the K1-EFI whose last try did not start (this session; for Ustawienia → Aktualizacje) */

void rozruch_wydanie(void)
{
    char t[40];
    if (k1_czytaj_plik(AT_FDCWD, WYDANIE, t, sizeof t) != 33 || t[32] != '\n' || k1_hex_bajty(t, wydanie, sizeof wydanie) != 0) {
        k1_log(TAG, "brak identyfikatora wydania — nowe miejsce A/B nie zostanie potwierdzone automatycznie");
        return;
    }
    wydanie_jest = 1;
}

/* 1: the build id of this K1 is known (/rdzen/system/wydanie, read once by rozruch_wydanie). */
int rozruch_wydanie_jest(void)
{
    return wydanie_jest;
}

/* 1: the slot's manifest names the very build that runs now (0.13.0: also ratunek.c, obraz_strefy.c). */
int rozruch_to_wydanie(const struct k1_manifest *m)
{
    return wydanie_jest && m->wersja != 0 && memcmp(m->wydanie, wydanie, sizeof wydanie) == 0;
}

/* A K1-EFI partition with grubenv (format 2): 0, *e and *fd open (`zapis`: for writing, exclusively), or -1. One place
 * for rozruch.c and ratunek.c (0.13.0 tidy-up). */
int k1efi_otworz(const char *efi, int zapis, struct k1_efi *e, int *fd)
{
    off_t rozm;
    *fd = otworz_urzadzenie(efi, zapis ? O_RDWR | O_EXCL : O_RDONLY);
    rozm = *fd < 0 ? -1 : lseek(*fd, 0, SEEK_END);
    if (rozm <= 0 || k1_efi_czytaj(*fd, (uint64_t)rozm, e) != 0 || e->format != 2 || !e->env_ok) {
        if (*fd >= 0)
            close(*fd);
        *fd = -1;
        return -1;
    }
    return 0;
}

/* Writes `env` as the grubenv of K1-EFI `p` (open for writing in fd): 0, or -1 with errno. */
int k1efi_zapisz_env(int fd, const struct k1_efi *p, const struct k1_grubenv *env)
{
    uint8_t g[K1_GRUBENV_BAJTY];
    k1_grubenv_zbuduj(env, g);
    return k1_pisz_calosc(fd, g, sizeof g, p->uklad.grubenv_off) == 0 && fdatasync(fd) == 0 ? 0 : -1;
}

/* 1: a try A/B waits on this disk and it is not this build (the next start may still go back). */
static int sprawdz_dysk(const char *efi)
{
    struct k1_efi p;
    struct k1_grubenv e;
    const struct k1_manifest *m;
    char w[16];
    enum k1_ab d;
    int fd, nr;
    if (k1efi_otworz(efi, 1, &p, &fd) != 0)
        return 0;
    e = p.env;
    nr = k1_slot_nr(e.start);
    m = nr >= 0 ? &p.slot[nr] : NULL;
    d = k1_ab_decyzja(&e, m != NULL && rozruch_to_wydanie(m));
    if (d == K1_AB_POTWIERDZ) { /* logged once it is on the disk (the tests read the disk right after the line) */
        e.slot = e.start;
        e.proba = 0;
        k1_wersja_tekst(m->wersja, w, sizeof w);
    } else if (d == K1_AB_NIEUDANA) {
        k1_log(TAG, "A/B: aktualizacja (miejsce %c) nie wystartowała — działa poprzednia wersja (%s, miejsce %c)", e.proba,
               efi, e.start ? e.start : '?');
        nr = k1_slot_nr(e.proba);
        ab_do_gui = k1_komunikat_nowy(K1_KOM_AB_NIEUDANA, 0, nr >= 0 ? (int32_t)p.slot[nr].wersja : 0,
                                      m != NULL ? (int32_t)m->wersja : 0, 0, 0, 0);
        e.proba = 0;
        snprintf(ab_nieudana, sizeof ab_nieudana, "%s", efi);
    }
    if (d != K1_AB_NIC) {
        if (k1efi_zapisz_env(fd, &p, &e) != 0)
            k1_log(TAG, "A/B: zapis grubenv nieudany (%s): %s", efi, strerror(errno));
        else if (d == K1_AB_POTWIERDZ)
            k1_log(TAG, "A/B: nowa wersja %s wystartowała — potwierdzona (%s, miejsce %c)", w, efi, e.slot);
    }
    close(fd);
    return d == K1_AB_NIC && k1_slot_nr(e.proba) >= 0;
}

/* ekran's control channel: EKRAN_OBRAZ only (sender: ekran). It may come again after a restart of ekran. */
void obsluz_ekran(void)
{
    for (int i = 0; i < 4 && sterowanie >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(sterowanie, K1_UID_EKRAN, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO || w == K1_ODB_KONIEC) /* its end: the restart of ekran closes ours (uslugi.c) */
            return;
        if (w == K1_ODB_JEST && k.typ == K1_KOM_EKRAN_OBRAZ) {
            if (!obraz) {
                k1_log(TAG, "ekran pokazuje obraz — od teraz liczy się czas do potwierdzenia nowej wersji (A/B)");
                ratunek_obraz(); /* 0.13.0 step 3: the start reached the screen */
            }
            obraz = 1;
        } else {
            k1_log(TAG, "odrzucono komunikat od ekranu");
        }
    }
}

void rozruch_po_starcie(double t)
{
    struct k1_dyski l;
    enum etap_startu e = etap_startu();
    int i, czeka = 0;
    if (ab_do_gui.typ != 0 && bateria_kanal >= 0 && gui_kanal_uid == K1_UID_GUI) { /* not the password screen */
        if (k1_kanal_wyslij(bateria_kanal, &ab_do_gui, sizeof ab_do_gui, -1) == 0)
            k1_log(TAG, "A/B: okienko o nieudanej próbie — do pulpitu");
        ab_do_gui.typ = 0;
    }
    if (ab_zrobione || (e != E_PULPIT && e != E_HASLO) || !obraz || tryb_ratunkowy) /* the rescue mode: nothing */
        return;
    if (ab_od == 0) {
        ab_od = t;
        return;
    }
    if (t - ab_od < ROZRUCH_OK || instalator()->pid > 0)
        return;
    ab_zrobione = 1;
    if (tpm_blokada()) { /* 0.13.0 step 2: an older K1 than the TPM knows is never confirmed */
        k1_log(TAG, "A/B: ta wersja jest starsza niż generacja w TPM — bez potwierdzenia");
        return;
    }
    tpm_potwierdzony(); /* the TPM learns this generation (tpm.c) */
    if (k1_dyski_skanuj(SYS, &l) == 0)
        for (i = 0; i < l.n; i++)
            if (l.d[i].efi[0] && l.d[i].dane[0])
                czeka |= sprawdz_dysk(l.d[i].efi);
    if (czeka)
        k1_log(TAG, "A/B: próba innej wersji czeka");
}

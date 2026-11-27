/* K1 nadzorca — the zone images outside the core (0.10.9 step 4, ADR 0050). The browser's zone image lies in the
 * system slot of its build (k1_obraz_strefy.h), the core holds only its line /rdzen/strefy/<image>.skrot. Here: the
 * image found, put under dm-verity with that root hash and handed to the VMMs (fd 6) — read only, every block checked
 * by the kernel; a changed byte is an I/O error in the zone, never different data.
 * Where it is looked for, in this order:
 *   1. every K1-EFI partition (format 2) — the internal disks first, then USB sticks: a slot whose manifest names this
 *      very build (/rdzen/system/wydanie), the slot GRUB started first; the image ends where the slot's image ends;
 *   2. a whole virtio disk of exactly the image's size (the QEMU tests with the core on /dev/vda: scripts/uruchom-qemu.sh).
 * Wherever it is found, only bytes that verify against the core's root hash are used: the place is no trust.
 * The device is made once and kept (the kernel's page cache of the checked blocks makes the next start of a zone
 * quicker); before every start its first block is read again — when that fails (the stick was pulled out), it is
 * dropped and looked for anew. Each device is marked for removal at once (DM_DEFERRED_REMOVE): it goes away by itself
 * when nadzorca and the last VMM let go of it, and its loop device with it (LO_FLAGS_AUTOCLEAR).
 * An image in the core (<image>.img: the test zone of the test image) is opened as before. Part of nadzorca. */
#include "nadzorca.h"
#include "dyski.h"
#include "k1_cmdline.h"
#include "k1_obraz_strefy.h"
#include "k1_verity.h"

#define STREFY "/rdzen/strefy"
#define OBRAZY_MAKS 2

static struct {
    char nazwa[16];
    int fd; /* the dm-verity device, -1: none */
} obrazy[OBRAZY_MAKS];
static unsigned licznik; /* device names are never reused: a removed one may still be held by a VMM */

static int pierwszy_blok(int fd)
{
    static uint8_t b[K1_VERITY_BLOK];
    return pread(fd, b, sizeof b, 0) == (ssize_t)sizeof b ? 0 : -1;
}

/* The image `o` from byte `od` of the block device `urz` under dm-verity; its fd, or -1 (logged by the caller). */
static int ustaw(const char *nazwa, const struct k1_obraz_strefy *o, const char *urz, uint64_t od)
{
    char sciezka[96], petla[64], dm[64], nazwa_dm[48];
    int fd, loop, wynik;
    snprintf(sciezka, sizeof sciezka, DEV "/%s", urz);
    fd = open(sciezka, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    loop = k1_loop_podlacz(DEV, fd, od, o->bajty, petla, sizeof petla);
    close(fd);
    if (loop < 0) {
        k1_log(TAG, "obraz strefy %s: urządzenie loop nieudane (%s)", nazwa, strerror(errno));
        return -1;
    }
    snprintf(nazwa_dm, sizeof nazwa_dm, "k1-strefa-%s-%u", nazwa, licznik++);
    wynik = k1_verity_utworz(DEV, nazwa_dm, petla, o->bloki, o->skrot, dm, sizeof dm);
    close(loop); /* dm-verity holds it now — or nobody, and it goes */
    if (wynik != 0) {
        k1_log(TAG, "obraz strefy %s: dm-verity nieudane (%s)", nazwa, strerror(errno));
        return -1;
    }
    fd = open(dm, O_RDONLY | O_CLOEXEC);
    k1_verity_usun(DEV, nazwa_dm); /* deferred: it lives while fd (and the VMMs' copies) do */
    if (fd >= 0 && pierwszy_blok(fd) != 0) {
        close(fd);
        fd = -1;
    }
    return fd;
}


/* 1: K1-EFI partition `czesc` holds a slot of this build with the image verified in it (*fd set). */
static int z_k1_efi(const char *nazwa, const struct k1_obraz_strefy *o, const char *czesc, int *fd)
{
    struct k1_efi e;
    int urz = otworz_urzadzenie(czesc, O_RDONLY), kolejnosc[2] = { 0, 1 };
    off_t rozm = urz < 0 ? -1 : lseek(urz, 0, SEEK_END);
    int ok = rozm > 0 && k1_efi_czytaj(urz, (uint64_t)rozm, &e) == 0 && e.format == 2;
    if (urz >= 0)
        close(urz);
    if (!ok)
        return 0;
    if (k1_slot_nr(e.env.start) == 1) /* the slot GRUB started this time first */
        kolejnosc[0] = 1, kolejnosc[1] = 0;
    for (int k = 0; k < 2; k++) {
        int i = kolejnosc[k];
        const struct k1_manifest *m = &e.slot[i];
        int64_t od = rozruch_to_wydanie(m) ? k1_obraz_strefy_w_miejscu(o, m->fat_bajty, e.uklad.slot_bajty) : -1;
        if (od < 0)
            continue;
        *fd = ustaw(nazwa, o, czesc, e.uklad.slot_off[i] + (uint64_t)od);
        if (*fd >= 0) {
            k1_log(TAG, "obraz strefy %s: miejsce %c na %s (%llu MiB), sprawdzony (dm-verity)", nazwa, "ab"[i], czesc,
                   (unsigned long long)(o->bajty >> 20));
            return 1;
        }
        k1_log(TAG, "obraz strefy %s w miejscu %c na %s jest zmieniony albo uszkodzony — pominięty", nazwa, "ab"[i], czesc);
    }
    return 0;
}

/* 1: the whole virtio disk `dysk` has exactly the image's size and the image verifies (*fd set). */
static int z_dysku(const char *nazwa, const struct k1_obraz_strefy *o, const char *dysk, int *fd)
{
    int urz = otworz_urzadzenie(dysk, O_RDONLY);
    off_t rozm = urz < 0 ? -1 : lseek(urz, 0, SEEK_END);
    if (urz >= 0)
        close(urz);
    if (rozm != (off_t)o->bajty)
        return 0;
    *fd = ustaw(nazwa, o, dysk, 0);
    if (*fd < 0) {
        k1_log(TAG, "obraz strefy %s na dysku %s jest zmieniony albo uszkodzony — pominięty", nazwa, dysk);
        return 0;
    }
    k1_log(TAG, "obraz strefy %s: dysk %s (%llu MiB), sprawdzony (dm-verity)", nazwa, dysk,
           (unsigned long long)(o->bajty >> 20));
    return 1;
}

/* Looks through SYS/class/block: pass 0 internal K1-EFI, 1 K1-EFI on USB, 2 whole virtio disks. */
static int znajdz(const char *nazwa, const struct k1_obraz_strefy *o)
{
    int fd = -1;
    if (!rozruch_wydanie_jest()) {
        k1_log(TAG, "obraz strefy %s: brak identyfikatora wydania", nazwa);
        return -1;
    }
    for (int przebieg = 0; przebieg < 3; przebieg++) {
        DIR *d = opendir(SYS "/class/block");
        struct dirent *de;
        while (d != NULL && (de = readdir(d)) != NULL) {
            char sciezka[320], buf[512];
            const char *n = de->d_name;
            if (n[0] == '.' || strlen(n) >= 32)
                continue;
            if (przebieg == 2) {
                snprintf(sciezka, sizeof sciezka, SYS "/class/block/%s/partition", n);
                if (strncmp(n, "vd", 2) == 0 && access(sciezka, F_OK) != 0 && z_dysku(nazwa, o, n, &fd))
                    break;
                continue;
            }
            snprintf(sciezka, sizeof sciezka, SYS "/class/block/%s/uevent", n);
            if (k1_czytaj_plik(AT_FDCWD, sciezka, buf, sizeof buf) < 0 || strstr(buf, "\nPARTNAME=K1-EFI\n") == NULL ||
                k1_dysk_na_usb(SYS, n) != (przebieg == 1))
                continue;
            if (z_k1_efi(nazwa, o, n, &fd))
                break;
        }
        if (d != NULL)
            closedir(d);
        if (fd >= 0)
            return fd;
    }
    return -1;
}

int obraz_strefy_otworz(const char *nazwa)
{
    char sciezka[96], t[160];
    struct k1_obraz_strefy o;
    ssize_t n;
    int i, wolne = -1;
    snprintf(sciezka, sizeof sciezka, STREFY "/%s.img", nazwa); /* in the core: the test zone */
    if (access(sciezka, F_OK) == 0)
        return open(sciezka, O_RDONLY | O_CLOEXEC);
    snprintf(sciezka, sizeof sciezka, STREFY "/%s.skrot", nazwa);
    n = k1_czytaj_plik(AT_FDCWD, sciezka, t, sizeof t);
    if (n <= 0 || k1_obraz_strefy_czytaj(t, (size_t)n, &o) != 0) {
        k1_log(TAG, "obraz strefy %s: brak skrótu obrazu w rdzeniu", nazwa);
        return -1;
    }
    for (i = 0; i < OBRAZY_MAKS; i++) {
        if (obrazy[i].nazwa[0] == '\0' && wolne < 0)
            wolne = i;
        if (strcmp(obrazy[i].nazwa, nazwa) != 0)
            continue;
        if (obrazy[i].fd >= 0 && pierwszy_blok(obrazy[i].fd) == 0)
            return fcntl(obrazy[i].fd, F_DUPFD_CLOEXEC, 0);
        if (obrazy[i].fd >= 0) {
            k1_log(TAG, "obraz strefy %s przestał być czytelny (nośnik odłączony?) — szukam go od nowa", nazwa);
            close(obrazy[i].fd);
        }
        obrazy[i].fd = znajdz(nazwa, &o);
        return obrazy[i].fd < 0 ? -1 : fcntl(obrazy[i].fd, F_DUPFD_CLOEXEC, 0);
    }
    if (wolne < 0)
        return -1;
    snprintf(obrazy[wolne].nazwa, sizeof obrazy[wolne].nazwa, "%s", nazwa);
    obrazy[wolne].fd = znajdz(nazwa, &o);
    if (obrazy[wolne].fd < 0) {
        k1_log(TAG, "obraz strefy %s: nie znaleziono (ani w miejscu A/B tej wersji, ani na dysku testowym)", nazwa);
        return -1;
    }
    return fcntl(obrazy[wolne].fd, F_DUPFD_CLOEXEC, 0);
}

/* 0.10.10 step 1 (od_nowa.c): every device let go — it goes when the last VMM lets go of it (DM_DEFERRED_REMOVE). */
void obrazy_stref_zwolnij(void)
{
    for (int i = 0; i < OBRAZY_MAKS; i++) {
        if (obrazy[i].fd >= 0 && obrazy[i].nazwa[0])
            close(obrazy[i].fd);
        obrazy[i].fd = -1;
        obrazy[i].nazwa[0] = '\0';
    }
}

static void przygotuj(const char *nazwa)
{
    char sciezka[96];
    int fd;
    snprintf(sciezka, sizeof sciezka, STREFY "/%s.skrot", nazwa);
    if (access(sciezka, F_OK) != 0) /* in the core, or not in this image at all (the test zone) */
        return;
    fd = obraz_strefy_otworz(nazwa);
    if (fd >= 0)
        close(fd);
}

/* Once, after the desktop (or the password screen) is up: the images of K1_STREFY looked for now, so the log says
 * where they are and the first zone starts without the search. */
void obrazy_stref_po_starcie(void)
{
    static int zrobione;
    if (zrobione || (etap_startu() != E_PULPIT && etap_startu() != E_HASLO))
        return;
    zrobione = 1;
#define X_Z(n_, nr_, profil_, uid_, obraz_, ...) przygotuj(obraz_);
    K1_STREFY(X_Z)
#undef X_Z
}

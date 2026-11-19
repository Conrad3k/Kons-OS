/* K1 in-system test — M5a: disks and USB.
 *
 * scripts/test-m1.sh starts this test image with extra hardware: an AHCI (SATA) disk with a GPT
 * partition named "K1-DANE", an NVMe disk with an old MBR table, a USB stick on EHCI, a USB
 * keyboard on xHCI and a USB mouse on OHCI. Checks that the kernel sees all of it, that wejscie
 * uses the USB keyboard and mouse, and that nobody but root can reach disks and raw USB. */
#include <sys/sysmacros.h>

#include "k1_test.h"

#define SYS "/rdzen/jadro/sys"
#define DEV "/rdzen/jadro/dev"

/* Reads the sysfs link of /sys/class/<klasa>/<nazwa> (the device path) into `out`. */
static int sciezka_urzadzenia(const char *klasa, const char *nazwa, char *out, size_t n)
{
    char p[320];
    ssize_t r;
    snprintf(p, sizeof p, SYS "/class/%s/%s", klasa, nazwa);
    r = readlink(p, out, n - 1);
    if (r <= 0)
        return -1;
    out[r] = '\0';
    return 0;
}

/* Finds a whole disk (not a partition) whose device path contains `szyna`. */
static int znajdz_dysk(const char *szyna, char *nazwa, size_t n)
{
    DIR *d = opendir(SYS "/class/block");
    struct dirent *de;
    int ok = 0;
    while (d != NULL && !ok && (de = readdir(d)) != NULL) {
        char link[512], p[320];
        if (de->d_name[0] == '.')
            continue;
        snprintf(p, sizeof p, SYS "/class/block/%s/partition", de->d_name);
        if (!absent(p) || sciezka_urzadzenia("block", de->d_name, link, sizeof link) != 0)
            continue;
        if (strstr(link, szyna) != NULL) {
            snprintf(nazwa, n, "%s", de->d_name);
            ok = 1;
        }
    }
    if (d != NULL)
        closedir(d);
    return ok;
}

/* Size of a block device in 512-byte sectors (sysfs "size"), -1 on error. */
static long rozmiar(const char *nazwa)
{
    char p[320], buf[32];
    snprintf(p, sizeof p, SYS "/class/block/%s/size", nazwa);
    if (read_file(p, buf, sizeof buf) != 0)
        return -1;
    return strtol(buf, NULL, 10);
}

/* Number of partitions of disk `nazwa`; the first PARTNAME= goes to `etykieta`. */
static int partycje(const char *nazwa, char *etykieta, size_t n)
{
    char p[320];
    DIR *d;
    struct dirent *de;
    int k = 0;
    etykieta[0] = '\0';
    snprintf(p, sizeof p, SYS "/class/block/%s/", nazwa);
    d = opendir(p);
    while (d != NULL && (de = readdir(d)) != NULL) {
        char plik[640], buf[1024], *s;
        if (strncmp(de->d_name, nazwa, strlen(nazwa)) != 0)
            continue;
        snprintf(plik, sizeof plik, "%s%s/partition", p, de->d_name);
        if (absent(plik))
            continue;
        k++;
        snprintf(plik, sizeof plik, "%s%s/uevent", p, de->d_name);
        if (etykieta[0] == '\0' && read_file(plik, buf, sizeof buf) == 0 && (s = strstr(buf, "PARTNAME=")) != NULL) {
            s += strlen("PARTNAME=");
            s[strcspn(s, "\n")] = '\0';
            snprintf(etykieta, n, "%s", s);
        }
    }
    if (d != NULL)
        closedir(d);
    return k;
}

/* Is a PCI device bound to the driver `sterownik` (/sys/bus/pci/drivers/<name>/0000:…)? */
static int sterownik_pci(const char *sterownik)
{
    char p[160];
    DIR *d;
    struct dirent *de;
    int ok = 0;
    snprintf(p, sizeof p, SYS "/bus/pci/drivers/%s", sterownik);
    d = opendir(p);
    while (d != NULL && !ok && (de = readdir(d)) != NULL)
        ok = strncmp(de->d_name, "0000:", 5) == 0;
    if (d != NULL)
        closedir(d);
    return ok;
}

/* Input event devices whose device path goes through USB. */
static int wejscia_usb(void)
{
    DIR *d = opendir(SYS "/class/input");
    struct dirent *de;
    int k = 0;
    while (d != NULL && (de = readdir(d)) != NULL) {
        char link[512];
        if (strncmp(de->d_name, "event", 5) != 0 || sciezka_urzadzenia("input", de->d_name, link, sizeof link) != 0)
            continue;
        if (strstr(link, "/usb") != NULL)
            k++;
    }
    if (d != NULL)
        closedir(d);
    return k;
}

/* How many input devices (/rdzen/jadro/dev/input/event*) process `pid` holds open. */
static int otwarte_wejscia(long pid)
{
    char dir[64];
    DIR *d;
    struct dirent *de;
    int k = 0;
    snprintf(dir, sizeof dir, "/rdzen/jadro/proc/%ld/fd", pid);
    d = opendir(dir);
    while (d != NULL && (de = readdir(d)) != NULL) {
        char p[384], link[128];
        ssize_t r;
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        r = readlink(p, link, sizeof link - 1);
        if (r <= 0)
            continue;
        link[r] = '\0';
        if (strncmp(link, DEV "/input/event", strlen(DEV "/input/event")) == 0)
            k++;
    }
    if (d != NULL)
        closedir(d);
    return k;
}

/* 0.10.9 step 4 (ADR 0050): a block device that is a zone image under dm-verity (k1-strefa-*), read only — what nadzorca
 * keeps for the VMMs; 1 yes, 0 anything else (a disk, a partition, a writable device). */
static int obraz_strefy_ro(const struct stat *st)
{
    char p[96], nazwa[64] = "", ro[8] = "";
    snprintf(p, sizeof p, SYS "/dev/block/%u:%u/dm/name", major(st->st_rdev), minor(st->st_rdev));
    read_file(p, nazwa, sizeof nazwa);
    snprintf(p, sizeof p, SYS "/dev/block/%u:%u/ro", major(st->st_rdev), minor(st->st_rdev));
    read_file(p, ro, sizeof ro);
    return strncmp(nazwa, "k1-strefa-", 10) == 0 && ro[0] == '1';
}

/* Processes (other than this test) holding an open block device — but a zone image under dm-verity, read only, in
 * nadzorca (`obrazy`: how many such fds there are). */
static int procesy_z_dyskiem(char *opis, size_t n, int *obrazy)
{
    DIR *d = opendir("/rdzen/jadro/proc");
    struct dirent *de;
    int k = 0;
    opis[0] = '\0';
    while (d != NULL && (de = readdir(d)) != NULL) {
        char dir[64], *end;
        long pid = strtol(de->d_name, &end, 10);
        DIR *f;
        struct dirent *fe;
        if (*end != '\0' || pid <= 0 || pid == (long)getpid())
            continue;
        snprintf(dir, sizeof dir, "/rdzen/jadro/proc/%ld/fd", pid);
        f = opendir(dir);
        while (f != NULL && (fe = readdir(f)) != NULL) {
            char p[384];
            struct stat st;
            if (fe->d_name[0] == '.')
                continue;
            snprintf(p, sizeof p, "%s/%s", dir, fe->d_name);
            if (stat(p, &st) == 0 && S_ISBLK(st.st_mode)) {
                char comm[32];
                comm_of(pid, comm, sizeof comm);
                if (strcmp(comm, "nadzorca") == 0 && obraz_strefy_ro(&st)) {
                    (*obrazy)++;
                    continue;
                }
                k++;
                snprintf(opis + strlen(opis), n - strlen(opis), "%s ", comm);
            }
        }
        if (f != NULL)
            closedir(f);
    }
    if (d != NULL)
        closedir(d);
    return k;
}

/* Device nodes in `katalog` of the given type (S_IFBLK/S_IFCHR, 0 = any) not owned by root with 0600. */
static int obce_wezly(const char *katalog, mode_t typ, int *wszystkie, char *opis, size_t n)
{
    DIR *d = opendir(katalog);
    struct dirent *de;
    int zle = 0;
    while (d != NULL && (de = readdir(d)) != NULL) {
        char p[384];
        struct stat st;
        if (de->d_name[0] == '.')
            continue;
        snprintf(p, sizeof p, "%s/%s", katalog, de->d_name);
        if (lstat(p, &st) != 0 || !(S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)))
            continue;
        if (typ != 0 && (st.st_mode & S_IFMT) != typ)
            continue;
        (*wszystkie)++;
        if (st.st_uid != 0 || (st.st_mode & 077) != 0) {
            zle++;
            snprintf(opis + strlen(opis), n - strlen(opis), "%s(%o,%u) ", de->d_name, st.st_mode & 07777,
                     (unsigned)st.st_uid);
        }
    }
    if (d != NULL)
        closedir(d);
    return zle;
}

void t_sprzet(void)
{
    char sata[256] = "", nvme[256] = "", usb[256] = "", etykieta[80], opis[512] = "";
    long wejscie = 0;
    int i, n, k, wszystkie = 0, zle = 0, otwarte = 0, obrazy = 0;
    DIR *d;
    struct dirent *de;
    struct stat st;

    /* USB disks appear a moment after boot (enumeration). */
    for (i = 0; i < 100 && !(znajdz_dysk("/ata", sata, sizeof sata) && znajdz_dysk("/nvme/", nvme, sizeof nvme) &&
                            znajdz_dysk("/usb", usb, sizeof usb)); i++)
        msleep(100);
    check(sata[0] && rozmiar(sata) == 32768, "dysk SATA (kontroler AHCI) widoczny, 16 MiB", "%s: %ld sektorów", sata,
          sata[0] ? rozmiar(sata) : -1L);
    check(nvme[0] && rozmiar(nvme) == 16384, "dysk NVMe widoczny, 8 MiB", "%s: %ld sektorów", nvme,
          nvme[0] ? rozmiar(nvme) : -1L);
    check(usb[0] && rozmiar(usb) == 8192, "pamięć USB (pendrive na EHCI) widoczna, 4 MiB", "%s: %ld sektorów", usb,
          usb[0] ? rozmiar(usb) : -1L);
    n = sata[0] ? partycje(sata, etykieta, sizeof etykieta) : -1;
    check(n == 1 && strcmp(etykieta, "K1-DANE") == 0, "tablica GPT na dysku SATA: partycja „K1-DANE” (nazwa z sysfs)",
          "partycji: %d, nazwa: '%s'", n, etykieta);
    n = nvme[0] ? partycje(nvme, etykieta, sizeof etykieta) : -1;
    check(n == 0, "NEGATYWNY: stara tablica MBR nie jest czytana przez jądro (dysk NVMe bez partycji)", "partycji: %d", n);

    check(sterownik_pci("xhci_hcd") && sterownik_pci("ehci-pci") && sterownik_pci("ohci-pci"),
          "kontrolery USB: xHCI (USB 3), EHCI (USB 2), OHCI (USB 1.1)", "xhci=%d ehci=%d ohci=%d", sterownik_pci("xhci_hcd"),
          sterownik_pci("ehci-pci"), sterownik_pci("ohci-pci"));
    for (i = 0; i < 100 && (k = wejscia_usb()) < 2; i++)
        msleep(100);
    check(k == 2, "klawiatura i mysz USB widoczne jako urządzenia wejścia", "urządzeń USB: %d", k);
    /* nadzorca notices new input devices within 2 s and restarts wejscie with all of them:
     * PS/2 keyboard and mouse + USB keyboard and mouse. */
    for (i = 0; i < 150 && otwarte < 4; i++) {
        msleep(100);
        wejscie = find_process("wejscie");
        otwarte = wejscie > 0 ? otwarte_wejscia(wejscie) : 0;
    }
    check(otwarte == 4, "wejscie używa klawiatury i myszy PS/2 oraz USB (4 urządzenia)", "otwarte: %d", otwarte);

    /* Negative: disks and raw USB only for root (0600) — services get a partition only as a descriptor
     * from nadzorca (K1-DANE for skarbiec, M5d). */
    zle = obce_wezly(DEV, S_IFBLK, &wszystkie, opis, sizeof opis);
    check(zle == 0 && wszystkie >= 5, "NEGATYWNY: dyski i partycje tylko dla roota (0600)", "węzłów: %d, złe: %s", wszystkie,
          opis);
    wszystkie = 0;
    opis[0] = '\0';
    zle = 0;
    d = opendir(DEV "/bus/usb");
    while (d != NULL && (de = readdir(d)) != NULL) {
        char p[320];
        if (de->d_name[0] == '.')
            continue;
        snprintf(p, sizeof p, DEV "/bus/usb/%s", de->d_name);
        zle += obce_wezly(p, S_IFCHR, &wszystkie, opis, sizeof opis);
    }
    if (d != NULL)
        closedir(d);
    if (lstat(DEV "/nvme0", &st) == 0) { /* nvme0 = admin interface of the NVMe controller */
        wszystkie++;
        if (st.st_uid != 0 || (st.st_mode & 077) != 0) {
            zle++;
            snprintf(opis + strlen(opis), sizeof opis - strlen(opis), "nvme0(%o) ", st.st_mode & 07777);
        }
    }
    check(zle == 0 && wszystkie >= 6, "NEGATYWNY: surowy dostęp do USB i NVMe tylko dla roota (0600)", "węzłów: %d, złe: %s",
          wszystkie, opis);
    check(absent(DEV "/hidraw0") && absent(DEV "/usb/hiddev0") && absent(DEV "/sg0") && absent(DEV "/bsg") &&
              absent(DEV "/sr0"),
          "NEGATYWNY: brak surowego HID (hidraw, hiddev), SCSI generic (sg, bsg) i napędów CD", "%s", "");
    k = procesy_z_dyskiem(opis, sizeof opis, &obrazy);
    check(k == 1 && strcmp(opis, "skarbiec ") == 0,
          "NEGATYWNY: dysk ma otwarty tylko skarbiec (jedna partycja K1-DANE), nikt inny (nadzorca: tylko obraz strefy "
          "pod dm-verity, do odczytu)", "procesy: %s, obrazy stref: %d", opis, obrazy);
}

/* 0.5.3: the battery and AC adapter from the test ACPI table (testy/acpi/bateria.asl) through the
 * kernel's ACPI drivers. That gui shows it is checked by scripts/test-m1.sh (gui's log) and
 * scripts/test-m5.sh (screenshot). */
void t_bateria(void)
{
    char typ[32] = "", proc[16] = "", stan[32] = "", ac[8] = "";
    read_file(SYS "/class/power_supply/BAT0/type", typ, sizeof typ);
    read_file(SYS "/class/power_supply/BAT0/capacity", proc, sizeof proc);
    read_file(SYS "/class/power_supply/BAT0/status", stan, sizeof stan);
    read_file(SYS "/class/power_supply/AC/online", ac, sizeof ac);
    check(strcmp(typ, "Battery") == 0 && strcmp(proc, "73") == 0 && strcmp(stan, "Discharging") == 0 &&
              strcmp(ac, "0") == 0,
          "bateria laptopa (ACPI): BAT0 73 %, rozładowywanie, zasilacz AC odłączony",
          "type='%s' capacity='%s' status='%s' AC online='%s'", typ, proc, stan, ac);
}

/* K1 nadzorca — devices: graphics card, input devices, the data partition (K1-DANE). Part of nadzorca (see main.c). */
#include "nadzorca.h"

int otworz_karte(void)
{
    int proba_nr, i;
    for (proba_nr = 0; proba_nr < 50; proba_nr++) {
        for (i = 0; i < 8; i++) {
            char sciezka[64];
            int fd;
            snprintf(sciezka, sizeof sciezka, DEV "/dri/card%d", i);
            fd = open(sciezka, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            if (fd >= 0)
                return fd;
        }
        struct timespec t = { 0, 100 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    return -1;
}

static uint32_t wejscia_maska; /* event0..31 handed to the running wejscie */

static int otworz_wejscie(int nr)
{
    char sciezka[64];
    snprintf(sciezka, sizeof sciezka, DEV "/input/event%d", nr);
    return open(sciezka, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
}

/* Opens up to MAKS_URZADZEN input devices; returns how many (fds in `fd`). */
int otworz_wejscia(int *fd)
{
    int n = 0, i, proba_nr;
    wejscia_maska = 0;
    for (proba_nr = 0; proba_nr < 20 && n == 0; proba_nr++) {
        for (i = 0; i < 32 && n < MAKS_URZADZEN; i++) {
            fd[n] = otworz_wejscie(i);
            if (fd[n] >= 0) {
                wejscia_maska |= 1u << i;
                n++;
            }
        }
        if (n == 0) {
            struct timespec t = { 0, 100 * 1000 * 1000 };
            nanosleep(&t, NULL);
        }
    }
    return n;
}

/* The data partition: a block device partition named "K1-DANE" (GPT, sysfs PARTNAME —
 * block/partitions/core.c part_uevent). Exactly one, or (M5f, ADR 0018) exactly one on an internal disk:
 * an installed K1 keeps its data there even when started from a K1 stick (an update), the stick's own
 * partition is then left alone. Otherwise -1 (*ile = how many were found; errno set if open failed). */
int otworz_partycje_danych(int *ile)
{
    char dev[64] = "", wew[64] = "";
    int wewn = 0;
    DIR *d = opendir(SYS "/class/block");
    struct dirent *de;
    *ile = 0;
    skarb_dane[0] = '\0';
    while (d != NULL && (de = readdir(d)) != NULL) {
        char sciezka[320], buf[512];
        const char *p;
        if (de->d_name[0] == '.')
            continue;
        snprintf(sciezka, sizeof sciezka, SYS "/class/block/%s/uevent", de->d_name);
        if (k1_czytaj_plik(AT_FDCWD, sciezka, buf, sizeof buf) < 0 || strstr(buf, "\nPARTNAME=K1-DANE\n") == NULL)
            continue;
        p = strstr(buf, "DEVNAME=");
        if (p == NULL)
            continue;
        p += strlen("DEVNAME=");
        if (strcspn(p, "\n") >= 32 || strchr(p, '/') != NULL)
            continue;
        snprintf(dev, sizeof dev, "%.*s", (int)strcspn(p, "\n"), p);
        if (!k1_dysk_na_usb(SYS, dev)) {
            wewn++;
            snprintf(wew, sizeof wew, "%s", dev);
        }
        (*ile)++;
    }
    if (d != NULL)
        closedir(d);
    if (*ile > 1 && wewn == 1) {
        k1_log(TAG, "K1-DANE na dysku wewnętrznym (%s) — partycja na pendrivie pominięta", wew);
        snprintf(dev, sizeof dev, "%s", wew);
    } else if (*ile != 1) {
        errno = EINVAL;
        return -1;
    }
    {
        char sciezka[96];
        int fd;
        snprintf(sciezka, sizeof sciezka, DEV "/%s", dev);
        /* O_EXCL on a block device: nobody else (no mount) may hold it exclusively */
        fd = open(sciezka, O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_EXCL);
        if (fd >= 0)
            snprintf(skarb_dane, sizeof skarb_dane, "%s", dev);
        return fd;
    }
}

/* Is a USB mass storage device bound (sysfs: an interface "2-1:1.0" under the usb-storage driver)?
 * Its disk shows up about a second later (usb-storage waits for the stick to settle). */
static int pamiec_usb_obecna(void)
{
    DIR *d = opendir(SYS "/bus/usb/drivers/usb-storage");
    struct dirent *de;
    int jest = 0;
    while (d != NULL && !jest && (de = readdir(d)) != NULL)
        jest = strchr(de->d_name, ':') != NULL;
    if (d != NULL)
        closedir(d);
    return jest;
}

/* Has every bound USB mass storage interface got its disk already? The disk's sysfs path goes
 * through the interface: /sys/class/block/sda -> ../../devices/.../usb2/2-1/2-1:1.0/host6/.../block/sda
 * (the partitions are read before the disk shows up there: block/genhd.c add_disk). */
static int pamiec_usb_gotowa(void)
{
    DIR *d = opendir(SYS "/bus/usb/drivers/usb-storage");
    struct dirent *de;
    int gotowa = 1;
    while (d != NULL && gotowa && (de = readdir(d)) != NULL) {
        char wzor[300];
        DIR *b;
        struct dirent *be;
        if (strchr(de->d_name, ':') == NULL)
            continue;
        snprintf(wzor, sizeof wzor, "/%s/host", de->d_name);
        gotowa = 0;
        b = opendir(SYS "/class/block");
        while (b != NULL && !gotowa && (be = readdir(b)) != NULL) {
            char sciezka[320], cel[512];
            ssize_t n;
            if (be->d_name[0] == '.')
                continue;
            snprintf(sciezka, sizeof sciezka, SYS "/class/block/%s", be->d_name);
            n = readlink(sciezka, cel, sizeof cel - 1);
            if (n <= 0)
                continue;
            cel[n] = '\0';
            gotowa = strstr(cel, wzor) != NULL;
        }
        if (b != NULL)
            closedir(b);
    }
    if (d != NULL)
        closedir(d);
    return gotowa;
}

/* Wait for the data partition after boot: until 3 s (USB devices are still being found), and while
 * a USB stick is bound but its disk is not there yet, until 15 s (booting from the K1 stick: its
 * K1-DANE partition appears ~1 s after the stick). A stick without K1-DANE (e.g. the ISO written to
 * it) does not hold the boot up: its disk is there, so we stop. A K1-DANE found later is still
 * picked up (szukaj_pozno). */
int czekaj_na_dane(double od_startu)
{
    return od_startu < 3.0 || (od_startu < 15.0 && pamiec_usb_obecna() && !pamiec_usb_gotowa());
}

double od_startu_systemu(void)
{
    struct timespec t;
    clock_gettime(CLOCK_BOOTTIME, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Input devices that appear after wejscie started (a fast boot can start us before the kernel has
 * registered the mouse; USB devices plugged in later): restart wejscie with the full set. Not
 * counted as a failure. A device that was unplugged (its node is gone) is forgotten, so a device
 * plugged in later under the same eventN is noticed. Full hotplug (kernel uevents, no restart):
 * M5b. */
void sprawdz_nowe_wejscia(void)
{
    struct usluga *u = usluga_profilu(K1_PROFIL_WEJSCIE);
    struct stat st;
    char sciezka[64];
    int i;
    for (i = 0; i < 32; i++) {
        if (!(wejscia_maska & (1u << i)))
            continue;
        snprintf(sciezka, sizeof sciezka, DEV "/input/event%d", i);
        if (lstat(sciezka, &st) < 0 && errno == ENOENT) {
            wejscia_maska &= ~(1u << i);
            k1_log(TAG, "urządzenie wejścia odłączone (event%d)", i);
        }
    }
    if (u->pid <= 0 || u->wylaczona || __builtin_popcount(wejscia_maska) >= MAKS_URZADZEN)
        return;
    for (i = 0; i < 32; i++) {
        int fd;
        if (wejscia_maska & (1u << i))
            continue;
        fd = otworz_wejscie(i);
        if (fd < 0)
            continue;
        close(fd);
        k1_log(TAG, "nowe urządzenie wejścia (event%d) — ponowne uruchomienie wejscie", i);
        u->bez_liczenia = 1;
        kill(u->pid, SIGTERM);
        return;
    }
}

/* ---- kernel device events (0.6.0, ADR 0019) ---- */

int uevent_fd = -1;
int uevent_wejscie, uevent_dysk, uevent_zasilanie, uevent_zalegle;

/* The kernel's uevent multicast group: new and removed input devices, partitions, batteries and
 * chargers arrive at once instead of being looked for every few seconds. The periodic checks stay
 * as a slow fallback (a missed event, a kernel without netlink). */
void uevent_otworz(void)
{
    struct sockaddr_nl a;
    int bufor = 256 * 1024;
    uevent_fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (uevent_fd < 0) {
        k1_log(TAG, "zdarzenia jądra (uevent) niedostępne: %s — sprawdzam urządzenia co %d s", strerror(errno), 2);
        return;
    }
    memset(&a, 0, sizeof a);
    a.nl_family = AF_NETLINK;
    a.nl_groups = 1; /* the kernel's group (udev's is 2: we have no udev) */
    setsockopt(uevent_fd, SOL_SOCKET, SO_RCVBUF, &bufor, sizeof bufor);
    if (bind(uevent_fd, (struct sockaddr *)&a, sizeof a) != 0) {
        k1_log(TAG, "uevent: bind: %s", strerror(errno));
        close(uevent_fd);
        uevent_fd = -1;
        return;
    }
    k1_log(TAG, "zdarzenia jądra (uevent): nasłuchuję");
}

void obsluz_uevent(void)
{
    int i;
    for (i = 0; i < 64 && uevent_fd >= 0; i++) {
        char buf[8192];
        struct sockaddr_nl od;
        struct iovec io = { buf, sizeof buf };
        struct msghdr m;
        struct k1_uevent u;
        ssize_t n;
        memset(&m, 0, sizeof m);
        m.msg_name = &od;
        m.msg_namelen = sizeof od;
        m.msg_iov = &io;
        m.msg_iovlen = 1;
        n = recvmsg(uevent_fd, &m, 0);
        if (n < 0) {
            if (errno == ENOBUFS) /* the queue overflowed: look at everything once */
                uevent_zalegle = 1;
            return;
        }
        /* only the kernel (port 0) — a process could send to the group too (with CAP_NET_ADMIN) */
        if (m.msg_namelen != sizeof od || od.nl_family != AF_NETLINK || od.nl_pid != 0 || (m.msg_flags & MSG_TRUNC) ||
            k1_uevent_parsuj(buf, (size_t)n, &u) != 0)
            continue;
        if (u.rodzaj == K1_UE_WEJSCIE && strncmp(u.devname, "input/event", 11) == 0 &&
            (u.akcja == K1_UE_DODANE || u.akcja == K1_UE_USUNIETE))
            uevent_wejscie = 1;
        else if (u.rodzaj == K1_UE_DYSK && (u.akcja == K1_UE_DODANE || u.akcja == K1_UE_ZMIANA || u.akcja == K1_UE_USUNIETE))
            uevent_dysk = 1; /* removed too (0.8.6): Pliki learn at once that the stick is gone, not at the next look */
        else if (u.rodzaj == K1_UE_ZASILANIE)
            uevent_zasilanie = 1;
    }
}


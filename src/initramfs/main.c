/* K1 initramfs — /init inside the initial RAM filesystem (PID 1 during early boot).
 *
 * Job (SPEC §6, ADR 0004): find the block device with the core image, mount it read-only
 * as /rdzen inside a new, read-only root (tmpfs with exactly three directories), delete itself
 * from the initramfs and hand over to /rdzen/init. Nothing else: no shell, no modules, no
 * configuration beyond one kernel command line parameter (k1.rdzen=/dev/XXX).
 * k1.rdzen=wbudowany (ADR 0010, ISO image): the core image is the file /rdzen.squashfs inside the
 * initramfs itself (second cpio archive loaded by the boot loader) and is mounted through a
 * read-only loop device — no storage drivers needed, works in any virtual machine.
 * M6b (ADR 0021): the image is mounted through dm-verity — its hash tree follows the SquashFS data,
 * the root hash is k1.rdzen_skrot= (signed with the kernel in M6d). The whole device is read once
 * before the mount: one changed byte stops the boot here, before the kernel parses anything of it.
 *
 * Any error ends in k1_awaria(): message on the console, power-off. Never exit(): PID 1
 * exiting means kernel panic. Documentation: docs/komponenty/initramfs.md. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <linux/dm-ioctl.h>
#include <linux/loop.h>
#include <sys/sysmacros.h>

#include "k1_cmdline.h"
#include "k1_log.h"

#define TAG "K1-INITRAMFS"

#define NEW_ROOT "/nowy_korzen"
#define CORE_DIR NEW_ROOT "/rdzen"
#define CORE_INIT CORE_DIR "/init"
#define CMDLINE_MAX 4096
#define DEVICE_WAIT_STEPS 100 /* x 100 ms = 10 s */
#define BUILTIN "wbudowany"
#define BUILTIN_IMAGE "/rdzen.squashfs"
#define VERITY_NAME "k1-rdzen"
#define VERITY_BLOCK 4096u
/* the build's fixed salt (scripts/wspolne.sh: K1_VERITY_SOL) */
#define VERITY_SALT "4b31207264657a656e2056657269747920736f6c00000000000000000000000000"

/* Reads /proc/cmdline into buf (NUL-terminated). Mounts proc only for the duration of the read. */
static void read_cmdline(char *buf, size_t size)
{
    ssize_t n;
    size_t total = 0;
    int fd;

    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
        k1_awaria(TAG, "nie można zamontować /proc: %s", strerror(errno));
    fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        k1_awaria(TAG, "nie można otworzyć /proc/cmdline: %s", strerror(errno));
    while (total < size - 1) {
        n = read(fd, buf + total, size - 1 - total);
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0)
            k1_awaria(TAG, "błąd odczytu /proc/cmdline: %s", strerror(errno));
        if (n == 0)
            break;
        total += (size_t)n;
    }
    buf[total] = '\0';
    close(fd);
    if (umount2("/proc", 0) != 0)
        k1_awaria(TAG, "nie można odmontować /proc: %s", strerror(errno));
}

static void wait_for_block_device(const char *path)
{
    struct stat st;
    struct timespec step = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
    int i;

    for (i = 0; i < DEVICE_WAIT_STEPS; i++) {
        if (stat(path, &st) == 0) {
            if (!S_ISBLK(st.st_mode))
                k1_awaria(TAG, "%s nie jest urządzeniem blokowym", path);
            return;
        }
        nanosleep(&step, NULL);
    }
    k1_awaria(TAG, "nie znaleziono urządzenia z obrazem rdzenia: %s", path);
}

/* Attaches the built-in image to a free loop device (read-only, freed with the mount);
 * writes the device path into `device`. Returns the open loop device: it must stay open until the
 * image is mounted (LO_FLAGS_AUTOCLEAR detaches the device when the last user closes it). */
static int attach_builtin(char *device, size_t size)
{
    struct loop_config cfg;
    int ctl, img, loop, n;
    img = open(BUILTIN_IMAGE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (img < 0)
        k1_awaria(TAG, "brak wbudowanego obrazu rdzenia %s: %s", BUILTIN_IMAGE, strerror(errno));
    ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (ctl < 0)
        k1_awaria(TAG, "brak /dev/loop-control: %s", strerror(errno));
    n = ioctl(ctl, (int)LOOP_CTL_GET_FREE);
    if (n < 0)
        k1_awaria(TAG, "LOOP_CTL_GET_FREE: %s", strerror(errno));
    close(ctl);
    snprintf(device, size, "/dev/loop%d", n);
    wait_for_block_device(device);
    loop = open(device, O_RDONLY | O_CLOEXEC);
    if (loop < 0)
        k1_awaria(TAG, "%s: %s", device, strerror(errno));
    memset(&cfg, 0, sizeof cfg);
    cfg.fd = (unsigned)img;
    cfg.info.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;
    if (ioctl(loop, (int)LOOP_CONFIGURE, &cfg) != 0)
        k1_awaria(TAG, "LOOP_CONFIGURE: %s", strerror(errno));
    close(img);
    return loop;
}

static void dm_init(struct dm_ioctl *io, size_t size)
{
    memset(io, 0, size);
    io->version[0] = DM_VERSION_MAJOR;
    io->data_size = (uint32_t)size;
    io->data_start = sizeof *io;
    snprintf(io->name, sizeof io->name, "%s", VERITY_NAME);
}

/* M6b: puts a dm-verity device over `device` (data: the SquashFS rounded up to 4 KiB, then the hash
 * tree; no superblock), reads it all once and writes its path ("/dev/dm-N") into `out`. */
static void verity(const char *device, const char *skrot, char *out, size_t outsz)
{
    union {
        struct dm_ioctl io;
        char b[4096];
    } u;
    struct dm_target_spec *t;
    unsigned char sb[96];
    static char buf[65536];
    unsigned long long bajty, bloki, rozmiar, i;
    struct stat st;
    int fd = open(device, O_RDONLY | O_CLOEXEC), ctl, dm;
    ssize_t n;

    if (fd < 0 || fstat(fd, &st) != 0 || pread(fd, sb, sizeof sb, 0) != (ssize_t)sizeof sb)
        k1_awaria(TAG, "nie można odczytać obrazu rdzenia z %s: %s", device, strerror(errno));
    if (sb[0] != 'h' || sb[1] != 's' || sb[2] != 'q' || sb[3] != 's')
        k1_awaria(TAG, "nie udało się zamontować obrazu rdzenia z %s: to nie jest obraz SquashFS", device);
    bajty = 0;
    for (i = 0; i < 8; i++)
        bajty |= (unsigned long long)sb[40 + i] << (8 * i); /* bytes_used */
    bloki = (bajty + VERITY_BLOCK - 1) / VERITY_BLOCK;
    rozmiar = (unsigned long long)lseek(fd, 0, SEEK_END);
    close(fd);
    /* one data block: the root hash is its own hash, no tree blocks follow */
    if (bloki == 0 || rozmiar < (bloki + (bloki > 1 ? 1 : 0)) * VERITY_BLOCK)
        k1_awaria(TAG, "obraz rdzenia bez drzewa skrótów (dm-verity) — nie może być sprawdzony");

    ctl = open("/dev/mapper/control", O_RDWR | O_CLOEXEC);
    if (ctl < 0)
        k1_awaria(TAG, "brak /dev/mapper/control (dm-verity): %s", strerror(errno));
    dm_init(&u.io, sizeof u);
    if (ioctl(ctl, (int)DM_DEV_CREATE, &u.io) != 0)
        k1_awaria(TAG, "DM_DEV_CREATE: %s", strerror(errno));
    dm_init(&u.io, sizeof u);
    u.io.target_count = 1;
    u.io.flags = DM_READONLY_FLAG;
    t = (struct dm_target_spec *)(u.b + sizeof u.io);
    t->sector_start = 0;
    t->length = bloki * (VERITY_BLOCK / 512);
    snprintf(t->target_type, sizeof t->target_type, "verity");
    snprintf((char *)(t + 1), sizeof u - sizeof u.io - sizeof *t, "1 %u:%u %u:%u %u %u %llu %llu sha256 %s %s",
             major(st.st_rdev), minor(st.st_rdev), major(st.st_rdev), minor(st.st_rdev), VERITY_BLOCK, VERITY_BLOCK,
             bloki, bloki, skrot, VERITY_SALT);
    t->next = 0;
    if (ioctl(ctl, (int)DM_TABLE_LOAD, &u.io) != 0)
        k1_awaria(TAG, "DM_TABLE_LOAD (dm-verity): %s", strerror(errno));
    dm_init(&u.io, sizeof u);
    if (ioctl(ctl, (int)DM_DEV_SUSPEND, &u.io) != 0) /* no SUSPEND flag: resume = activate the table */
        k1_awaria(TAG, "DM_DEV_SUSPEND (dm-verity): %s", strerror(errno));
    close(ctl);
    /* huge_encode_dev: minor = low 8 bits | bits 20.. */
    snprintf(out, outsz, "/dev/dm-%u", (unsigned)((u.io.dev & 0xff) | ((u.io.dev >> 12) & ~0xffull)));
    wait_for_block_device(out);

    dm = open(out, O_RDONLY | O_CLOEXEC);
    if (dm < 0)
        k1_awaria(TAG, "%s: %s", out, strerror(errno));
    for (i = 0; i < bloki * VERITY_BLOCK; i += (unsigned long long)n) {
        n = read(dm, buf, sizeof buf);
        if (n < 0 && errno == EINTR) {
            n = 0;
            continue;
        }
        if (n <= 0)
            k1_awaria(TAG, "obraz rdzenia jest zmieniony albo uszkodzony (dm-verity: %s przy %llu KiB) — "
                           "system się nie uruchomi", n < 0 ? strerror(errno) : "koniec danych", i / 1024);
    }
    close(dm);
    k1_log(TAG, "obraz rdzenia sprawdzony (dm-verity, %llu bloków po 4 KiB)", bloki);
}

static void make_dir(const char *path)
{
    if (mkdir(path, 0755) != 0)
        k1_awaria(TAG, "mkdir %s: %s", path, strerror(errno));
}

int main(void)
{
    char cmdline[CMDLINE_MAX];
    char device[K1_CMDLINE_VALUE_MAX], skrot[80], dm[32];
    struct stat st;
    char *const argv[] = { (char *)"/rdzen/init", NULL };
    char *const envp[] = { NULL };
    int builtin, loop = -1;

    if (getpid() != 1) {
        k1_log(TAG, "ten program działa tylko jako PID 1 (initramfs)");
        return 1;
    }
    umask(0077);
    k1_log(TAG, "start");

    read_cmdline(cmdline, sizeof cmdline);
    switch (k1_cmdline_get(cmdline, strlen(cmdline), "k1.rdzen", device, sizeof device)) {
    case K1_CMDLINE_FOUND:
        break;
    case K1_CMDLINE_ABSENT:
        k1_awaria(TAG, "brak parametru k1.rdzen w linii poleceń jądra");
    case K1_CMDLINE_INVALID:
    default:
        k1_awaria(TAG, "niepoprawny parametr k1.rdzen (za długi, powtórzony albo z cudzysłowem)");
    }
    switch (k1_cmdline_get(cmdline, strlen(cmdline), "k1.rdzen_skrot", skrot, sizeof skrot)) {
    case K1_CMDLINE_FOUND:
        if (!k1_valid_hex64(skrot))
            k1_awaria(TAG, "niepoprawny parametr k1.rdzen_skrot: potrzebne 64 znaki szesnastkowe (a-f, 0-9)");
        break;
    case K1_CMDLINE_ABSENT:
        k1_awaria(TAG, "brak parametru k1.rdzen_skrot — obrazu rdzenia nie da się sprawdzić (dm-verity)");
    case K1_CMDLINE_INVALID:
    default:
        k1_awaria(TAG, "niepoprawny parametr k1.rdzen_skrot (za długi, powtórzony albo z cudzysłowem)");
    }
    builtin = strcmp(device, BUILTIN) == 0;
    if (!builtin && !k1_valid_block_device(device))
        k1_awaria(TAG, "niepoprawny parametr k1.rdzen: dozwolone tylko /dev/[a-z0-9]{1,31} albo " BUILTIN);

    if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC, NULL) != 0)
        k1_awaria(TAG, "nie można zamontować /dev: %s", strerror(errno));
    if (builtin)
        loop = attach_builtin(device, sizeof device);
    else
        wait_for_block_device(device);

    /* New root: small tmpfs holding only the three top-level directories (SPEC §5). */
    if (mount("korzen", NEW_ROOT, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
              "mode=0755,size=16k,nr_inodes=8") != 0)
        k1_awaria(TAG, "nie można utworzyć korzenia (tmpfs): %s", strerror(errno));
    make_dir(CORE_DIR);
    make_dir(NEW_ROOT "/aplikacje");
    make_dir(NEW_ROOT "/dane_uzytkownika");

    verity(device, skrot, dm, sizeof dm); /* M6b: the kernel parses only verified bytes */
    if (mount(dm, CORE_DIR, "squashfs", MS_RDONLY | MS_NOSUID | MS_NODEV, NULL) != 0)
        k1_awaria(TAG, "nie udało się zamontować obrazu rdzenia z %s: %s", device, strerror(errno));
    if (loop >= 0)
        close(loop); /* the mount holds the device now */
    if (stat(CORE_INIT, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) == 0)
        k1_awaria(TAG, "obraz rdzenia nie zawiera programu init (/rdzen/init)");

    /* Root becomes read-only for good (superblock remount, same per-mount flags + RDONLY). */
    if (mount(NULL, NEW_ROOT, NULL, MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC,
              NULL) != 0)
        k1_awaria(TAG, "nie można przełączyć korzenia w tryb tylko do odczytu: %s", strerror(errno));

    /* The core image keeps its block device open; /dev is remounted later by /rdzen/init. */
    if (umount2("/dev", 0) != 0)
        k1_awaria(TAG, "nie można odmontować /dev: %s", strerror(errno));

    /* switch_root: free the initramfs, move the new root over "/" and enter it. Its contents
     * are known: our cpio (scripts/zbuduj-obraz.sh) plus the kernel's built-in default list
     * (usr/default_cpio_list: /dev, /dev/console, /root). fds 0-2 keep pointing at the console
     * opened by the kernel before /init started. */
    if (unlink("/init") != 0 || unlink("/dev/console") != 0 || rmdir("/dev") != 0 ||
        rmdir("/proc") != 0 || (rmdir("/root") != 0 && errno != ENOENT) ||
        (builtin && unlink(BUILTIN_IMAGE) != 0)) /* the loop device keeps the file's data */
        k1_awaria(TAG, "nie można usunąć plików initramfs: %s", strerror(errno));
    if (chdir(NEW_ROOT) != 0 || mount(".", "/", NULL, MS_MOVE, NULL) != 0 || chroot(".") != 0 ||
        chdir("/") != 0)
        k1_awaria(TAG, "nie można przełączyć korzenia systemu: %s", strerror(errno));

    k1_log(TAG, "obraz rdzenia zamontowany z %s, uruchamiam /rdzen/init", device);
    execve(argv[0], argv, envp);
    k1_awaria(TAG, "nie można uruchomić /rdzen/init: %s", strerror(errno));
}

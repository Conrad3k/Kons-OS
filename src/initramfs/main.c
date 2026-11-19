/* K1 initramfs — /init inside the initial RAM filesystem (PID 1 during early boot).
 *
 * Job (SPEC §6, ADR 0004): find the block device with the core image, mount it read-only
 * as /rdzen inside a new, read-only root (tmpfs with exactly three directories), delete itself
 * from the initramfs and hand over to /rdzen/init. Nothing else: no shell, no modules, no
 * configuration beyond one kernel command line parameter (k1.rdzen=/dev/XXX).
 * k1.rdzen=wbudowany (k1.efi, 0.7.0): the core image is the file /rdzen.squashfs inside the initramfs
 * built into the kernel and is mounted through a read-only loop device — no storage drivers needed.
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
#include <sys/stat.h>
#include <unistd.h>


#include "k1_cmdline.h"
#include "k1_log.h"
#include "k1_verity.h"

#define TAG "K1-INITRAMFS"

#define NEW_ROOT "/nowy_korzen"
#define CORE_DIR NEW_ROOT "/rdzen"
#define CORE_INIT CORE_DIR "/init"
#define CMDLINE_MAX 4096
#define DEVICE_WAIT_STEPS 100 /* x 100 ms = 10 s */
#define BUILTIN "wbudowany"
#define BUILTIN_IMAGE "/rdzen.squashfs"
#define VERITY_NAME "k1-rdzen"

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
    if (k1_czekaj_na_wezel(path, DEVICE_WAIT_STEPS) == 0)
        return;
    if (errno == ENOTBLK)
        k1_awaria(TAG, "%s nie jest urządzeniem blokowym", path);
    k1_awaria(TAG, "nie znaleziono urządzenia z obrazem rdzenia: %s", path);
}

/* Attaches the built-in image to a free loop device (read-only, freed with the mount);
 * writes the device path into `device`. Returns the open loop device: it must stay open until the
 * image is mounted (LO_FLAGS_AUTOCLEAR detaches the device when the last user closes it). */
static int attach_builtin(char *device, size_t size)
{
    int img, loop;
    img = open(BUILTIN_IMAGE, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (img < 0)
        k1_awaria(TAG, "brak wbudowanego obrazu rdzenia %s: %s", BUILTIN_IMAGE, strerror(errno));
    loop = k1_loop_podlacz("/dev", img, 0, 0, device, size);
    if (loop < 0)
        k1_awaria(TAG, "%s: %s", loop == -K1_VERITY_WEZEL ? "brak urządzenia loop" : "LOOP_CONFIGURE", strerror(errno));
    close(img);
    return loop;
}

/* M6b: puts a dm-verity device over `device` (data: the SquashFS rounded up to 4 KiB, then the hash
 * tree; no superblock), reads it all once and writes its path ("/dev/dm-N") into `out`. */
static void verity(const char *device, const char *skrot, char *out, size_t outsz)
{
    unsigned char sb[96];
    static char buf[65536];
    unsigned long long bajty, bloki, rozmiar, i;
    int fd = open(device, O_RDONLY | O_CLOEXEC), dm;
    ssize_t n;

    if (fd < 0 || pread(fd, sb, sizeof sb, 0) != (ssize_t)sizeof sb)
        k1_awaria(TAG, "nie można odczytać obrazu rdzenia z %s: %s", device, strerror(errno));
    if (sb[0] != 'h' || sb[1] != 's' || sb[2] != 'q' || sb[3] != 's')
        k1_awaria(TAG, "nie udało się zamontować obrazu rdzenia z %s: to nie jest obraz SquashFS", device);
    bajty = 0;
    for (i = 0; i < 8; i++)
        bajty |= (unsigned long long)sb[40 + i] << (8 * i); /* bytes_used */
    bloki = (bajty + K1_VERITY_BLOK - 1) / K1_VERITY_BLOK;
    rozmiar = (unsigned long long)lseek(fd, 0, SEEK_END);
    close(fd);
    /* one data block: the root hash is its own hash, no tree blocks follow */
    if (bloki == 0 || rozmiar < (bloki + (bloki > 1 ? 1 : 0)) * K1_VERITY_BLOK)
        k1_awaria(TAG, "obraz rdzenia bez drzewa skrótów (dm-verity) — nie może być sprawdzony");

    switch (k1_verity_utworz("/dev", VERITY_NAME, device, bloki, skrot, out, outsz)) {
    case 0:
        break;
    case -K1_VERITY_WEZEL:
        k1_awaria(TAG, "brak urządzenia dm-verity %s: %s", out, strerror(errno));
    default:
        k1_awaria(TAG, "dm-verity (/dev/mapper/control): %s", strerror(errno));
    }
    dm = open(out, O_RDONLY | O_CLOEXEC);
    if (dm < 0)
        k1_awaria(TAG, "%s: %s", out, strerror(errno));
    for (i = 0; i < bloki * K1_VERITY_BLOK; i += (unsigned long long)n) {
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

/* k1.rdzen (the core image: a block device or the one built into k1.efi) and k1.rdzen_skrot (its dm-verity
 * root hash) from the kernel command line; anything wrong stops the boot. Returns 1 for the built-in image. */
static int parametry(char *device, size_t n_device, char *skrot, size_t n_skrot)
{
    char cmdline[CMDLINE_MAX];
    int builtin;

    read_cmdline(cmdline, sizeof cmdline);
    switch (k1_cmdline_get(cmdline, strlen(cmdline), "k1.rdzen", device, n_device)) {
    case K1_CMDLINE_FOUND:
        break;
    case K1_CMDLINE_ABSENT:
        k1_awaria(TAG, "brak parametru k1.rdzen w linii poleceń jądra");
    case K1_CMDLINE_INVALID:
    default:
        k1_awaria(TAG, "niepoprawny parametr k1.rdzen (za długi, powtórzony albo z cudzysłowem)");
    }
    switch (k1_cmdline_get(cmdline, strlen(cmdline), "k1.rdzen_skrot", skrot, n_skrot)) {
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
    return builtin;
}

/* switch_root: free the initramfs, move the new root over "/" and enter it. Its contents
 * are known: our cpio (scripts/zbuduj-obraz.sh) plus the kernel's built-in default list
 * (usr/default_cpio_list: /dev, /dev/console, /root). fds 0-2 keep pointing at the console
 * opened by the kernel before /init started. */
static void przelacz_korzen(int builtin)
{
    if (unlink("/init") != 0 || unlink("/dev/console") != 0 || rmdir("/dev") != 0 ||
        rmdir("/proc") != 0 || (rmdir("/root") != 0 && errno != ENOENT) ||
        (builtin && unlink(BUILTIN_IMAGE) != 0)) /* the loop device keeps the file's data */
        k1_awaria(TAG, "nie można usunąć plików initramfs: %s", strerror(errno));
    if (chdir(NEW_ROOT) != 0 || mount(".", "/", NULL, MS_MOVE, NULL) != 0 || chroot(".") != 0 ||
        chdir("/") != 0)
        k1_awaria(TAG, "nie można przełączyć korzenia systemu: %s", strerror(errno));
}

int main(void)
{
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

    builtin = parametry(device, sizeof device, skrot, sizeof skrot);

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

    przelacz_korzen(builtin);
    k1_log(TAG, "obraz rdzenia zamontowany z %s, uruchamiam /rdzen/init", device);
    execve(argv[0], argv, envp);
    k1_awaria(TAG, "nie można uruchomić /rdzen/init: %s", strerror(errno));
}

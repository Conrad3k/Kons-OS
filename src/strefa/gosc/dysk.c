/* K1 gosc — the disk of a zone that remembers (0.10.0 step 5, ADR 0042 T11): the second virtio-blk disk (/dev/vdb) of
 * the VMM, an ext4 made at K1's build (the zone needs no mkfs). A zone that forgets has the disk too, with 0 sectors:
 * nothing to mount, its data stays in memory. A journal: a machine that ended without unmounting (K1 switched off, a
 * crash) is repaired by the kernel at the next mount. In the test zone (gosc's own mode) a counter of starts on it shows
 * that it remembers (test-strefa reads it in the log). Paths are the zone image's, never K1's. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/fs.h>

#include "gosc.h"

#define DYSK "/dev/vdb"

int dysk_montuj(const char *gdzie)
{
    uint64_t bajty = 0;
    int fd = open(DYSK, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || ioctl(fd, (int)BLKGETSIZE64, &bajty) != 0)
        bajty = 0;
    if (fd >= 0)
        close(fd);
    if (bajty == 0) {
        loguj("dysk: brak — sesja zapomina po zamknięciu");
        return -1;
    }
    if (mount(DYSK, gdzie, "ext4", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_NOATIME, "errors=remount-ro") != 0) {
        loguj("dysk: montowanie nieudane (%s) — dane tylko w pamięci", strerror(errno));
        return -1;
    }
    loguj("dysk: %llu MiB, zamontowany", (unsigned long long)(bajty >> 20));
    return 0;
}

void dysk_odmontuj(const char *gdzie)
{
    sync();
    for (int i = 0; i < 20 && umount2(gdzie, 0) != 0; i++) /* the processes killed just before may still hold it */
        usleep(100 * 1000);
}

int dysk_licznik(const char *gdzie)
{
    char sciezka[64], b[16] = { 0 };
    int n = 0, fd;
    snprintf(sciezka, sizeof sciezka, "%s/licznik", gdzie);
    fd = open(sciezka, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (read(fd, b, sizeof b - 1) > 0)
        n = atoi(b);
    n++;
    snprintf(b, sizeof b, "%d\n", n);
    if (pwrite(fd, b, strlen(b), 0) != (ssize_t)strlen(b) || fsync(fd) != 0)
        n = -1;
    close(fd);
    loguj("dysk: uruchomienie %d", n);
    return n;
}

void dysk_zamkniecie(const char *gdzie)
{
    char sciezka[64];
    int fd, ok;
    snprintf(sciezka, sizeof sciezka, "%s/zamkniecie", gdzie);
    fd = open(sciezka, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    ok = fd >= 0 && write(fd, "tak\n", 4) == 4 && fsync(fd) == 0;
    if (fd >= 0)
        close(fd);
    loguj("dysk: zamknięcie zapisane: %s", ok ? "tak" : "nie");
}

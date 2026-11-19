/* K1 — a verified image: dm-verity over a read-only loop device (k1_verity.h). */
#include "k1_verity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <time.h>
#include <unistd.h>

#include <linux/dm-ioctl.h>
#include <linux/loop.h>

int k1_czekaj_na_wezel(const char *sciezka, int kroki)
{
    struct stat st;
    struct timespec krok = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
    for (int i = 0; i < kroki; i++) {
        if (stat(sciezka, &st) == 0) {
            if (S_ISBLK(st.st_mode))
                return 0;
            errno = ENOTBLK;
            return -1;
        }
        nanosleep(&krok, NULL);
    }
    errno = ENOENT;
    return -1;
}

int k1_loop_podlacz(const char *dev, int plik, uint64_t od, uint64_t bajty, char *out, size_t n)
{
    struct loop_config cfg;
    char sciezka[64];
    int ctl, loop, nr, e;
    snprintf(sciezka, sizeof sciezka, "%s/loop-control", dev);
    ctl = open(sciezka, O_RDWR | O_CLOEXEC);
    if (ctl < 0)
        return -K1_VERITY_LOOP;
    nr = ioctl(ctl, (int)LOOP_CTL_GET_FREE);
    e = errno;
    close(ctl);
    if (nr < 0) {
        errno = e;
        return -K1_VERITY_LOOP;
    }
    snprintf(out, n, "%s/loop%d", dev, nr);
    if (k1_czekaj_na_wezel(out, 100) != 0)
        return -K1_VERITY_WEZEL;
    loop = open(out, O_RDONLY | O_CLOEXEC);
    if (loop < 0)
        return -K1_VERITY_LOOP;
    memset(&cfg, 0, sizeof cfg);
    cfg.fd = (unsigned)plik;
    cfg.info.lo_offset = od;
    cfg.info.lo_sizelimit = bajty;
    cfg.info.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;
    if (ioctl(loop, (int)LOOP_CONFIGURE, &cfg) != 0) {
        e = errno;
        close(loop);
        errno = e;
        return -K1_VERITY_LOOP;
    }
    return loop;
}

static void dm_init(struct dm_ioctl *io, size_t size, const char *nazwa)
{
    memset(io, 0, size);
    io->version[0] = DM_VERSION_MAJOR;
    io->data_size = (uint32_t)size;
    io->data_start = sizeof *io;
    snprintf(io->name, sizeof io->name, "%s", nazwa);
}

int k1_verity_utworz(const char *dev, const char *nazwa, const char *urz, uint64_t bloki, const char *skrot, char *out,
                     size_t n)
{
    union {
        struct dm_ioctl io;
        char b[4096];
    } u;
    struct dm_target_spec *t;
    struct stat st;
    char sciezka[64];
    int ctl, e;
    if (stat(urz, &st) != 0 || !S_ISBLK(st.st_mode)) {
        errno = ENOTBLK;
        return -K1_VERITY_DM;
    }
    snprintf(sciezka, sizeof sciezka, "%s/mapper/control", dev);
    ctl = open(sciezka, O_RDWR | O_CLOEXEC);
    if (ctl < 0)
        return -K1_VERITY_DM;
    dm_init(&u.io, sizeof u, nazwa);
    if (ioctl(ctl, (int)DM_DEV_CREATE, &u.io) != 0)
        goto blad;
    dm_init(&u.io, sizeof u, nazwa);
    u.io.target_count = 1;
    u.io.flags = DM_READONLY_FLAG;
    t = (struct dm_target_spec *)(u.b + sizeof u.io);
    t->sector_start = 0;
    t->length = bloki * (K1_VERITY_BLOK / 512);
    snprintf(t->target_type, sizeof t->target_type, "verity");
    snprintf((char *)(t + 1), sizeof u - sizeof u.io - sizeof *t, "1 %u:%u %u:%u %u %u %llu %llu sha256 %s %s",
             major(st.st_rdev), minor(st.st_rdev), major(st.st_rdev), minor(st.st_rdev), K1_VERITY_BLOK, K1_VERITY_BLOK,
             (unsigned long long)bloki, (unsigned long long)bloki, skrot, K1_VERITY_SOL);
    t->next = 0;
    if (ioctl(ctl, (int)DM_TABLE_LOAD, &u.io) != 0)
        goto usun;
    dm_init(&u.io, sizeof u, nazwa);
    if (ioctl(ctl, (int)DM_DEV_SUSPEND, &u.io) != 0) /* no SUSPEND flag: resume = activate the table */
        goto usun;
    close(ctl);
    /* huge_encode_dev: minor = low 8 bits | bits 20.. */
    snprintf(out, n, "%s/dm-%u", dev, (unsigned)((u.io.dev & 0xff) | ((u.io.dev >> 12) & ~0xffull)));
    return k1_czekaj_na_wezel(out, 100) == 0 ? 0 : -K1_VERITY_WEZEL;
usun:
    e = errno;
    dm_init(&u.io, sizeof u, nazwa);
    ioctl(ctl, (int)DM_DEV_REMOVE, &u.io); /* the empty device made above */
    errno = e;
blad:
    e = errno;
    close(ctl);
    errno = e;
    return -K1_VERITY_DM;
}

int k1_verity_usun(const char *dev, const char *nazwa)
{
    struct dm_ioctl io;
    char sciezka[64];
    int ctl, r;
    snprintf(sciezka, sizeof sciezka, "%s/mapper/control", dev);
    ctl = open(sciezka, O_RDWR | O_CLOEXEC);
    if (ctl < 0)
        return -1;
    dm_init(&io, sizeof io, nazwa);
    io.flags = DM_DEFERRED_REMOVE;
    r = ioctl(ctl, (int)DM_DEV_REMOVE, &io);
    close(ctl);
    return r == 0 || errno == ENXIO ? 0 : -1; /* ENXIO: there was no such device */
}

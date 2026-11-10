/* K1 nadzorca — K1-DANE to the end of the USB stick (0.8.0 step 5, ADR 0033).
 *
 * The image for a stick has a K1-DANE of 64 MiB; written to a stick of 16 GB, the rest of it would be wasted. On a
 * start, before skarbiec gets the partition: if K1-DANE is on a USB stick, is still empty (its first MiB is zeros —
 * skarbiec has never formatted it, so nothing can be lost) and the stick has at least 16 MiB more after it, we write
 * a new partition table with K1-DANE up to the end of the stick (k1_gpt_powieksz: the table read back is checked —
 * header, CRCs, K1-DANE where the kernel says it is and the last partition) and tell the kernel the new size
 * (BLKPG_RESIZE_PARTITION). Order on the stick: the new backup copy at its end, then the primary entries and header,
 * then the protective MBR — a power loss in between leaves one valid copy. A stick with data in K1-DANE is never
 * touched. Part of nadzorca (see main.c). */
#include "nadzorca.h"

#include <linux/blkpg.h>
#include <sys/ioctl.h>

#include "gpt.h"
#include "k1_bajty.h"

#define PUSTE (1u << 20) /* "empty" for skarbiec too (magazyn.c PUSTE_BAJTY) */

static uint8_t mbr[4096], glowny[4096], zapasowy[4096], wpisy[K1_GPT_WPISY_BAJTY], zera[K1_GPT_WPISY_BAJTY];

static int pusta(int fd)
{
    static uint8_t buf[PUSTE];
    if (k1_czytaj_calosc(fd, buf, sizeof buf, 0) != 0)
        return 0;
    for (size_t i = 0; i < sizeof buf; i++)
        if (buf[i] != 0)
            return 0;
    return 1;
}

static int pisz(int fd, const void *b, size_t n, uint64_t lba, uint32_t sektor)
{
    return k1_pisz_calosc(fd, b, n, lba * sektor);
}

/* The new table on the whole stick `d` (opened read-write) and the kernel told. 0, or -1 (logged). */
static int zapisz(int d, const struct k1_czesc *c, uint64_t sektory)
{
    uint32_t s = (uint32_t)c->sektor, ws = K1_GPT_WPISY_BAJTY / s;
    uint64_t koniec, stary_zapasowy;
    struct blkpg_partition p;
    struct blkpg_ioctl_arg a;
    int r;
    if (k1_czytaj_calosc(d, mbr, s, 0) != 0 || k1_czytaj_calosc(d, glowny, s, s) != 0 ||
        k1_czytaj_calosc(d, wpisy, sizeof wpisy, 2ull * s) != 0)
        return -1;
    stary_zapasowy = k1_c64(glowny + 32);
    r = k1_gpt_powieksz(s, sektory, c->od_512 * 512 / s, mbr, glowny, zapasowy, wpisy, &koniec);
    if (r != 0) {
        k1_log(TAG, "K1-DANE: %s", r > 0 ? "pendrive nie ma więcej miejsca" : "tablica partycji inna niż K1 — bez zmian");
        return -1;
    }
    if (pisz(d, wpisy, sizeof wpisy, sektory - 1 - ws, s) != 0 || pisz(d, zapasowy, s, sektory - 1, s) != 0 ||
        fdatasync(d) != 0 || pisz(d, wpisy, sizeof wpisy, 2, s) != 0 || pisz(d, glowny, s, 1, s) != 0 || fdatasync(d) != 0 ||
        pisz(d, mbr, s, 0, s) != 0)
        return -1;
    if (stary_zapasowy > 2 + ws && stary_zapasowy < sektory - 1 - ws) /* the old backup copy, now inside K1-DANE */
        if (pisz(d, zera, s, stary_zapasowy, s) != 0 || pisz(d, zera, sizeof zera, stary_zapasowy - ws, s) != 0)
            k1_log(TAG, "K1-DANE: starej kopii tablicy nie udało się wyczyścić");
    if (fdatasync(d) != 0)
        return -1;
    memset(&p, 0, sizeof p);
    p.pno = c->numer;
    p.start = (long long)(c->od_512 * 512);
    p.length = (long long)((koniec + 1) * s - c->od_512 * 512);
    a = (struct blkpg_ioctl_arg){ .op = BLKPG_RESIZE_PARTITION, .datalen = (int)sizeof p, .data = &p };
    if (ioctl(d, BLKPG, &a) != 0) {
        k1_log(TAG, "K1-DANE: jądro nie przyjęło nowego rozmiaru: %s", strerror(errno));
        return -1;
    }
    k1_log(TAG, "K1-DANE powiększona do końca pendrive'a: %llu → %llu MiB", (unsigned long long)(c->dl_512 >> 11),
           (unsigned long long)((uint64_t)p.length >> 20));
    return 0;
}

/* The data partition skarbiec gets (otworz_partycje_danych), first grown to the end of a USB stick when it is still
 * empty. */
int otworz_dane(int *ile)
{
    struct k1_czesc c;
    char dev[64];
    int fd = otworz_partycje_danych(ile), d;
    if (fd < 0 || !k1_dysk_na_usb(SYS, skarb_dane) || k1_czesc_opis(SYS, skarb_dane, &c) != 0 ||
        c.dysk_512 - c.od_512 - c.dl_512 < (16u << 11) || !pusta(fd))
        return fd;
    snprintf(dev, sizeof dev, DEV "/%s", c.dysk);
    d = open(dev, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (d < 0)
        return fd;
    close(fd); /* the kernel changes the partition's size: open it again afterwards */
    if (zapisz(d, &c, c.dysk_512 * 512 / (uint64_t)c.sektor) != 0)
        k1_log(TAG, "K1-DANE: powiększenie nieudane — zostaje jak była");
    close(d);
    return otworz_partycje_danych(ile);
}

/* K1 nadzorca — K1-DANE to the end of the USB stick (0.8.0 step 5, ADR 0033).
 *
 * The image for a stick has a K1-DANE of 64 MiB; written to a stick of 16 GB, the rest of it would be wasted. On a
 * start, before skarbiec gets the partition: if K1-DANE is on a USB stick, is still empty (its first MiB is zeros —
 * skarbiec has never formatted it, so nothing can be lost) and the stick has at least 16 MiB more after it, we write
 * a new partition table with K1-DANE up to the end of the stick (k1_gpt_powieksz: the table read back is checked —
 * header, CRCs, K1-DANE where the kernel says it is and the last partition) and tell the kernel the new size
 * (BLKPG_RESIZE_PARTITION). Order on the stick: the new backup copy at its end, then the primary entries and header,
 * then the protective MBR — a power loss in between leaves one valid copy. A stick with data in K1-DANE is not touched
 * at start; since 0.8.1 (step 3, ADR 0033) it grows when the user asks in Ustawienia → Informacje ("Użyj całego
 * pendrive'a"): skarbiec, with the vault open, sends POWIEKSZ_DANE; the same table writes, then skarbiec takes the
 * grown partition (it keeps the partition open: BLKPG_RESIZE_PARTITION does not mind, block/partitions/core.c).
 * Part of nadzorca (see main.c). */
#include "nadzorca.h"

#include <linux/blkpg.h>
#include <sys/ioctl.h>

#include "gpt.h"
#include "k1_bajty.h"

#define PUSTE (1u << 20) /* "empty" for skarbiec too (magazyn.c PUSTE_BAJTY) */
#define ZAPAS_MIN (16u << 11) /* in 512-byte units: less after K1-DANE is not worth a new table */

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

/* The new table on the whole stick `d` (opened read-write) and the kernel told. 0, K1_SKARB_ARG (the table is not one
 * K1 wrote, no room) or K1_SKARB_IO (logged). */
static int zapisz(int d, const struct k1_czesc *c, uint64_t sektory)
{
    uint32_t s = (uint32_t)c->sektor, ws = K1_GPT_WPISY_BAJTY / s;
    uint64_t koniec, stary_zapasowy;
    struct blkpg_partition p;
    struct blkpg_ioctl_arg a;
    int r;
    if (k1_czytaj_calosc(d, mbr, s, 0) != 0 || k1_czytaj_calosc(d, glowny, s, s) != 0 ||
        k1_czytaj_calosc(d, wpisy, sizeof wpisy, 2ull * s) != 0)
        return K1_SKARB_IO;
    stary_zapasowy = k1_c64(glowny + 32);
    r = k1_gpt_powieksz(s, sektory, c->od_512 * 512 / s, mbr, glowny, zapasowy, wpisy, &koniec);
    if (r != 0) {
        k1_log(TAG, "K1-DANE: %s", r > 0 ? "pendrive nie ma więcej miejsca" : "tablica partycji inna niż K1 — bez zmian");
        return K1_SKARB_ARG;
    }
    if (pisz(d, wpisy, sizeof wpisy, sektory - 1 - ws, s) != 0 || pisz(d, zapasowy, s, sektory - 1, s) != 0 ||
        fdatasync(d) != 0 || pisz(d, wpisy, sizeof wpisy, 2, s) != 0 || pisz(d, glowny, s, 1, s) != 0 || fdatasync(d) != 0 ||
        pisz(d, mbr, s, 0, s) != 0)
        return K1_SKARB_IO;
    if (stary_zapasowy > 2 + ws && stary_zapasowy < sektory - 1 - ws) /* the old backup copy, now inside K1-DANE */
        if (pisz(d, zera, s, stary_zapasowy, s) != 0 || pisz(d, zera, sizeof zera, stary_zapasowy - ws, s) != 0)
            k1_log(TAG, "K1-DANE: starej kopii tablicy nie udało się wyczyścić");
    if (fdatasync(d) != 0)
        return K1_SKARB_IO;
    memset(&p, 0, sizeof p);
    p.pno = c->numer;
    p.start = (long long)(c->od_512 * 512);
    p.length = (long long)((koniec + 1) * s - c->od_512 * 512);
    a = (struct blkpg_ioctl_arg){ .op = BLKPG_RESIZE_PARTITION, .datalen = (int)sizeof p, .data = &p };
    if (ioctl(d, BLKPG, &a) != 0) {
        k1_log(TAG, "K1-DANE: jądro nie przyjęło nowego rozmiaru: %s", strerror(errno));
        return K1_SKARB_IO;
    }
    k1_log(TAG, "K1-DANE powiększona do końca pendrive'a: %llu → %llu MiB", (unsigned long long)(c->dl_512 >> 11),
           (unsigned long long)((uint64_t)p.length >> 20));
    return 0;
}

/* K1-DANE skarbiec has, on a USB stick with at least 16 MiB after it: its description in *c. 1 or 0. */
static int da_sie(struct k1_czesc *c)
{
    return skarb_dane[0] != '\0' && k1_dysk_na_usb(SYS, skarb_dane) && k1_czesc_opis(SYS, skarb_dane, c) == 0 &&
           c->dysk_512 - c->od_512 - c->dl_512 >= ZAPAS_MIN;
}

/* The whole stick of K1-DANE `c`, the new table written (0 or K1_SKARB_*). */
static int powieksz(const struct k1_czesc *c)
{
    char dev[64];
    int d, r;
    snprintf(dev, sizeof dev, DEV "/%s", c->dysk);
    d = open(dev, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (d < 0)
        return K1_SKARB_IO;
    r = zapisz(d, c, c->dysk_512 * 512 / (uint64_t)c->sektor);
    close(d);
    return r;
}

int zapas_danych_mib(void)
{
    struct k1_czesc c;
    return da_sie(&c) ? (int)((c.dysk_512 - c.od_512 - c.dl_512) >> 11) : 0;
}

/* The data partition skarbiec gets (otworz_partycje_danych), first grown to the end of a USB stick when it is still
 * empty. */
int otworz_dane(int *ile)
{
    struct k1_czesc c;
    int fd = otworz_partycje_danych(ile);
    if (fd < 0 || !da_sie(&c) || !pusta(fd))
        return fd;
    close(fd); /* the kernel changes the partition's size: open it again afterwards */
    if (powieksz(&c) != 0)
        k1_log(TAG, "K1-DANE: powiększenie nieudane — zostaje jak była");
    return otworz_partycje_danych(ile);
}

/* skarbiec's POWIEKSZ_DANE (0.8.1 step 3, ADR 0033): the user clicked "Użyj całego pendrive'a" in Ustawienia and the
 * vault is open. The partition grows with its data in it; skarbiec gets the result (POWIEKSZONO) and takes the room. */
void powieksz_na_prosbe(void)
{
    struct k1_czesc c;
    int r = K1_SKARB_ARG;
    struct k1_komunikat k;
    if (da_sie(&c)) {
        k1_log(TAG, "K1-DANE: powiększenie na prośbę z Ustawień (dane zostają na miejscu)");
        r = powieksz(&c);
        if (r != 0)
            k1_log(TAG, "K1-DANE: powiększenie nieudane — zostaje jak była");
    } else {
        k1_log(TAG, "K1-DANE: powiększenie niemożliwe (nie na pendrivie albo brak miejsca za nią)");
    }
    k = k1_komunikat_nowy(K1_KOM_POWIEKSZONO, 0, r, 0, 0, 0, 0);
    if (skarb_ster >= 0 && k1_kanal_wyslij(skarb_ster, &k, sizeof k, -1) != 0)
        k1_log(TAG, "odpowiedź do skarbca: %s", strerror(errno));
}

/* K1 — host test of the balloon's lists, blocks and statistics (src/strefa/strony.c; 0.10.1 step 2, ADR 0044): what
 * the guest's balloon driver gives the VMM. Good lists join neighbouring pages and land in the memfd where the pages
 * are (below the devices, and above 4 GiB for a larger guest); every page outside the guest's RAM or in the hole, a
 * list not of whole numbers or too long, a block not of whole pages, statistics of a wrong length or tag — refused. */
#include <string.h>
#include <sys/mman.h>

#include "k1_test_host.h"
#include "strony.h"

static void lista_z(uint32_t *nr, int n, uint8_t *b)
{
    memcpy(b, nr, 4u * (unsigned)n);
}

static void t_lista(void)
{
    static uint8_t pam[1u << 20];
    const struct gosc_pamiec m = { pam, sizeof pam, 0 };
    struct strony_zakres z[STRONY_LISTA_MAKS];
    uint8_t b[4 * (STRONY_LISTA_MAKS + 1)];
    uint32_t nr[] = { 3, 4, 5, 9, 8, 255 };
    const char *powod = NULL;
    lista_z(nr, 6, b);
    EXPECT(strony_lista(b, 24, &m, z, &powod) == 4);
    EXPECT(z[0].od == 3 * STRONA && z[0].dl == 3 * STRONA); /* 3, 4, 5 joined */
    EXPECT(z[1].od == 9 * STRONA && z[1].dl == STRONA && z[2].od == 8 * STRONA); /* 9, 8: not in order — apart */
    EXPECT(z[3].od == 255 * STRONA);
    EXPECT(strony_lista(b, 0, &m, z, &powod) == 0);
    nr[0] = 256; /* past 1 MiB */
    lista_z(nr, 1, b);
    EXPECT(strony_lista(b, 4, &m, z, &powod) == -1 && strstr(powod, "poza pamięcią") != NULL);
    nr[0] = 0xffffffffu;
    lista_z(nr, 1, b);
    EXPECT(strony_lista(b, 4, &m, z, &powod) == -1);
    EXPECT(strony_lista(b, 6, &m, z, &powod) == -1); /* not whole numbers */
    for (uint32_t i = 0; i <= STRONY_LISTA_MAKS; i++)
        memcpy(b + 4 * i, &i, 4);
    EXPECT(strony_lista(b, 4 * STRONY_LISTA_MAKS, &m, z, &powod) == 1 && z[0].dl == STRONA * STRONY_LISTA_MAKS); /* 0..255 */
    memset(b, 0, sizeof b);
    EXPECT(strony_lista(b, 4 * STRONY_LISTA_MAKS, &m, z, &powod) == (int)STRONY_LISTA_MAKS); /* one page 256 times: apart */
    EXPECT(strony_lista(b, 4 * (STRONY_LISTA_MAKS + 1), &m, z, &powod) == -1); /* too long */
}

static void t_gora(void) /* a guest of 6 GiB: 3 GiB below the devices, 3 GiB from 4 GiB */
{
    const uint64_t cala = 6ull << 30;
    uint8_t *baza = mmap(NULL, cala, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    const struct gosc_pamiec m = { baza, cala, 3ull << 30 };
    struct strony_zakres z[STRONY_LISTA_MAKS];
    uint32_t nr[3] = { (uint32_t)((3ull << 30) / STRONA) - 1, (uint32_t)(GP_GORA / STRONA), (uint32_t)(GP_GORA / STRONA) + 1 };
    uint8_t b[12];
    const char *powod = NULL;
    if (baza == MAP_FAILED) {
        EXPECT(0);
        return;
    }
    lista_z(nr, 3, b);
    /* the last page below the devices and the first two from 4 GiB: neighbours in the memfd, one run */
    EXPECT(strony_lista(b, 12, &m, z, &powod) == 1 && z[0].od == (3ull << 30) - STRONA && z[0].dl == 3 * STRONA);
    nr[0] = (uint32_t)((3ull << 30) / STRONA); /* the first page of the hole */
    lista_z(nr, 1, b);
    EXPECT(strony_lista(b, 4, &m, z, &powod) == -1);
    nr[0] = (uint32_t)((GP_GORA + (3ull << 30)) / STRONA); /* just past the end */
    lista_z(nr, 1, b);
    EXPECT(strony_lista(b, 4, &m, z, &powod) == -1);
    munmap(baza, cala);
}

static void t_blok(void)
{
    static uint8_t pam[1u << 20];
    const struct gosc_pamiec m = { pam, sizeof pam, 0 };
    struct strony_zakres z;
    const char *powod = NULL;
    EXPECT(strony_blok(&m, pam + 2 * STRONA, 8 * STRONA, &z, &powod) == 0 && z.od == 2 * STRONA && z.dl == 8 * STRONA);
    EXPECT(strony_blok(&m, pam + 2 * STRONA + 512, STRONA, &z, &powod) == -1); /* not on a page */
    EXPECT(strony_blok(&m, pam + 2 * STRONA, STRONA / 2, &z, &powod) == -1);   /* half a page */
    EXPECT(strony_blok(&m, pam, 0, &z, &powod) == -1);                         /* empty */
}

static void stat(uint8_t *b, unsigned i, uint16_t tag, uint64_t v)
{
    memcpy(b + STRONY_STAT_DL * i, &tag, 2);
    memcpy(b + STRONY_STAT_DL * i + 2, &v, 8);
}

static void t_staty(void)
{
    uint8_t b[STRONY_STAT_DL * (STRONY_STAT_MAKS + 1)];
    uint64_t st[STRONY_STAT_MAKS];
    uint32_t jest;
    const char *powod = NULL;
    stat(b, 0, 6, 812ull << 20); /* VIRTIO_BALLOON_S_AVAIL */
    stat(b, 1, 7, 100ull << 20); /* CACHES */
    EXPECT(strony_statystyki(b, 2 * STRONY_STAT_DL, st, &jest, &powod) == 0 && jest == ((1u << 6) | (1u << 7)) &&
           st[6] == 812ull << 20 && st[7] == 100ull << 20);
    EXPECT(strony_statystyki(b, 0, st, &jest, &powod) == 0 && jest == 0);
    EXPECT(strony_statystyki(b, 15, st, &jest, &powod) == -1);  /* not whole entries */
    stat(b, 1, 6, 1);
    EXPECT(strony_statystyki(b, 2 * STRONY_STAT_DL, st, &jest, &powod) == -1); /* the same tag twice */
    stat(b, 1, 16, 1);
    EXPECT(strony_statystyki(b, 2 * STRONY_STAT_DL, st, &jest, &powod) == -1); /* an unknown tag */
    for (unsigned i = 0; i <= STRONY_STAT_MAKS; i++)
        stat(b, i, (uint16_t)(i % STRONY_STAT_MAKS), 1);
    EXPECT(strony_statystyki(b, sizeof b, st, &jest, &powod) == -1);           /* more than 16 */
}

int main(void)
{
    t_lista();
    t_gora();
    t_blok();
    t_staty();
    return K1_TEST_KONIEC("test_balon");
}

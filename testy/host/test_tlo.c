/* K1 — host test of reading a virtio-gpu resource's backing (src/strefa/tlo.c; 0.10.5): a guest with memory in two
 * parts (below the devices and from 4 GiB, ADR 0044) — the bytes of a page above 4 GiB come from where that part lies in
 * the VMM's memory (right after the lower one), not from its guest address (the black browser window on an 8 GB X230);
 * a page split over entries of both parts reads whole; anything outside the backing or the guest's memory — refused. */
#include <string.h>

#include "k1_test_host.h"
#include "tlo.h"

#define DOL (64u << 10)
#define GORA (64u << 10)

static uint8_t pam[DOL + GORA];

static void t_gora(void)
{
    const struct gosc_pamiec m = { pam, sizeof pam, DOL };
    struct virtio_gpu_mem_entry t[3] = { { .addr = 4096, .length = 4096 },              /* lower part */
                                         { .addr = GP_GORA + 8192, .length = 4096 },    /* upper part */
                                         { .addr = GP_GORA, .length = 4096 } };
    uint64_t od[3] = { 0, 4096, 8192 };
    uint8_t b[12288];
    unsigned i;
    for (i = 0; i < sizeof pam; i++)
        pam[i] = (uint8_t)(i >> 12); /* each page its number */
    EXPECT(tlo_czytaj(&m, t, od, 3, 0, b, sizeof b) == 0);
    EXPECT(b[0] == 1 && b[4095] == 1);                     /* guest page 1 */
    EXPECT(b[4096] == (DOL >> 12) + 2 && b[8191] == (DOL >> 12) + 2); /* GP_GORA + 8 KiB: after the lower part */
    EXPECT(b[8192] == (DOL >> 12));                        /* GP_GORA: the first page after the lower part */
    EXPECT(tlo_czytaj(&m, t, od, 3, 4000, b, 200) == 0 && b[0] == 1 && b[199] == (DOL >> 12) + 2); /* across entries */
    EXPECT(tlo_czytaj(&m, t, od, 3, 12000, b, 288) == 0 && b[0] == (DOL >> 12));
}

static void t_poza(void)
{
    const struct gosc_pamiec m = { pam, sizeof pam, DOL };
    struct virtio_gpu_mem_entry t[1] = { { .addr = GP_GORA + GORA - 4096, .length = 4096 } };
    uint64_t od[1] = { 0 };
    uint8_t b[8192];
    EXPECT(tlo_czytaj(&m, t, od, 1, 0, b, 4096) == 0);
    EXPECT(tlo_czytaj(&m, t, od, 1, 0, b, 4097) == -1);     /* past the backing */
    EXPECT(tlo_czytaj(&m, t, od, 1, 4096, b, 1) == -1);
    t[0].addr = GP_GORA + GORA; /* past the guest's memory (never after a checked attach) */
    EXPECT(tlo_czytaj(&m, t, od, 1, 0, b, 1) == -1);
    t[0].addr = DOL; /* the hole between the parts */
    EXPECT(tlo_czytaj(&m, t, od, 1, 0, b, 1) == -1);
    EXPECT(tlo_czytaj(&m, t, od, 0, 0, b, 1) == -1);        /* no backing */
    EXPECT(tlo_czytaj(&m, t, od, 0, 0, b, 0) == 0);
}

int main(void)
{
    t_gora();
    t_poza();
    return K1_TEST_KONIEC("test_tlo");
}

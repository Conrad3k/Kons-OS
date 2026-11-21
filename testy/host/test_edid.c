/* K1 — host test of the zone display's EDID (src/strefa/edid.c; 0.11.0 step 1, T9): read back the way the zone's
 * kernel does (drm_edid.c: header, checksum, drm_mode_detailed) — the mode is exactly the window's size for every width,
 * also those not a multiple of 8 (1366: the X230 and the IdeaPad), the sync pulses are not zero (else the kernel drops
 * the mode), the blanking holds the sync, the pixel clock is never 0; a size the timing cannot hold — refused. */
#include <string.h>

#include "edid.h"
#include "k1_test_host.h"

struct tryb {
    unsigned zegar, w, h, htotal, hsync_start, hsync_end, vtotal, vsync_start, vsync_end;
};

static int czytaj(const uint8_t *e, struct tryb *t)
{
    static const uint8_t nagl[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    const uint8_t *d = e + 54;
    uint8_t suma = 0;
    unsigned i;
    for (i = 0; i < EDID_DL; i++)
        suma = (uint8_t)(suma + e[i]);
    if (memcmp(e, nagl, 8) != 0 || suma != 0 || e[18] != 1 || e[19] != 4 || e[126] != 0)
        return -1;
    t->zegar = (unsigned)(d[0] | d[1] << 8) * 10u;
    t->w = (unsigned)((d[4] & 0xf0) << 4 | d[2]);
    t->h = (unsigned)((d[7] & 0xf0) << 4 | d[5]);
    t->htotal = t->w + (unsigned)((d[4] & 0xf) << 8 | d[3]);
    t->vtotal = t->h + (unsigned)((d[7] & 0xf) << 8 | d[6]);
    t->hsync_start = t->w + (unsigned)((d[11] & 0xc0) << 2 | d[8]);
    t->hsync_end = t->hsync_start + (unsigned)((d[11] & 0x30) << 4 | d[9]);
    t->vsync_start = t->h + (unsigned)((d[11] & 0xc) << 2 | d[10] >> 4);
    t->vsync_end = t->vsync_start + (unsigned)((d[11] & 0x3) << 4 | (d[10] & 0xf));
    if (t->hsync_end == t->hsync_start || t->vsync_end == t->vsync_start || (d[17] & 0x80) || !(d[17] & 0x18))
        return -1; /* drm_mode_detailed: a zero pulse, stereo, composite sync */
    return 0;
}

static void t_rozmiary(void)
{
    static const uint32_t r[][2] = { { 1366, 728 }, { 1366, 768 }, { 1360, 696 }, { 1920, 1080 }, { 1920, 968 },
                                     { 1918, 1001 }, { 640, 480 }, { 64, 64 }, { 4095, 4095 }, { 2560, 1600 } };
    uint8_t e[EDID_DL];
    struct tryb t;
    unsigned i;
    for (i = 0; i < sizeof r / sizeof r[0]; i++) {
        memset(e, 0xaa, sizeof e);
        EXPECT(edid_zbuduj(e, r[i][0], r[i][1]) == 0);
        EXPECT(czytaj(e, &t) == 0);
        EXPECT(t.w == r[i][0] && t.h == r[i][1]);
        EXPECT(t.hsync_end <= t.htotal && t.vsync_end <= t.vtotal && t.zegar > 0);
        EXPECT(e[24] & 0x02);                         /* preferred timing: the first detailed one */
        EXPECT(e[72 + 3] == 0xfc && memcmp(e + 72 + 5, "K1 strefa\n", 10) == 0);
        EXPECT(e[90 + 3] == 0x10 && e[108 + 3] == 0x10);
    }
    EXPECT(edid_zbuduj(e, 1366, 768) == 0 && czytaj(e, &t) == 0);
    EXPECT(t.zegar >= 73000 && t.zegar <= 73100); /* kHz: (1366 + 160) × (768 + 30) × 60 Hz */
    EXPECT(edid_zbuduj(e, 4095, 4095) == 0 && czytaj(e, &t) == 0 && t.zegar == 655350); /* the largest that fits */
}

static void t_poza(void)
{
    uint8_t e[EDID_DL];
    EXPECT(edid_zbuduj(e, 4096, 768) == -1);
    EXPECT(edid_zbuduj(e, 1366, 4096) == -1);
    EXPECT(edid_zbuduj(e, 63, 768) == -1);
    EXPECT(edid_zbuduj(e, 1366, 0) == -1);
}

int main(void)
{
    t_rozmiary();
    t_poza();
    return K1_TEST_KONIEC("test_edid");
}

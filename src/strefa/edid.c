/* K1 strefa — the EDID of the zone's display (edid.h; VESA E-EDID release A rev. 2, EDID 1.4). */
#include <string.h>

#include "edid.h"

/* The blanking of the timing: any the zone's kernel accepts (a display that does not exist; only the active part
 * matters). The pixel clock follows 60 Hz while it fits in 16 bits of 10 kHz, else the largest that fits. */
#define H_WYG 160u
#define H_PRZED 48u
#define H_SYNC 32u
#define V_WYG 30u
#define V_PRZED 3u
#define V_SYNC 5u
#define ODSWIEZANIE 60u
#define PX_NA_CM 38u /* about 96 dpi: the size in mm / cm only for the record */

static void opis_nazwa(uint8_t *d)
{
    static const char n[] = "K1 strefa";
    memset(d, 0, 18);
    d[3] = 0xfc; /* display product name */
    memset(d + 5, ' ', 13);
    memcpy(d + 5, n, sizeof n - 1);
    d[5 + sizeof n - 1] = '\n';
}

static void opis_pusty(uint8_t *d)
{
    memset(d, 0, 18);
    d[3] = 0x10; /* dummy descriptor */
}

static void taktowanie(uint8_t *d, uint32_t w, uint32_t h)
{
    uint64_t zegar = (uint64_t)(w + H_WYG) * (h + V_WYG) * ODSWIEZANIE / 10000u;
    uint32_t mm_w = w * 10u / PX_NA_CM, mm_h = h * 10u / PX_NA_CM;
    if (zegar > 0xffffu)
        zegar = 0xffffu;
    d[0] = (uint8_t)zegar;
    d[1] = (uint8_t)(zegar >> 8);
    d[2] = (uint8_t)w;
    d[3] = (uint8_t)H_WYG;
    d[4] = (uint8_t)(((w >> 8) & 0xfu) << 4 | ((H_WYG >> 8) & 0xfu));
    d[5] = (uint8_t)h;
    d[6] = (uint8_t)V_WYG;
    d[7] = (uint8_t)(((h >> 8) & 0xfu) << 4 | ((V_WYG >> 8) & 0xfu));
    d[8] = (uint8_t)H_PRZED;
    d[9] = (uint8_t)H_SYNC;
    d[10] = (uint8_t)((V_PRZED & 0xfu) << 4 | (V_SYNC & 0xfu));
    d[11] = 0; /* the high bits of the four above: all fit */
    d[12] = (uint8_t)mm_w;
    d[13] = (uint8_t)mm_h;
    d[14] = (uint8_t)(((mm_w >> 8) & 0xfu) << 4 | ((mm_h >> 8) & 0xfu));
    d[15] = d[16] = 0;
    d[17] = 0x1e; /* digital separate sync, both positive, not interlaced, not stereo */
}

int edid_zbuduj(uint8_t e[EDID_DL], uint32_t w, uint32_t h)
{
    static const uint8_t naglowek[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    uint8_t suma = 0;
    unsigned i;
    if (w < EDID_BOK_MIN || w > EDID_BOK_MAKS || h < EDID_BOK_MIN || h > EDID_BOK_MAKS)
        return -1;
    memset(e, 0, EDID_DL);
    memcpy(e, naglowek, sizeof naglowek);
    e[8] = 0x2d, e[9] = 0xee; /* manufacturer "KON": 5 bits a letter (A = 1), big-endian — none of the kernel's quirks */
    e[10] = 1;                /* product code 1 */
    e[17] = 2026 - 1990;      /* year */
    e[18] = 1, e[19] = 4;     /* EDID 1.4 */
    e[20] = 0x80;             /* digital input */
    e[21] = (uint8_t)(w / PX_NA_CM); /* cm: at least 1 (64 px), at most 107 */
    e[22] = (uint8_t)(h / PX_NA_CM);
    e[23] = 120;  /* gamma 2.2 */
    e[24] = 0x02; /* the first detailed timing is the preferred (native) mode */
    memset(e + 38, 0x01, 16); /* no standard timings */
    taktowanie(e + 54, w, h);
    opis_nazwa(e + 72);
    opis_pusty(e + 90);
    opis_pusty(e + 108);
    for (i = 0; i < EDID_DL - 1; i++)
        suma = (uint8_t)(suma + e[i]);
    e[EDID_DL - 1] = (uint8_t)(0x100u - suma);
    return 0;
}

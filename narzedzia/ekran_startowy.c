/* The boot screen of K1 (0.5.8): the K1 mark and "Kons K1" on black (like the firmware's screen before
 * it; GRUB's text cursor, black too, stays invisible), written as a PNG for GRUB (background_image, stretched to the screen mode it set). The kernel keeps
 * the picture (gfxpayload=keep, simpledrm) until the screen process draws its first frame, so it
 * is what the owner sees from the firmware logo to the desktop — no text.
 *     ekran_startowy > start.png
 * Drawn on the build host with the same code as K1 (k1_rysuj_znak, Plex fonts). The image is
 * 1320×780: between 16:10 (1280×800) and 16:9 (1366×768), so the mark is stretched by at most ~5 %
 * either way. The same input gives the same bytes (zlib, fixed settings). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "k1_grafika.h"
#include "k1_rysuj.h"

#define SZER 1320u
#define WYS 780u
#define TLO 0x000000u
#define NAPIS 0xeeebe5u /* cream text of the dark theme */

static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int fragment(const char *typ, const unsigned char *dane, uint32_t dl)
{
    unsigned char n[8];
    uLong crc = crc32(0L, (const Bytef *)typ, 4);
    if (dl > 0)
        crc = crc32(crc, dane, dl);
    be32(n, dl);
    memcpy(n + 4, typ, 4);
    if (fwrite(n, 1, 8, stdout) != 8 || (dl > 0 && fwrite(dane, 1, dl, stdout) != dl))
        return -1;
    be32(n, (uint32_t)crc);
    return fwrite(n, 1, 4, stdout) == 4 ? 0 : -1;
}

int main(void)
{
    static const unsigned char sygnatura[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    const size_t wiersz = 1 + (size_t)SZER * 3, surowe_dl = wiersz * WYS;
    unsigned char ihdr[13];
    unsigned char *surowe, *spak;
    uLongf spak_dl;
    struct k1_obraz o;
    const char *napis = "Kons K1";
    size_t x, y;

    o.szer = SZER;
    o.wys = WYS;
    o.krok = SZER;
    o.px = malloc((size_t)SZER * WYS * sizeof *o.px);
    surowe = malloc(surowe_dl);
    spak_dl = compressBound((uLong)surowe_dl);
    spak = malloc(spak_dl);
    if (o.px == NULL || surowe == NULL || spak == NULL)
        return 1;
    k1_wyczysc(&o, k1_kolor(TLO, 255));
    k1_rysuj_znak(&o, SZER / 2.0f, WYS / 2.0f - 28.0f, 88.0f);
    k1_tekst(&o, K1_CZ_TYTUL, (int32_t)(SZER / 2) - k1_tekst_szer(K1_CZ_TYTUL, napis) / 2, (int32_t)(WYS / 2) + 56, napis,
             k1_kolor(NAPIS, 255), 0);

    for (y = 0; y < WYS; y++) {
        unsigned char *w = surowe + y * wiersz;
        w[0] = 0; /* filter: none */
        for (x = 0; x < SZER; x++) {
            uint32_t p = o.px[y * SZER + x]; /* opaque: premultiplied = plain */
            w[1 + x * 3] = (unsigned char)(p >> 16);
            w[2 + x * 3] = (unsigned char)(p >> 8);
            w[3 + x * 3] = (unsigned char)p;
        }
    }
    if (compress2(spak, &spak_dl, surowe, (uLong)surowe_dl, 9) != Z_OK)
        return 1;
    be32(ihdr, SZER);
    be32(ihdr + 4, WYS);
    ihdr[8] = 8;  /* bits per channel */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = 0; /* deflate */
    ihdr[11] = 0; /* adaptive filtering */
    ihdr[12] = 0; /* no interlace */
    if (fwrite(sygnatura, 1, 8, stdout) != 8 || fragment("IHDR", ihdr, 13) != 0 ||
        fragment("IDAT", spak, (uint32_t)spak_dl) != 0 || fragment("IEND", NULL, 0) != 0 || fflush(stdout) != 0)
        return 1;
    free(o.px);
    free(surowe);
    free(spak);
    return 0;
}

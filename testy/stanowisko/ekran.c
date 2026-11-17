/* K1 — the host stand (scripts/stanowisko.sh, 0.10.1 step 5): the zone compositor's display in memory instead of
 * drm.c. With $K1_ZRZUT set, the next drawing after a file "$K1_ZRZUT.prosba" appears writes "$K1_ZRZUT.ppm". */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "kompozytor.h"

#define SZER 1024 /* the zone's window in the QEMU tests (test-strefa: 1024 wide) */
#define WYS 700

struct wyswietlacz ekran;

int ekran_start(void)
{
    ekran = (struct wyswietlacz){ calloc((size_t)SZER * WYS, 4), SZER, WYS, SZER };
    return ekran.px != NULL ? 0 : -1;
}

static void zrzut(void)
{
    char prosba[512], plik[512];
    const char *z = getenv("K1_ZRZUT");
    FILE *f;
    int i;
    if (z == NULL)
        return;
    snprintf(prosba, sizeof prosba, "%s.prosba", z);
    if (access(prosba, F_OK) != 0)
        return;
    unlink(prosba);
    snprintf(plik, sizeof plik, "%s.ppm", z);
    f = fopen(plik, "wb");
    if (f == NULL)
        return;
    fprintf(f, "P6\n%d %d\n255\n", ekran.szer, ekran.wys);
    for (i = 0; i < ekran.szer * ekran.wys; i++) {
        uint32_t c = ekran.px[i];
        unsigned char b[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}

void ekran_pokaz(int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)x, (void)y, (void)w, (void)h;
    zrzut();
}

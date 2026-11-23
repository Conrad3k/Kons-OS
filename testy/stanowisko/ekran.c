/* K1 — the host stand (scripts/stanowisko.sh, 0.10.1 step 5): the zone compositor's display in memory instead of
 * drm.c. With $K1_ZRZUT set, the next drawing after a file "$K1_ZRZUT.prosba" appears writes "$K1_ZRZUT.ppm". */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "kompozytor.h"

#define SZER 1024 /* the zone's window in the QEMU tests (test-strefa: 1024 wide); K1_EKRAN="WxH" another (0.11.4) */
#define WYS 700

struct wyswietlacz ekran;

int ekran_start(void)
{
    int w = SZER, h = WYS;
    const char *e = getenv("K1_EKRAN");
    if (e != NULL && (sscanf(e, "%dx%d", &w, &h) != 2 || w < 320 || h < 240 || w > 4096 || h > 4096))
        w = SZER, h = WYS;
    ekran = (struct wyswietlacz){ calloc((size_t)w * (size_t)h, 4), w, h, w };
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

/* 0.10.5 step 4: K1 resizes the zone's window — never on the host (a fixed display) */
int ekran_zmiany_fd(void)
{
    return -1;
}

int ekran_zmiana(void)
{
    return 0;
}

/* 0.10.11 step 2: no buffers of the card on the host (Firefox draws with the processor there) */
int ekran_pokaz_bufor(int dmabuf, int32_t w, int32_t h, int32_t krok, int32_t przes)
{
    (void)dmabuf, (void)w, (void)h, (void)krok, (void)przes;
    return -1;
}

int ekran_pokaz_swoj(void)
{
    return 0;
}

void ekran_zapomnij(int dmabuf)
{
    (void)dmabuf;
}

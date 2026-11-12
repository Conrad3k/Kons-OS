/* K1 ekran — the wallpaper photo (0.8.0 step 4, ADR 0032).
 *
 * Only Pliki may give one (TAPETA_ZDJECIE): pixels decoded by a dekoder (at most K1_DEK_MAKS_W × K1_DEK_MAKS_H) in
 * a sealed memfd of exactly that size — we never see the file itself. We keep the photo, draw it to fill the screen
 * (centred, cut at the sides or the top and bottom, k1_skaluj) at every size, and save it in skarbiec as our own
 * objects: K1_OB_ZDJECIE "K1Z1", width, height (u32 each), then the pixels as 3 bytes (R, G, B) each in objects
 * K1_OB_ZDJECIE_KAWALEK + i of 1 MiB — the size is written last, so a photo cut off by a power loss is never read.
 * Everything read back is checked: the sizes against the limits and the length of every piece. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_dekoder.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_skarb_klient.h"
#include "k1_skaluj.h"

#define MAGIA "K1Z1"
#define KAWALEK ((size_t)K1_SKARB_MAKS)
_Static_assert((size_t)K1_DEK_MAKS_W * K1_DEK_MAKS_H * 3 <= (size_t)K1_ZDJECIE_KAWALKI * KAWALEK,
               "zdjecie: the largest photo does not fit in its pieces");

static uint32_t *zdj; /* premultiplied 0xAARRGGBB, zw per row (alpha is ignored: drawn over black) */
static uint32_t zw, zh;
static uint8_t kawalek[KAWALEK];

static size_t kawalkow(uint32_t w, uint32_t h)
{
    return ((size_t)w * h * 3 + KAWALEK - 1) / KAWALEK;
}

/* Piece `k` of the photo as RGB bytes into `kawalek`: its length. */
static size_t do_kawalka(size_t k)
{
    size_t od = k * KAWALEK, dl = (size_t)zw * zh * 3 - od;
    if (dl > KAWALEK)
        dl = KAWALEK;
    for (size_t i = 0; i < dl; i++) {
        size_t b = od + i;
        kawalek[i] = (uint8_t)(zdj[b / 3] >> (16 - 8 * (b % 3)));
    }
    return dl;
}

static void z_kawalka(uint32_t *px, size_t k, size_t dl)
{
    for (size_t i = 0; i < dl; i++) {
        size_t b = k * KAWALEK + i;
        px[b / 3] |= (uint32_t)kawalek[i] << (16 - 8 * (b % 3));
    }
}

static void zapisz(void)
{
    int fd = ustawienia_fd();
    uint8_t nagl[12];
    uint32_t wh[2] = { zw, zh };
    size_t n = kawalkow(zw, zh);
    if (fd < 0)
        return;
    for (size_t k = 0; k < n; k++)
        if (k1_skarb_zapisz(fd, (int)(K1_OB_ZDJECIE_KAWALEK + k), kawalek, do_kawalka(k)) != 0)
            k1_log(TAG, "zdjęcie: zapis kawałka %zu nieudany", k);
    for (size_t k = n; k < K1_ZDJECIE_KAWALKI; k++) /* pieces of a larger photo before */
        k1_skarb_usun(fd, (int)(K1_OB_ZDJECIE_KAWALEK + k));
    memcpy(nagl, MAGIA, 4);
    memcpy(nagl + 4, wh, sizeof wh);
    if (k1_skarb_zapisz(fd, K1_OB_ZDJECIE, nagl, sizeof nagl) != 0)
        k1_log(TAG, "zdjęcie: zapis nieudany");
}

/* At start, after the settings: the saved photo, if there is a whole one. */
void zdjecie_wczytaj(void)
{
    uint8_t nagl[12];
    uint32_t wh[2], *px;
    int fd = ustawienia_fd(), tryb;
    if (ustawienia_czeka()) /* skarbiec still opens the vault: the photo comes with the late settings (zapis.c) */
        return;
    if (fd >= 0 && k1_skarb_czytaj(fd, K1_OB_ZDJECIE, nagl, sizeof nagl, 1500, &tryb) == (int)sizeof nagl &&
        memcmp(nagl, MAGIA, 4) == 0) {
        memcpy(wh, nagl + 4, sizeof wh);
        if (wh[0] >= 1 && wh[0] <= K1_DEK_MAKS_W && wh[1] >= 1 && wh[1] <= K1_DEK_MAKS_H &&
            (px = calloc((size_t)wh[0] * wh[1], 4)) != NULL) {
            size_t n = kawalkow(wh[0], wh[1]), k, cale = (size_t)wh[0] * wh[1] * 3;
            for (k = 0; k < n; k++) {
                size_t dl = cale - k * KAWALEK > KAWALEK ? KAWALEK : cale - k * KAWALEK;
                if (k1_skarb_czytaj(fd, (int)(K1_OB_ZDJECIE_KAWALEK + k), kawalek, KAWALEK, 1500, &tryb) != (int)dl)
                    break;
                z_kawalka(px, k, dl);
            }
            if (k == n) {
                for (size_t i = 0; i < (size_t)wh[0] * wh[1]; i++)
                    px[i] |= 0xff000000u;
                zdj = px;
                zw = wh[0];
                zh = wh[1];
            } else {
                free(px);
                k1_log(TAG, "zdjęcie na tapetę: niepełne w skarbcu — pominięte");
            }
        }
    }
    s.ust[K1_UST_ZDJECIE] = zdj != NULL;
    if (s.ust[K1_UST_TAPETA] == K1_TAPETA_ZDJECIE && zdj == NULL)
        s.ust[K1_UST_TAPETA] = 0;
    if (zdj != NULL)
        k1_log(TAG, "zdjęcie na tapetę wczytane ze skarbca (%ux%u)", zw, zh);
}

/* The wallpaper `wariant` on s.tlo: the photo, or a painted variant. */
void rysuj_tlo(int wariant)
{
    if (wariant == K1_TAPETA_ZDJECIE && zdj != NULL &&
        k1_skaluj(zdj, zw, zh, zw, k1_skaluj_wypelnij(zw, zh, s.tlo.szer, s.tlo.wys), s.tlo.px, s.tlo.szer, s.tlo.wys,
                  s.tlo.krok) == 0)
        return;
    k1_rysuj_tlo(&s.tlo, wariant == K1_TAPETA_ZDJECIE ? 0 : wariant);
}

/* The applications that may set a photo as the wallpaper (the user's gesture there: "Ustaw jako tapetę"); a new one:
 * one line. The name is for the log. */
static const struct {
    int apl;
    const char *od;
} nadawcy[] = {
    { K1_APL_PLIKI, "Plików" },     
    { K1_APL_PODGLAD, "Podglądu" }, /* 0.8.2 step 4, ADR 0037 */
};

/* TAPETA_ZDJECIE from client `kl` (k1_komunikat checked the sizes; fd is closed here): NULL when taken, else why not. */
const char *zdjecie_przyjmij(int kl, const struct k1_komunikat *m, int fd)
{
    size_t n = (size_t)m->a * (size_t)m->b, i = 0;
    uint32_t *px = NULL;
    const char *powod = NULL;
    while (i < sizeof nadawcy / sizeof nadawcy[0] && (s.kl[kl].rola != K1_ROLA_APLIKACJA || s.kl[kl].apl != nadawcy[i].apl))
        i++;
    if (i == sizeof nadawcy / sizeof nadawcy[0])
        powod = "tapeta ze zdjęcia tylko od Plików i Podglądu";
    else if (fd < 0 || (px = malloc(n * 4)) == NULL || k1_memfd_czytaj(fd, px, n * 4) != 0)
        powod = "zdjęcie: deskryptor nie jest zapieczętowanym memfd o podanym rozmiarze";
    if (fd >= 0)
        close(fd);
    if (powod != NULL) {
        free(px);
        return powod;
    }
    for (size_t j = 0; j < n; j++)
        px[j] |= 0xff000000u; /* drawn over black: premultiplied colours stay as they are */
    free(zdj);
    zdj = px;
    zw = (uint32_t)m->a;
    zh = (uint32_t)m->b;
    k1_log(TAG, "zdjęcie na tapetę od %s: %ux%u", nadawcy[i].od, zw, zh);
    zapisz();
    ustawienie(K1_UST_ZDJECIE, 1);
    s.ust[K1_UST_TAPETA] = -1; /* drawn again even when the photo was the wallpaper already */
    ustawienie(K1_UST_TAPETA, K1_TAPETA_ZDJECIE);
    return NULL;
}

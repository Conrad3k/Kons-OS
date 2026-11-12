/* K1 — files of the user: names, types, the entry layout (0.8.0, ADR 0029). See k1_dokument.h. */
#include "k1_dokument.h"

#include <stdlib.h>
#include <string.h>

#include "k1_bajty.h"
#include "k1_uslugi.h"

/* Text: UTF-8 without control characters other than tab, line breaks and form feed. A character cut by the
 * end of the sample (when there is more content) does not count against it. */
static int tekst(const uint8_t *p, size_t n, uint64_t calosc)
{
    size_t i = 0;
    while (i < n) {
        uint32_t k;
        size_t d = k1_dok_utf8(p + i, n - i, &k);
        if (d == 0)
            return calosc > n && n - i < 4 && (p[i] & 0xc0) == 0xc0;
        if ((k < 0x20 && k != '\t' && k != '\n' && k != '\r' && k != '\f') || k == 0x7f)
            return 0;
        i += d;
    }
    return 1;
}

/* BMP: "BM" is too short to tell by, so also the size of the header that follows (one of the versions Windows and
 * OS/2 wrote: 12, 40, 52, 56, 64, 108, 124 bytes). */
static int bmp(const uint8_t *p, size_t n)
{
    static const uint8_t naglowki[] = { 12, 40, 52, 56, 64, 108, 124 };
    if (n < 18 || p[0] != 'B' || p[1] != 'M' || p[15] != 0 || p[16] != 0 || p[17] != 0)
        return 0;
    return memchr(naglowki, p[14], sizeof naglowki) != NULL;
}

int k1_dok_typ(const uint8_t *p, size_t n, uint64_t calosc)
{
    static const uint8_t png[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (calosc == 0 || n == 0)
        return K1_TYP_TEKST; /* a new, empty text file (as "Nowy dokument tekstowy" in Windows) */
    if (n >= 8 && memcmp(p, png, 8) == 0)
        return K1_TYP_PNG;
    if (n >= 3 && p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff)
        return K1_TYP_JPEG;
    if (n >= 6 && (memcmp(p, "GIF87a", 6) == 0 || memcmp(p, "GIF89a", 6) == 0))
        return K1_TYP_GIF;
    if (n >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0)
        return K1_TYP_WEBP;
    if (bmp(p, n))
        return K1_TYP_BMP;
    if (n >= 5 && memcmp(p, "%PDF-", 5) == 0)
        return K1_TYP_PDF;
    if (n >= 4 && memcmp(p, "PK\x03\x04", 4) == 0)
        return K1_TYP_ZIP;
    return tekst(p, n, calosc) ? K1_TYP_TEKST : K1_TYP_INNY;
}

void k1_dok_wpis_zapisz(const struct k1_dok_wpis *w, uint8_t b[K1_DOK_WPIS_BAJTY])
{
    memset(b, 0, K1_DOK_WPIS_BAJTY);
    k1_z32(b, w->id);
    k1_z32(b + 4, w->rodzic);
    b[8] = w->rodzaj;
    b[9] = w->typ;
    b[10] = w->pochodzenie;
    b[11] = w->tworca;
    k1_z32(b + 12, w->generacja);
    k1_z64(b + 16, w->rozmiar);
    k1_z64(b + 24, (uint64_t)w->utworzono);
    k1_z64(b + 32, (uint64_t)w->zmieniono);
    k1_z64(b + 40, (uint64_t)w->kosz);
    memcpy(b + 48, w->nazwa, strnlen(w->nazwa, K1_DOK_NAZWA - 1));
}

int k1_dok_wpis_czytaj(const uint8_t b[K1_DOK_WPIS_BAJTY], struct k1_dok_wpis *w)
{
    size_t n = strnlen((const char *)b + 48, K1_DOK_NAZWA), i;
    memset(w, 0, sizeof *w);
    w->id = k1_c32(b);
    w->rodzic = k1_c32(b + 4);
    w->rodzaj = b[8];
    w->typ = b[9];
    w->pochodzenie = b[10];
    w->tworca = b[11];
    w->generacja = k1_c32(b + 12);
    w->rozmiar = k1_c64(b + 16);
    w->utworzono = (int64_t)k1_c64(b + 24);
    w->zmieniono = (int64_t)k1_c64(b + 32);
    w->kosz = (int64_t)k1_c64(b + 40);
    for (i = n; i < K1_DOK_NAZWA; i++)
        if (b[48 + i] != 0)
            n = K1_DOK_NAZWA; /* bytes after the NUL: refused below */
    if (w->id < K1_DOK_PIERWSZY || w->id > K1_DOK_ID_MAKS || w->rodzic < 1 || w->rodzic > K1_DOK_ID_MAKS ||
        w->rodzic == w->id || (w->rodzic < K1_DOK_PIERWSZY && w->rodzic > K1_FOLDERY_MAKS) ||
        (w->rodzaj != K1_DOK_PLIK && w->rodzaj != K1_DOK_FOLDER) || w->pochodzenie < 1 || w->pochodzenie > K1_POCH_MAKS ||
        w->tworca < 1 || w->tworca > K1_PROFIL_MAKS || w->rozmiar > K1_DOK_MAKS_BAJTY || w->utworzono < 0 ||
        w->zmieniono < 0 || w->kosz < 0 || n >= K1_DOK_NAZWA || !k1_dok_nazwa_poprawna((const char *)b + 48, n) ||
        (w->rodzaj == K1_DOK_PLIK ? w->typ < 1 || w->typ > K1_TYPY_MAKS
                                  : w->typ != 0 || w->rozmiar != 0 || w->generacja != 0)) {
        memset(w, 0, sizeof *w);
        return -1;
    }
    memcpy(w->nazwa, b + 48, n);
    return 0;
}

size_t k1_dok_lista_zapisz(const struct k1_dok_wpis *w, int n, uint32_t w_koszu, uint8_t *buf)
{
    int i;
    memcpy(buf, "K1D1", 4);
    k1_z32(buf + 4, (uint32_t)n);
    k1_z32(buf + 8, w_koszu);
    k1_z32(buf + 12, 0);
    for (i = 0; i < n; i++)
        k1_dok_wpis_zapisz(&w[i], buf + K1_DOK_LISTA_NAGL + (size_t)i * K1_DOK_WPIS_BAJTY);
    return K1_DOK_LISTA_NAGL + (size_t)n * K1_DOK_WPIS_BAJTY;
}

static int mniejszy(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

int k1_dok_lista_czytaj(const uint8_t *buf, size_t dl, struct k1_dok_wpis *w, int maks, uint32_t *w_koszu)
{
    static uint32_t numery[K1_DOK_MAKS];
    uint32_t n;
    int i;
    if (dl < K1_DOK_LISTA_NAGL || memcmp(buf, "K1D1", 4) != 0 || k1_c32(buf + 12) != 0)
        return -1;
    n = k1_c32(buf + 4);
    if (n > K1_DOK_MAKS || (int)n > maks || dl != K1_DOK_LISTA_NAGL + (size_t)n * K1_DOK_WPIS_BAJTY ||
        k1_c32(buf + 8) > K1_DOK_MAKS)
        return -1;
    for (i = 0; i < (int)n; i++) {
        if (k1_dok_wpis_czytaj(buf + K1_DOK_LISTA_NAGL + (size_t)i * K1_DOK_WPIS_BAJTY, &w[i]) != 0)
            return -1;
        numery[i] = w[i].id;
    }
    qsort(numery, n, sizeof numery[0], mniejszy); /* numbers unique: sorted, no two neighbours equal (16 384 entries) */
    for (i = 1; i < (int)n; i++)
        if (numery[i] == numery[i - 1])
            return -1;
    *w_koszu = k1_c32(buf + 8);
    return (int)n;
}

const char *k1_folder_nazwa(uint32_t nr)
{
#define X_NAZWA(n, nr_, nazwa, ...) case nr_: return nazwa;
    switch (nr) {
        K1_FOLDERY(X_NAZWA)
    default:
        return NULL;
    }
#undef X_NAZWA
}

int k1_typ_rodzina(int typ)
{
#define X_RODZ(n, nr_, nazwa, ik, rodzina) case nr_: return rodzina;
    switch (typ) {
        K1_TYPY(X_RODZ)
    default:
        return 0;
    }
#undef X_RODZ
}

const char *k1_typ_nazwa(int typ)
{
#define X_NAZWA(n, nr_, nazwa, ...) case nr_: return nazwa;
    switch (typ) {
        K1_TYPY(X_NAZWA)
    default:
        return "Plik";
    }
#undef X_NAZWA
}

const char *k1_poch_nazwa(int poch)
{
#define X_NAZWA(n, nr_, nazwa) case nr_: return nazwa;
    switch (poch) {
        K1_POCHODZENIA(X_NAZWA)
    default:
        return "";
    }
#undef X_NAZWA
}

#define X_PARA(apl_, typ_) { K1_APL_##apl_, K1_TYP_##typ_ },
static const struct {
    int apl, typ;
} otwieranie[] = { K1_OTWIERANIE(X_PARA) };
#undef X_PARA

int k1_dok_otwiera(int apl, int typ)
{
    if (typ < 1 || typ > K1_TYPY_MAKS)
        return 0;
    if (apl == K1_APL_PLIKI)
        return 1;
    for (size_t i = 0; i < sizeof otwieranie / sizeof otwieranie[0]; i++)
        if (otwieranie[i].apl == apl && otwieranie[i].typ == typ)
            return 1;
    return 0;
}

int k1_dok_domyslna(int typ)
{
    for (size_t i = 0; i < sizeof otwieranie / sizeof otwieranie[0]; i++)
        if (otwieranie[i].typ == typ)
            return otwieranie[i].apl;
    return K1_APL_PLIKI;
}

int k1_dok_aplikacje(int typ, int *apl)
{
    int n = 0;
    for (size_t i = 0; i < sizeof otwieranie / sizeof otwieranie[0]; i++)
        if (otwieranie[i].typ == typ)
            apl[n++] = otwieranie[i].apl;
    if (typ >= 1 && typ <= K1_TYPY_MAKS)
        apl[n++] = K1_APL_PLIKI;
    return n;
}

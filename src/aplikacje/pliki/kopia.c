/* K1 Pliki — a copy of files (0.11.0 step 4, the owner's D6: A: dragged with Ctrl — in Pliki, onto the desktop and
 * from it). One file after another, piece by piece, as an import from the stick (pendrive.c): DOK_CZYTAJ piece i →
 * DOK_TRESC → DOK_NOWY (the name and the first piece; "more follow") or DOK_KAWALEK (the same memfd passed on) →
 * DOK_WYNIK → the next piece …, DOK_KONIEC. skarbiec gives the copy a free name in the target ("nazwa (2).pdf" — as
 * Windows does); the copy keeps the original's origin (a file from the browser stays one). Folders are never copied (the
 * table of dragging says so before). One copy at a time, never during an import; the footer says how it went. Part of
 * Pliki (pliki.h). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_przeciaganie.h"
#include "pliki.h"

static struct {
    int trwa, i, n, zrobione, bledy, pierwszy_blad, zapis;
    uint32_t folder, kawalek, kawalkow;
    uint32_t id[K1_DOK_MAKS];
    char cel[K1_DOK_NAZWA];
} k;
static uint8_t bufor[K1_DOK_NAZWA + K1_DOK_KAWALEK];
static char komunikat[320];

int kopia_trwa(void)
{
    return k.trwa;
}

static void koniec(void)
{
    const char *pl = k1_odmiana(k.zrobione, "plik", "pliki", "plików");
    size_t d;
    k.trwa = 0;
    snprintf(komunikat, sizeof komunikat, "Skopiowano %d %s do „%s”.", k.zrobione, pl, k.cel);
    if (k.bledy > 0 && k.zrobione == 0)
        snprintf(komunikat, sizeof komunikat, "%s", k1_prz_powod(k.pierwszy_blad));
    else if (k.bledy > 0 && (d = strlen(komunikat)) < sizeof komunikat - 1)
        snprintf(komunikat + d, sizeof komunikat - d, " Nie udało się: %d. %s", k.bledy, k1_prz_powod(k.pierwszy_blad));
    blad = komunikat;
    k1_log(TAG, "kopia: koniec — skopiowane %d, błędy %d", k.zrobione, k.bledy);
    k1_apl_odswiez(&apl);
}

/* The next file (skips what is not a file any more), or the end. */
static void nastepny(void)
{
    for (; k.i < k.n; k.i++) {
        const struct k1_dok_wpis *w = wpis(k.id[k.i]);
        if (w == NULL || w->rodzaj != K1_DOK_PLIK || k1_dok_w_koszu(w)) {
            k.bledy++, k.pierwszy_blad = k.pierwszy_blad != 0 ? k.pierwszy_blad : K1_DOK_BLAD_BRAK;
            continue;
        }
        k.kawalek = 0, k.zapis = 0;
        k.kawalkow = (uint32_t)((w->rozmiar + K1_DOK_KAWALEK - 1) / K1_DOK_KAWALEK);
        k.kawalkow = k.kawalkow == 0 ? 1 : k.kawalkow;
        do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, w->id, (int32_t)K1_DOK_KAWALEK, 0, 0, 0, 0), -1);
        return;
    }
    koniec();
}

void kopia_start(const uint32_t *id, int n, uint32_t folder)
{
    const struct k1_dok_wpis *f = folder > K1_FOLDERY_MAKS ? wpis(folder) : NULL;
    if (k.trwa || importuje || n < 1)
        return;
    memset(&k, 0, sizeof k);
    k.n = n < K1_DOK_MAKS ? n : K1_DOK_MAKS;
    k.folder = folder;
    memcpy(k.id, id, (size_t)k.n * sizeof id[0]);
    snprintf(k.cel, sizeof k.cel, "%s", folder <= K1_FOLDERY_MAKS ? k1_folder_nazwa(folder) : f != NULL ? f->nazwa : "");
    k.trwa = 1;
    k1_log(TAG, "kopia: start — %d %s", k.n, k1_odmiana(k.n, "plik", "pliki", "plików"));
    nastepny();
}

/* A file that failed: its write in progress abandoned, the next one. */
static void nieudany(int wynik)
{
    if (k.bledy++ == 0)
        k.pierwszy_blad = wynik;
    if (k.zapis)
        do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 1, 0, 0, 0, 0), -1);
    k.zapis = 0;
    k.i++;
    if (wynik == K1_DOK_BLAD_BRAK_MIEJSCA || wynik == K1_SKARB_PELNY || wynik == K1_SKARB_ZLE_HASLO) {
        k.bledy += k.n - k.i; /* the rest would fail too */
        koniec();
    } else {
        nastepny();
    }
}

int kopia_tresc(const struct k1_komunikat *m, int fd)
{
    const struct k1_dok_wpis *w;
    size_t nl;
    int nowy;
    if (!k.trwa || k.i >= k.n || m->obiekt != k.id[k.i] || (uint32_t)m->b != k.kawalek || (w = wpis(m->obiekt)) == NULL)
        return 0;
    if (k.kawalek > 0) { /* the piece as it came: the same memfd */
        do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_KAWALEK, 0, m->a, 0, 0, 0, 0), fd);
        return 1;
    }
    nl = strlen(w->nazwa); /* the name, then the first piece: one memfd */
    memcpy(bufor, w->nazwa, nl);
    if (k1_memfd_czytaj(fd, bufor + nl, (size_t)m->a) != 0 || (nowy = k1_memfd_z_danymi("k1-kopia", bufor, nl + (size_t)m->a)) < 0) {
        nieudany(K1_DOK_BLAD_BRAK);
        return 1;
    }
    do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_NOWY, k.folder, K1_DOK_PLIK, (int32_t)nl, m->a, w->pochodzenie,
                                      k.kawalkow > 1),
                    nowy);
    close(nowy);
    k.zapis = k.kawalkow > 1;
    return 1;
}

int kopia_wynik(const struct k1_komunikat *m)
{
    if (!k.trwa || (m->a != K1_KOM_DOK_NOWY && m->a != K1_KOM_DOK_KAWALEK && m->a != K1_KOM_DOK_KONIEC &&
                    !(m->a == K1_KOM_DOK_CZYTAJ && m->obiekt == k.id[k.i])))
        return 0;
    if (m->b != 0) {
        k1_log(TAG, "kopia: skarbiec odmówił (komunikat %d, wynik %d)", m->a, m->b);
        nieudany(m->b);
        return 1;
    }
    if (m->a == K1_KOM_DOK_KONIEC || (m->a == K1_KOM_DOK_NOWY && k.kawalkow == 1)) {
        k.zrobione++, k.zapis = 0, k.i++;
        nastepny();
        return 1;
    }
    if (m->a == K1_KOM_DOK_CZYTAJ)
        return 1;
    if (++k.kawalek < k.kawalkow)
        do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, k.id[k.i], (int32_t)K1_DOK_KAWALEK, (int32_t)k.kawalek, 0, 0, 0), -1);
    else
        do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1);
    return 1;
}

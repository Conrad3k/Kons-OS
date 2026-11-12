/* K1 Pliki — one action on many entries (0.8.2 step 6): moving, into Kosz, back from Kosz, deleting for good. skarbiec
 * takes one entry per request (DOK_PRZENIES, DOK_KOSZ, DOK_PRZYWROC, DOK_USUN): they go one after another, the next
 * after the answer to the one before, and the footer says at the end how it went — as after an import. Part of Pliki
 * (pliki.h). */
#include <stdio.h>
#include <string.h>

#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_przeciaganie.h"
#include "pliki.h"

static struct {
    int typ, i, n, b, zrobione, bledy, pierwszy_blad, trwa;
    uint32_t folder;
    uint32_t id[K1_DOK_MAKS];
    char cel[K1_DOK_NAZWA];
} z;
static char komunikat[320];

int zbiorczo_trwa(void)
{
    return z.trwa;
}

static void wyslij(void)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)z.typ, z.id[z.i],
                                              z.typ == K1_KOM_DOK_PRZENIES || z.typ == K1_KOM_DOK_PRZYWROC ? (int32_t)z.folder : 0,
                                              z.typ == K1_KOM_DOK_PRZENIES ? z.b : 0, 0, 0, 0);
    do_skarbca_z_fd(m, -1);
}

/* "Przeniesiono 12 elementów do „Obrazy”." — and what failed, in the words of the table of dragging. */
static void koniec(void)
{
    const char *el = k1_odmiana(z.zrobione, "element", "elementy", "elementów");
    size_t d;
    z.trwa = 0;
    komunikat[0] = '\0';
    if (z.typ == K1_KOM_DOK_PRZENIES)
        snprintf(komunikat, sizeof komunikat, "Przeniesiono %d %s do „%s”.", z.zrobione, el, z.cel);
    else if (z.typ == K1_KOM_DOK_PRZYWROC && z.folder != 0)
        snprintf(komunikat, sizeof komunikat, "Przywrócono %d %s do „%s”.", z.zrobione, el, z.cel);
    else if (z.typ == K1_KOM_DOK_PRZYWROC)
        snprintf(komunikat, sizeof komunikat, "Przywrócono %d %s.", z.zrobione, el);
    else if (z.n > 1 && z.typ == K1_KOM_DOK_KOSZ)
        snprintf(komunikat, sizeof komunikat, "Przeniesiono do Kosza: %d.", z.zrobione);
    else if (z.n > 1)
        snprintf(komunikat, sizeof komunikat, "Usunięto na zawsze: %d.", z.zrobione);
    if (z.bledy > 0 && (z.n == 1 || z.zrobione == 0))
        snprintf(komunikat, sizeof komunikat, "%s", k1_prz_powod(z.pierwszy_blad)); /* only why */
    else if (z.bledy > 0 && (d = strlen(komunikat)) < sizeof komunikat - 1)
        snprintf(komunikat + d, sizeof komunikat - d, " Nie udało się: %d — %s", z.bledy, k1_prz_powod(z.pierwszy_blad));
    blad = komunikat[0] != '\0' ? komunikat : NULL;
    k1_log(TAG, "zbiorczo: koniec (komunikat %d) — zrobione %d, błędy %d", z.typ, z.zrobione, z.bledy);
}

void zbiorczo(int typ, const uint32_t *id, int n, uint32_t folder, int b)
{
    const struct k1_dok_wpis *f = folder > K1_FOLDERY_MAKS ? wpis(folder) : NULL;
    if (z.trwa || n < 1)
        return;
    memset(&z, 0, sizeof z);
    z.typ = typ;
    z.n = n < K1_DOK_MAKS ? n : K1_DOK_MAKS;
    z.folder = folder;
    z.b = b;
    memcpy(z.id, id, (size_t)z.n * sizeof id[0]);
    snprintf(z.cel, sizeof z.cel, "%s", folder >= 1 && folder <= K1_FOLDERY_MAKS ? k1_folder_nazwa(folder) : f != NULL ? f->nazwa : "");
    z.trwa = 1;
    k1_log(TAG, "zbiorczo: start (komunikat %d), %d %s", typ, z.n, k1_odmiana(z.n, "element", "elementy", "elementów"));
    wyslij();
}

int zbiorczo_wynik(const struct k1_komunikat *k)
{
    if (!z.trwa || k->a != z.typ || k->obiekt != z.id[z.i])
        return 0;
    if (k->b == 0) {
        z.zrobione++;
    } else {
        if (z.bledy++ == 0)
            z.pierwszy_blad = k->b;
        k1_log(TAG, "zbiorczo: skarbiec odmówił (komunikat %d, wynik %d)", z.typ, k->b);
    }
    if (++z.i < z.n && k->b != K1_DOK_BLAD_BRAK_MIEJSCA && k->b != K1_SKARB_PELNY && k->b != K1_SKARB_ZLE_HASLO)
        wyslij();
    else {
        z.bledy += z.n - z.i; /* no room, or the vault locked: the rest would fail too */
        koniec();
    }
    return 1;
}

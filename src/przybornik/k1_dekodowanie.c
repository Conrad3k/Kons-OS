/* K1 przybornik — a picture through a dekoder (k1_dekodowanie.h). Moved here from Pliki (obrazy.c, 0.8.0 step 4 and
 * 0.8.2 step 3) when Podgląd needed the same (0.8.2 step 4). */
#include "k1_dekodowanie.h"

#include <fcntl.h>
#include <unistd.h>

#include "k1_dekoder.h"
#include "k1_dokument.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"

enum { E_NIC = 0, E_KANAL, E_TRESC, E_WYNIK }; /* waiting for: the channel to dekoder, a piece, its answer */

const struct k1_dek_praca *k1_dek_robi(const struct k1_dekodowanie *d)
{
    return d->etap != E_NIC ? &d->robi : NULL;
}

const struct k1_dek_praca *k1_dek_czeka(const struct k1_dekodowanie *d)
{
    return d->chce.id != 0 ? &d->chce : NULL;
}

static void zacznij(struct k1_dekodowanie *d)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0);
    if (d->etap != E_NIC || d->chce.id == 0 || d->czytany_id != 0) /* a piece of the last one still to come: after it */
        return;
    d->robi = d->chce;
    d->chce.id = 0;
    if (d->aktualna != NULL && !d->aktualna(&d->robi))
        return;
    d->etap = E_KANAL;
    if (k1_kanal_wyslij(d->nadzorca, &m, sizeof m, -1) != 0)
        d->etap = E_NIC;
}

void k1_dek_zadaj(struct k1_dekodowanie *d, const struct k1_dek_praca *p)
{
    d->chce = *p;
    zacznij(d);
}

static void skoncz(struct k1_dekodowanie *d)
{
    if (d->fd >= 0)
        close(d->fd);
    d->fd = -1;
    d->apl->kanaly[d->kanal].fd = -1;
    d->etap = E_NIC;
    zacznij(d);
}

static void czytaj_kawalek(struct k1_dekodowanie *d)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, d->robi.id, (int32_t)K1_DOK_KAWALEK,
                                              (int32_t)d->kawalek, 0, 0, 0);
    d->czytany_id = d->robi.id;
    d->czytany_kawalek = d->kawalek;
    if (d->skarbiec >= 0)
        k1_kanal_wyslij(d->skarbiec, &m, sizeof m, -1);
}

void k1_dek_kanal(struct k1_dekodowanie *d, int fd)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DEK_OBRAZ, d->robi.id, (int32_t)d->robi.dlugosc, d->robi.mw,
                                              d->robi.mh, 0, 0);
    if (d->etap != E_KANAL || fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        close(fd);
        return;
    }
    d->fd = fd;
    d->apl->kanaly[d->kanal].fd = fd;
    if (k1_kanal_wyslij(fd, &m, sizeof m, -1) != 0) {
        k1_log(d->apl->tag, "obraz: wysłanie do dekodera nieudane");
        skoncz(d);
        return;
    }
    d->etap = E_TRESC;
    d->kawalek = 0;
    czytaj_kawalek(d);
}

int k1_dek_tresc(struct k1_dekodowanie *d, const struct k1_komunikat *k, int fd)
{
    struct k1_komunikat m;
    uint64_t od = (uint64_t)d->kawalek * K1_DOK_KAWALEK, dl = d->robi.dlugosc;
    uint64_t ile = dl - od < K1_DOK_KAWALEK ? dl - od : K1_DOK_KAWALEK;
    if (d->czytany_id == 0 || k->obiekt != d->czytany_id || (uint32_t)k->b != d->czytany_kawalek)
        return 0;
    d->czytany_id = 0;
    if (d->etap != E_TRESC || k->obiekt != d->robi.id) { /* the picture is no longer wanted: the next one may start */
        zacznij(d);
        return 1;
    }
    m = k1_komunikat_nowy(K1_KOM_DEK_KAWALEK, d->robi.id, (int32_t)d->kawalek, k->a, 0, 0, 0);
    if (fd < 0 || (uint64_t)k->a != ile || k1_kom_u64(k->c, k->d) != dl ||
        k1_kanal_wyslij(d->fd, &m, sizeof m, fd) != 0) { /* the file changed meanwhile, or dekoder is gone */
        skoncz(d);
        return 1;
    }
    d->kawalek++;
    if ((uint64_t)d->kawalek * K1_DOK_KAWALEK < dl)
        czytaj_kawalek(d);
    else
        d->etap = E_WYNIK;
    return 1;
}

int k1_dek_wynik_skarbca(struct k1_dekodowanie *d, const struct k1_komunikat *k)
{
    if (k->a != K1_KOM_DOK_CZYTAJ || d->czytany_id == 0 || k->obiekt != d->czytany_id)
        return 0;
    d->czytany_id = 0;
    k1_log(d->apl->tag, "obraz: skarbiec nie dał kawałka %u (%d)", d->kawalek, k->b);
    if (d->etap == E_TRESC && k->obiekt == d->robi.id)
        skoncz(d); /* dekoder ends without the rest; it says so (DEK_BLAD) or just ends */
    else
        zacznij(d);
    return 1;
}

void k1_dek_odbierz(struct k1_dekodowanie *d)
{
    struct k1_komunikat k;
    struct k1_dek_wynik w = { K1_DEK_BLAD_USZKODZONY, 0, 0, 0, 0, 0 }; /* it ended without an answer (its time ran out) */
    int fd = -1;
    enum k1_odbior_wynik r = k1_odbierz_komunikat(d->fd, K1_UID_DEKODER, K1_KIER_OD_DEKODERA, &k, &fd);
    if (r == K1_ODB_PUSTO)
        return;
    if (r == K1_ODB_JEST && k.obiekt == d->robi.id && k.typ == K1_KOM_DEK_PIKSELE && d->etap == E_WYNIK)
        w = (struct k1_dek_wynik){ 0, 0, k.a, k.b, k.c, k.d };
    else if (r == K1_ODB_JEST && k.obiekt == d->robi.id && k.typ == K1_KOM_DEK_BLAD)
        w = (struct k1_dek_wynik){ k.a, k.b, 0, 0, 0, 0 };
    if (w.blad != 0 && fd >= 0) {
        close(fd);
        fd = -1;
    }
    d->gotowe(&d->robi, &w, fd);
    if (fd >= 0)
        close(fd);
    skoncz(d); /* one answer per dekoder */
    k1_apl_odswiez(d->apl);
}

/* K1 — the checking of k1_komunikat, split by channel (0.9.0 step 2): what the parts share. Not for programs: they use
 * k1_komunikat_sprawdz() (k1_komunikat.h). Each k1_kom_sprawdz_<group> answers 1 for a valid message of its group, 0 for
 * an invalid one and for a type of another group; every type belongs to exactly one group (k1_komunikat.c). */
#ifndef K1_KOM_WEW_H
#define K1_KOM_WEW_H

#include "k1_dokument.h"
#include "k1_komunikat.h"

static inline int w(int32_t v, int32_t lo, int32_t hi)
{
    return v >= lo && v <= hi;
}

/* Parameters from index `od` on must all be zero (a=0, b=1, ...). */
static inline int zera(const struct k1_komunikat *k, int od)
{
    const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
    int i;
    for (i = od; i < 5; i++)
        if (p[i] != 0)
            return 0;
    return 1;
}

/* A folder or a file of the user; only a file or folder the user made (not the fixed folders). */
static inline int wpis(uint32_t obiekt)
{
    return (obiekt >= 1 && obiekt <= K1_FOLDERY_MAKS) || (obiekt >= K1_DOK_PIERWSZY && obiekt <= K1_DOK_ID_MAKS);
}

static inline int wpis_uzytkownika(uint32_t obiekt)
{
    return obiekt >= K1_DOK_PIERWSZY && obiekt <= K1_DOK_ID_MAKS;
}

/* A size in two fields (k1_kom_u64) of a file that has at least `od` bytes: 1 when in range. */
static inline int rozmiar_pliku(int32_t niskie, int32_t wysokie, uint64_t od)
{
    uint64_t r = k1_kom_u64(niskie, wysokie);
    return r <= K1_DOK_MAKS_BAJTY && r >= od;
}

int k1_kom_sprawdz_ekran(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);      /* k1_kom_ekran.c */
int k1_kom_sprawdz_skarb(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);      /* k1_kom_skarb.c */
int k1_kom_sprawdz_dok(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);        /* k1_kom_skarb.c */
int k1_kom_sprawdz_sterowanie(const struct k1_komunikat *k, enum k1_kanal_kierunek kier); /* k1_kom_nadzorca.c */
int k1_kom_sprawdz_nadzorca(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);   /* k1_kom_nadzorca.c */
int k1_kom_sprawdz_pendrive(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);   /* k1_kom_pliki.c */
int k1_kom_sprawdz_dekoder(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);    /* k1_kom_pliki.c */
int k1_kom_sprawdz_siec(const struct k1_komunikat *k, enum k1_kanal_kierunek kier);       /* k1_kom_siec.c */

#endif

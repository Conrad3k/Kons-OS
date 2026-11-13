/* K1 skarbiec — the room for files (0.8.1 step 3, ADR 0033): how much there is and how much is taken (SKARB_MIEJSCE to
 * Ustawienia → Informacje and to Pliki), and K1-DANE to the end of its USB stick when the user asks in Ustawienia: the
 * partition table belongs to nadzorca, so skarbiec asks it (POWIEKSZ_DANE) and then takes the grown partition
 * (k1_mag_powieksz: the data stays where it is). Only numbers reach the log. Part of skarbiec (skarbiec.h). */
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "skarbiec.h"

#define ZAPAS_MIN 16 /* MiB: less after K1-DANE is not worth a new partition table (nadzorca: powieksz.c) */

static int zapas;     /* MiB the stick has after K1-DANE (nadzorca, SKARB_KLIENT d) */
static int czeka;     /* nadzorca is growing K1-DANE */

void miejsce_zapas(int mib)
{
    zapas = mib;
}

static int32_t mib(uint64_t bajty)
{
    uint64_t m = bajty >> 20;
    return m > K1_PAMIEC_MAKS_MIB ? K1_PAMIEC_MAKS_MIB : (int32_t)m;
}

struct k1_komunikat miejsce_komunikat(void)
{
    uint64_t calosc = ob_pojemnosc(), wolne = ob_wolne(), rezerwa = dok_rezerwa();
    uint64_t na_pliki = calosc > rezerwa ? calosc - rezerwa : 0, zajete = calosc - wolne;
    if (zablokowany)
        return k1_komunikat_nowy(K1_KOM_SKARB_MIEJSCE, 1, 0, 0, 0, 0, 0);
    if (zajete > na_pliki) /* the system took some of its reserve */
        zajete = na_pliki;
    return k1_komunikat_nowy(K1_KOM_SKARB_MIEJSCE, 1, mib(zajete), mib(na_pliki), dok_ile(), trwaly && !czeka ? zapas : 0,
                             !trwaly); /* in memory: Pliki say so when it is full (0.8.2 step 2) */
}

void miejsce_wyslij(int fd)
{
    struct k1_komunikat m = miejsce_komunikat();
    k1_kanal_wyslij(fd, &m, sizeof m, -1);
}

/* Ustawienia → Miejsce (0.8.5 step 3): one CZESC per part, numbers only — to Ustawienia alone, with the vault open;
 * anyone else gets nothing (K1_MAG_ARG for its WYNIK). */
int miejsce_podzial(int fd, int wlasciciel)
{
    uint64_t bajty[K1_CZESCI_MAKS + 1];
    uint32_t pliki[K1_CZESCI_MAKS + 1];
    if (wlasciciel != K1_PROFIL_USTAWIENIA || zablokowany)
        return K1_MAG_ARG;
    dok_podzial(bajty, pliki);
    for (int c = 1; c <= K1_CZESCI_MAKS; c++) {
        uint64_t kib = (bajty[c] + 1023) >> 10;
        struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_CZESC, (uint32_t)c, kib > INT32_MAX ? INT32_MAX : (int32_t)kib,
                                                  (int32_t)pliki[c], 0, 0, 0);
        k1_kanal_wyslij(fd, &m, sizeof m, -1);
    }
    return K1_MAG_OK;
}

int miejsce_powieksz(void)
{
    if (!trwaly || czeka || zapas < ZAPAS_MIN)
        return K1_MAG_ARG;
    czeka = 1;
    k1_log(TAG, "powiększenie K1-DANE: prośba z Ustawień (pendrive ma jeszcze %d MiB) — pytam nadzorcę", zapas);
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_POWIEKSZ_DANE, 0, 0, 0, 0, 0, 0));
    return K1_MAG_OK;
}

int miejsce_powiekszono(int wynik)
{
    off_t bajty;
    if (!czeka)
        return K1_MAG_ARG;
    czeka = 0;
    if (wynik != 0) {
        k1_log(TAG, "powiększenie K1-DANE: nadzorca nie powiększył partycji (%s) — magazyn bez zmian", k1_mag_opis(wynik));
        return wynik;
    }
    zapas = 0;
    bajty = lseek(dysk.fd, 0, SEEK_END); /* the kernel has the new size of the partition (BLKPG) */
    if (bajty <= 0)
        return K1_MAG_IO;
    return ob_powieksz((uint64_t)bajty, "na prośbę");
}

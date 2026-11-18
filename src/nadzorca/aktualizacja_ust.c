/* K1 nadzorca — "Sprawdzaj sam" (0.10.5 step 2b, D4) kept in skarbiec: owner K1_WL_SYSTEM, object K1_OB_AKTUALIZACJE,
 * 8 bytes: "K1S1" and 1 (on) or 0 (off), little endian. Until it is read nadzorca does not check by itself (a K1 the
 * owner told not to must not look while it starts); the vault in memory (no K1-DANE) or nothing saved: on (the
 * default). A malformed object: on. Part of nadzorca (aktualizacja.c asks; see main.c). */
#include "nadzorca.h"

#include "k1_bajty.h"

#define K1_OB_AKTUALIZACJE 5 /* nadzorca (K1_WL_SYSTEM): "Sprawdzaj sam" — beside K1_OB_STREFY (sesje.h) */

static int kanal = -1, wczytane, czyta;

static void rozlacz(void)
{
    if (kanal >= 0)
        close(kanal);
    kanal = -1;
    czyta = 0;
}

int akt_ustawienie_wczytane(void)
{
    return wczytane || stan_skarbca != K1_SKARB_TRWALY;
}

void akt_zapisz_ustawienie(int wlaczone)
{
    uint8_t b[8];
    memcpy(b, "K1S1", 4);
    k1_z32(b + 4, wlaczone ? 1u : 0u);
    if (kanal < 0 || k1_skarb_zapisz(kanal, K1_OB_AKTUALIZACJE, b, sizeof b) != 0)
        k1_log(TAG, "aktualizacja: „Sprawdzaj sam” — zapis w skarbcu niemożliwy teraz");
}

static void odbierz(void)
{
    for (int i = 0; i < 8 && kanal >= 0; i++) {
        struct k1_komunikat k;
        uint8_t b[8];
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(kanal, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            rozlacz();
            return;
        }
        if (w == K1_ODB_JEST && k.obiekt == K1_OB_AKTUALIZACJE && czyta && k.typ == K1_KOM_SKARB_DANE) {
            int dobre = k.a == 8 && k1_memfd_czytaj(fd, b, 8) == 0 && memcmp(b, "K1S1", 4) == 0 && k1_c32(b + 4) <= 1;
            czyta = 0;
            wczytane = 1;
            akt_ustawienie(dobre ? (int)k1_c32(b + 4) : 1);
        } else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_CZYTAJ && czyta) {
            czyta = 0;
            wczytane = 1; /* nothing saved: on */
        }
        if (fd >= 0)
            close(fd);
    }
}

void akt_ustawienie_okresowe(void)
{
    if (stan_skarbca != K1_SKARB_TRWALY || skarbiec()->pid <= 0) {
        if (kanal >= 0)
            rozlacz();
    } else if (kanal < 0) {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_CZYTAJ, K1_OB_AKTUALIZACJE, 0, 0, 0, 0, 0);
        kanal = kanal_do_skarbca(K1_WL_SYSTEM, 0);
        if (kanal < 0 || fcntl(kanal, F_SETFL, O_NONBLOCK) != 0 || k1_kanal_wyslij(kanal, &k, sizeof k, -1) != 0)
            rozlacz();
        else
            czyta = !wczytane;
    }
    odbierz();
}

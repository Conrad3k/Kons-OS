/* K1 — host test of what the windows of the writing program may ask nadzorca for (src/nadzorca/inst_prawa.c; 0.10.5
 * step 1, T1). NEGATIVE: Ustawienia never installs (wipes a disk), never turns on Secure Boot, never powers off and
 * restarts only after its finished update (0.13.0 step 4: a report is no such job — instalacja.c); Instalator never
 * updates nor writes the report; nobody else is a window; unknown types refused. */
#include "k1_test_host.h"
#include "inst_prawa.h"

static int moze(int okno, uint8_t typ, int32_t a, int sukces, int sb)
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, typ == K1_KOM_INST_START && a != K1_INST_RAPORT ? 1 : 0, a, 0, 0, 0, 0);
    return inst_dozwolone(okno, &k, sukces, sb);
}

int main(void)
{
    /* both: the list of disks */
    EXPECT(moze(OKNO_INSTALATOR, K1_KOM_INST_DYSKI, 0, 0, 0) && moze(OKNO_USTAWIENIA, K1_KOM_INST_DYSKI, 0, 0, 0));
    /* each its own job only */
    EXPECT(moze(OKNO_INSTALATOR, K1_KOM_INST_START, K1_INST_INSTALUJ, 0, 0));
    EXPECT(!moze(OKNO_INSTALATOR, K1_KOM_INST_START, K1_INST_AKTUALIZUJ, 0, 0));
    EXPECT(moze(OKNO_USTAWIENIA, K1_KOM_INST_START, K1_INST_AKTUALIZUJ, 0, 0));
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_INST_START, K1_INST_INSTALUJ, 0, 0)); /* NEGATIVE: never wipes a disk */
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_INST_START, K1_INST_INSTALUJ, 1, 1));
    /* 0.13.0 step 4: the report onto a USB stick — Ustawienia → Informacje only (the rescue mode asks by OCHRONA) */
    EXPECT(moze(OKNO_USTAWIENIA, K1_KOM_INST_START, K1_INST_RAPORT, 0, 0));
    EXPECT(!moze(OKNO_INSTALATOR, K1_KOM_INST_START, K1_INST_RAPORT, 0, 0));
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_INST_START, K1_INST_SIEC_ZAINSTALUJ, 0, 0)); /* NEGATIVE: nadzorca's own jobs */
    /* restart: after a finished job; Instalator also for MokManager */
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_WYLACZ, 1, 0, 0));
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_WYLACZ, 1, 0, 1)); /* the K1 key is Instalator's */
    EXPECT(moze(OKNO_USTAWIENIA, K1_KOM_WYLACZ, 1, 1, 0));
    EXPECT(moze(OKNO_INSTALATOR, K1_KOM_WYLACZ, 1, 0, 1) && !moze(OKNO_INSTALATOR, K1_KOM_WYLACZ, 1, 0, 0));
    /* power off: Instalator after an install only; log out never */
    EXPECT(moze(OKNO_INSTALATOR, K1_KOM_WYLACZ, 0, 1, 0) && !moze(OKNO_INSTALATOR, K1_KOM_WYLACZ, 0, 0, 1));
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_WYLACZ, 0, 1, 0));
    EXPECT(!moze(OKNO_INSTALATOR, K1_KOM_WYLACZ, 2, 1, 1) && !moze(OKNO_USTAWIENIA, K1_KOM_WYLACZ, 2, 1, 1));
    /* Secure Boot and the link: Instalator only */
    EXPECT(moze(OKNO_INSTALATOR, K1_KOM_SB_KLUCZ, 0, 0, 0) && !moze(OKNO_USTAWIENIA, K1_KOM_SB_KLUCZ, 0, 0, 0));
    EXPECT(moze(OKNO_INSTALATOR, K1_KOM_OTWORZ_AKTUALIZACJE, 0, 0, 0));
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_OTWORZ_AKTUALIZACJE, 0, 0, 0));
    /* 0.10.5 step 2b: updates through the network — Ustawienia only; NEGATIVE: Instalator */
    EXPECT(moze(OKNO_USTAWIENIA, K1_KOM_AKT_PROSBA, K1_AKT_P_ZAINSTALUJ, 0, 0));
    EXPECT(moze(OKNO_USTAWIENIA, K1_KOM_AKT_PROSBA, K1_AKT_P_WROC, 0, 0));
    EXPECT(!moze(OKNO_INSTALATOR, K1_KOM_AKT_PROSBA, K1_AKT_P_ZAINSTALUJ, 0, 0));
    EXPECT(!moze(OKNO_INSTALATOR, K1_KOM_AKT_PROSBA, K1_AKT_P_SPRAWDZ, 0, 0));
    /* anything else, and no other window */
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_URUCHOM, 1, 1, 1) && !moze(OKNO_INSTALATOR, K1_KOM_INST_ZADANIE, 1, 1, 1));
    EXPECT(!moze(OKNO_USTAWIENIA, K1_KOM_INST_GOTOWE, 0, 1, 1));
    EXPECT(!moze(OKIEN, K1_KOM_INST_DYSKI, 0, 1, 1) && !moze(-1, K1_KOM_INST_DYSKI, 0, 1, 1));
    return K1_TEST_KONIEC("test_inst_prawa");
}

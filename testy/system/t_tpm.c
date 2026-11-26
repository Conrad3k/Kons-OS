/* K1 — in-system test: the TPM (0.13.0 step 2, ADR 0057).
 * t_tpm (test-m1, no TPM in QEMU): nadzorca refuses OCHRONA TEST (no TPM 2.0 to write to), and an OCHRONA TEST with
 * generation 0 is refused by the message's check — a test program cannot lower the generation either.
 * t_tpm_scenariusz: the scenario of scripts/test-tpm.sh (a disk with the serial "k1-test-tpm", or
 * "k1-test-tpm-gen"): this program writes one object to skarbiec (the first write of the session — skarbiec then
 * tells nadzorca the vault's generation for the TPM), with "-generacja" also asks nadzorca to put the next generation in
 * the TPM (as a newer K1 would leave it), and waits; the script reads the log and starts the machine again. */
#include <unistd.h>

#include "k1_skarb_klient.h"
#include "k1_test.h"
#include "k1_tpm.h"

void t_tpm(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_TEST, K1_GENERACJA + 1, 0, 0, 0);
    check(zapytaj(k, NULL, 5) == -1, "tpm: bez TPM 2.0 nadzorca nie zapisuje generacji", "OCHRONA TEST odrzucona");
    k.b = 0;
    check(zapytaj(k, NULL, 5) == -1, "tpm: generacja 0 odrzucona już przy sprawdzaniu komunikatu", "OCHRONA TEST b=0");
}

int t_tpm_scenariusz(int generacja)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_TEST, K1_GENERACJA + 1, 0, 0, 0);
    int fd = -1, i, w;
    for (i = 0; i < 120 && fd < 0; i++) /* skarbiec takes a moment to start */
        if (zapytaj(k1_komunikat_nowy(K1_KOM_TEST_SKARB, 0, K1_PROFIL_NOTATKI, 0, 0, 0, 0), &fd, 2) != 0 || fd < 0) {
            fd = -1;
            msleep(500);
        }
    w = fd >= 0 ? k1_skarb_zapisz(fd, 1, "tpm", 3) : -1;
    k1_log(TAG, "TPM: obiekt do skarbca %s", w == 0 ? "wysłany" : "NIE wysłany");
    if (generacja) {
        w = zapytaj(k, NULL, 10);
        k1_log(TAG, "TPM: generacja %d %s", K1_GENERACJA + 1, w == 0 ? "zapisana" : "NIE zapisana");
    }
    for (;;) /* the script powers the machine off */
        pause();
}

/* K1 skarbiec — the vault's generation for the TPM (0.13.0 step 2, ADR 0057). Part of skarbiec (see main.c).
 * nadzorca remembers in the TPM how far the vault has come and so sees a vault rolled back to an older copy. It gets,
 * as SKARB_EPOKA: the vault's id (the first 8 bytes of its uuid) and a generation of its header that is ON the disk
 * (written and synced — k1_magazyn.na_dysku; never one only in memory, or a failed write would look like a roll-back
 * on the next start) — once right after the vault opened, and once after the first write of this opening (the
 * generation then is newer than any copy of the disk made before it). Nothing of it in the log. */
#include <string.h>

#include "k1_bajty.h"
#include "skarbiec.h"

static uint64_t przy_otwarciu;
static int po_zapisie;

static void zglos(int e)
{
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_SKARB_EPOKA, 0, (int32_t)k1_c32(mag.uuid), (int32_t)k1_c32(mag.uuid + 4),
                                  (int32_t)(uint32_t)mag.na_dysku, (int32_t)(uint32_t)(mag.na_dysku >> 32), e));
}

void epoka_otwarta(void)
{
    przy_otwarciu = mag.na_dysku;
    po_zapisie = 0;
    if (trwaly)
        zglos(0);
}

void epoka_sprawdz(void)
{
    if (trwaly && !po_zapisie && mag.na_dysku > przy_otwarciu) {
        po_zapisie = 1;
        zglos(1);
    }
}

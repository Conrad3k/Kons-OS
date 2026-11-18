/* libFuzzer harness for the desktop arrangement read back from skarbiec (src/gui/rozmieszczenie.c, 0.7.5; 0.8.0: Kosz and files;
 * 0.10.5 step 6: keys on the bar, "K1R3"): arbitrary bytes are refused or give a sane arrangement that saves and reads
 * back as the same one. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rozmieszczenie.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct rozmieszczenie r, w;
    uint8_t b[ROZM_BAJTY_MAKS];
    size_t n;
    int i, j;
    if (rozm_czytaj(data, size, &r) != 0)
        return 0;
    if (r.n_pasek > ROZM_PASEK_MAKS || r.n_ikon > ROZM_IKON_MAKS)
        abort();
    for (i = 0; i < r.n_pasek; i++) /* only applications and zones with a Start-menu line, each once */
        for (j = 0; j <= i; j++)
            if ((!rozm_aplikacja(r.pasek[i]) && !rozm_strefa(r.pasek[i])) || (j < i && r.pasek[i] == r.pasek[j]))
                abort();
    for (i = 0; i < r.n_ikon; i++)
        for (j = 0; j < i; j++)
            if (r.ikony[i].klucz == r.ikony[j].klucz ||
                (r.ikony[i].kol == r.ikony[j].kol && r.ikony[i].wiersz == r.ikony[j].wiersz))
                abort();
    n = rozm_zapisz(&r, b, sizeof b);
    /* the old format is written anew in the new one; the new one back byte by byte */
    if ((memcmp(data, ROZM_ZNAK, 4) == 0 && (n != size || memcmp(b, data, n) != 0)) || rozm_czytaj(b, n, &w) != 0 ||
        memcmp(&r, &w, sizeof r) != 0)
        abort();
    rozm_dopasuj(&w, 3, 2); /* the rules keep it sane too */
    rozm_przenies_ikone(&w, data[5] % (K1_APL_MAKS + 2), data[4] % 70, data[4] % 3);
    rozm_uloz(&w, data[4] % 9);
    rozm_przesun_na_pasku(&w, data[5] % (K1_APL_MAKS + 2), data[4] % 9 - 1);
    rozm_przypnij(&w, ROZM_STREFA + data[5] % (K1_STREFY_MAKS + 2));
    rozm_odepnij(&w, data[4] % (K1_APL_MAKS + 2));
    if (w.n_pasek > ROZM_PASEK_MAKS || w.n_ikon > ROZM_IKON_MAKS)
        abort();
    return 0;
}

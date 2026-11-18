/* libFuzzer harness for the release announcement (0.10.5 step 2, ADR 0046 T3): arbitrary bytes from the network —
 * the parser, the choice of what K1 shows, the signature check. Nothing accepted may break a rule of the format. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_ogloszenie.h"
#include "k1_ogloszenie_podpis.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t klucz[32] = { 1, 2, 3 };
    static struct k1_ogloszenie o;
    struct k1_ogl_wybor w;
    uint32_t i;
    if (k1_ogloszenie_czytaj(data, size, &o) == 0) {
        if (o.wersja == 0 || o.n > K1_OGL_OPISOW_MAKS || (size_t)o.dl + 64 != size || o.paczka_bajty > K1_OGL_PACZKA_MAKS)
            abort();
        for (i = 0; i < o.n; i++)
            if (o.opis[i].wersja > o.wersja || strlen(o.opis[i].tekst) != o.opis[i].dl ||
                (o.opis[i].rodzaj == K1_OGL_OPIS && !k1_ogl_tekst_dobry(o.opis[i].tekst, o.opis[i].dl)))
                abort();
        k1_ogl_wybierz(&o, data[0], &w);
        if (w.n < 0 || w.n > (int)o.n)
            abort();
    }
    (void)k1_ogl_tekst_dobry((const char *)data, size);
    (void)k1_ogloszenie_sprawdz_podpis(data, size, klucz);
    return 0;
}

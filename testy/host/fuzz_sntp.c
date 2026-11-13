/* libFuzzer harness for siec's clock from the internet (0.9.0 step 8): arbitrary answers of time servers, one after
 * another, into a round of every source. An answer is either rejected or gives a time within K1_SIEC_CZAS_MIN..MAKS;
 * the result of a round is either "no" or a time that at least two answers, more than half of them, agree on. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "czas.h"
#include "k1_siec.h"
#include "sntp.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint64_t los[K1_SIEC_ZRODLA_MAKS] = { 1, 2, 3, 4, 5, 6 };
    const uint32_t router[2] = { 0x0a000202u, 0x0a000203u };
    struct k1_czas c;
    double t = 1000, czas, przes;
    size_t i = 0;
    int n, j, odp, zg;
    k1_czas_polaczono(&c, t);
    n = k1_czas_start(&c, router, 2, los, t);
    for (j = 0; j < n; j++)
        k1_czas_wyslano(&c, j, t);
    /* the input: answers of [1 byte source][1 byte length × 8][length bytes] */
    while (i + 2 <= size) {
        size_t dl = (size_t)data[i + 1] * 8;
        int zr = data[i] % (K1_SIEC_ZRODLA_MAKS + 1);
        uint8_t *p;
        i += 2;
        if (dl > size - i)
            dl = size - i;
        p = malloc(dl ? dl : 1); /* its own buffer: ASan sees every read past its end */
        if (p == NULL)
            return 0;
        memcpy(p, data + i, dl);
        if (dl >= K1_SNTP_DLUGOSC) { /* our bits, so that answers reach the checks after them */
            memset(p + 24, 0, 7);
            p[31] = (uint8_t)(zr % K1_SIEC_ZRODLA_MAKS + 1);
        }
        if (k1_sntp_czytaj(p, dl, los[zr % K1_SIEC_ZRODLA_MAKS], &czas) == 0 &&
            (czas < (double)K1_SIEC_CZAS_MIN || czas > (double)K1_SIEC_CZAS_MAKS))
            abort();
        k1_czas_odpowiedz(&c, zr, p, dl, t + 0.1);
        free(p);
        i += dl;
    }
    if (k1_czas_wynik(&c, t + K1_CZAS_RUNDA, &przes, &odp, &zg) &&
        (zg < 2 || 2 * zg <= odp || t + przes < (double)K1_SIEC_CZAS_MIN - 10 || t + przes > (double)K1_SIEC_CZAS_MAKS + 10))
        abort();
    return 0;
}

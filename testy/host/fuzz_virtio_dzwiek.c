/* libFuzzer harness: the guest's control requests to a zone's sound card (src/strefa/dzwiek_prosby.c; 0.12.0 step 3).
 * The input is a hostile guest's requests one after another (each: a byte of its length, the answer's room, the bytes):
 * never an answer longer than its room, the stream's sizes always as SET_PARAMS allows, the states only virtio's. */
#include <stdlib.h>
#include <string.h>

#include "dzwiek.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct dzw_strumien s = { 0, 0, 0 };
    size_t i = 0;
    while (i + 2 <= size) {
        uint8_t req[DZW_PROSBA_MAKS + 8], odp[DZW_ODPOWIEDZ_MAKS + 8];
        uint32_t n = data[i] % (DZW_PROSBA_MAKS + 4), max = data[i + 1] % (DZW_ODPOWIEDZ_MAKS + 1), m;
        int zrob;
        i += 2;
        memset(req, 0, sizeof req);
        memcpy(req, data + i, n < size - i ? n : size - i);
        i += n < size - i ? n : size - i;
        m = dzw_prosba(&s, req, n, odp, max, &zrob);
        if (m > max || (m == 0 && max >= 4) || s.stan < DZW_NIC || s.stan > DZW_ZWOLNIONY ||
            (s.stan != DZW_NIC && (s.okres == 0 || s.okres % 4 != 0 || s.okres > DZW_DANE_MAKS ||
                                   s.bufor % s.okres != 0 || s.bufor > DZW_BUFOR_MAKS)) ||
            zrob < DZW_ZROB_NIC || zrob > DZW_ZROB_ZWOLNIJ)
            abort();
        if (n >= 4 && dzw_transfer(req, 4, (uint64_t)n * 997u) > 0x8003u)
            abort();
    }
    return 0;
}

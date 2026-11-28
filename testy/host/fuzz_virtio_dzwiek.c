/* libFuzzer harness: the guest's control requests to a zone's sound card (src/strefa/dzwiek_prosby.c; 0.12.0 step 3).
 * The input is a hostile guest's requests one after another (each: a byte of its length, the answer's room, the bytes):
 * never an answer longer than its room, the stream's sizes always as SET_PARAMS allows (0.12.3: whole frames of its
 * format), the states only virtio's; the samples of any bytes in any format into floats within the buffer (ASan). 0.13.5
 * step 2: with or without the microphone (the first byte) — without it stream 1 never changes; the floats back. */
#include <stdlib.h>
#include <string.h>

#include "dzwiek.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct dzw_strumien st[DZW_STRUMIENIE] = { { 0 }, { 0 } };
    int strumienie = size > 0 && (data[0] & 1) ? 2 : 1; /* 0.13.5 step 2: with the microphone, or not */
    size_t i = 0;
    while (i + 2 <= size) {
        uint8_t req[DZW_PROSBA_MAKS + 8], odp[DZW_ODPOWIEDZ_MAKS + 8];
        uint32_t n = data[i] % (DZW_PROSBA_MAKS + 4), max = data[i + 1] % (DZW_ODPOWIEDZ_MAKS + 1), m;
        int zrob, ktory;
        i += 2;
        memset(req, 0, sizeof req);
        memcpy(req, data + i, n < size - i ? n : size - i);
        i += n < size - i ? n : size - i;
        m = dzw_prosba(st, strumienie, req, n, odp, max, &zrob, &ktory);
        if (m > max || (m == 0 && max >= 4) || zrob < DZW_ZROB_NIC || zrob > DZW_ZROB_ZWOLNIJ || ktory < 0 ||
            ktory >= strumienie || (strumienie == 1 && st[1].stan != DZW_NIC))
            abort();
        for (int k = 0; k < DZW_STRUMIENIE; k++) {
            const struct dzw_strumien *s = &st[k];
            if (s->stan < DZW_NIC || s->stan > DZW_ZWOLNIONY ||
                (s->stan != DZW_NIC && (s->okres == 0 || s->ramka == 0 || s->ramka != dzw_ramka(s->format) ||
                                        s->okres % s->ramka != 0 || s->okres > DZW_DANE_MAKS ||
                                        s->bufor % s->okres != 0 || s->bufor > DZW_BUFOR_MAKS)))
                abort();
        }
        if (n >= 4 && dzw_transfer(req, 4, (uint64_t)n * 997u, st[ktory].ramka, (uint32_t)ktory) > 0x8003u)
            abort();
        {
            float f[2 * 8];
            uint8_t g[8 * 8];
            dzw_na_f32(f, req, (uint32_t)(sizeof req / 8 < 8 ? sizeof req / 8 : 8), req[0]);
            dzw_z_f32(g, f, 8, req[1]); /* 0.13.5 step 2: back into any format, within its bytes */
        }
    }
    return 0;
}

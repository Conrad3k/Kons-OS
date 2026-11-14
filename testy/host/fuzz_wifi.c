/* libFuzzer harness for siec's Wi-Fi (0.9.1 step 1): arbitrary bytes as one access point's description from the air
 * and as a whole scan result. A description is either rejected or gives a name that is safe to show and a known
 * security; a result is either rejected or a list in which every network passes the checks of the messages. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_wifi.h"
#include "wifi.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_wifi_opis o;
    static struct k1_wifi_wynik w;
    uint8_t *p;
    int i;
    if (size < 2)
        return 0;
    p = malloc(size); /* its own buffer: ASan sees every read past its end */
    if (p == NULL)
        return 0;
    memcpy(p, data, size);
    if (k1_wifi_opis_czytaj(p + 2, size - 2, (uint16_t)(p[0] | p[1] << 8), &o) == 0 &&
        ((o.dl != 0 && !k1_wifi_nazwa_dobra(o.nazwa, o.dl)) || o.zabezpieczenie < K1_WIFI_OTWARTA ||
         o.zabezpieczenie > K1_WIFI_ZABEZP_MAKS))
        abort();
    if (k1_wifi_wyniki_czytaj(p + 1, size - 1, p[0] % (K1_WIFI_REKORDY_MAKS + 2), &w) == 0) {
        if (w.n < 0 || w.n > K1_WIFI_LISTA_MAKS)
            abort();
        for (i = 0; i < w.n; i++)
            if (!k1_wifi_pola_dobre(w.sieci[i].poziom, w.sieci[i].zabezpieczenie, w.sieci[i].dbm, w.sieci[i].mhz,
                                    w.sieci[i].dl) || !k1_wifi_nazwa_dobra(w.sieci[i].nazwa, w.sieci[i].dl) ||
                (i > 0 && w.sieci[i].dbm > w.sieci[i - 1].dbm))
                abort();
    }
    free(p);
    return 0;
}

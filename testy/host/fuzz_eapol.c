/* libFuzzer harness for logging in to Wi-Fi (0.9.1 step 2): arbitrary bytes as an EAPOL frame from the router — in the
 * state "waiting for message 1" and after a real message 1 (waiting for message 3, keys being computed) — and as
 * unwrapped key data. A frame is either refused with a known reason or answered with a frame we can read back. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "eapol.h"
#include "wpa.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t aa[6] = { 2, 0, 0, 0, 1, 0 }, spa[6] = { 2, 0, 0, 0, 0, 0 }, rsn[] = { 48, 2, 1, 0 };
    static struct k1_wpa w;
    uint8_t pmk[32], los[32], m1[128], *p, g[32], kck[16] = { 0 };
    const uint8_t *r = NULL;
    size_t dl_r = 0, dl_g = 0;
    struct k1_eapol e;
    int wynik, id = 0;
    if (size < 1)
        return 0;
    p = malloc(size); /* its own buffer: ASan sees every read past its end */
    if (p == NULL)
        return 0;
    memcpy(p, data, size);
    memset(pmk, 0x42, sizeof pmk);
    memset(los, 0x24, sizeof los);
    k1_wpa_start(&w, pmk, aa, spa, rsn, sizeof rsn, p[0] & 1 ? K1_WIFI_SZYFR_TKIP : K1_WIFI_SZYFR_CCMP);
    if (p[0] & 2) { /* a real message 1 first */
        size_t n = k1_eapol_buduj(m1, sizeof m1, 0x008a, 1, los, NULL, 0, kck);
        m1[7] = 0;
        m1[8] = 16;
        if (k1_wpa_odebrano(&w, m1, n, los) != K1_WPA_A_WYSLIJ)
            abort();
    }
    wynik = k1_wpa_odebrano(&w, p + 1, size - 1, los);
    if (wynik < K1_EAPOL_POWOD_MIN || wynik > 15 || (wynik < 0 && w.dl_ramki != 0))
        abort();
    if (wynik > 0 && (wynik & K1_WPA_A_WYSLIJ) && (w.dl_ramki == 0 || w.dl_ramki > sizeof w.ramka ||
                                       k1_eapol_czytaj(w.ramka, w.dl_ramki, &e) != 0))
        abort();
    if (wynik > 0 && (wynik & K1_WPA_A_KLUCZ_GRUPY) && (w.id_gtk < 1 || w.id_gtk > 3 || (w.dl_gtk != 16 && w.dl_gtk != 32)))
        abort();
    if (k1_eapol_kde(p + 1, size - 1, &r, &dl_r, &id, g, &dl_g) == 0 &&
        (r < p + 1 || r + dl_r > p + size || id < 1 || id > 3 || (dl_g != 16 && dl_g != 32)))
        abort();
    k1_wpa_stop(&w);
    free(p);
    return 0;
}

/* K1 kompozytor — the measurement of the picture's way in the zone (0.10.11 step 1, plan T1, kompozytor.h): how long
 * the compositor waits for the card (VIRTGPU_WAIT of a buffer of the card), copies a wl_shm buffer, draws the scene and
 * sends it to the zone's kernel (DIRTYFB: virtio-gpu to the VMM); how many frames and how many buffers of the card or of
 * memory (does the browser draw with the card — plan 1 b). Every POMIAR_MS, when something was drawn: one line to gosc
 * on fd 3, "pomiar" and 12 numbers (µs, counts; the last since 0.10.11 step 2: frames shown straight from the browser's
 * buffer) — gosc passes it to the VMM, which logs it (vsock.c, pomiar.c there).
 * Numbers only, never anything of the browser's. */
#include <stdio.h>

#include "kompozytor.h"

#define POMIAR_MS 10000u
#define LICZBA_MAKS (1u << 24) /* the VMM takes numbers up to this (linia.c) */

struct k1_pomiar pomiar_czekanie, pomiar_kopia, pomiar_scena, pomiar_pokaz;
unsigned long pomiar_klatki, pomiar_bufory_karty, pomiar_bufory_pamieci, pomiar_bezposrednie;

static uint32_t ogranicz(uint64_t v)
{
    return v > LICZBA_MAKS ? LICZBA_MAKS : (uint32_t)v;
}

void pomiar_zglos(void)
{
    static uint32_t od;
    static int byl;
    uint32_t t = teraz_ms();
    char l[160];
    int n;
    if (!byl) {
        od = t, byl = 1;
        return;
    }
    if (t - od < POMIAR_MS)
        return;
    od = t;
    if (pomiar_klatki == 0)
        return;
    n = snprintf(l, sizeof l, "pomiar %u %u %u %u %u %u %u %u %u %u %u %u\n", ogranicz(pomiar_klatki),
                 ogranicz(pomiar_bufory_karty), ogranicz(pomiar_bufory_pamieci),
                 ogranicz(k1_pomiar_srednia(&pomiar_czekanie)), ogranicz(k1_pomiar_p95(&pomiar_czekanie)),
                 ogranicz(k1_pomiar_srednia(&pomiar_kopia)), ogranicz(k1_pomiar_p95(&pomiar_kopia)),
                 ogranicz(k1_pomiar_srednia(&pomiar_scena)), ogranicz(k1_pomiar_p95(&pomiar_scena)),
                 ogranicz(k1_pomiar_srednia(&pomiar_pokaz)), ogranicz(k1_pomiar_p95(&pomiar_pokaz)),
                 ogranicz(pomiar_bezposrednie));
    if (n > 0 && n < (int)sizeof l)
        zglos(l, (size_t)n);
    k1_pomiar_zeruj(&pomiar_czekanie);
    k1_pomiar_zeruj(&pomiar_kopia);
    k1_pomiar_zeruj(&pomiar_scena);
    k1_pomiar_zeruj(&pomiar_pokaz);
    pomiar_klatki = pomiar_bufory_karty = pomiar_bufory_pamieci = pomiar_bezposrednie = 0;
}

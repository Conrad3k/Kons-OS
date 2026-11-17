/* K1 — host test: the upper limit of a zone's memory (src/nadzorca/pamiec_stref.c; 0.10.1 step 2, ADR 0044, D2). */
#include "k1_test_host.h"
#include "pamiec_stref.h"

int main(void)
{
    /* the reserve by the computer's memory (D3's recommendation) */
    EXPECT(pamiec_rezerwa_k1(3800) == 768);
    EXPECT(pamiec_rezerwa_k1(7800) == 1024);
    EXPECT(pamiec_rezerwa_k1(64u << 10) == 1024);
    /* D2: the computer's memory minus the reserve — IdeaPad 110 (MemTotal ~3.7 GiB), X230 (~7.6 GiB) */
    EXPECT(pamiec_granica(3790, 1536) == 3022);
    EXPECT(pamiec_granica(7790, 1536) == 6766);
    /* never below the zone's memory at the start; whole 2 MiB; at most the maximum */
    EXPECT(pamiec_granica(1000, 1536) == 1536);
    EXPECT(pamiec_granica(900, 128) == 132);
    EXPECT(pamiec_granica(500, 128) == 128);
    EXPECT(pamiec_granica(0, 128) == 128);
    EXPECT(pamiec_granica(1u << 20, 1536) == PAMIEC_STREFY_MAKS);
    EXPECT(pamiec_granica(3791, 1536) % 2 == 0);
    return K1_TEST_KONIEC("test_pamiec_stref");
}

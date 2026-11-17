/* K1 — host test: the memory of the zones (src/nadzorca/pamiec_stref.c): the upper limit of a zone (0.10.1 step 2, ADR
 * 0044, D2) and the rozdzielacz (step 3, T6, D3). */
#include "k1_test_host.h"
#include "pamiec_stref.h"

/* a zone with a 2900 MiB limit, 1536 at the start, K1 holding `w_k1`, `dostepna` free in the guest */
static struct pamiec_strefy strefa(int fokus, int32_t w_k1, int32_t dostepna)
{
    return (struct pamiec_strefy){ fokus, 2900, 1536, w_k1, dostepna, 0, 0 };
}

static void granica(void)
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
}

/* The guest after its balloon took `cel`: K1 holds `zysk` less of it, its free memory is smaller by the balloon. */
static void oddal(struct pamiec_strefy *z, int32_t zysk)
{
    z->dostepna -= z->cel - z->balon;
    z->balon = z->cel;
    z->w_k1 -= zysk;
}

static void rozdzielacz(void)
{
    struct pamiec_strefy s[3];
    int z;
    /* plenty of memory: nothing changes; no zone: not the zones' business */
    s[0] = strefa(1, 2400, 1200);
    EXPECT(pamiec_rozdziel(2000, 768, s, 1, &z) == PAMIEC_DOBRZE && s[0].cel == 0 && z == -1);
    EXPECT(pamiec_rozdziel(500, 768, s, 0, &z) == PAMIEC_DOBRZE);
    /* what a balloon gives K1: the memory K1 holds the guest does not need — 2400 held, 1700 needed (2900 - 1200 free),
     * minus the zapas (256): 444; the target makes the guest as big as K1 should hold */
    EXPECT(pamiec_moze_oddac(&s[0]) == 444);
    /* K1 300 MiB below its reserve: the zone in the background gives first (a step at most), the focused one the rest */
    s[0] = strefa(1, 2400, 1200), s[1] = strefa(0, 2400, 1200);
    EXPECT(pamiec_rozdziel(468, 768, s, 2, &z) == PAMIEC_DOBRZE);
    EXPECT(s[1].cel == 2900 - (2400 - 256) && s[0].cel == 2900 - (2400 - 44));
    /* asked but not given yet counts: no new target while the balloons are on their way */
    EXPECT(pamiec_rozdziel(468, 768, s, 2, &z) == PAMIEC_DOBRZE && s[1].cel == 756 && s[0].cel == 544);
    /* given: the next second takes what is still missing, the background one again first */
    oddal(&s[1], 256), oddal(&s[0], 44);
    EXPECT(pamiec_moze_oddac(&s[1]) == 188 && pamiec_moze_oddac(&s[0]) == 400);
    EXPECT(pamiec_rozdziel(568, 768, s, 2, &z) == PAMIEC_DOBRZE && s[1].cel == 944 && s[0].cel == 556);
    /* the free pages the guest already reported are K1's: a balloon over them would give nothing */
    s[0] = strefa(0, 400, 2000);
    EXPECT(pamiec_moze_oddac(&s[0]) == 0);
    /* never into a guest's zapas */
    s[0] = strefa(0, 2400, 200);
    EXPECT(pamiec_moze_oddac(&s[0]) == 0);
    /* the focused one never below its memory at the start (1536): K1 may get back only what it holds above it; one in
     * the background gives all it does not need above its zapas */
    s[0] = strefa(1, 1600, 2000);
    EXPECT(pamiec_moze_oddac(&s[0]) == 64);
    s[0].fokus = 0;
    EXPECT(pamiec_moze_oddac(&s[0]) == 1600 - 900 - 256);
    /* old statistics (more free than the guest can have): the target never takes the guest below two zapasy */
    s[0] = strefa(0, 2800, 2800), s[0].cel = s[0].balon = 2000;
    EXPECT(pamiec_moze_oddac(&s[0]) == 2800 - (2900 - 2388) - (2800 - (2900 - 2000)));
    EXPECT(pamiec_rozdziel(100, 768, s, 1, &z) != PAMIEC_KRYTYCZNIE || s[0].cel <= 2900 - 512);
    EXPECT(s[0].cel <= 2900 - 512);
    /* statistics not known yet: nothing from that zone */
    s[0] = strefa(0, 2400, -1);
    EXPECT(pamiec_moze_oddac(&s[0]) == 0);
    /* among the zones in the background the one with the most to give first */
    s[0] = strefa(0, 2000, 900), s[1] = strefa(0, 2400, 1200), s[2] = strefa(1, 2400, 1200);
    EXPECT(pamiec_rozdziel(668, 768, s, 3, &z) == PAMIEC_DOBRZE && s[1].cel == 600 && s[0].cel == 0 && s[2].cel == 0);
    /* enough again (more than the reserve + 128): the balloons shrink, the focused one first, a step at most */
    s[0] = strefa(0, 900, 500), s[0].cel = s[0].balon = 300;
    s[1] = strefa(1, 900, 500), s[1].cel = s[1].balon = 300;
    EXPECT(pamiec_rozdziel(1196, 768, s, 2, &z) == PAMIEC_DOBRZE && s[1].cel == 44 && s[0].cel == 256);
    EXPECT(pamiec_rozdziel(1400, 768, s, 2, &z) == PAMIEC_DOBRZE && s[1].cel == 0 && s[0].cel == 0);
    s[0].cel = s[0].balon = 100;
    EXPECT(pamiec_rozdziel(850, 768, s, 2, &z) == PAMIEC_DOBRZE && s[0].cel == 100); /* within the hysteresis: stays */
    /* short and no balloon can give more: the window (D3 (2)); critical (under a quarter of the reserve): the zone in
     * the background with the most memory is to be closed (D3 (3)), never the focused one */
    s[0] = strefa(1, 2500, 100), s[1] = strefa(0, 600, 150), s[2] = strefa(0, 900, 120);
    EXPECT(pamiec_rozdziel(500, 768, s, 3, &z) == PAMIEC_MALO && z == -1);
    EXPECT(pamiec_rozdziel(150, 768, s, 3, &z) == PAMIEC_KRYTYCZNIE && z == 2);
    EXPECT(pamiec_rozdziel(150, 768, s, 1, &z) == PAMIEC_KRYTYCZNIE && z == -1); /* only the focused one */
    /* short, but a balloon can still give more next second (the step): not the window yet */
    s[0] = strefa(0, 2400, 1200);
    EXPECT(pamiec_rozdziel(100, 768, s, 1, &z) == PAMIEC_DOBRZE && s[0].cel == 756);
    /* two sessions against each other: what is missing is asked of them, the reserve stays K1's */
    s[0] = strefa(1, 2400, 1200), s[1] = strefa(0, 2400, 1200);
    EXPECT(pamiec_rozdziel(700, 768, s, 2, &z) == PAMIEC_DOBRZE && s[1].cel == 568 && s[0].cel == 0);
}

int main(void)
{
    granica();
    rozdzielacz();
    return K1_TEST_KONIEC("test_pamiec_stref");
}

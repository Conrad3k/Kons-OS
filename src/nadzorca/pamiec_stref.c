/* K1 nadzorca — the memory of the zones (pamiec_stref.h). Part of nadzorca. Since 0.10.1 step 2 (ADR 0044) a zone has a
 * balloon: its guest has its whole upper limit from the start, but K1 holds only what it really uses — the rest goes
 * back (src/strefa/balon.c). The limit is the computer's memory minus K1's reserve (D2); the VMM's sandbox gets it plus
 * the VMM's own room (strefy.c) — fixed for the zone's whole life (T4).
 * Since 0.10.1 step 3 (T6, the owner's D3) the rozdzielacz shares the computer's memory: once a second strefy_balon.c
 * gives it what K1 has free and what each zone said of itself; it answers the balloons' targets and whether memory
 * is short. Pure: no system calls (test_pamiec_stref). */
#include "pamiec_stref.h"

/* K1's reserve: from `od_mib` of the computer's memory on (the last row that fits counts). */
static const struct {
    uint64_t od_mib;
    uint32_t rezerwa_mib;
} REZERWA[] = {
    { 0, 768 },     /* 4 GB (IdeaPad 110) */
    { 6144, 1024 }, /* 8 GB (X230) and more */
};

/* The rozdzielacz's thresholds (one table, AGENTS.md rule 5; MiB). */
static const struct {
    int32_t zapas_goscia; /* free memory a guest always keeps (for a sudden jump — T1), at most a quarter of its limit */
    int32_t krok;         /* a balloon grows or shrinks at most this much a second */
    int32_t histereza;    /* the balloons shrink only when K1 has this much more than its reserve */
    int32_t krytyczna_z;  /* critical: K1 has less than this part of its reserve (1/krytyczna_z) */
} PROGI = { 256, 256, 128, 4 };

uint32_t pamiec_rezerwa_k1(uint64_t ram_mib)
{
    uint32_t r = REZERWA[0].rezerwa_mib;
    for (unsigned i = 0; i < sizeof REZERWA / sizeof REZERWA[0]; i++)
        if (ram_mib >= REZERWA[i].od_mib)
            r = REZERWA[i].rezerwa_mib;
    return r;
}

uint32_t pamiec_granica(uint64_t ram_mib, uint32_t start_mib)
{
    uint64_t r = pamiec_rezerwa_k1(ram_mib), g = ram_mib > r ? ram_mib - r : 0;
    g &= ~(uint64_t)1;
    if (g > PAMIEC_STREFY_MAKS)
        g = PAMIEC_STREFY_MAKS;
    return g > start_mib ? (uint32_t)g : start_mib;
}

static int32_t zapas(const struct pamiec_strefy *z)
{
    int32_t c = (int32_t)(z->granica / 4);
    return c < PROGI.zapas_goscia ? c : PROGI.zapas_goscia;
}

/* What K1 gets back when the balloon reaches `cel`: the guest is then granica - cel big, and K1 can hold no more of it
 * than that. The free pages the guest already reported are not in w_k1 — a balloon over them gives K1 nothing (it takes
 * them first): only the memory the guest holds and does not need (its page cache) comes back. */
static int32_t oddaje(const struct pamiec_strefy *z, int32_t cel)
{
    int32_t r = z->w_k1 - ((int32_t)z->granica - cel);
    return r > 0 ? r : 0;
}

/* The largest target: the guest keeps its zapas of free memory; the focused one never gets below its memory at the
 * start either (D3: "the focused session's balloon to its minimum" — the zones in the background give all they can). */
static int32_t cel_maks(const struct pamiec_strefy *z)
{
    int32_t a = z->balon + z->dostepna - zapas(z), b = (int32_t)z->granica - (int32_t)z->start, m = z->fokus && b < a ? b : a;
    int32_t sufit = (int32_t)z->granica - 2 * zapas(z); /* whatever the guest says, it keeps at least two zapasy */
    m = m < sufit ? m : sufit;
    return m > z->cel ? m : z->cel;
}

static int32_t w_drodze(const struct pamiec_strefy *z) /* asked of the balloon, not given yet */
{
    return z->cel > z->balon ? oddaje(z, z->cel) : 0;
}

int32_t pamiec_moze_oddac(const struct pamiec_strefy *z)
{
    if (z->dostepna < 0)
        return 0;
    return oddaje(z, cel_maks(z)) - oddaje(z, z->cel);
}

/* Zone a goes before zone b. Growing the balloons: the background ones first, among them the one with the most to
 * give. Shrinking: the focused one first, then the one with the least free memory. */
static int przed(const struct pamiec_strefy *a, const struct pamiec_strefy *b, int rosnie)
{
    if (!a->fokus != !b->fokus)
        return rosnie ? !a->fokus : a->fokus;
    return rosnie ? pamiec_moze_oddac(a) > pamiec_moze_oddac(b) : a->dostepna < b->dostepna;
}

/* The next zone in that order not taken yet (bit i of *wziete); -1: none left. */
static int nastepna(const struct pamiec_strefy *s, int n, unsigned *wziete, int rosnie)
{
    int b = -1;
    for (int i = 0; i < n; i++)
        if (!(*wziete & 1u << i) && (b < 0 || przed(&s[i], &s[b], rosnie)))
            b = i;
    if (b >= 0)
        *wziete |= 1u << b;
    return b;
}

int pamiec_rozdziel(int64_t dostepna_mib, uint32_t rezerwa_mib, struct pamiec_strefy *s, int n, int *zamknij)
{
    int64_t brak = (int64_t)rezerwa_mib - dostepna_mib, reszta = 0;
    unsigned wziete = 0;
    int i, malo = 0;
    *zamknij = -1;
    if (n <= 0 || n > PAMIEC_STREF_MAKS)
        return PAMIEC_DOBRZE;
    for (i = 0; i < n; i++)
        brak -= w_drodze(&s[i]);
    if (brak > 0) { /* K1 below its reserve: the balloons in the background first, the focused one last (D3 (1)) */
        while ((i = nastepna(s, n, &wziete, 1)) >= 0 && brak > 0) {
            int32_t d = pamiec_moze_oddac(&s[i]);
            d = d < PROGI.krok ? d : PROGI.krok;
            d = (int64_t)d < brak ? d : (int32_t)brak;
            if (d > 0) /* the guest shrinks to what K1 holds of it minus d */
                s[i].cel = (int32_t)s[i].granica - (s[i].w_k1 - oddaje(&s[i], s[i].cel) - d);
            brak -= d;
        }
        for (i = 0; i < n; i++)
            reszta += pamiec_moze_oddac(&s[i]);
        malo = brak > 0 && reszta == 0;
    } else if (dostepna_mib > (int64_t)rezerwa_mib + PROGI.histereza) { /* enough again: the focused one first */
        int64_t nadmiar = dostepna_mib - rezerwa_mib - PROGI.histereza;
        while ((i = nastepna(s, n, &wziete, 0)) >= 0 && nadmiar > 0) {
            int32_t d = s[i].cel < PROGI.krok ? s[i].cel : PROGI.krok;
            d = (int64_t)d < nadmiar ? d : (int32_t)nadmiar;
            s[i].cel -= d, nadmiar -= d;
        }
    }
    if (!malo)
        return PAMIEC_DOBRZE;
    if (dostepna_mib >= (int64_t)rezerwa_mib / PROGI.krytyczna_z)
        return PAMIEC_MALO;
    for (i = 0; i < n; i++) /* D3 (3): never the session the user works in */
        if (!s[i].fokus && (*zamknij < 0 || s[i].w_k1 > s[*zamknij].w_k1))
            *zamknij = i;
    return PAMIEC_KRYTYCZNIE;
}

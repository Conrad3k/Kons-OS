/* K1 strefa — the clipboard and the zone (0.11.0 step 5, ADR 0052 T8; the owner's D5: A), on the files port:
 *   - Ctrl+V in the window: ekran gives us K1's text (SCHOWEK) just before the key; it goes to the agent as WKLEJ +
 *     WKLEJ_TEKST, and the keys from ekran wait here (TRZYMAJ_S at most) until the agent says the compositor has it as
 *     the selection (WKLEJONE) — so the browser pastes that text, not an older one;
 *   - a copy in the zone: KOPIA + KOPIA_TEKST from the agent (checked here: the whole text as told, k1_schowek_dobry,
 *     at most one each ODSTEP_S) → SCHOWEK_USTAW to ekran (it takes it only soon after the user's gesture in the window).
 * The text never reaches the log — only lengths. Everything under mz.zamek (vsock thread and main thread). */
#include <string.h>

#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_schowek.h"
#include "k1_teraz.h"
#include "okno.h"
#include "pliki.h"
#include "strefa.h"

#define TRZYMAJ_S 0.5
#define ODSTEP_S 0.2
#define KLAWISZY 32

static struct {
    uint8_t t[K1_SCHOWEK_MAKS];
    uint32_t dl, od, nr, zrodlo;
    int jest, naglowek;              /* K1's text for the agent; its WKLEJ went */
} do_;
static struct {
    uint8_t t[K1_SCHOWEK_MAKS];
    uint32_t dl, jest, nr;
    int trwa, pomin, gotowy;         /* its frames coming; a copy too soon (dropped); whole, for ekran */
    double ostatni;
} z;
static struct {
    int trzyma, puszczaj, n;
    double od;
    struct { int kod, wartosc; } k[KLAWISZY];
} kl;
static unsigned long l_do, l_z, l_odrzucone;

void schowek_od_ekranu(const struct k1_komunikat *m, int fd)
{
    if (k1_memfd_czytaj(fd, do_.t, (size_t)m->a) != 0 ||
        !k1_schowek_dobry(do_.t, (size_t)m->a) || !pliki_polaczony()) {
        l_odrzucone++;
        return;
    }
    do_.dl = (uint32_t)m->a, do_.od = 0, do_.zrodlo = (uint32_t)m->b, do_.nr++, do_.jest = 1, do_.naglowek = 0;
    kl.trzyma = 1, kl.puszczaj = 0, kl.n = 0, kl.od = k1_teraz();
    l_do++;
    k1_log(TAG, "schowek: tekst z K1 dla Ctrl+V (%u B)", do_.dl);
    obudz();
}

int schowek_klawisz(int kod, int wartosc)
{
    if (!kl.trzyma || kl.n == KLAWISZY)
        return 0;
    kl.k[kl.n].kod = kod, kl.k[kl.n].wartosc = wartosc;
    kl.n++;
    return 1;
}

/* vsock: room in the stream — WKLEJ, then the text in whole frames. */
void schowek_do_goscia(void)
{
    struct k1_ps_ramka r;
    if (!do_.jest || !pliki_polaczony())
        return;
    if (!do_.naglowek) {
        r = (struct k1_ps_ramka){ .typ = K1_PS_WKLEJ, .nr = do_.nr, .x = do_.dl, .y = do_.zrodlo };
        if (pliki_dopisz(&r) != 0)
            return;
        do_.naglowek = 1;
    }
    while (do_.od < do_.dl && pliki_wolne() >= K1_PS_RAMKA_MAKS + 64) {
        r = (struct k1_ps_ramka){ .typ = K1_PS_WKLEJ_TEKST, .nr = do_.nr, .dane = do_.t + do_.od };
        r.dl = do_.dl - do_.od < K1_PS_DANE_MAKS ? do_.dl - do_.od : K1_PS_DANE_MAKS;
        if (pliki_dopisz(&r) != 0)
            return;
        do_.od += r.dl;
    }
    if (do_.od == do_.dl)
        do_.jest = 0;
}

/* A frame of the clipboard from the agent: 0, or -1 (against the rules: the files port closes). */
int schowek_ramka(const struct k1_ps_ramka *r)
{
    if (r->typ == K1_PS_WKLEJONE) {
        if (r->nr == do_.nr && kl.trzyma) {
            kl.puszczaj = 1;
            obudz();
        }
        return 0;
    }
    if (r->typ == K1_PS_KOPIA) {
        if (z.trwa)
            return -1; /* a new one before the text of the one before */
        z.trwa = 1, z.nr = r->nr, z.dl = r->x, z.jest = 0, z.gotowy = 0;
        z.pomin = k1_teraz() - z.ostatni < ODSTEP_S;
        z.ostatni = k1_teraz();
        return 0;
    }
    if (!z.trwa || r->nr != z.nr || r->dl > z.dl - z.jest)
        return -1; /* KOPIA_TEKST without its KOPIA, or more than it told */
    if (!z.pomin)
        memcpy(z.t + z.jest, r->dane, r->dl);
    z.jest += r->dl;
    if (z.jest < z.dl)
        return 0;
    z.trwa = 0;
    if (z.pomin || !k1_schowek_dobry(z.t, z.dl)) {
        l_odrzucone++;
        return 0;
    }
    z.gotowy = 1;
    obudz();
    return 0;
}

void schowek_rozlacz(void)
{
    z.trwa = 0;
    do_.jest = 0;
    kl.puszczaj = 1; /* nobody will say WKLEJONE: the keys go now */
}

/* Main thread: the keys held go when the guest has the text (or after TRZYMAJ_S); a copy of the zone to ekran. */
void schowek_obsluz(void)
{
    if (kl.trzyma && (kl.puszczaj || k1_teraz() - kl.od > TRZYMAJ_S)) {
        if (!kl.puszczaj)
            k1_log(TAG, "schowek: gość nie potwierdził tekstu w %.1f s — klawisze idą", TRZYMAJ_S);
        kl.trzyma = 0;
        for (int i = 0; i < kl.n; i++)
            wejscie_klawisz(kl.k[i].kod, kl.k[i].wartosc);
        kl.n = 0;
    }
    if (z.gotowy) {
        z.gotowy = 0;
        l_z++;
        k1_log(TAG, "schowek: tekst skopiowany w strefie (%u B) — do ekranu", z.dl);
        okno_kopia(z.t, z.dl);
    }
}

int schowek_czeka_ms(void)
{
    return kl.trzyma ? 50 : -1;
}

void schowek_podsumowanie(void)
{
    k1_log(TAG, "schowek: do strefy %lu, ze strefy %lu, odrzucone %lu", l_do, l_z, l_odrzucone);
}

/* K1 ekran — the security screen (0.13.0 step 6, ADR 0061, D8: A): Ctrl+Alt+Del always leads to K1.
 *
 * Ctrl+Alt+Del is ekran's: the Delete of that combination reaches no window — no zone, no application — only the shell,
 * which shows its security screen (gui zabezpieczenia.c: "Zablokuj", "Hasło", "Uruchom ponownie", "Wyłącz"). A zone's
 * full screen ends first; a zone with the focus gets Ctrl and Alt going up (its guest saw them go down). The screen is
 * a surface of its own kind (K1_POW_ZABEZPIECZENIA, the shell's only, one): above everything, even a zone's window in
 * full screen, the rest of the screen darkened under it; while it shows, every key and every click goes to it — nothing
 * typed there (the password) reaches a window. A window cannot draw over it, and only Ctrl+Alt+Del (never a window)
 * opens it: what shows after Ctrl+Alt+Del is K1. The password screen (logowanie) gets the combination as any key. */
#include <linux/input-event-codes.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_zdarzenie.h"

static int delete_trzymany; /* the Delete of Ctrl+Alt+Del is still down: its release goes nowhere */

int zabezpieczenia(void)
{
    int sh = powloka();
    for (int i = 0; i < MAKS_POW && sh >= 0; i++)
        if (s.p[i].uzyta && s.p[i].kl == sh && s.p[i].rodzaj == K1_POW_ZABEZPIECZENIA && widac(&s.p[i]))
            return i;
    return -1;
}

/* The first surface of the shell (its security screen, when it has one): where Ctrl+Alt+Del goes. */
static int cel_powloki(int sh)
{
    int pierwsza = -1;
    for (int i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == sh) {
            if (s.p[i].rodzaj == K1_POW_ZABEZPIECZENIA)
                return i;
            pierwsza = pierwsza < 0 ? i : pierwsza;
        }
    return pierwsza;
}

int zab_ctrl_alt_del(const struct k1_zdarzenie *z)
{
    int sh = powloka(), cel;
    struct pow *f = s.fokus >= 0 ? &s.p[s.fokus] : NULL;
    if (z->kod != KEY_DELETE && z->kod != KEY_KPDOT)
        return 0;
    if (z->wartosc == 0 && delete_trzymany) {
        delete_trzymany = 0;
        return 1;
    }
    if (z->wartosc != 1 || (s.mody & (K1_MOD_CTRL | K1_MOD_ALT)) != (K1_MOD_CTRL | K1_MOD_ALT) || logowanie() >= 0 ||
        sh < 0 || (cel = cel_powloki(sh)) < 0)
        return 0;
    delete_trzymany = 1;
    pelny_koniec("Ctrl+Alt+Del");
    if (f != NULL && s.kl[f->kl].rola == K1_ROLA_STREFA) { /* its guest saw Ctrl and Alt go down */
        wyslij(f->kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, f->id, KEY_LEFTCTRL, 0, 0, 0, 0));
        wyslij(f->kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, f->id, KEY_LEFTALT, 0, 0, 0, 0));
    }
    k1_log(TAG, "Ctrl+Alt+Del: do powłoki — ekran zabezpieczeń%s",
           f != NULL && s.kl[f->kl].rola == K1_ROLA_STREFA ? " (strefa z fokusem: puszczone Ctrl i Alt)" : "");
    wyslij(sh, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[cel].id, KEY_DELETE, 1, s.mody, 0, 0));
    return 1;
}

int zab_klawisz(const struct k1_zdarzenie *z)
{
    int i = zabezpieczenia();
    if (i < 0)
        return 0;
    wyslij(s.p[i].kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[i].id, z->kod, z->wartosc, s.mody, 0, 0));
    return 1;
}

/* x * a / 255 for x, a in 0..255 */
static inline uint32_t mnoz(uint32_t x, uint32_t a)
{
    uint32_t t = x * a + 128;
    return (t + (t >> 8)) >> 8;
}

void zab_zaslona(const struct prost *r)
{
    for (int32_t y = r->y; y < r->y + r->h; y++) {
        uint32_t *w = s.scena.px + (size_t)y * s.scena.krok + (size_t)r->x;
        for (int32_t x = 0; x < r->w; x++) {
            uint32_t p = w[x];
            /* a quarter of the picture over K1's darkest brown (#1C1714) */
            w[x] = (0x1cu + mnoz((p >> 16) & 0xff, 64)) << 16 | (0x17u + mnoz((p >> 8) & 0xff, 64)) << 8 |
                   (0x14u + mnoz(p & 0xff, 64));
        }
    }
}

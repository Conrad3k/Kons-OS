/* K1 Notatki — selecting and the clipboard (0.11.0 step 5, ADR 0052 T8, D5: A): Shift with the arrows, Home and End
 * selects from where the cursor was (as in Windows), Ctrl+A all of it; Ctrl+C copies the selection (ekran keeps it),
 * Ctrl+X copies and deletes it, Ctrl+V pastes (the skeleton asks ekran, the text comes to wklej — it replaces the
 * selection; "\r\n" and a lone "\r" become "\n"; what does not fit the note is left out, whole characters only). Typing,
 * Enter, Backspace and Delete with a selection replace it. The text never reaches the log — only how many bytes. */
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_log.h"
#include "notatki.h"

static uint32_t kotwica; /* where the selection started */
static int zaznacza;     /* a selection is there (kotwica ≠ kursor) */

int zaznaczenie(uint32_t *od, uint32_t *do_)
{
    if (!zaznacza || kotwica == kursor || kotwica > b->dl || kursor > b->dl)
        return 0;
    *od = kotwica < kursor ? kotwica : kursor;
    *do_ = kotwica < kursor ? kursor : kotwica;
    return 1;
}

void zaznaczenie_zapomnij(void)
{
    zaznacza = 0;
}

/* Before a key that moves the cursor: with Shift the selection starts (or goes on) here; without it, it ends. */
void przed_ruchem(unsigned kod, unsigned mody)
{
    if (kod != KEY_LEFT && kod != KEY_RIGHT && kod != KEY_HOME && kod != KEY_END && kod != KEY_UP && kod != KEY_DOWN)
        return;
    if (!(mody & K1_MOD_SHIFT))
        zaznacza = 0;
    else if (!zaznacza)
        zaznacza = 1, kotwica = kursor;
}

/* The selection deleted (before typing, Enter, Backspace, Delete): 1 when there was one. */
int usun_zaznaczenie(void)
{
    uint32_t od, do_;
    if (!zaznaczenie(&od, &do_))
        return 0;
    usun(od, do_);
    zaznacza = 0;
    return 1;
}

static void kopiuj(int wytnij)
{
    uint32_t od, do_;
    if (!zaznaczenie(&od, &do_))
        return;
    k1_apl_kopiuj(&apl, b->tekst + od, do_ - od);
    k1_log(TAG, "schowek: %s %u B", wytnij ? "wycięto" : "skopiowano", do_ - od);
    if (wytnij)
        usun_zaznaczenie();
}

int schowek_klawisz(unsigned kod, unsigned mody)
{
    if (!(mody & K1_MOD_CTRL) || (mody & K1_MOD_ALT) || !b->wczytana)
        return 0;
    if (kod == KEY_A) {
        zaznacza = b->dl > 0, kotwica = 0, kursor = b->dl;
        return 1;
    }
    if (kod == KEY_C || kod == KEY_X) {
        kopiuj(kod == KEY_X);
        return 1;
    }
    return 0;
}

void wklej(const char *t, size_t n)
{
    static char czysty[MAKS_TEKST];
    size_t d = 0;
    if (!b->wczytana)
        return;
    usun_zaznaczenie();
    for (size_t i = 0; i < n && b->dl + d < MAKS_TEKST;) {
        size_t c = 1;
        while (i + c < n && ((unsigned char)t[i + c] & 0xc0) == 0x80)
            c++;
        if (t[i] == '\r') { /* "\r\n" → "\n", a lone "\r" → "\n" */
            if (i + 1 < n && t[i + 1] == '\n')
                c = 2;
            czysty[d++] = '\n';
        } else if (b->dl + d + c <= MAKS_TEKST) {
            memcpy(czysty + d, t + i, c);
            d += c;
        } else {
            break;
        }
        i += c;
    }
    if (d > 0)
        wstaw(czysty, (uint32_t)d);
    k1_log(TAG, "schowek: wklejono %zu B", d);
    k1_apl_odswiez(&apl);
}

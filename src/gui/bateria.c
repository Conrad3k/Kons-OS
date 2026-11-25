/* K1 gui — the battery on the top bar (0.5.3, ADR 0014): its state from nadzorca (fd 5, K1_KOM_BATERIA; one way, sender
 * UID 0 checked by k1_odbierz_komunikat), its cell in the bar's pill — shown only when the computer has a battery.
 * Split from pasek_gorny.c before the battery's popover (0.11.8 step 3, T4). */
#include <stdio.h>

#include "gui.h"
#include "k1_log.h"

static struct {
    int obecna, procent, stan, zasilacz;
} bat = { 0, -1, K1_BAT_NIEZNANY, 0 };
static int bat_zalogowana;

static void zaloguj_baterie(void)
{
    static const char *const stany[] = { "stan nieznany", "rozładowywanie", "ładowanie", "pełna", "nie ładuje" };
    char proc[16] = "?";
    if (!bat.obecna) {
        k1_log(TAG, "brak baterii (zasilacz %s)", bat.zasilacz ? "podłączony" : "—");
        return;
    }
    if (bat.procent >= 0)
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
    k1_log(TAG, "bateria %s, %s, zasilacz %s", proc, stany[bat.stan], bat.zasilacz ? "podłączony" : "odłączony");
}

void bateria_komunikat(const struct k1_komunikat *m)
{
    /* log only what a person would notice: presence, state, mains, every 5 % */
    int zmiana = !bat_zalogowana || m->a != bat.obecna || m->c != bat.stan || m->d != bat.zasilacz ||
                 (m->b / 5 != bat.procent / 5);
    bat.obecna = m->a;
    bat.procent = m->b;
    bat.stan = m->c;
    bat.zasilacz = m->d;
    if (zmiana) {
        zaloguj_baterie();
        bat_zalogowana = 1;
    }
    gora.brudna = 1;
}

/* The battery's cell from x: [bolt] 73% [battery], 8 px on either side (the bolt only while charging). */
#define BAT_BLYSK 11
int32_t bateria_szer(void)
{
    char proc[16];
    if (!bat.obecna)
        return 0;
    snprintf(proc, sizeof proc, "%d%%", bat.procent);
    return 8 + (bat.stan == K1_BAT_LADOWANIE ? BAT_BLYSK : 0) + (bat.procent >= 0 ? k1_tekst_szer(K1_CZ_MALA_MOCNA, proc) : 0) +
           6 + 25 + 8;
}

void bateria_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem)
{
    char proc[16];
    x += 8;
    if (bat.stan == K1_BAT_LADOWANIE) { /* lightning bolt */
        float bx = (float)x;
        k1_kreska(o, bx + 5, 9, bx + 1.5f, 16.5f, 1.5f, krem);
        k1_kreska(o, bx + 1.5f, 16.5f, bx + 6.5f, 15.5f, 1.5f, krem);
        k1_kreska(o, bx + 6.5f, 15.5f, bx + 3, 23, 1.5f, krem);
        x += BAT_BLYSK;
    }
    if (bat.procent >= 0) {
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
        x += k1_tekst(o, K1_CZ_MALA_MOCNA, x, 21, proc, krem, 0);
    }
    k1_bateria(o, (float)x + 6, 10, bat.procent, k1_bateria_niska(bat.procent, bat.stan), 255); /* body 22×12 + terminal */
}

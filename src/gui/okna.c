/* K1 gui — the windows of the applications (the list ekran sends: K1_KOM_OKNO) and what a click on an
 * application does: start it (nadzorca, URUCHOM), or show / minimise its window (ekran). */
#include <errno.h>
#include <string.h>

#include "gui.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_teraz.h"

static struct okno okna[32];
static double uruchamiane[K1_APL_MAKS + 1]; /* when it was started (k1_teraz); 0 = not starting */

#define N_OKIEN (sizeof okna / sizeof okna[0])

struct okno *okno_aplikacji(int apl)
{
    size_t i;
    for (i = 0; i < N_OKIEN; i++)
        if (okna[i].id != 0 && okna[i].apl == apl)
            return &okna[i];
    return NULL;
}

int aktywna_aplikacja(void)
{
    size_t i;
    for (i = 0; i < N_OKIEN; i++)
        if (okna[i].id != 0 && okna[i].stan == K1_OKNO_AKTYWNE)
            return okna[i].apl;
    return 0;
}

void zmiana_okna(const struct k1_komunikat *m)
{
    size_t i, wolny = N_OKIEN;
    for (i = 0; i < N_OKIEN; i++) {
        if (okna[i].id == m->obiekt) {
            if (m->b == K1_OKNO_ZAMKNIETE)
                okna[i].id = 0;
            else
                okna[i].stan = m->b;
            return;
        }
        if (okna[i].id == 0 && wolny == N_OKIEN)
            wolny = i;
    }
    if (m->b != K1_OKNO_ZAMKNIETE && wolny < N_OKIEN) {
        okna[wolny].id = m->obiekt;
        okna[wolny].apl = m->a;
        okna[wolny].stan = m->b;
        uruchamiane[m->a] = 0;
    }
}

void do_nadzorcy(struct k1_komunikat m)
{
    if (k1_kanal_wyslij(FD_NADZORCA, &m, sizeof m, -1) != 0)
        k1_log(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

void aplikacja(int apl)
{
    struct okno *w = okno_aplikacji(apl);
    pokaz_menu(0);
    if (w == NULL) {
        /* already starting (0 = not: k1_teraz() counts from boot, a fast computer is under 5 s here) */
        if (uruchamiane[apl] > 0 && k1_teraz() - uruchamiane[apl] < 5.0)
            return;
        uruchamiane[apl] = k1_teraz();
        k1_log(TAG, "uruchamiam: %s", aplikacje[apl].nazwa);
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_URUCHOM, 0, apl, 0, 0, 0, 0));
    } else if (w->stan == K1_OKNO_AKTYWNE) {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_MINIMALIZUJ_OKNO, w->id, 0, 0, 0, 0, 0));
    } else {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, w->id, 0, 0, 0, 0, 0));
    }
}

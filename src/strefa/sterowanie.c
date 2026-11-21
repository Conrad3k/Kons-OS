/* K1 strefa — the lines of the guest's control channel (vsock port 1; split from vsock.c in 0.11.0 step 2, readability
 * rule 7): what each line means and the counters' line (0.10.1 step 1, S13). vsock.c keeps the connection, its credit
 * and the line being read; here only lines that are whole. Under mz.zamek (vsock_liczniki, vsock_podsumowanie: the
 * main thread). The lines themselves are listed in vsock.c's header. */
#include <stdio.h>
#include <string.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "linia.h"
#include "okno.h"
#include "polaczenia.h"
#include "pomiar.h"
#include "sterowanie.h"
#include "strefa.h"

/* The counters (0.10.1 step 1, S13): the guest's numbers from "liczniki" and ours, one line every LICZNIKI_ODSTEP s
 * (the main thread: vsock_liczniki — whatever the guest sends, or does not) and once more at the machine's end
 * (vsock_podsumowanie). */
#define LICZNIKI_ODSTEP 5.0
static struct {
    uint32_t g[3];   /* the guest's: available MiB, zram MiB, OOM kills */
    int sa;          /* the guest sent them at least once */
    double t;        /* the last line (k1_teraz) */
} lz;

static void liczniki_loguj(const char *kiedy)
{
    unsigned zasoby, mib;
    struct balon_stan b;
    char g[80];
    int czeka;
    unsigned long odrzucone;
    vsock_kolejka(&czeka, &odrzucone);
    gpu_liczniki(&zasoby, &mib);
    balon_stan(&b); /* 0.10.1 step 2: what K1 holds of the guest's memory, the balloon */
    if (lz.sa)
        snprintf(g, sizeof g, "dostępna %u MiB, zram %u MiB, zabite %u", lz.g[0], lz.g[1], lz.g[2]);
    else
        snprintf(g, sizeof g, "brak");
    k1_log(TAG, "liczniki%s: gość — %s; pamięć w K1 %u MiB, oddane %u MiB, balon %u MiB; grafika: zasoby %u, %u MiB; "
                "połączenia %u; vsock czeka %d, odrzucone %lu", kiedy, g, b.w_k1_mib, b.oddane_mib, b.balon_mib, zasoby,
           mib, pol_zajete(), czeka, odrzucone);
}

/* The main thread, once a second (main.c): the counters' line every LICZNIKI_ODSTEP s — also when the guest sends no
 * numbers of its own (a guest that is busy, a hostile one). */
void vsock_liczniki(void)
{
    if (k1_teraz() - lz.t < LICZNIKI_ODSTEP)
        return;
    lz.t = k1_teraz();
    liczniki_loguj("");
}

void vsock_podsumowanie(void)
{
    liczniki_loguj(" przy końcu");
}

/* "kto <1 browser|2 compositor> <code> <rule>" (0.10.1 step 1, S15): who of the guest's programs ended by itself —
 * numbers only, logged once (the first one counts, as "koniec"). */
static void kto(const uint32_t *w)
{
    static int byl;
    if (byl || w[0] < 1 || w[0] > 2)
        return;
    byl = 1;
    if (w[2] > 0)
        k1_log(TAG, "gość: %s zakończony (kod %u) — kompozytor odrzucił przeglądarkę (reguła %u)",
               w[0] == 1 ? "przeglądarka" : "kompozytor", w[1], w[2]);
    else
        k1_log(TAG, "gość: %s zakończony (kod %u)", w[0] == 1 ? "przeglądarka" : "kompozytor", w[1]);
}

int sterowanie_linia(const char *l)
{
    uint32_t w[POMIAR_GOSC_LICZB];
    if (linia_liczby(l, "pomiar", w, POMIAR_GOSC_LICZB, 1u << 24)) /* 0.10.11 step 1: the compositor's links */
        pomiar_gosc(w);
    else if (linia_liczby(l, "liczniki", w, 3, 1u << 24)) {
        memcpy(lz.g, w, sizeof lz.g);
        lz.sa = 1;
    } else if (linia_liczby(l, "kto", w, 3, 999))
        kto(w);
    else if (strcmp(l, "gotowa") == 0)
        k1_log(TAG, "gość gotowy (kanał sterujący) po %.1f s", k1_teraz() - mz.start_t);
    else if (strcmp(l, "etap start") == 0) { /* 0.10.2 step 1: the start screen stays until "etap okno" */
        okno_etap(OKNO_ETAP_START);
        k1_log(TAG, "gość: program strefy startuje — ekran startu do jego okna");
    } else if (strcmp(l, "pelny 1") == 0 || strcmp(l, "pelny 0") == 0) { /* 0.11.0 step 1: full screen */
        okno_pelny(l[6] == '1');
    } else if (strcmp(l, "etap okno") == 0) { /* S30: how long the browser takes to start */
        okno_etap(OKNO_ETAP_OKNO);
        k1_log(TAG, "gość: okno przeglądarki po %.1f s od startu maszyny", k1_teraz() - mz.start_t);
    }
    else if (strcmp(l, "zamykam") == 0)
        k1_log(TAG, "gość się zamyka");
    else if (strcmp(l, "zamykanie gotowe") == 0 || strcmp(l, "zamykanie sygnal") == 0 ||
             strcmp(l, "zamykanie po czasie") == 0)
        k1_log(TAG, "gość: przeglądarka %s po %.1f s",
               l[10] == 'g' ? "zamknęła się sama (dane sesji zapisane)" :
               l[10] == 's' ? "zamknięta sygnałem (bez zapisu danych sesji)" : "nie zamknęła się w czasie — zatrzymana",
               mz.zamykana_t > 0 ? k1_teraz() - mz.zamykana_t : 0.0);
    else if (strcmp(l, "koniec pamiec") == 0 || strcmp(l, "koniec awaria") == 0 ||
             strcmp(l, "koniec sam") == 0) {
        int kod = l[7] == 'p' ? K1_STREFA_KONIEC_PAMIEC : l[7] == 'a' ? K1_STREFA_KONIEC_AWARIA : 0;
        if (!mz.gosc_powiedzial)
            mz.kod_goscia = kod;
        mz.gosc_powiedzial = 1;
        k1_log(TAG, "gość: program strefy zakończył się sam (%s)",
               kod == K1_STREFA_KONIEC_PAMIEC ? "brak pamięci" : kod == K1_STREFA_KONIEC_AWARIA ? "awaria" : "zamknięty");
    } else if (strcmp(l, "pamiec strona") == 0)
        k1_log(TAG, "gość: strona zakończona — brak pamięci w strefie (%u)", ++mz.strony_bez_pamieci);
    else
        return 0;
    return 1;
}

/* K1 ekran — the screen "Wyłączanie" (0.10.7 step 6, T7; the owner's D7: sketch B "Kropki"). nadzorca says WYLACZANIE on
 * the control channel as soon as K1 begins to power off (or restart); from then on ekran draws k1_wylaczanie over the
 * whole screen — over every window, a zone's too, and without the cursor — until the end, and gives no input to the
 * windows. The dots move by ekran's clock (T8: a still frame while the test program reads the screen's description, so
 * that it is the same every time). Not shown on log out. Part of ekran (ekran.h). */
#include "ekran.h"
#include "k1_log.h"
#include "k1_wylaczanie.h"

#define KLATKA_S (1.0 / 15.0) /* the dots: 15 frames a second are enough for them */

static int trwa, restart;
static double od, ostatnia;

void wylaczanie_start(int restart_)
{
    if (trwa)
        return;
    trwa = 1;
    restart = restart_;
    od = ostatnia = k1_teraz();
    k1_log(TAG, "ekran wyłączania: %s", restart ? "ponowne uruchamianie" : "wyłączanie");
    uszkodz(0, 0, s.W, s.H);
}

int wylaczanie_trwa(void)
{
    return trwa;
}

const char *wylaczanie_napis(void)
{
    return k1_wylaczanie_napis(restart);
}

static double czas_animacji(void)
{
    return s.ust[K1_UST_OPIS] ? 0.0 : k1_teraz() - od;
}

void wylaczanie_rysuj(const struct prost *r)
{
    struct k1_obraz o = { s.scena.px + (size_t)r->y * s.scena.krok + (size_t)r->x, (uint32_t)r->w, (uint32_t)r->h,
                          s.scena.krok };
    if (trwa)
        k1_wylaczanie(&o, r->x, r->y, s.W, s.H, k1_motyw(s.ust[K1_UST_MOTYW]), restart, czas_animacji());
}

int wylaczanie_czas_ms(void)
{
    double zostalo;
    int32_t k[4];
    if (!trwa || s.ust[K1_UST_OPIS])
        return -1;
    zostalo = ostatnia + KLATKA_S - k1_teraz();
    if (zostalo > 0)
        return (int)(zostalo * 1000.0) + 1;
    ostatnia = k1_teraz();
    k1_wylaczanie_kropki(s.W, s.H, k);
    uszkodz(k[0], k[1], k[2], k[3]);
    return (int)(KLATKA_S * 1000.0);
}

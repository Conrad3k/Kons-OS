/* K1 skarbiec — work in the quiet moments of the loop (0.8.2 step 1, ADR 0036): compaction of one segment in steps of
 * 4 MiB. Also the longest time skarbiec did not answer, in the log with the work done. (Format 1 and its transition to
 * format 2 are gone since 0.10.0.) Part of skarbiec (skarbiec.h). */
#include <string.h>
#include <time.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "skarbiec.h"

#define KROK_BLOKOW 1024u   /* a step: 4 MiB moved — about 0.02 s of processor, a fraction of a second on USB 2.0 */
#define CISZA_KROK 0.5      /* a step: after half a second without one */
#define DLUGA_PRZERWA 1.0   /* a request that kept skarbiec busy longer is logged (at most every 10 s) */
static int kompaktowanie_wstrzymane;
static struct {
    unsigned krokow;
    double od, najdluzszy, procesor; /* the longest step: by the clock and the processor time of skarbiec in it */
} praca;                     /* the work being done: a segment */
static double najdluzsza;    /* the longest time skarbiec did not answer since the last line about the work */
static double dluga_wpis = -100.0;

void przerwa_trwala(double s)
{
    if (s > najdluzsza)
        najdluzsza = s;
    if (s > DLUGA_PRZERWA && k1_teraz() - dluga_wpis >= 10.0) {
        dluga_wpis = k1_teraz();
        k1_log(TAG, "skarbiec nie odpowiadał %.1f s (jedna prośba)", s);
    }
}

/* The processor time of skarbiec, s: how much work a step was, without the time it waited for the disk (0.8.5: on a
 * loaded machine a step of 10 ms took 1.25 s by the clock — the disk of the host, not skarbiec). */
static double procesor(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t) != 0)
        return 0;
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void krok_zrobiony(double t0, double c0)
{
    double t = k1_teraz() - t0, c = procesor() - c0;
    if (praca.krokow++ == 0)
        praca.od = t0;
    if (t > praca.najdluzszy)
        praca.najdluzszy = t;
    if (c > praca.procesor)
        praca.procesor = c;
    przerwa_trwala(t);
}

static void koniec_pracy(void)
{
    memset(&praca, 0, sizeof praca);
    najdluzsza = 0;
}

double przerwa_czeka(void)
{
    int k;
    if (!trwaly)
        return -1;
    k = kompaktowanie_wstrzymane ? 0 : k1_mag_kompaktowac(&mag);
    if (k == 0)
        return -1;
    return k == 2 ? 0 : CISZA_KROK; /* urgent: a step between requests too, before a write must compact itself */
}

static void krok_kompaktowania(void)
{
    double t0 = k1_teraz(), c0 = procesor();
    int r = k1_mag_kompaktuj_krok(&mag, KROK_BLOKOW);
    krok_zrobiony(t0, c0);
    if (r == 1)
        return;
    if (r == K1_MAG_OK) {
        k1_log(TAG, "kompaktowanie (w przerwach): odcinek wolny po %u krokach, najdłuższy krok %.2f s (procesor %.2f s); "
               "najdłuższa przerwa w odpowiedziach %.2f s", praca.krokow, praca.najdluzszy, praca.procesor, najdluzsza);
    } else {
        kompaktowanie_wstrzymane = 1; /* a write still compacts when it must */
        k1_log(TAG, "kompaktowanie w przerwach wstrzymane: %s", k1_mag_opis(r));
    }
    koniec_pracy();
}

int przerwa_praca(void)
{
    krok_kompaktowania();
    return 0;
}

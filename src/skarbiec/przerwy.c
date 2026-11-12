/* K1 skarbiec — work in the quiet moments of the loop (0.8.2 step 1, ADR 0036): compaction (format 1: everything at
 * once, ADR 0034; format 2: one segment in steps of 4 MiB) and the transition of a vault of 0.5.6–0.8.1 to format 2,
 * once nadzorca says the start of this version is confirmed (START_POTWIERDZONY: a rollback A/B must still read the
 * data). Also the longest time skarbiec did not answer, in the log with the work done. Part of skarbiec
 * (skarbiec.h). */
#include <string.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "skarbiec.h"

#define KROK_BLOKOW 1024u   /* a step: 4 MiB moved — about 0.02 s of processor, a fraction of a second on USB 2.0 */
#define CISZA_CALOSC 2.0    /* format 1 compacts everything at once: after 2 s without a request (ADR 0034) */
#define CISZA_KROK 0.5      /* a step: after half a second without one */
#define DLUGA_PRZERWA 1.0   /* a request that kept skarbiec busy longer is logged (at most every 10 s) */
#define PONOWNIE (8ull << 20) /* a postponed transition tries again when 8 MiB more are free (or K1-DANE grew) */

static enum { PR_CZEKA, PR_WOLNO, PR_ODLOZONE, PR_KONIEC } przejscie;
static uint64_t odlozone_wolne, odlozone_pojemnosc;
static int kompaktowanie_wstrzymane;
static struct {
    unsigned krokow;
    double od, najdluzszy;
} praca;                     /* the work being done: a segment, or the transition */
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

static void krok_zrobiony(double t0)
{
    double t = k1_teraz() - t0;
    if (praca.krokow++ == 0)
        praca.od = t0;
    if (t > praca.najdluzszy)
        praca.najdluzszy = t;
    przerwa_trwala(t);
}

static void koniec_pracy(void)
{
    memset(&praca, 0, sizeof praca);
    najdluzsza = 0;
}

void przerwa_kompaktuj_calosc(const char *kiedy)
{
    double t0 = k1_teraz();
    uint64_t przed = mag.koniec;
    int r = k1_mag_kompaktuj(&mag);
    k1_log(TAG, "kompaktowanie (%s): dziennik %llu → %llu MiB, żywe dane %llu MiB, %.1f s%s%s", kiedy,
           (unsigned long long)(przed * K1_MAG_BLOK >> 20), (unsigned long long)(mag.koniec * K1_MAG_BLOK >> 20),
           (unsigned long long)(mag.zywe * K1_MAG_BLOK >> 20), k1_teraz() - t0, r == K1_MAG_OK ? "" : " — ",
           r == K1_MAG_OK ? "" : k1_mag_opis(r));
    przerwa_trwala(k1_teraz() - t0);
}

void przejscie_wolno(void)
{
    if (przejscie != PR_CZEKA)
        return;
    przejscie = PR_WOLNO;
    if (zablokowany || (trwaly && mag.format == 1))
        k1_log(TAG, "start tej wersji potwierdzony — magazyn przejdzie na format 2 w przerwach");
}

static int przejscie_trwa(void)
{
    if (przejscie == PR_ODLOZONE &&
        (k1_mag_wolne(&mag) >= odlozone_wolne + PONOWNIE || k1_mag_pojemnosc(&mag) > odlozone_pojemnosc))
        przejscie = PR_WOLNO; /* files deleted, or K1-DANE grew: try again */
    return trwaly && mag.format == 1 && przejscie == PR_WOLNO;
}

double przerwa_czeka(void)
{
    int k;
    if (!trwaly)
        return -1;
    if (przejscie_trwa())
        return CISZA_KROK;
    k = kompaktowanie_wstrzymane ? 0 : k1_mag_kompaktowac(&mag, 0);
    if (k == 0)
        return -1;
    if (mag.format == 1)
        return CISZA_CALOSC;
    return k == 2 ? 0 : CISZA_KROK; /* urgent: a step between requests too, before a write must compact itself */
}

/* The end of the transition (r: the last step's result); 1 when the room changed. */
static int po_przejsciu(int r, uint64_t przed)
{
    przejscie = r == K1_MAG_PELNY ? PR_ODLOZONE : PR_KONIEC;
    if (r == K1_MAG_OK)
        k1_log(TAG, "przejście na format 2: gotowe — %d obiektów, %u kroków w %.1f s, najdłuższy krok %.2f s, "
               "najdłuższa przerwa w odpowiedziach %.2f s; miejsce na dane %llu → %llu MiB", mag.n_wpisow,
               praca.krokow, k1_teraz() - praca.od, praca.najdluzszy, najdluzsza, (unsigned long long)(przed >> 20),
               (unsigned long long)(k1_mag_pojemnosc(&mag) >> 20));
    else if (r == K1_MAG_PELNY)
        k1_log(TAG, "przejście na format 2 odłożone: za mało miejsca w wolnej połowie K1-DANE (żywe dane %llu MiB) — "
               "magazyn działa dalej w formacie 1", (unsigned long long)(mag.zywe * K1_MAG_BLOK >> 20));
    else if (mag.format == 0) /* the spis or the header failed: the disk has the old vault or the new one */
        k1_koniec(TAG, "przejście na format 2: zapis nagłówka nieudany (%s) — ponowne otwarcie magazynu", k1_mag_opis(r));
    else
        k1_log(TAG, "przejście na format 2 przerwane: %s — magazyn działa dalej w formacie 1", k1_mag_opis(r));
    odlozone_wolne = k1_mag_wolne(&mag);
    odlozone_pojemnosc = k1_mag_pojemnosc(&mag);
    koniec_pracy();
    return r == K1_MAG_OK;
}

static int krok_przejscia(void)
{
    double t0 = k1_teraz();
    uint64_t przed = k1_mag_pojemnosc(&mag);
    int r;
    if (praca.krokow == 0)
        k1_log(TAG, "przejście na format 2 (ADR 0036): kopiuję %d obiektów (%llu MiB) do wolnej połowy K1-DANE, "
               "w przerwach po %u MiB", mag.n_wpisow, (unsigned long long)(mag.zywe * K1_MAG_BLOK >> 20),
               KROK_BLOKOW * K1_MAG_BLOK >> 20);
    r = k1_mag_przejscie_krok(&mag, KROK_BLOKOW);
    krok_zrobiony(t0);
    return r == 1 ? 0 : po_przejsciu(r, przed);
}

static void krok_kompaktowania(void)
{
    double t0 = k1_teraz();
    int r = k1_mag_kompaktuj_krok(&mag, KROK_BLOKOW);
    krok_zrobiony(t0);
    if (r == 1)
        return;
    if (r == K1_MAG_OK) {
        k1_log(TAG, "kompaktowanie (w przerwach): odcinek wolny po %u krokach, najdłuższy krok %.2f s; najdłuższa "
               "przerwa w odpowiedziach %.2f s", praca.krokow, praca.najdluzszy, najdluzsza);
    } else {
        kompaktowanie_wstrzymane = 1; /* a write still compacts when it must */
        k1_log(TAG, "kompaktowanie w przerwach wstrzymane: %s", k1_mag_opis(r));
    }
    koniec_pracy();
}

int przerwa_praca(void)
{
    if (przejscie_trwa())
        return krok_przejscia();
    if (mag.format == 1)
        przerwa_kompaktuj_calosc("w przerwie");
    else
        krok_kompaktowania();
    return 0;
}

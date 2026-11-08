/* K1 Zasoby — native application: processor and memory use of the whole system over the last
 * minute and of every service (M4.1); the battery with the power draw and the time left (0.6.3).
 *
 * The numbers come from the supervisor once a second on fd 4 (k1_komunikat ZASOBY_SYSTEM and
 * ZASOBY_USLUGA, from the cgroup statistics and sysinfo): the application reads no files and
 * cannot write to that channel (the supervisor shut its reading side). Every packet is checked:
 * sender UID 0 (SO_PASSCRED), no descriptors, every field (k1_komunikat_sprawdz).
 * Sandbox: UID 203, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_aplikacja.h"
#include "k1_liczba.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"

#define TAG "K1-ZASOBY"
#define FD_NADZORCA 4
#define PROBKI 60          /* one a second: the last minute */
#define MARG 24
#define WYKRES_H 76
#define WIERSZ 22

static struct k1_apl apl;
static int32_t cpu[PROBKI], pam[PROBKI]; /* ‰ of all CPUs, ‰ of the memory */
static int probek;                        /* how many are valid (≤ PROBKI) */
static int32_t pam_uzyta, pam_calk, procesory, czas_pracy;
static struct {
    int32_t cpu, kib, dziala;
} uslugi[K1_PROFIL_MAKS + 1];
static struct {
    int32_t znana, obecna, procent, stan, moc_mw, minuty;
} bat;
static unsigned long odrzucone;
static int pierwsze = 1;


/* Memory in MiB as "850 MB" or "1,2 GB". */
static void pamiec_tekst(char *buf, size_t n, int32_t mib)
{
    char d[24];
    if (mib < 1024) {
        snprintf(buf, n, "%d MB", (int)mib);
        return;
    }
    k1_dziesietne(d, sizeof d, ((long)mib * 10 + 512) / 1024);
    snprintf(buf, n, "%s GB", d);
}

/* One series, one hue: a soft area under a 2-px line, a hairline grid at 50 %. */
static void wykres(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h,
                   const int32_t *v)
{
    int i;
    float krok = (float)(w - 16) / (float)(PROBKI - 1), x0 = (float)x + 8, y0 = (float)y + 8, wys = (float)(h - 16);
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 8, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)x, (float)y, (float)w, (float)h, 8, 1.0f, k1_kolor(m->tekst, 20));
    k1_prostokat(o, x + 8, y + h / 2, w - 16, 1, k1_kolor(m->tekst, 14));
    if (probek < 2) {
        k1_tekst(o, K1_CZ_MALA, x + 16, y + h / 2 + 5, "Zbieram dane…", k1_kolor(m->tekst3, 255), 0);
        return;
    }
    /* newest sample on the right edge */
    for (i = PROBKI - probek; i < PROBKI; i++) {
        float px = x0 + (float)i * krok, py = y0 + wys - wys * (float)v[i] / 1000.0f;
        int32_t sx = (int32_t)px, sw = (int32_t)(px + krok) - sx, sy = (int32_t)py;
        if (i < PROBKI - 1) /* columns side by side, no overlap (no stripes in the soft fill) */
            k1_prostokat(o, sx, sy, sw, (int32_t)(y0 + wys) - sy, k1_kolor(m->akcent, 36));
        if (i > PROBKI - probek)
            k1_kreska(o, px - krok, y0 + wys - wys * (float)v[i - 1] / 1000.0f, px, py, 2.0f, k1_kolor(m->akcent, 255));
    }
}

/* One line under the headline: what the battery does now. */
static void bateria_opis(char *buf, size_t n)
{
    char w[24], c[32];
    k1_dziesietne(w, sizeof w, ((long)bat.moc_mw + 50) / 100);
    k1_minuty(c, sizeof c, bat.minuty);
    if (!bat.obecna) {
        snprintf(buf, n, "brak baterii — zasilanie z sieci");
    } else if (bat.stan == K1_BAT_ROZLADOWANIE) {
        if (bat.moc_mw < 0)
            snprintf(buf, n, "rozładowuje się");
        else if (bat.minuty < 0)
            snprintf(buf, n, "pobór %s W", w);
        else
            snprintf(buf, n, "pobór %s W · zostało ok. %s", w, c);
    } else if (bat.stan == K1_BAT_LADOWANIE) {
        if (bat.moc_mw < 0)
            snprintf(buf, n, "ładuje się");
        else if (bat.minuty < 0)
            snprintf(buf, n, "ładowanie %s W", w);
        else
            snprintf(buf, n, "ładowanie %s W · do pełna ok. %s", w, c);
    } else if (bat.stan == K1_BAT_PELNA) {
        snprintf(buf, n, "naładowana, zasilanie z sieci");
    } else if (bat.stan == K1_BAT_NIE_LADUJE) {
        snprintf(buf, n, "zasilacz podłączony, nie ładuje się");
    } else {
        snprintf(buf, n, "stan nieznany");
    }
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char a[40], b[40], buf[96];
    int32_t y = K1_OKNO_TYTUL + 12, w = apl.szer - 2 * MARG, p;
    int32_t ostatni_cpu = probek ? cpu[PROBKI - 1] : 0;

    /* Processor: a headline number, then the last minute */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Procesor", k1_kolor(m->tekst, 255), 0);
    k1_dziesietne(a, sizeof a, ostatni_cpu);
    snprintf(buf, sizeof buf, "%s %%", a);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + w - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), y + 16, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(buf, sizeof buf, "%d %s, ostatnia minuta", (int)procesory,
             procesory == 1 ? "rdzeń" : (procesory % 10 >= 2 && procesory % 10 <= 4 && (procesory % 100 < 12 || procesory % 100 > 14) ? "rdzenie" : "rdzeni"));
    k1_tekst(o, K1_CZ_MALA, MARG + 84, y + 16, procesory ? buf : "", k1_kolor(m->tekst3, 255), 0);
    wykres(o, m, MARG, y + 26, w, WYKRES_H, cpu);
    y += 26 + WYKRES_H + 16;

    /* Memory */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Pamięć", k1_kolor(m->tekst, 255), 0);
    pamiec_tekst(a, sizeof a, pam_uzyta);
    pamiec_tekst(b, sizeof b, pam_calk);
    snprintf(buf, sizeof buf, "%s z %s", a, b);
    if (pam_calk > 0)
        k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + w - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), y + 16, buf,
                 k1_kolor(m->tekst, 255), 0);
    wykres(o, m, MARG, y + 26, w, WYKRES_H, pam);
    y += 26 + WYKRES_H + 20;

    /* Battery (0.6.3): percent, power draw now, time left at that rate */
    if (bat.znana) {
        k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Bateria", k1_kolor(m->tekst, 255), 0);
        bateria_opis(buf, sizeof buf);
        k1_tekst(o, K1_CZ_MALA, MARG + 84, y + 16, buf, k1_kolor(m->tekst2, 255), w - 84 - 56);
        if (bat.obecna && bat.procent >= 0) {
            snprintf(a, sizeof a, "%d %%", (int)bat.procent);
            k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + w - k1_tekst_szer(K1_CZ_TEKST_MOCNA, a), y + 16, a,
                     k1_kolor(m->tekst, 255), 0);
        }
        y += 36;
    }

    /* Services: name, processor, memory */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Usługi i aplikacje", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + w - 200, y + 16, "procesor", k1_kolor(m->tekst3, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + w - 72, y + 16, "pamięć", k1_kolor(m->tekst3, 255), 0);
    y += 24;
    k1_prostokat(o, MARG, y, w, 1, k1_kolor(m->tekst, 20));
    for (p = 1; p <= K1_PROFIL_MAKS; p++) {
        if (!uslugi[p].dziala)
            continue;
        k1_tekst(o, K1_CZ_MALA, MARG, y + 16, k1_profil_nazwa(p), k1_kolor(m->tekst, 255), 0);
        k1_dziesietne(a, sizeof a, uslugi[p].cpu);
        snprintf(buf, sizeof buf, "%s %%", a);
        k1_tekst(o, K1_CZ_MALA, MARG + w - 136 - k1_tekst_szer(K1_CZ_MALA, buf), y + 16, buf, k1_kolor(m->tekst2, 255), 0);
        k1_dziesietne(a, sizeof a, ((long)uslugi[p].kib * 10 + 512) / 1024);
        snprintf(buf, sizeof buf, "%s MB", a);
        k1_tekst(o, K1_CZ_MALA, MARG + w - k1_tekst_szer(K1_CZ_MALA, buf), y + 16, buf, k1_kolor(m->tekst2, 255), 0);
        y += WIERSZ;
    }
    snprintf(buf, sizeof buf, "System działa od %d min", (int)(czas_pracy / 60));
    k1_tekst(o, K1_CZ_MALA, MARG, apl.wys - 16, czas_pracy ? buf : "", k1_kolor(m->tekst3, 255), 0);
}

static void probka(int32_t *tab, int32_t v)
{
    memmove(tab, tab + 1, (PROBKI - 1) * sizeof *tab);
    tab[PROBKI - 1] = v;
}

/* Everything the supervisor sent (its channel has data). */
static void odbierz(void)
{
    int zmiana = 0, i;
    for (i = 0; i < 64; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC)
            k1_koniec(TAG, "nadzorca zamknął kanał");
        if (w != K1_ODB_JEST ||
            (k.typ != K1_KOM_ZASOBY_SYSTEM && k.typ != K1_KOM_ZASOBY_USLUGA && k.typ != K1_KOM_ZASOBY_BATERIA)) {
            if (odrzucone++ < 3)
                k1_log(TAG, "odrzucono komunikat od nadzorcy");
            continue;
        }
        if (k.typ == K1_KOM_ZASOBY_SYSTEM) {
            pam_uzyta = k.b;
            pam_calk = k.c;
            procesory = k.d;
            czas_pracy = k.e;
            probka(cpu, k.a);
            probka(pam, (int32_t)((long long)k.b * 1000 / k.c));
            if (probek < PROBKI)
                probek++;
            if (pierwsze) {
                pierwsze = 0;
                k1_log(TAG, "dane od nadzorcy: procesor %d‰, pamięć %d z %d MiB, %d CPU", (int)k.a, (int)k.b, (int)k.c,
                       (int)k.d);
            }
        } else if (k.typ == K1_KOM_ZASOBY_BATERIA) {
            char opis[96];
            int nowa = !bat.znana || bat.obecna != k.a || bat.stan != k.c;
            bat.znana = 1;
            bat.obecna = k.a;
            bat.procent = k.b;
            bat.stan = k.c;
            bat.moc_mw = k.d;
            bat.minuty = k.e;
            bateria_opis(opis, sizeof opis);
            if (nowa) /* once per state, not every second */
                k1_log(TAG, "bateria: %s", opis);
        } else {
            uslugi[k.obiekt].cpu = k.a;
            uslugi[k.obiekt].kib = k.b;
            uslugi[k.obiekt].dziala = k.c;
        }
        zmiana = 1;
    }
    if (zmiana)
        k1_apl_odswiez(&apl);
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_ZASOBY;
    apl.szer = 560;
    /* charts and the battery (356 px), a row per service that can run at once (the password screen and
     * the desktop never do), the footer */
    apl.wys = 356 + (K1_PROFIL_MAKS - 1) * WIERSZ + 34;
    apl.rysuj = rysuj;
    apl.kanaly[0].fd = FD_NADZORCA;
    apl.kanaly[0].dane = odbierz;
    k1_apl_uruchom(&apl);
}

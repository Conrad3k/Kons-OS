/* K1 Zasoby — native application: processor and memory use of the whole system over the last
 * minute and of every service (M4.1); the battery with the power draw and the time left (0.6.3); every running
 * session's memory — what it uses of what it may get (0.10.1 step 3, the owner's D3: sketch B "paski sesji").
 * 0.10.10 step 2 (the owner's choice of 2026-11-20): two tabs — "Procesor i pamięć" (all the above) and "Karta
 * graficzna" (karta.c: how busy the card is, its clock, the power draw of the card and of the laptop).
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

#include "k1_liczba.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_opis_klient.h"
#include "k1_odbior.h"
#include "zasoby.h"

#define FD_NADZORCA 4
#define WIERSZ 22
/* rows of services at most (0.8.2: with Podgląd a row for each would not fit 1024 × 768; 0.10.10: 12 — the tabs) */
#define WIERSZE_MAKS 12
#define ZAKL_Y (K1_OKNO_TYTUL + 10)
#define ZAKL_H 32

struct k1_apl apl;
static int zakladka, najechana = -1; /* 0: Procesor i pamięć, 1: Karta graficzna */
static const char *const zakladki[] = { "Procesor i pamięć", "Karta graficzna" };
static int32_t cpu[PROBKI], pam[PROBKI]; /* ‰ of all CPUs, ‰ of the memory */
static int probek;                        /* how many are valid (≤ PROBKI) */
static int32_t pam_uzyta, pam_calk, procesory, czas_pracy;
static struct {
    int32_t cpu, kib, dziala;
} uslugi[K1_PROFIL_MAKS + 1];
static struct {
    int32_t znana, obecna, procent, stan, moc_mw, minuty;
} bat;
struct zasoby_strefa strefy[K1_STREFY_MAKS + 1];
static unsigned long odrzucone;
static int pierwsze = 1;
#define SESJA_H 46

/* The running zones nadzorca told us about (their VMM's profile runs). */
static int sesje(int *nr)
{
    int n = 0;
    for (int z = 1; z <= K1_STREFY_MAKS; z++)
        if (strefy[z].znana && uslugi[k1_strefa(z)->profil].dziala)
            nr[n++] = z;
    return n;
}

/* A changing place (0.10.2 step 3, k1_opis.h K1_EL_ZMIENNE): the reference screenshots leave it out. */
void zmienna(int32_t x, int32_t y, int32_t w, int32_t h, const char *nazwa)
{
    k1_el(K1_EL_ZMIENNE, nazwa, x, y, w, h, 0);
}

/* The sessions: a bar each in its colour — what it uses of what it may get; returns the new y. */
static int32_t rysuj_sesje(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t w)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr), przegladarka = 1;
    char a[24], b[24], buf[64];
    for (int i = 0; i < n; i++)
        przegladarka &= strefy[nr[i]].nazwa[0] != '\0';
    if (n == 0)
        return y;
    zmienna(MARG, y, w, 28 + n * SESJA_H + 24, "Sesje teraz"); /* which run and how much memory each uses */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, przegladarka ? "Sesje przeglądarki" : "Strefy", k1_kolor(m->tekst, 255), 0);
    y += 28;
    for (int i = 0; i < n; i++, y += SESJA_H) {
        const struct k1_strefa_opis *z = k1_strefa(nr[i]);
        int32_t moze = strefy[nr[i]].moze > 0 ? strefy[nr[i]].moze : 1, u = strefy[nr[i]].uzywa;
        k1_kolo(o, (float)MARG + 5, (float)y + 8, 4, k1_kolor(z->kolor, 255));
        k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + 16, y + 13, strefy[nr[i]].nazwa[0] ? strefy[nr[i]].nazwa : z->tytul,
                 k1_kolor(m->tekst, 255), w / 2);
        k1_pamiec_tekst(a, sizeof a, u);
        k1_pamiec_tekst(b, sizeof b, strefy[nr[i]].moze);
        snprintf(buf, sizeof buf, "%s z %s", a, b);
        k1_tekst(o, K1_CZ_MALA, MARG + w - k1_tekst_szer(K1_CZ_MALA, buf), y + 13, buf, k1_kolor(m->tekst2, 255), 0);
        k1_zaokraglony(o, MARG, (float)y + 22, (float)w, 10, 5, k1_kolor(m->tekst, 22));
        k1_zaokraglony(o, MARG, (float)y + 22, (float)w * (float)(u < moze ? u : moze) / (float)moze, 10, 5,
                       k1_kolor(z->kolor, 255));
    }
    return y;
}

/* Rows of services the sessions leave room for. */
static int wiersze_uslug(void)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr), zajete = n ? (28 + n * SESJA_H + 24 + WIERSZ - 1) / WIERSZ : 0;
    return WIERSZE_MAKS - zajete > 3 ? WIERSZE_MAKS - zajete : 3;
}


void ramka_wykresu(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h)
{
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 8, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)x, (float)y, (float)w, (float)h, 8, 1.0f, k1_kolor(m->tekst, 20));
    k1_prostokat(o, x + 8, y + h / 2, w - 16, 1, k1_kolor(m->tekst, 14));
}

/* One series, one hue: a soft area under a 2-px line, a hairline grid at 50 %. */
static void wykres(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h,
                   const int32_t *v)
{
    int i;
    float krok = (float)(w - 16) / (float)(PROBKI - 1), x0 = (float)x + 8, y0 = (float)y + 8, wys = (float)(h - 16);
    ramka_wykresu(o, m, x, y, w, h);
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
    const char *s = k1_bateria_stan(bat.stan); /* the same words as Ustawienia → Bateria */
    int laduje = bat.stan == K1_BAT_LADOWANIE;
    k1_dziesietne(w, sizeof w, ((long)bat.moc_mw + 50) / 100);
    k1_minuty(c, sizeof c, bat.minuty);
    if (!bat.obecna) {
        snprintf(buf, n, "Brak baterii, zasilanie z sieci");
    } else if ((laduje || bat.stan == K1_BAT_ROZLADOWANIE) && bat.moc_mw >= 0) {
        if (bat.minuty < 0)
            snprintf(buf, n, "%s · %s W", s, w);
        else
            snprintf(buf, n, "%s · %s W · %s ok. %s", s, w, laduje ? "do pełna" : "zostało", c);
    } else {
        snprintf(buf, n, "%s", s);
    }
}

static void rysuj_uslugi(struct k1_obraz *o, const struct k1_motyw *m, int32_t y);

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char a[40], b[40], buf[96];
    int32_t y = ZAKL_Y + ZAKL_H + 10, w = apl.szer - 2 * MARG;
    int32_t ostatni_cpu = probek ? cpu[PROBKI - 1] : 0;
    static int zalogowane;

    /* 0.10.10 step 2: the tabs */
    k1_segment(o, m, MARG, ZAKL_Y, w, ZAKL_H, zakladki, 2, zakladka, najechana);
    if (!zalogowane) { /* the tests click the tabs by this line */
        zalogowane = 1;
        k1_log(TAG, "zakładki: %d,%d i %d,%d", MARG + w / 4, ZAKL_Y + ZAKL_H / 2, MARG + 3 * w / 4, ZAKL_Y + ZAKL_H / 2);
    }
    if (zakladka == 1) {
        rysuj_karte(o, m, y);
        return;
    }

    /* Processor: a headline number, then the last minute */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Procesor", k1_kolor(m->tekst, 255), 0);
    k1_dziesietne(a, sizeof a, ostatni_cpu);
    snprintf(buf, sizeof buf, "%s %%", a);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + w - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), y + 16, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(buf, sizeof buf, "%d %s, ostatnia minuta", (int)procesory,
             procesory == 1 ? "rdzeń" : (procesory % 10 >= 2 && procesory % 10 <= 4 && (procesory % 100 < 12 || procesory % 100 > 14) ? "rdzenie" : "rdzeni"));
    k1_tekst(o, K1_CZ_MALA, MARG + 84, y + 16, procesory ? buf : "", k1_kolor(m->tekst3, 255), 0);
    wykres(o, m, MARG, y + 26, w, WYKRES_H, cpu);
    zmienna(MARG + 260, y, w - 260, 22, "Procesor teraz"); /* 0.10.2 step 3: the numbers and charts change */
    zmienna(MARG, y + 24, w, WYKRES_H + 4, "Wykres procesora");
    y += 26 + WYKRES_H + 16;

    /* Memory */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Pamięć", k1_kolor(m->tekst, 255), 0);
    k1_pamiec_tekst(a, sizeof a, pam_uzyta);
    k1_pamiec_tekst(b, sizeof b, pam_calk);
    snprintf(buf, sizeof buf, "%s z %s", a, b);
    if (pam_calk > 0)
        k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + w - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), y + 16, buf,
                 k1_kolor(m->tekst, 255), 0);
    wykres(o, m, MARG, y + 26, w, WYKRES_H, pam);
    zmienna(MARG + 120, y, w - 120, 22, "Pamięć teraz");
    zmienna(MARG, y + 24, w, WYKRES_H + 4, "Wykres pamięci");
    y += 26 + WYKRES_H + 20;

    /* Battery (0.6.3): percent, power draw now, time left at that rate */
    if (bat.znana) {
        zmienna(MARG + 80, y, w - 80, 22, "Bateria teraz");
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

    rysuj_uslugi(o, m, rysuj_sesje(o, m, y, w)); /* 0.10.1 step 3: the sessions above the services */
}

/* Services: name, processor, memory; the time since start at the bottom. */
static void rysuj_uslugi(struct k1_obraz *o, const struct k1_motyw *m, int32_t y)
{
    char a[40], buf[96];
    int32_t w = apl.szer - 2 * MARG, p, dzialaja, wiersz, maks;
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Usługi i aplikacje", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + w - 200, y + 16, "procesor", k1_kolor(m->tekst3, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + w - 72, y + 16, "pamięć", k1_kolor(m->tekst3, 255), 0);
    y += 24;
    zmienna(MARG, y, w, apl.wys - y, "Usługi i aplikacje teraz"); /* which run, their numbers, the time since start */
    k1_prostokat(o, MARG, y, w, 1, k1_kolor(m->tekst, 20));
    for (p = 1, dzialaja = 0; p <= K1_PROFIL_MAKS; p++)
        dzialaja += uslugi[p].dziala != 0;
    for (p = 1, wiersz = 0, maks = wiersze_uslug(); p <= K1_PROFIL_MAKS; p++) {
        if (!uslugi[p].dziala)
            continue;
        if (++wiersz == maks && dzialaja > maks) { /* the last row says how many more */
            snprintf(buf, sizeof buf, "… i jeszcze %d", dzialaja - maks + 1);
            k1_tekst(o, K1_CZ_MALA, MARG, y + 16, buf, k1_kolor(m->tekst3, 255), 0);
            break;
        }
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
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_STREFY_STAN || k.typ == K1_KOM_STREFY_GRAFIKA))
            continue; /* after the names: nothing Zasoby shows */
        if (w == K1_ODB_JEST && k.typ == K1_KOM_ZASOBY_KARTA) { /* 0.10.10 step 2: the laptop's draw on the battery */
            karta_dane(&k, bat.obecna && bat.stan == K1_BAT_ROZLADOWANIE ? bat.moc_mw : -1);
            zmiana = 1;
            continue;
        }
        if (w != K1_ODB_JEST ||
            (k.typ != K1_KOM_ZASOBY_SYSTEM && k.typ != K1_KOM_ZASOBY_USLUGA && k.typ != K1_KOM_ZASOBY_BATERIA &&
             k.typ != K1_KOM_STREFA_PRZYDZIAL && k.typ != K1_KOM_STREFA_NAZWA)) {
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
        } else if (k.typ == K1_KOM_STREFA_PRZYDZIAL) { /* 0.10.1 step 3: a running zone's share */
            if (!strefy[k.obiekt].znana) /* once a zone: the tests read it */
                k1_log(TAG, "strefa %u: używa %d MiB z %d MiB", k.obiekt, (int)k.a, (int)k.b);
            strefy[k.obiekt].uzywa = k.a, strefy[k.obiekt].moze = k.b, strefy[k.obiekt].znana = 1;
        } else if (k.typ == K1_KOM_STREFA_NAZWA) {
            const int32_t n[5] = { k.a, k.b, k.c, k.d, k.e }; /* checked: k1_strefa_nazwa_dobra */
            memcpy(strefy[k.obiekt].nazwa, n, K1_STREFA_NAZWA_MAKS);
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

/* 0.10.10 step 2: the tabs under the pointer and their click. */
static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int s = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_segment_trafienie(MARG, ZAKL_Y, apl.szer - 2 * MARG, ZAKL_H, 2, x, y);
    if (s != najechana) {
        najechana = s;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || s < 0 || s == zakladka)
        return;
    zakladka = s;
    k1_log(TAG, "zakładka: %s", zakladki[s]);
    k1_apl_odswiez(&apl);
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_ZASOBY;
    apl.szer = 560;
    /* the tabs (40 px), charts and the battery (356 px), a row per service running (at most WIERSZE_MAKS: the window
     * fits a screen of 1024 × 768 below the top bar), the footer */
    apl.wys = 40 + 356 + WIERSZE_MAKS * WIERSZ + 34;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.kanaly[0].fd = FD_NADZORCA;
    apl.kanaly[0].dane = odbierz;
    k1_apl_uruchom(&apl);
}

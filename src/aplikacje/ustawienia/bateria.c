/* K1 Ustawienia — Bateria (0.5.5): state and charge history since boot from the supervisor (fd 4, one way,
 * sender UID 0 checked), earlier boots as a sealed memfd (0.6.0). */
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "k1_bat_arch.h"
#include "k1_czas.h"
#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "ustawienia.h"

#define WYKRES_Y_OD 40 /* below seg_y() */
#define WYKRES_H 150

static struct {
    int znana, obecna, procent, stan, zasilacz;
} bat;
static uint8_t hist[K1_BAT_HIST_MAKS];
static int hist_n, hist_interwal = 30, kanal_nadzorcy = 1, odrzucone;
static struct k1_bat_arch arch; /* earlier boots (0.6.0), from the supervisor as a sealed memfd */
static int arch_znane;

static void odbierz_archiwum(const struct k1_komunikat *k, int fd)
{
    static uint8_t buf[K1_BAT_ARCH_BAJTY];
    if (k->a <= (int32_t)sizeof buf && k1_memfd_czytaj(fd, buf, (size_t)k->a) == 0 &&
        k1_bat_arch_czytaj(buf, (size_t)k->a, &arch) == 0) {
        arch_znane = 1;
        k1_log(TAG, "bateria: poprzednich uruchomień %d", arch.n);
    } else {
        k1_log(TAG, "bateria: odrzucono historię poprzednich uruchomień");
    }
}

/* Reads what the supervisor sent; returns 1 if the battery section must be redrawn. */
int bateria_odbierz(void)
{
    int zmiana = 0, i, j;
    for (i = 0; i < 128 && kanal_nadzorcy; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            kanal_nadzorcy = 0; /* closed or missing: the rest of the settings work without it */
            apl.kanaly[0].fd = -1;
            break;
        }
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_SIEC_STAN || k.typ == K1_KOM_SIEC_KARTA ||
                                k.typ == K1_KOM_SIEC_SZCZEGOLY || k.typ == K1_KOM_SIEC_STEROWNIK || k.typ == K1_KOM_SIEC_ADRESY)) {
            siec_odbierz(&k); /* the same channel (0.9.0 step 7) */
            zmiana = 1;
            continue;
        }
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_WIFI_STAN || k.typ == K1_KOM_WIFI_SIEC || k.typ == K1_KOM_WIFI_NAZWA)) {
            wifi_odbierz(&k); /* 0.9.1: Wi-Fi (wifi.c) */
            zmiana = 1;
            continue;
        }
        if (w != K1_ODB_JEST ||
            (k.typ != K1_KOM_BATERIA && k.typ != K1_KOM_BATERIA_HIST && k.typ != K1_KOM_BATERIA_ARCH)) {
            if (fd >= 0)
                close(fd);
            if (odrzucone++ < 3)
                k1_log(TAG, "odrzucono komunikat od nadzorcy");
            continue;
        }
        if (k.typ == K1_KOM_BATERIA_ARCH) {
            odbierz_archiwum(&k, fd);
            close(fd);
        } else if (k.typ == K1_KOM_BATERIA) {
            bat.znana = 1;
            bat.obecna = k.a;
            bat.procent = k.b;
            bat.stan = k.c;
            bat.zasilacz = k.d;
        } else {
            if (k.obiekt == 0)
                hist_n = 0; /* the history starts again (compacted by the supervisor) */
            hist_interwal = k.a;
            for (j = 0; j < K1_BAT_HIST_W_KOM; j++) {
                unsigned v = k1_bat_probka(&k, j), idx = k.obiekt + (unsigned)j;
                if (v == K1_BAT_BRAK_PROBKI || idx >= K1_BAT_HIST_MAKS)
                    break;
                hist[idx] = (uint8_t)v;
                if ((int)idx + 1 > hist_n)
                    hist_n = (int)idx + 1;
            }
        }
        zmiana = 1;
    }
    return zmiana;
}

static void wykres_prost(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = TRESC_X;
    *y = seg_y() + WYKRES_Y_OD;
    *w = apl.szer - TRESC_X - 24;
    *h = WYKRES_H;
}

/* One series, one hue (like Zasoby): a soft area under a 2-px accent line, a hairline at 50 %;
 * under the chart a thin strip where the computer was on mains. Left edge = boot, right = now. */
static void wykres_baterii(struct k1_obraz *o, const struct k1_motyw *m)
{
    int32_t x, y, w, h, i;
    float x0, y0, wys, krok;
    wykres_prost(&x, &y, &w, &h);
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 8, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)x, (float)y, (float)w, (float)h, 8, 1.0f, k1_kolor(m->tekst, 20));
    k1_prostokat(o, x + 8, y + h / 2, w - 16, 1, k1_kolor(m->tekst, 14));
    k1_tekst(o, K1_CZ_MALA, x + w - 8 - k1_tekst_szer(K1_CZ_MALA, "50 %"), y + h / 2 - 5, "50 %", k1_kolor(m->tekst3, 255), 0);
    if (hist_n < 2) {
        k1_tekst(o, K1_CZ_MALA, x + 16, y + h / 2 + 20, "Zbieram dane — pomiar co pół minuty…", k1_kolor(m->tekst3, 255), 0);
        return;
    }
    x0 = (float)x + 8;
    y0 = (float)y + 10;
    wys = (float)(h - 20);
    krok = (float)(w - 16) / (float)(hist_n - 1);
    for (i = 0; i < hist_n; i++) {
        float px = x0 + (float)i * krok, py = y0 + wys - wys * (float)(hist[i] & 0x7f) / 100.0f;
        if (i < hist_n - 1) {
            int32_t sx = (int32_t)px, sw = (int32_t)(px + krok) - sx, sy = (int32_t)py;
            k1_prostokat(o, sx, sy, sw, (int32_t)(y0 + wys) - sy, k1_kolor(m->akcent, 36));
            if (hist[i] & K1_BAT_ZASILACZ) /* on mains: strip under the chart */
                k1_prostokat(o, sx, y + h + 6, sw, 3, k1_kolor(m->tekst2, 160));
        }
        if (i > 0)
            k1_kreska(o, px - krok, y0 + wys - wys * (float)(hist[i - 1] & 0x7f) / 100.0f, px, py, 2.0f,
                      k1_kolor(m->akcent, 255));
    }
}

/* The last earlier boot in one line — when, how long, from … to … % — with a small sparkline
 * (0.6.0: the history is kept in skarbiec between boots). */
static void poprzednie(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w)
{
    const struct k1_bat_sesja *s;
    struct k1_data d;
    char buf[160], a[32];
    int32_t sx = x + w - 96, i;
    if (!arch_znane || arch.n == 0) {
        k1_tekst(o, K1_CZ_MALA, x, y,
                 arch_znane ? "Poprzednie uruchomienia pojawią się tutaj (zapis na dysku K1)."
                            : "Historia z poprzednich uruchomień — gdy dane są zapisywane na dysku K1.",
                 k1_kolor(m->tekst3, 255), 0);
        return;
    }
    s = &arch.s[arch.n - 1];
    k1_czas_warszawa((int64_t)s->start_min * 60, &d);
    k1_minuty(a, sizeof a, (long)(s->n - 1) * s->interwal / 60);
    snprintf(buf, sizeof buf, "Poprzednio: %s %d:%02d · %s · %d %% → %d %%%s", k1_dzien_skrot(d.dzien_tygodnia), d.godzina,
             d.minuta, a, arch.p[s->od] & 0x7f, arch.p[s->od + s->n - 1] & 0x7f, arch.n > 1 ? " · i starsze" : "");
    k1_tekst(o, K1_CZ_MALA, x, y, buf, k1_kolor(m->tekst2, 255), sx - x - 12);
    for (i = 1; i < s->n && s->n >= 2; i++) { /* sparkline 96 × 16 */
        float x0 = (float)sx + 96.0f * (float)(i - 1) / (float)(s->n - 1), x1 = (float)sx + 96.0f * (float)i / (float)(s->n - 1);
        float y0 = (float)y - 2 - 16.0f * (float)(arch.p[s->od + i - 1] & 0x7f) / 100.0f;
        float y1 = (float)y - 2 - 16.0f * (float)(arch.p[s->od + i] & 0x7f) / 100.0f;
        k1_kreska(o, x0, y0, x1, y1, 1.5f, k1_kolor(m->akcent, 200));
    }
}

void bateria_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[96], a[32];
    int32_t x, y, w, h, i, najnizej = 100;
    naglowek(o, m, "Bateria", "Poziom naładowania od uruchomienia systemu");
    if (!bat.znana || !bat.obecna) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16,
                 bat.znana ? "Ten komputer nie ma baterii (albo system jej nie widzi)." : "Czekam na dane…",
                 k1_kolor(m->tekst2, 255), 0);
        return;
    }
    if (bat.procent >= 0)
        snprintf(buf, sizeof buf, "%d %%", bat.procent);
    else
        snprintf(buf, sizeof buf, "— %%");
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, seg_y() + 18, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(a, sizeof a, "%s", k1_bateria_stan(bat.stan));
    snprintf(buf, sizeof buf, "%s%s", a,
             (bat.stan == K1_BAT_LADOWANIE || bat.stan == K1_BAT_PELNA) && bat.zasilacz ? " · zasilacz podłączony" : "");
    k1_tekst(o, K1_CZ_TEKST, TRESC_X + 88, seg_y() + 16, buf, k1_kolor(m->tekst2, 255), 0);
    wykres_baterii(o, m);
    wykres_prost(&x, &y, &w, &h);
    /* time axis: boot … now; summary */
    if (hist_n >= 2) {
        long trwa = (long)(hist_n - 1) * hist_interwal;
        struct timespec t;
        struct k1_data d;
        clock_gettime(CLOCK_REALTIME, &t);
        k1_czas_warszawa((int64_t)t.tv_sec - trwa, &d);
        snprintf(buf, sizeof buf, "%d:%02d", d.godzina, d.minuta);
        k1_tekst(o, K1_CZ_MALA, x, y + h + 26, buf, k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, x + w - k1_tekst_szer(K1_CZ_MALA, "teraz"), y + h + 26, "teraz", k1_kolor(m->tekst3, 255), 0);
        k1_prostokat(o, x + w / 2 - 60, y + h + 20, 12, 3, k1_kolor(m->tekst2, 160));
        k1_tekst(o, K1_CZ_MALA, x + w / 2 - 42, y + h + 26, "zasilacz podłączony", k1_kolor(m->tekst3, 255), 0);
        for (i = 0; i < hist_n; i++)
            if ((int32_t)(hist[i] & 0x7f) < najnizej)
                najnizej = hist[i] & 0x7f;
        k1_minuty(a, sizeof a, (long)trwa / 60);
        snprintf(buf, sizeof buf, "Od uruchomienia: %s · najniżej %d %%", a, (int)najnizej);
        k1_tekst(o, K1_CZ_MALA, x, y + h + 50, buf, k1_kolor(m->tekst2, 255), 0);
    }
    poprzednie(o, m, x, apl.wys - 18, w);
}

void bateria_loguj(void)
{
    int32_t wx, wy, ww, wh;
    wykres_prost(&wx, &wy, &ww, &wh);
    k1_log(TAG, "bateria: %s, %d%%, próbek %d co %d s; wykres %d,%d %dx%d", bat.obecna ? "jest" : "brak", bat.procent,
           hist_n, hist_interwal, wx, wy, ww, wh);
}

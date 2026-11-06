/* K1 Zegar — native application: an analog dial and the time and date for Poland (M4, ADR 0008).
 * Sandbox: UID 200, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <math.h>
#include <stdio.h>
#include <time.h>

#include "k1_aplikacja.h"
#include "k1_czas.h"

#define TAG "K1-ZEGAR"

static struct k1_apl apl;
static time_t ostatnia;

static void wskazowka(struct k1_obraz *o, float cx, float cy, float kat, float dl, float ogon, float g, uint32_t kolor)
{
    float s = sinf(kat), c = cosf(kat);
    k1_kreska(o, cx - s * ogon, cy + c * ogon, cx + s * dl, cy - c * dl, g, kolor);
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct timespec t;
    struct k1_data d;
    char buf[64];
    const float cx = 140, cy = 44 + 128, r = 92, pi = 3.14159265f;
    int i;
    int32_t x = 272;

    clock_gettime(CLOCK_REALTIME, &t);
    k1_czas_warszawa((int64_t)t.tv_sec, &d);
    /* dial: the one rich element of the window */
    k1_kolo(o, cx, cy, r, k1_kolor(m->pole, 255));
    k1_okrag(o, cx, cy, r, 1.0f, k1_kolor(m->tekst, 30));
    for (i = 0; i < 60; i++) {
        float a = (float)i * pi / 30.0f, dl = i % 15 == 0 ? 12.0f : (i % 5 == 0 ? 8.0f : 3.0f);
        float g = i % 5 == 0 ? 2.0f : 1.0f;
        k1_kreska(o, cx + sinf(a) * (r - 8 - dl), cy - cosf(a) * (r - 8 - dl), cx + sinf(a) * (r - 8),
                  cy - cosf(a) * (r - 8), g, k1_kolor(i % 5 == 0 ? m->tekst2 : m->tekst3, 255));
    }
    wskazowka(o, cx, cy, ((float)(d.godzina % 12) + (float)d.minuta / 60.0f) * pi / 6.0f, 46, 8, 5, k1_kolor(m->tekst, 255));
    wskazowka(o, cx, cy, ((float)d.minuta + (float)d.sekunda / 60.0f) * pi / 30.0f, 70, 10, 3.5f, k1_kolor(m->tekst, 255));
    wskazowka(o, cx, cy, (float)d.sekunda * pi / 30.0f, 78, 16, 1.5f, k1_kolor(m->akcent, 255));
    k1_kolo(o, cx, cy, 4.5f, k1_kolor(m->akcent, 255));

    snprintf(buf, sizeof buf, "%d:%02d", d.godzina, d.minuta);
    x += k1_tekst(o, K1_CZ_ZEGAR, x, 44 + 118, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(buf, sizeof buf, ":%02d", d.sekunda);
    k1_tekst(o, K1_CZ_TYTUL, x + 4, 44 + 118, buf, k1_kolor(m->tekst3, 255), 0);
    snprintf(buf, sizeof buf, "%s, %d %s %d", k1_dzien_tygodnia(d.dzien_tygodnia), d.dzien, k1_miesiac_dopelniacz(d.miesiac),
             d.rok);
    k1_tekst(o, K1_CZ_TEKST, 274, 44 + 156, buf, k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, 274, 44 + 180, d.letni ? "Polska, czas letni (UTC+2)" : "Polska, czas zimowy (UTC+1)",
             k1_kolor(m->tekst3, 255), 0);
}

static int zegar(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    if (t.tv_sec != ostatnia) {
        ostatnia = t.tv_sec;
        k1_apl_odswiez(&apl);
    }
    return (int)(1000 - t.tv_nsec / 1000000) + 2;
}

int main(void)
{
    apl.tag = TAG;
    apl.tytul = "Zegar";
    apl.szer = 540;
    apl.wys = 316;
    apl.rysuj = rysuj;
    apl.zegar = zegar;
    k1_apl_uruchom(&apl);
}

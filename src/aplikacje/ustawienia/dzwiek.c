/* K1 Ustawienia — Dźwięk (0.12.0 step 2): where the sound goes now (Głośniki / Słuchawki — the card says what is in the
 * headphone jack, it switches by itself), the mute and the volume of that output, "Odtwórz próbę". Ustawienia is a steering
 * client of the sound service like gui (przybornik k1_dzwiek_klient — one way for both): its channel comes from nadzorca
 * on fd 4 (DZWIEK_KANAL; again after the service starts anew). gui remembers the volume (D5): it hears every change.
 * Part of Ustawienia (ustawienia.h). */
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "k1_dzwiek_klient.h"
#include "k1_log.h"
#include "ustawienia.h"

#define WIERSZ_Y (seg_y() + 64) /* the mute and the slider */
#define SUWAK_X (TRESC_X + 52)
#define SUWAK_W 280
#define PROBA_Y (WIERSZ_Y + 64)

static struct k1_dzw_polaczenie dz = K1_DZW_POLACZENIE_START(TAG);
static int hover = -1, wcisniete = -1;
enum { C_WYCISZ = 0, C_SUWAK, C_PROBA };

static struct k1_przycisk przycisk_proby(void)
{
    struct k1_przycisk p = { TRESC_X, PROBA_Y, 180, 36, "Odtwórz próbę", K1_PRZ_ZWYKLY };
    return p;
}

void dzwiek_kanal(int fd)
{
    k1_dzwk_kanal(&dz, fd);
    apl.kanaly[3].fd = dz.fd >= 0 && fcntl(dz.fd, F_SETFL, O_NONBLOCK) == 0 ? dz.fd : -1;
    k1_apl_odswiez(&apl);
}

void dzwiek_dane(void)
{
    int r = k1_dzwk_odbierz(&dz);
    if (r & K1_DZWK_KONIEC)
        apl.kanaly[3].fd = -1; /* nadzorca gives the next one */
    if (r && sekcja == S_DZWIEK)
        k1_apl_odswiez(&apl);
}

static void bez_karty(struct k1_obraz *o, const struct k1_motyw *m)
{
    const char *t = dz.karta == K1_DZW_KARTA_SZUKA ? "Szukam karty dźwiękowej…"
                    : dz.karta == K1_DZW_KARTA_BRAK ? "Ten komputer nie ma karty dźwiękowej."
                                                    : "Karta dźwiękowa przestała grać.";
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 20, t, k1_kolor(m->tekst2, 255), 0);
}

void dzwiek_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p = przycisk_proby();
    char proc[16];
    naglowek(o, m, "Dźwięk");
    if (dz.karta != K1_DZW_KARTA_JEST) {
        bez_karty(o, m);
        return;
    }
    k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 14, "Wyjście", k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, seg_y() + 38, k1_dzw_wyjscie_nazwa(dz.wyjscie), k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 120, seg_y() + 38, "Słuchawki przejmują dźwięk same po podłączeniu.",
             k1_kolor(m->tekst3, 255), 0);
    k1_zaokraglony(o, (float)TRESC_X, (float)WIERSZ_Y, 36, 36, 18,
                   k1_kolor(dz.wyciszone ? m->akcent : hover == C_WYCISZ ? m->najechanie : m->pole, 255));
    if (!dz.wyciszone)
        k1_obrys(o, (float)TRESC_X, (float)WIERSZ_Y, 36, 36, 18, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, (enum k1_ikona)k1_dzwk_ikona(&dz), (float)(TRESC_X + 7), (float)(WIERSZ_Y + 7), 22,
             k1_kolor(dz.wyciszone ? m->na_akcencie : m->tekst, 255));
    k1_el(K1_EL_PRZYCISK, "Wycisz", TRESC_X, WIERSZ_Y, 36, 36, dz.wyciszone ? K1_EL_WYBRANY : 0);
    k1_suwak(o, m, (float)SUWAK_X, (float)(WIERSZ_Y + 18), SUWAK_W, dz.glosnosc, K1_DZW_GLOSNOSC_MAKS, dz.wyciszone,
             hover == C_SUWAK || wcisniete == C_SUWAK);
    k1_el(K1_EL_PRZYCISK, "Głośność", SUWAK_X, WIERSZ_Y + 6, SUWAK_W, 24, 0);
    snprintf(proc, sizeof proc, "%d %%", dz.glosnosc);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, SUWAK_X + SUWAK_W + 20, WIERSZ_Y + 24, proc,
             k1_kolor(dz.wyciszone ? m->tekst3 : m->tekst, 255), 0);
    k1_przycisk(o, m, &p, K1_CZ_TEKST, hover == C_PROBA, !dz.gra);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, PROBA_Y + 36 + 32, "K1 pamięta głośność głośników i słuchawek osobno.",
             k1_kolor(m->tekst3, 255), 0);
}

static int pod(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk_proby();
    int32_t dx = x - (TRESC_X + 18), dy = y - (WIERSZ_Y + 18);
    if (dz.karta != K1_DZW_KARTA_JEST || x < 0)
        return -1;
    if (dx * dx + dy * dy <= 18 * 18)
        return C_WYCISZ;
    if (x >= SUWAK_X - 10 && x <= SUWAK_X + SUWAK_W + 10 && y >= WIERSZ_Y + 6 && y <= WIERSZ_Y + 30)
        return C_SUWAK;
    return k1_przycisk_pod(&p, 1, x, y) == 0 ? C_PROBA : -1;
}

static void suwak_do(int32_t x)
{
    int v = k1_suwak_wartosc((float)SUWAK_X, SUWAK_W, x, K1_DZW_GLOSNOSC_MAKS);
    if (v != dz.glosnosc || dz.wyciszone)
        k1_dzwk_glosnosc(&dz, v, 0, K1_DZW_WYJSCIE_BRAK); /* moving the slider unmutes */
}

int dzwiek_wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int c = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y), zmiana = c != hover;
    hover = c;
    if (rodzaj == K1_WSK_WCISNIECIE && przycisk == K1_PRZYCISK_LEWY) {
        wcisniete = c;
        if (c == C_SUWAK)
            suwak_do(x);
        zmiana = 1;
    } else if (rodzaj == K1_WSK_RUCH && wcisniete == C_SUWAK) {
        suwak_do(x);
        zmiana = 1;
    } else if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY) {
        if (wcisniete == C_SUWAK)
            k1_log(TAG, "dźwięk: głośność %d%% (suwak)", dz.glosnosc);
        else if (c == C_WYCISZ && wcisniete == C_WYCISZ)
            k1_dzwk_glosnosc(&dz, dz.glosnosc, !dz.wyciszone, K1_DZW_WYJSCIE_BRAK);
        else if (c == C_PROBA && wcisniete == C_PROBA)
            k1_dzwk_proba(&dz);
        zmiana |= wcisniete >= 0;
        wcisniete = -1;
    }
    return zmiana;
}

void dzwiek_pokazany(void)
{
    hover = wcisniete = -1;
    k1_log(TAG, "dźwięk: karta %d, wyjście %d, głośność %d%%%s", dz.karta, dz.wyjscie, dz.glosnosc,
           dz.wyciszone ? ", wyciszone" : "");
}

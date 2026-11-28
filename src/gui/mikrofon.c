/* K1 gui — the microphone (0.13.5 step 1, ADR 0065; the owner's D2: A). While anything hears the microphone (the sound
 * service says so: DZWIEK_STAN e — K1, e.g. Ustawienia's "Powiedz coś"; step 2: DZWIEK_SESJA e — a session of the
 * browser), the top bar has the sign "mikrofon włączony": a terracotta pill of its own left of the network, the battery
 * and the sound — grey and crossed out while the microphone is muted. It is K1's (gui's panel, from the service, never
 * from whoever hears), so nothing that hears can hide it or fake it. A click opens its popover "Mikrofon": who hears it
 * now, and "Wycisz mikrofon" (the service then gives every listener silence). The key "microphone off" of a laptop
 * (KEY_MICMUTE) does the same, at any time. While gui's security screen (Ctrl+Alt+Del) is on top, the service gives
 * silence (WSTRZYMAJ, T4). Sessions' names are the user's text: never in the log. Part of gui (gui.h). */
#include <stdio.h>

#include "gui.h"
#include "k1_dzwiek_klient.h"
#include "k1_log.h"

#define P 16
#define LISTA_Y (MARG + 74) /* "Słucha teraz" and its rows */
#define WIERSZ 28
#define WYC_H 52            /* the row "Wycisz mikrofon" at the bottom */

static int hover = -1, wcisniete = -1, slyszy; /* the mute's button under the pointer / pressed; the sign shown */
enum { C_WYCISZ = 0 };

/* Who hears now: K1 (one row) and each session that does (step 2). */
static int kto(int *strefy)
{
    const struct k1_dzw_polaczenie *dz = dzwiek_pol();
    int n = 0;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++)
        if (dz->sesje[nr].mik & K1_DZW_SES_SLUCHA)
            strefy[n++] = nr;
    return n;
}

int32_t mikrofon_okienko_wys(void)
{
    int strefy[K1_STREFY_MAKS];
    int n = kto(strefy) + (dzwiek_pol()->mik.slucha_k1 != 0);
    return LISTA_Y - MARG + 24 + WIERSZ * (n > 0 ? n : 1) + WYC_H;
}

int32_t mikrofon_szer(void)
{
    return k1_dzwk_mik_slucha(dzwiek_pol()) ? 32 : 0;
}

/* The service said something of the microphone: the sign appears or goes (log), the popover follows. */
void mikrofon_zmiana(void)
{
    int s = k1_dzwk_mik_slucha(dzwiek_pol());
    if (s != slyszy) {
        k1_log(TAG, "mikrofon: znak %s", s ? "pokazany (ktoś słucha)" : "zniknął");
        slyszy = s;
        if (!s)
            okienko_mikrofonu_zamknij(); /* nobody hears: the sign and its popover go */
    }
    okienko_paska_wysokosc();
    gora.brudna = okienko_paska.brudna = 1;
}

void mikrofon_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem)
{
    int wyc = dzwiek_pol()->mik.wyciszony;
    (void)krem;
    k1_zaokraglony(o, (float)x, 4, 32, 24, 12, wyc ? k1_kolor(K1_KREM, 56) : k1_kolor(K1_TERAKOTA, 255));
    k1_ikona(o, wyc ? K1_IK_MIKROFON_WYCISZONY : K1_IK_MIKROFON, (float)x + 8, 8, 16, k1_kolor(K1_KREM, 255));
}

void mikrofon_wycisz(void)
{
    struct k1_dzw_polaczenie *dz = dzwiek_pol();
    k1_dzwk_mikrofon(dz, dz->mik.poziom, !dz->mik.wyciszony);
    k1_log(TAG, "mikrofon: %s", dz->mik.wyciszony ? "wyciszony" : "słychać");
    gora.brudna = okienko_paska.brudna = 1;
}

void mikrofon_zabezpieczenia(int otwarty)
{
    k1_dzwk_mik_wstrzymaj(dzwiek_pol(), otwarty);
}

static void wiersz(struct k1_obraz *o, const struct k1_motyw *m, int i, uint32_t kropka, const char *tekst)
{
    int32_t y = LISTA_Y + 24 + WIERSZ * i + WIERSZ / 2;
    k1_kolo(o, MARG + P + 5, (float)y, 5, kropka);
    k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + P + 16, y + 5, tekst, k1_kolor(m->tekst, 255), MIKROFON_OKIENKO_SZER - 2 * P - 16);
}

void mikrofon_okienko(struct k1_obraz *o, const struct k1_motyw *m)
{
    const struct k1_dzw_polaczenie *dz = dzwiek_pol();
    int strefy[K1_STREFY_MAKS], n = kto(strefy), i = 0, wyc = dz->mik.wyciszony;
    int32_t y = mikrofon_okienko_wys() + MARG - WYC_H + 8;
    k1_ikona(o, wyc ? K1_IK_MIKROFON_WYCISZONY : K1_IK_MIKROFON, MARG + P, MARG + 16, 20, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 48, MARG + 31, "Mikrofon", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 52, dz->mik.zrodlo ? k1_dzw_zrodlo_nazwa(dz->mik.zrodlo) : "",
             k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + P, LISTA_Y + 16, "Słucha teraz", k1_kolor(m->tekst2, 255), 0);
    if (dz->mik.slucha_k1)
        wiersz(o, m, i++, k1_kolor(m->akcent, 255), "Ustawienia → Dźwięk");
    for (int j = 0; j < n; j++) {
        int nr = strefy[j];
        wiersz(o, m, i++, k1_kolor(k1_strefa(nr)->kolor, 255),
               nazwa_sesji(nr)[0] ? nazwa_sesji(nr) : k1_strefa(nr)->sesja[0] ? k1_strefa(nr)->sesja : k1_strefa(nr)->tytul);
    }
    if (i == 0)
        k1_tekst(o, K1_CZ_MALA, MARG + P, LISTA_Y + 24 + WIERSZ / 2 + 5, "Nikt nie słucha.", k1_kolor(m->tekst3, 255), 0);
    k1_prostokat(o, MARG + 8, y - 8, MIKROFON_OKIENKO_SZER - 16, 1, k1_kolor(m->tekst, 22));
    k1_zaokraglony(o, MARG + P, (float)y, 36, 36, 18,
                   k1_kolor(wyc ? m->akcent : hover == C_WYCISZ ? m->najechanie : m->pole, 255));
    if (!wyc)
        k1_obrys(o, MARG + P, (float)y, 36, 36, 18, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, wyc ? K1_IK_MIKROFON_WYCISZONY : K1_IK_MIKROFON, MARG + P + 8, (float)y + 8, 20,
             k1_kolor(wyc ? m->na_akcencie : m->tekst, 255));
    k1_el(K1_EL_PRZYCISK, "Wycisz mikrofon", MARG + P, y, 36, 36, wyc ? K1_EL_WYBRANY : 0);
    k1_tekst(o, K1_CZ_TEKST, MARG + P + 48, y + 23, wyc ? "Mikrofon wyciszony" : "Wycisz mikrofon",
             k1_kolor(m->tekst, 255), 0);
}

void mikrofon_okienko_wskaznik(const struct k1_komunikat *m)
{
    int32_t y = mikrofon_okienko_wys() + MARG - WYC_H + 8, dx = m->a - (MARG + P + 18), dy = m->b - (y + 18);
    int c = m->c != K1_WSK_WYJSCIE && dx * dx + dy * dy <= 18 * 18 ? C_WYCISZ : -1;
    if (c != hover) {
        hover = c;
        okienko_paska.brudna = 1;
    }
    if (m->d != K1_PRZYCISK_LEWY)
        return;
    if (m->c == K1_WSK_WCISNIECIE)
        wcisniete = c;
    if (m->c == K1_WSK_PUSZCZENIE) {
        if (c == C_WYCISZ && wcisniete == C_WYCISZ)
            mikrofon_wycisz();
        wcisniete = -1;
    }
}

void mikrofon_okienko_otwarte(int tak)
{
    hover = wcisniete = -1;
    if (tak)
        k1_log(TAG, "okienko mikrofonu: %s", dzwiek_pol()->mik.wyciszony ? "wyciszony" : "słychać");
}

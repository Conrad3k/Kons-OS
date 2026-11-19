/* K1 Ustawienia — Aktualizacje through the network (0.10.5 step 2b, ADR 0046; the owner's D1 sketch A, D4, D5, D7, D13):
 * on a disk with K1 — "Sprawdzono dziś o 14:05 — masz najnowszą wersję", "Pobieram K1 0.10.6… 45 %", "K1 0.10.6 jest
 * gotowa" with what it changes (the announcement, k1_ogl_wybierz) and "Zainstaluj i uruchom ponownie", "Sprawdź teraz",
 * the switch "Sprawdzaj sam", "Wróć do poprzedniej wersji" (asked once more before it is sent); on a stick — "nagraj
 * nowy pendrive" (D5). From nadzorca on fd 7: AKT_STAN, AKT_ETAP, AKT_OPIS (a sealed memfd, read again with
 * k1_ogloszenie_czytaj: fail closed). To it: AKT_PROSBA. Part of Ustawienia (ustawienia.h). */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_czas.h"
#include "k1_log.h"
#include "k1_manifest.h"
#include "k1_memfd.h"
#include "k1_ogloszenie.h"
#include "ustawienia.h"

#define PRZ_H 36
#define WIERSZ 18

static struct k1_komunikat st, et; /* AKT_STAN, AKT_ETAP (typ 0: none yet) */
static struct k1_ogloszenie ogl;
static int ogl_jest, pytanie_wroc, hover = -1, zalogowany = -1;
enum { P_GLOWNY, P_SAM, P_WROC, P_LICZBA };
static int32_t prz_y, sam_y; /* where the drawing put the button and the switch (the clicks use them) */

int akt_siec_komunikat(const struct k1_komunikat *k, int fd)
{
    static uint8_t b[K1_OGL_MAKS];
    if (k->typ == K1_KOM_AKT_STAN) {
        st = *k;
    } else if (k->typ == K1_KOM_AKT_ETAP) {
        et = *k;
        if (k->a != K1_AKT_E_NIC)
            pytanie_wroc = 0;
    } else if (k->typ == K1_KOM_AKT_OPIS) {
        ogl_jest = fd >= 0 && k1_memfd_czytaj(fd, b, (size_t)k->a) == 0 && k1_ogloszenie_czytaj(b, (size_t)k->a, &ogl) == 0;
        if (!ogl_jest)
            k1_log(TAG, "aktualizacje: opis wersji odrzucony");
    } else {
        return 0;
    }
    zalogowany = -1;
    return 1;
}

int akt_siec_widok(void)
{
    return st.typ == K1_KOM_AKT_STAN && (st.e & K1_AKT_FL_DYSK) && !(st.e & K1_AKT_FL_PROBA);
}

int akt_siec_pendrive(char *buf, size_t n) /* D5: the line under a stick's state, or 0 */
{
    char w[16];
    if (st.typ != K1_KOM_AKT_STAN || !(st.e & K1_AKT_FL_PENDRIVE) || st.c == 0)
        return 0;
    k1_wersja_tekst((uint32_t)st.c, w, sizeof w);
    snprintf(buf, n, "Jest nowsza wersja K1 %s. Nagraj nowy pendrive (README).", w);
    return 1;
}

enum widok { W_SPRAWDZA, W_POBIERA, W_INSTALUJE, W_GOTOWA, W_WROC, W_NAJNOWSZA, W_BRAK_SIECI, W_NIEUDANE, W_ODRZUCONE,
             W_NIGDY };
static const char *const nazwy[] = { "sprawdzam", "pobieram", "instaluję", "gotowa", "wrócić?", "najnowsza",
                                     "brak połączenia", "nie udało się", "odrzucona", "nie sprawdzano" };

static enum widok widok(void)
{
    if (et.a == K1_AKT_E_SPRAWDZA)
        return W_SPRAWDZA;
    if (et.a == K1_AKT_E_POBIERA)
        return W_POBIERA;
    if (et.a == K1_AKT_E_INSTALUJE || et.a == K1_AKT_E_WRACA)
        return W_INSTALUJE;
    if (pytanie_wroc && st.d != 0)
        return W_WROC;
    if (st.b != 0)
        return W_GOTOWA;
    return et.c == K1_AKT_W_OK ? W_NAJNOWSZA : et.c == K1_AKT_W_BRAK_SIECI ? W_BRAK_SIECI
           : et.c == K1_AKT_W_NIEUDANE ? W_NIEUDANE : et.c == K1_AKT_W_ODRZUCONE ? W_ODRZUCONE : W_NIGDY;
}

/* The main button of the view: its label, NULL none. */
static const char *etykieta(void)
{
    switch (widok()) {
    case W_GOTOWA: return "Zainstaluj i uruchom ponownie";
    case W_WROC: return "Wróć i uruchom ponownie";
    case W_SPRAWDZA:
    case W_POBIERA:
    case W_INSTALUJE: return NULL;
    default: return "Sprawdź teraz";
    }
}

static int przycisk(int i, struct k1_przycisk *p)
{
    static char wroc[48];
    char w[16];
    memset(p, 0, sizeof *p);
    if (i == P_GLOWNY) {
        p->etykieta = etykieta();
        p->styl = widok() == W_GOTOWA || widok() == W_WROC ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY;
    } else if (i == P_WROC && st.d != 0 && widok() != W_WROC && widok() != W_INSTALUJE && widok() != W_POBIERA) {
        k1_wersja_tekst((uint32_t)st.d, w, sizeof w);
        snprintf(wroc, sizeof wroc, "Wróć do poprzedniej wersji (%s)", w);
        p->etykieta = wroc;
        p->styl = K1_PRZ_ZWYKLY;
    }
    if (p->etykieta == NULL)
        return 0;
    p->x = i == P_GLOWNY ? TRESC_X : TRESC_X + k1_tekst_szer(K1_CZ_TEKST_MOCNA, etykieta() ? etykieta() : "") + 56;
    p->y = prz_y;
    p->h = PRZ_H;
    p->w = k1_tekst_szer(K1_CZ_TEKST_MOCNA, p->etykieta) + 40;
    return 1;
}

/* A paragraph broken into lines of at most `szer` px (at spaces); the y after it. */
static int32_t akapit(struct k1_obraz *o, int32_t y, const char *t, uint32_t kolor, int32_t szer)
{
    char w[K1_OGL_TEKST_MAKS + 1], prob[K1_OGL_TEKST_MAKS + 1];
    size_t n = 0, i = 0, dl = strlen(t);
    w[0] = '\0';
    while (i <= dl) {
        size_t j = i;
        while (j < dl && t[j] != ' ')
            j++;
        snprintf(prob, sizeof prob, "%s%s%.*s", w, n ? " " : "", (int)(j - i), t + i);
        if (n > 0 && k1_tekst_szer(K1_CZ_MALA, prob) > szer) {
            k1_tekst(o, K1_CZ_MALA, TRESC_X, y, w, kolor, szer);
            y += WIERSZ;
            snprintf(w, sizeof w, "%.*s", (int)(j - i), t + i);
        } else {
            memcpy(w, prob, strlen(prob) + 1);
        }
        n = 1;
        i = j + 1;
    }
    if (w[0] != '\0')
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y, w, kolor, szer), y += WIERSZ;
    return y;
}

/* What the waiting version changes (D13): each version newer than the running one, the newest first. */
static int32_t opisy(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t szer)
{
    struct k1_ogl_wybor w;
    char v[16];
    int i;
    if (!ogl_jest)
        return y;
    k1_ogl_wybierz(&ogl, (uint32_t)st.a, &w);
    for (i = 0; i < w.n && y < apl.wys - 120; i++) {
        k1_wersja_tekst(w.w[i].wersja, v, sizeof v);
        k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X, y, v, k1_kolor(m->tekst, 255), 0);
        y = akapit(o, y + WIERSZ, w.w[i].tekst, k1_kolor(m->tekst2, 255), szer) + 6;
    }
    if (w.wczesniejsze)
        y = akapit(o, y, "oraz wcześniejsze zmiany.", k1_kolor(m->tekst2, 255), szer);
    k1_el(K1_EL_ZMIENNE, "Opis wersji", TRESC_X - 4, y - 200 < 0 ? 0 : y - 200, szer + 8, 200, 0);
    return y;
}

static void postep(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t szer)
{
    int32_t p = et.b;
    k1_zaokraglony(o, (float)TRESC_X, (float)y, (float)szer, 8, 4, k1_kolor(m->tekst, 24));
    if (p > 0)
        k1_zaokraglony(o, (float)TRESC_X, (float)y, (float)(szer * p / 100 < 8 ? 8 : szer * p / 100), 8, 4,
                       k1_kolor(m->akcent, 255));
}

/* The first sentence and the small lines of the view; the y after them. */
static int32_t zdania(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t szer)
{
    char t[160], w[16], d[16];
    enum widok v = widok();
    struct k1_data kiedy;
    k1_wersja_tekst((uint32_t)(st.b ? st.b : st.c), w, sizeof w);
    k1_wersja_tekst((uint32_t)st.d, d, sizeof d);
    switch (v) {
    case W_SPRAWDZA: snprintf(t, sizeof t, "Sprawdzam, czy jest nowa wersja K1…"); break;
    case W_POBIERA: snprintf(t, sizeof t, "Pobieram K1 %s… %d %%", w, (int)et.b); break;
    case W_INSTALUJE: snprintf(t, sizeof t, "Zapisuję… %d %%. Potem komputer uruchomi się ponownie.", (int)et.b); break;
    case W_GOTOWA: snprintf(t, sizeof t, "K1 %s jest gotowa do instalacji.", w); break;
    case W_WROC: snprintf(t, sizeof t, "Wrócić do K1 %s? Komputer uruchomi się ponownie.", d); break;
    case W_NAJNOWSZA: /* 0.10.7 step 5: K1's own Polish time — musl's localtime_r opened /etc/localtime (code 159) */
        k1_czas_warszawa(et.d, &kiedy);
        snprintf(t, sizeof t, "Sprawdzono o %02d:%02d. Masz najnowszą wersję.", kiedy.godzina, kiedy.minuta);
        break;
    case W_BRAK_SIECI: snprintf(t, sizeof t, "Nie udało się sprawdzić (brak połączenia)."); break;
    case W_NIEUDANE: snprintf(t, sizeof t, "Nie udało się sprawdzić, czy jest nowa wersja."); break;
    case W_ODRZUCONE: snprintf(t, sizeof t, "Przyszło ogłoszenie bez podpisu K1. Odrzucone."); break;
    default: snprintf(t, sizeof t, "K1 sprawdza nowe wersje, gdy jest połączenie z internetem."); break;
    }
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, y, t, k1_kolor(m->tekst, 255), szer);
    if (v == W_POBIERA || v == W_INSTALUJE) {
        postep(o, m, y + 14, szer);
        k1_el(K1_EL_ZMIENNE, "Postęp", TRESC_X - 4, y - 18, szer + 8, 40, 0);
        return y + 52;
    }
    y += 24;
    if (v == W_GOTOWA) {
        y = opisy(o, m, y, szer);
        if (st.e & K1_AKT_FL_NOWA_LINIA)
            y = akapit(o, y, "Nowe jądro Linux. Jeśli coś przestanie działać, wróć do poprzedniej wersji.",
                       k1_kolor(m->akcent_tekst, 255), szer);
    } else if (v == W_WROC) {
        y = akapit(o, y, "Wersja, z której wrócisz, nie będzie proponowana ponownie. Dopiero następna.",
                   k1_kolor(m->tekst2, 255), szer);
    } else if (v == W_NAJNOWSZA || v == W_NIGDY) {
        k1_el(K1_EL_ZMIENNE, "Czas sprawdzenia", TRESC_X - 4, y - 42, szer + 8, 24, 0);
    }
    return y + 8;
}

static void loguj(void)
{
    struct k1_przycisk p;
    int v = (int)widok(), jest = przycisk(P_GLOWNY, &p);
    if (v == zalogowany)
        return;
    zalogowany = v;
    k1_log(TAG, "aktualizacje: sieć — %s (działa %d, czeka %d, nowsza %d, poprzednia %d, sam %d); przycisk %d,%d",
           nazwy[v], (int)st.a, (int)st.b, (int)st.c, (int)st.d, !!(st.e & K1_AKT_FL_SAM), jest ? p.x + p.w / 2 : -1,
           jest ? p.y + PRZ_H / 2 : -1);
}

void akt_siec_rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t y)
{
    int32_t szer = apl.szer - TRESC_X - 28;
    struct k1_przycisk p;
    int i;
    y = zdania(o, m, y, szer);
    prz_y = y;
    for (i = P_GLOWNY; i <= P_WROC; i++)
        if (i != P_SAM && przycisk(i, &p))
            k1_przycisk(o, m, &p, K1_CZ_TEKST_MOCNA, hover == i, 1);
    sam_y = apl.wys - 52; /* the switch at the foot of the section */
    k1_przelacznik(o, m, TRESC_X, sam_y, !!(st.e & K1_AKT_FL_SAM), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X + 48, sam_y + 15, "Sprawdzaj sam", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 172, sam_y + 15, "po połączeniu z internetem i co 6 godzin; instaluje dopiero po Twojej zgodzie",
             k1_kolor(m->tekst2, 255), szer - 172);
    k1_el(K1_EL_PRZYCISK, "Sprawdzaj sam", TRESC_X, sam_y, 160, 20, st.e & K1_AKT_FL_SAM ? K1_EL_WYBRANY : 0);
    loguj();
}

int akt_siec_najechanie(int32_t x, int32_t y)
{
    struct k1_przycisk p;
    int h = -1, i;
    for (i = P_GLOWNY; i <= P_WROC && x >= 0; i++)
        if (i != P_SAM && przycisk(i, &p) && k1_przycisk_pod(&p, 1, x, y) == 0)
            h = i;
    if (h == hover)
        return 0;
    hover = h;
    return 1;
}

static void prosba(int a)
{
    aktualizacje_wyslij(k1_komunikat_nowy(K1_KOM_AKT_PROSBA, 0, a, 0, 0, 0, 0));
}

int akt_siec_klik(int32_t x, int32_t y)
{
    struct k1_przycisk p;
    if (x >= TRESC_X && x < TRESC_X + 160 && y >= sam_y && y < sam_y + 20) {
        k1_log(TAG, "aktualizacje: „Sprawdzaj sam” %s", st.e & K1_AKT_FL_SAM ? "wyłączam" : "włączam");
        prosba(st.e & K1_AKT_FL_SAM ? K1_AKT_P_SAM_WYL : K1_AKT_P_SAM_WL);
        st.e ^= K1_AKT_FL_SAM; /* at once; nadzorca's AKT_STAN says it again */
        return 1;
    }
    if (przycisk(P_WROC, &p) && k1_przycisk_pod(&p, 1, x, y) == 0) {
        pytanie_wroc = 1;
        k1_log(TAG, "aktualizacje: pytanie o powrót do poprzedniej wersji");
        return 1;
    }
    if (!przycisk(P_GLOWNY, &p) || k1_przycisk_pod(&p, 1, x, y) != 0)
        return 0;
    switch (widok()) {
    case W_GOTOWA: k1_log(TAG, "aktualizacje: zainstaluj"); prosba(K1_AKT_P_ZAINSTALUJ); break;
    case W_WROC: k1_log(TAG, "aktualizacje: wróć do poprzedniej wersji"); pytanie_wroc = 0; prosba(K1_AKT_P_WROC); break;
    default: k1_log(TAG, "aktualizacje: sprawdź teraz"); prosba(K1_AKT_P_SPRAWDZ); break;
    }
    hover = -1;
    return 1;
}

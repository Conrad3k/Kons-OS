/* K1 gui — the top bar: name of the active application, battery on laptops (0.5.3, ADR 0014), sound,
 * date and time; and the sound popover under the sound icon. The battery and session state come from
 * nadzorca on fd 5 (one way, sender UID 0 checked by k1_odbierz_komunikat). */
#include <stdio.h>
#include <time.h>

#include "gui.h"
#include "k1_czas.h"
#include "k1_log.h"
#include "k1_odbior.h"

static char zegar_tekst[64];
static int hover_gora, dzwiek_otwarty;

static void tekst_zegara(char *buf, size_t n)
{
    struct timespec t;
    struct k1_data d;
    clock_gettime(CLOCK_REALTIME, &t);
    k1_czas_warszawa((int64_t)t.tv_sec, &d);
    if (k.ustawienia[K1_UST_SEKUNDY])
        snprintf(buf, n, "%s %d %s   %d:%02d:%02d", k1_dzien_skrot(d.dzien_tygodnia), d.dzien, k1_miesiac_skrot(d.miesiac),
                 d.godzina, d.minuta, d.sekunda);
    else
        snprintf(buf, n, "%s %d %s   %d:%02d", k1_dzien_skrot(d.dzien_tygodnia), d.dzien, k1_miesiac_skrot(d.miesiac),
                 d.godzina, d.minuta);
}

int32_t glosnik_x(void)
{
    if (zegar_tekst[0] == '\0')
        tekst_zegara(zegar_tekst, sizeof zegar_tekst);
    return k.szer_ekranu - 16 - k1_tekst_szer(K1_CZ_MALA_MOCNA, zegar_tekst) - 16 - 28;
}

/* ---- battery: state from the supervisor, shown only when the computer has one ---- */

static struct {
    int obecna, procent, stan, zasilacz;
} bat = { 0, -1, K1_BAT_NIEZNANY, 0 };
static int bat_odrzucone, bat_zalogowana;
int bat_kanal = 1;
int wyloguj_mozna = -1;

static void zaloguj_baterie(void)
{
    static const char *const stany[] = { "stan nieznany", "rozładowywanie", "ładowanie", "pełna", "nie ładuje" };
    char proc[16] = "?";
    if (!bat.obecna) {
        k1_log(TAG, "brak baterii (zasilacz %s)", bat.zasilacz ? "podłączony" : "—");
        return;
    }
    if (bat.procent >= 0)
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
    k1_log(TAG, "bateria %s, %s, zasilacz %s", proc, stany[bat.stan], bat.zasilacz ? "podłączony" : "odłączony");
}

static void sesja(int zainstalowany) /* 0.7.2: "Wyloguj" only when K1 is installed here */
{
    if (zainstalowany != wyloguj_mozna)
        k1_log(TAG, "%s", zainstalowany ? "K1 zainstalowany na tym komputerze: Wyloguj dostępne"
                                        : "sesja z pendrive'a: bez Wyloguj");
    wyloguj_mozna = zainstalowany;
    menu.brudna = 1;
}

void odbierz_baterie(void)
{
    int i;
    for (i = 0; i < 16 && bat_kanal; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_BATERIA, 0, K1_KIER_OD_NADZORCY, &m, NULL);
        int zmiana;
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            bat_kanal = 0; /* closed or missing: stop watching it */
            return;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_SESJA) {
            sesja(m.a);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_POKAZ_APL) { /* an application got a file to open (ADR 0030) */
            pokaz_aplikacje(m.a);
            continue;
        }
        if (w != K1_ODB_JEST || m.typ != K1_KOM_BATERIA) {
            if (bat_odrzucone++ < 3)
                k1_log(TAG, "kanał baterii: odrzucono komunikat");
            continue;
        }
        /* log only what a person would notice: presence, state, mains, every 5 % */
        zmiana = !bat_zalogowana || m.a != bat.obecna || m.c != bat.stan || m.d != bat.zasilacz ||
                 (m.b / 5 != bat.procent / 5);
        bat.obecna = m.a;
        bat.procent = m.b;
        bat.stan = m.c;
        bat.zasilacz = m.d;
        if (zmiana) {
            zaloguj_baterie();
            bat_zalogowana = 1;
        }
        gora.brudna = 1;
    }
}

/* Draws [bolt] 73% [battery] ending just left of the sound icon. */
static void rysuj_baterie(struct k1_obraz *o, uint32_t krem)
{
    const float x = (float)glosnik_x() - 4 - 26, y = 10; /* battery body 22×12 + 2 px terminal */
    char proc[16];
    int32_t tx;
    k1_bateria(o, x, y, bat.procent, k1_bateria_niska(bat.procent, bat.stan), 255);
    if (bat.procent >= 0) {
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
        tx = (int32_t)x - 6 - k1_tekst_szer(K1_CZ_MALA_MOCNA, proc);
        k1_tekst(o, K1_CZ_MALA_MOCNA, tx, 21, proc, krem, 0);
    } else {
        tx = (int32_t)x - 2;
    }
    if (bat.stan == K1_BAT_LADOWANIE) { /* lightning bolt */
        float bx = (float)tx - 11;
        k1_kreska(o, bx + 5, 9, bx + 1.5f, 16.5f, 1.5f, krem);
        k1_kreska(o, bx + 1.5f, 16.5f, bx + 6.5f, 15.5f, 1.5f, krem);
        k1_kreska(o, bx + 6.5f, 15.5f, bx + 3, 23, 1.5f, krem);
    }
}

/* ---- the bar and the popover ---- */

void rysuj_gora(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&gora);
    const uint32_t krem = k1_kolor(K1_KREM, 255);
    int apl = aktywna_aplikacja();
    int32_t gx;
    if (o == NULL)
        return;
    tekst_zegara(zegar_tekst, sizeof zegar_tekst);
    k1_wyczysc(o, k1_kolor(K1_NOC, 150));
    k1_tekst(o, K1_CZ_MALA_MOCNA, 16, 21, apl ? aplikacje[apl].nazwa : "Kons K1", krem, 300);
    k1_tekst(o, K1_CZ_MALA_MOCNA, k.szer_ekranu - 16 - k1_tekst_szer(K1_CZ_MALA_MOCNA, zegar_tekst), 21, zegar_tekst, krem, 0);
    gx = glosnik_x();
    if (hover_gora || dzwiek_otwarty)
        k1_zaokraglony(o, (float)gx, 4, 28, 24, 6, k1_kolor(K1_KREM, 36));
    k1_ikona(o, K1_IK_GLOSNIK, (float)gx + 6, 8, 16, krem);
    if (bat.obecna)
        rysuj_baterie(o, krem);
    k1_pow_zatwierdz(&k, &gora);
}

void rysuj_dzwiek(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&dzwiek);
    if (o == NULL)
        return;
    tlo_panelu(o, DZW_SZER, DZW_WYS);
    k1_ikona(o, K1_IK_GLOSNIK, MARG + 16, MARG + 16, 20, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 48, MARG + 31, "Dźwięk", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 54, "Ta wersja nie obsługuje jeszcze", k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 71, "karty dźwiękowej.", k1_kolor(m->tekst2, 255), 0);
    k1_pow_zatwierdz(&k, &dzwiek);
}

void pokaz_dzwiek(int tak)
{
    if (tak == dzwiek_otwarty)
        return;
    dzwiek_otwarty = tak;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_DZWIEK, tak, 0, 0, 0, 0));
    gora.brudna = 1;
}

void wskaznik_gora(const struct k1_komunikat *m)
{
    int nowy = m->c != K1_WSK_WYJSCIE && m->a >= glosnik_x() && m->a < glosnik_x() + 28;
    if (nowy != hover_gora) {
        hover_gora = nowy;
        gora.brudna = 1;
    }
    if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY && nowy)
        pokaz_dzwiek(!dzwiek_otwarty);
    if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY)
        pokaz_menu(0);
}

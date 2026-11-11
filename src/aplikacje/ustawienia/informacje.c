/* K1 Ustawienia — Informacje: the system, where the data is, the room for files (0.8.1 step 3: SKARB_MIEJSCE from
 * skarbiec) and — on a K1 stick that has more room after K1-DANE — "Użyj całego pendrive'a na dane" (ADR 0033: the
 * click is the user's consent; skarbiec and nadzorca do the rest, the files stay where they are). Part of Ustawienia
 * (ustawienia.h). */
#include <stdio.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

#define PASEK_W 360
#define PRZYCISK_W 300
#define ZAPAS_MIN 16 /* MiB: skarbiec and nadzorca do nothing for less */

static struct {
    int znane, czeka, blad;
    int32_t zajete, calosc, wpisy, zapas; /* MiB, MiB, entries, MiB (SKARB_MIEJSCE) */
    char komunikat[120];
} in;
static int najechany;

/* "820 MB", "7,4 GB" */
static void rozmiar(char *buf, size_t n, int32_t mib)
{
    if (mib < 1024)
        snprintf(buf, n, "%d MB", (int)mib);
    else
        snprintf(buf, n, "%d,%d GB", (int)(mib / 1024), (int)(mib % 1024 * 10 / 1024));
}

static int przycisk_jest(void)
{
    return in.znane && in.zapas >= ZAPAS_MIN;
}

static struct k1_przycisk przycisk(void)
{
    return (struct k1_przycisk){ TRESC_X, seg_y() + 192, PRZYCISK_W, 32, "Użyj całego pendrive'a na dane", K1_PRZ_GLOWNY };
}

void informacje_miejsce(const struct k1_komunikat *k)
{
    struct k1_przycisk p = przycisk();
    int zmiana = !in.znane || k->a != in.zajete || k->b != in.calosc || k->c != in.wpisy || k->d != in.zapas;
    in.znane = 1;
    in.zajete = k->a;
    in.calosc = k->b;
    in.wpisy = k->c;
    in.zapas = k->d;
    if (zmiana) /* the tests read the numbers and click the button by this line */
        k1_log(TAG, "miejsce: zajęte %d z %d MiB, pliki i foldery %d z %d, pendrive ma więcej %d MiB; przycisk %d,%d",
               (int)in.zajete, (int)in.calosc, (int)in.wpisy, K1_DOK_MAKS, (int)in.zapas,
               przycisk_jest() ? p.x + p.w / 2 : -1, przycisk_jest() ? p.y + p.h / 2 : -1);
}

void informacje_wynik(const struct k1_komunikat *k)
{
    in.czeka = 0;
    in.blad = k->b != 0;
    if (k->b == 0)
        snprintf(in.komunikat, sizeof in.komunikat, "Gotowe — cały pendrive jest teraz na dane.");
    else
        snprintf(in.komunikat, sizeof in.komunikat, "Nie udało się powiększyć — pliki zostały bez zmian.");
    k1_log(TAG, "powiększenie K1-DANE: %s (wynik %d)", k->b == 0 ? "gotowe" : "nieudane", (int)k->b);
}

static void miejsce(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[128], a[24], b[24];
    int32_t y = seg_y() + 132, wypelnienie;
    if (!in.znane)
        return;
    rozmiar(b, sizeof b, in.calosc);
    if (haslo_tylko_pamiec()) { /* no K1-DANE: memory until power off (plan L20) */
        rozmiar(a, sizeof a, in.calosc - in.zajete);
        snprintf(buf, sizeof buf, "Miejsce na pliki    w pamięci do wyłączenia: zostało %s z %s", a, b);
    } else {
        rozmiar(a, sizeof a, in.zajete);
        snprintf(buf, sizeof buf, "Miejsce na pliki    zajęte %s z %s", a, b);
    }
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, y, buf, k1_kolor(m->tekst, 255), 0);
    k1_zaokraglony(o, TRESC_X, (float)y + 10, PASEK_W, 6, 3, k1_kolor(m->tekst, 24));
    wypelnienie = in.calosc > 0 ? (int32_t)((int64_t)PASEK_W * in.zajete / in.calosc) : 0;
    if (wypelnienie > 0)
        k1_zaokraglony(o, TRESC_X, (float)y + 10, (float)(wypelnienie < 6 ? 6 : wypelnienie), 6, 3,
                       k1_kolor(m->akcent, 255));
    snprintf(buf, sizeof buf, "Pliki i foldery    %d z %d %03d", (int)in.wpisy, K1_DOK_MAKS / 1000, K1_DOK_MAKS % 1000);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, y + 44, buf, k1_kolor(m->tekst, 255), 0);
}

void informacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[128], r[24];
    naglowek(o, m, "Informacje", "Kons K1 Sovereign");
    snprintf(buf, sizeof buf, "Wersja systemu    %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(buf, sizeof buf, "Ekran    %d × %d", (int)apl.k.szer_ekranu, (int)apl.k.wys_ekranu);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 44, buf, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 72, "Jądro    Linux 6.18 (własna konfiguracja)", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 100, haslo_gdzie_dane(), k1_kolor(m->tekst, 255), 0);
    miejsce(o, m);
    if (przycisk_jest() || in.czeka) {
        struct k1_przycisk p = przycisk();
        k1_przycisk(o, m, &p, K1_CZ_TEKST_MOCNA, najechany, !in.czeka);
        rozmiar(r, sizeof r, in.zapas);
        if (in.czeka)
            snprintf(buf, sizeof buf, "Powiększam… Nie wyjmuj pendrive'a.");
        else
            snprintf(buf, sizeof buf, "Pendrive ma jeszcze %s wolnego miejsca. Pliki zostają na miejscu.", r);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, p.y + p.h + 22, buf, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
    } else if (in.komunikat[0] != '\0') {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 214, in.komunikat, k1_kolor(in.blad ? m->akcent_tekst : m->tekst2, 255),
                 apl.szer - TRESC_X - 24);
    }
    k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, "Każda część systemu działa w osobnej, zamkniętej klatce.",
             k1_kolor(m->tekst3, 255), 0);
}

int informacje_najechanie(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk();
    int n = przycisk_jest() && !in.czeka && k1_przycisk_pod(&p, 1, x, y) == 0;
    if (n == najechany)
        return 0;
    najechany = n;
    return 1;
}

int informacje_klik(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk();
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_POWIEKSZ, 1, 0, 0, 0, 0, 0);
    if (!przycisk_jest() || in.czeka || k1_przycisk_pod(&p, 1, x, y) != 0)
        return 0;
    if (skarbiec_kanal() < 0 || k1_kanal_wyslij(skarbiec_kanal(), &k, sizeof k, -1) != 0) {
        snprintf(in.komunikat, sizeof in.komunikat, "Nie udało się wysłać prośby do skarbca.");
        in.blad = 1;
        return 1;
    }
    in.czeka = 1;
    in.komunikat[0] = '\0';
    najechany = 0;
    k1_log(TAG, "powiększenie K1-DANE: prośba wysłana (gest użytkownika)");
    k1_apl_odswiez(&apl);
    return 1;
}

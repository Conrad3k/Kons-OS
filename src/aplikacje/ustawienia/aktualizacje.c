/* K1 Ustawienia — Aktualizacje (0.10.5 step 1, D1: sketch A "the version tile"): the version that runs, where it runs
 * from, what is in the other slot (A/B), the result of the last try, and the update of K1 on a disk from a K1 stick
 * (moved here from Instalator — D3). Its own channel from nadzorca (fd 7, KANAL_AKTUALIZACJI; sender UID 0 checked):
 * INST_DYSKI → INST_DYSK, INST_AB, INST_LISTA; INST_START (only K1_INST_AKTUALIZUJ — nadzorca refuses anything else from
 * Ustawienia) → INST_POSTEP, INST_WYNIK; WYLACZ a=1 after a finished update; AKT_POKAZ: show this section.
 * Part of Ustawienia (ustawienia.h). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "k1_inst_teksty.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_manifest.h"
#include "k1_odbior.h"
#include "ustawienia.h"

#define KAFEL_Y (seg_y() - 4)
#define KAFEL_H 72
#define ZDANIE_Y (KAFEL_Y + KAFEL_H + 32)
#define PRZ_Y (ZDANIE_Y + 52)
#define PRZ_H 36

enum stan { A_SZUKAM, A_BEZ_INSTALACJI, A_NA_DYSKU, A_NIEUDANA, A_STARSZA, A_TA_SAMA, A_NOWSZA, A_BRAK_K1, A_DWA, A_TRWA,
            A_GOTOWE, A_BLAD };
static const char *const nazwy_stanow[] = { "sprawdzam", "bez instalacji", "na dysku", "nieudana próba", "dysk starszy",
                                            "ta sama wersja", "dysk nowszy", "dysk bez K1", "dwa pendrive'y", "trwa", "gotowe",
                                            "błąd" };

static int kanal = -1;
static struct {
    int32_t k1, zajety, ab_zapas, ab_slot, ab_fl, ab_podpis;
} dyski[K1_INST_DYSKI_MAKS];
static int n_dyskow, lista, zrodlo, cel = -1, procent, etap, wynik, nowe_miejsce, nieudana_widziana, zalogowany = -1;
static int32_t wersja_zrodla, zrodlo_podpisane;
static enum stan zadanie = A_SZUKAM; /* A_TRWA, A_GOTOWE, A_BLAD while/after a job; else A_SZUKAM */
static int hover;

static int32_t wersja_dysku(int i) /* -1 no K1, 0 K1 without a manifest (0.5.9–0.6.1), else the version */
{
    if (!dyski[i].k1)
        return -1;
    return dyski[i].k1 & K1_INST_WERSJA ? dyski[i].k1 & 0xffffff : 0;
}

static int dysk_dziala(void) /* the disk K1 runs from (its K1-DANE is open), -1 */
{
    int i;
    for (i = 0; i < n_dyskow; i++)
        if (dyski[i].k1 && dyski[i].zajety)
            return i;
    return -1;
}

static void wersja(int32_t w, char *buf, size_t n)
{
    if (w > 0)
        k1_wersja_tekst((uint32_t)w, buf, n);
    else
        snprintf(buf, n, "%s", w == 0 ? "0.5.9–0.6.1" : "?");
}

/* What the section shows now. */
static enum stan stan(void)
{
    int32_t w;
    if (zadanie != A_SZUKAM)
        return zadanie;
    if (!lista)
        return A_SZUKAM;
    if (zrodlo == 2)
        return A_DWA;
    if (zrodlo != 1 && dysk_dziala() >= 0)
        return dyski[dysk_dziala()].ab_fl & K1_AB_FL_NIEUDANA && !nieudana_widziana ? A_NIEUDANA : A_NA_DYSKU;
    if (zrodlo != 1)
        return A_BEZ_INSTALACJI;
    if (cel < 0)
        return A_BRAK_K1;
    w = wersja_dysku(cel);
    if (w > 0 && wersja_zrodla > 0 && w > wersja_zrodla)
        return A_NOWSZA;
    return w > 0 && w == wersja_zrodla ? A_TA_SAMA : A_STARSZA;
}

static void wyslij(struct k1_komunikat k)
{
    if (kanal >= 0 && k1_kanal_wyslij(kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "aktualizacje: wysłanie do nadzorcy nieudane");
}

static void szukaj(void)
{
    if (zadanie == A_TRWA)
        return;
    zadanie = A_SZUKAM;
    lista = 0;
    n_dyskow = 0;
    cel = -1;
    wyslij(k1_komunikat_nowy(K1_KOM_INST_DYSKI, 0, 0, 0, 0, 0, 0));
}

/* The main button of the state: its label, NULL none. */
static const char *etykieta(char *buf, size_t n)
{
    char w[16];
    switch (stan()) {
    case A_STARSZA:
        wersja(wersja_zrodla, w, sizeof w);
        snprintf(buf, n, "Zaktualizuj K1 na dysku do %s", w);
        return buf;
    case A_TA_SAMA: /* as Instalator did: a developer build has the same number every time */
        return "Zapisz K1 na dysku jeszcze raz";
    case A_GOTOWE:
        return "Uruchom ponownie";
    case A_BLAD:
        return "Sprawdź jeszcze raz";
    case A_NIEUDANA:
        return "Rozumiem";
    default:
        return NULL;
    }
}

static int przycisk(struct k1_przycisk *p)
{
    static char buf[64];
    enum stan s = stan();
    p->etykieta = etykieta(buf, sizeof buf);
    p->x = TRESC_X;
    p->y = PRZ_Y + (s == A_GOTOWE ? 18 : 0);
    p->h = PRZ_H;
    p->w = p->etykieta ? k1_tekst_szer(K1_CZ_TEKST_MOCNA, p->etykieta) + 40 : 0;
    p->styl = s == A_STARSZA || s == A_GOTOWE ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY; /* the same version: not urged */
    return p->etykieta != NULL;
}

/* The tests click by this line (once per state): the state, the disk, the stick, the button in the window. */
static void loguj(void)
{
    struct k1_przycisk p;
    char d[16] = "-", z[16] = "-";
    enum stan s = stan();
    int jest = przycisk(&p);
    if ((int)s == zalogowany || sekcja != S_AKTUALIZACJE)
        return;
    zalogowany = (int)s;
    if (cel >= 0 || dysk_dziala() >= 0)
        wersja(wersja_dysku(cel >= 0 ? cel : dysk_dziala()), d, sizeof d);
    if (zrodlo == 1)
        wersja(wersja_zrodla, z, sizeof z);
    k1_log(TAG, "aktualizacje: %s (dysk: %s, pendrive: %s); przycisk %d,%d", nazwy_stanow[s], d, z,
           jest ? p.x + p.w / 2 : -1, jest ? p.y + PRZ_H / 2 : -1);
}

static void komunikat(const struct k1_komunikat *k)
{
    int i;
    switch (k->typ) {
    case K1_KOM_AKT_POKAZ:
        k1_log(TAG, "aktualizacje: otwarte na prośbę nadzorcy");
        ustawienia_pokaz(S_AKTUALIZACJE);
        break;
    case K1_KOM_INST_DYSK:
        if (!lista && (int)k->obiekt == n_dyskow + 1) {
            memset(&dyski[n_dyskow], 0, sizeof dyski[0]);
            dyski[n_dyskow].k1 = k->c;
            dyski[n_dyskow].zajety = k->e;
            n_dyskow++;
        }
        break;
    case K1_KOM_INST_AB:
        if (!lista && (int)k->obiekt == n_dyskow && n_dyskow > 0) {
            dyski[n_dyskow - 1].ab_zapas = k->b;
            dyski[n_dyskow - 1].ab_slot = k->c;
            dyski[n_dyskow - 1].ab_fl = k->d;
            dyski[n_dyskow - 1].ab_podpis = k->e;
        }
        break;
    case K1_KOM_INST_LISTA:
        if (lista || k->a != n_dyskow)
            break;
        lista = 1;
        zrodlo = k->b;
        wersja_zrodla = k->c;
        zrodlo_podpisane = k->d;
        for (i = 0, cel = -1; i < n_dyskow && cel < 0; i++) /* the disk to update: the first with K1 */
            if (dyski[i].k1)
                cel = i;
        break;
    case K1_KOM_INST_POSTEP:
        if (zadanie == A_TRWA) {
            if (k->a / 25 != procent / 25 || k->a == 100)
                k1_log(TAG, "aktualizacje: postęp %d%% (etap %d)", (int)k->a, (int)k->b);
            procent = k->a;
            etap = k->b;
        }
        break;
    case K1_KOM_INST_WYNIK:
        if (zadanie != A_TRWA)
            break;
        wynik = k->a;
        nowe_miejsce = k->c;
        zadanie = k->a == 0 ? A_GOTOWE : A_BLAD;
        k1_log(TAG, "aktualizacje: %s (kod %d)", k->a == 0 ? (k->c ? "zaktualizowano (nowe miejsce A/B)" : "zaktualizowano")
                                                         : "błąd aktualizacji",
               (int)k->a);
        break;
    default:
        break;
    }
}

static void odbierz(void)
{
    int j;
    for (j = 0; j < 32 && kanal >= 0; j++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(kanal, 0, K1_KIER_OD_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            kanal = -1;
            break;
        }
        if (w != K1_ODB_JEST) {
            k1_log(TAG, "aktualizacje: odrzucono komunikat");
            continue;
        }
        komunikat(&k);
    }
    if (sekcja == S_AKTUALIZACJE)
        k1_apl_odswiez(&apl);
    loguj();
}

int aktualizacje_start(void)
{
    if (fcntl(FD_AKTUALIZACJE, F_GETFD) == 0)
        kanal = FD_AKTUALIZACJE;
    return kanal;
}

void aktualizacje_dane(void)
{
    odbierz();
}

void aktualizacje_pokazane(void)
{
    zalogowany = -1;
    szukaj();
}

/* ---- drawing ---- */

static void kafel(struct k1_obraz *o, const struct k1_motyw *m)
{
    char w[24], pod[96];
    int d = dysk_dziala();
    int32_t szer = apl.szer - TRESC_X - 28;
    const char *podpis = d >= 0 ? (dyski[d].ab_slot ? (dyski[d].ab_podpis ? "Zweryfikowano" : "wersja deweloperska")
                                                    : "")
                                : zrodlo == 1 && wersja_zrodla > 0 ? (zrodlo_podpisane ? "Zweryfikowano"
                                                                                         : "wersja deweloperska")
                                                                   : "";
    const char *skad = d >= 0 ? (dyski[d].ab_slot == 2 ? "działa z miejsca B na dysku" : "działa z miejsca A na dysku")
                       : zrodlo == 1 ? "działa z pendrive'a"
                                     : "działa bez instalacji";
    k1_zaokraglony(o, (float)TRESC_X, (float)KAFEL_Y, (float)szer, KAFEL_H, 10, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)TRESC_X, (float)KAFEL_Y, (float)szer, KAFEL_H, 10, 1.0f, k1_kolor(m->tekst, 24));
    k1_zaokraglony(o, (float)TRESC_X + 16, (float)KAFEL_Y + 16, 40, 40, 10, k1_kolor(m->akcent, 255));
    k1_ikona(o, K1_IK_AKTUALIZACJE, (float)TRESC_X + 24, (float)KAFEL_Y + 24, 24, k1_kolor(m->na_akcencie, 255));
    snprintf(w, sizeof w, "%s", K1_WERSJA); /* the owner (2026-11-17): the number alone */
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 72, KAFEL_Y + 32, w, k1_kolor(m->tekst, 255), 0);
    if (lista)
        snprintf(pod, sizeof pod, "%s%s%s", podpis, podpis[0] ? " · " : "", skad);
    else
        pod[0] = '\0';
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 72, KAFEL_Y + 54, pod, k1_kolor(m->tekst2, 255), szer - 88);
    k1_el(K1_EL_ZMIENNE, "Wersja", TRESC_X + 68, KAFEL_Y + 12, szer - 80, 50, 0); /* a new version every release */
}

static void zdanie(struct k1_obraz *o, const struct k1_motyw *m, const char *t, const char *d1, const char *d2, int wazne)
{
    int32_t szer = apl.szer - TRESC_X - 28;
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, ZDANIE_Y, t, k1_kolor(wazne ? m->akcent_tekst : m->tekst, 255), szer);
    if (d1)
        k1_tekst(o, K1_CZ_MALA, TRESC_X, ZDANIE_Y + 22, d1, k1_kolor(m->tekst2, 255), szer);
    if (d2)
        k1_tekst(o, K1_CZ_MALA, TRESC_X, ZDANIE_Y + 40, d2, k1_kolor(m->tekst2, 255), szer);
}

static void postep(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[96];
    int32_t szer = apl.szer - TRESC_X - 28;
    snprintf(buf, sizeof buf, "Aktualizuję K1 na dysku… %s", k1_inst_etap_tekst(etap));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, ZDANIE_Y, buf, k1_kolor(m->tekst, 255), szer - 60);
    snprintf(buf, sizeof buf, "%d%%", procent);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + szer - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), ZDANIE_Y, buf,
             k1_kolor(m->tekst, 255), 0);
    k1_zaokraglony(o, (float)TRESC_X, (float)ZDANIE_Y + 14, (float)szer, 8, 4, k1_kolor(m->tekst, 24));
    if (procent > 0)
        k1_zaokraglony(o, (float)TRESC_X, (float)ZDANIE_Y + 14, (float)(szer * procent / 100 < 8 ? 8 : szer * procent / 100), 8,
                       4, k1_kolor(m->akcent, 255));
    k1_tekst(o, K1_CZ_MALA, TRESC_X, ZDANIE_Y + 44, "Nie wyłączaj komputera ani nie wyjmuj pendrive'a.",
             k1_kolor(m->tekst2, 255), 0);
    k1_el(K1_EL_ZMIENNE, "Postęp", TRESC_X - 4, ZDANIE_Y - 18, szer + 8, 40, 0);
}

/* The sentence of a disk K1 runs from: a failed try, a waiting one, or how to update. */
static void na_dysku(struct k1_obraz *o, const struct k1_motyw *m)
{
    char a[16], b[160], c[160];
    int d = dysk_dziala();
    wersja(dyski[d].ab_zapas, a, sizeof a);
    snprintf(c, sizeof c, "W drugim miejscu na dysku: %s.", dyski[d].ab_zapas > 0 ? a : "nic");
    if (stan() == A_NIEUDANA) {
        snprintf(b, sizeof b, "Nowa wersja %s nie uruchomiła się — działa poprzednia.", a);
        zdanie(o, m, b, "Nic nie zginęło. Komputer sam wrócił do wersji, która działała.", NULL, 1);
    } else if (dyski[d].ab_fl & K1_AB_FL_NOWY) {
        snprintf(b, sizeof b, "Nowa wersja %s czeka na dysku.", a);
        zdanie(o, m, b, "Wystartuje na próbę przy następnym uruchomieniu komputera.", NULL, 0);
    } else {
        zdanie(o, m, "Aktualizacja: z pendrive'a z nowszą wersją K1.",
               "Podłącz go albo uruchom z niego komputer i otwórz tę sekcję.", dyski[d].ab_slot ? c : NULL, 0);
    }
}

void aktualizacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char a[16], b[16], t[160], t2[160];
    struct k1_przycisk p;
    enum stan s = stan();
    naglowek(o, m, "Aktualizacje", "Wersja K1 na tym komputerze i jej aktualizacje");
    kafel(o, m);
    wersja(cel >= 0 ? wersja_dysku(cel) : -1, a, sizeof a);
    wersja(wersja_zrodla, b, sizeof b);
    switch (s) {
    case A_SZUKAM:
        zdanie(o, m, "Sprawdzam dyski…", NULL, NULL, 0);
        break;
    case A_BEZ_INSTALACJI:
        zdanie(o, m, "K1 działa bez instalacji.", "Aktualizacje są dla K1 zainstalowanego na dysku (Instalator).", NULL, 0);
        break;
    case A_NA_DYSKU:
    case A_NIEUDANA:
        na_dysku(o, m);
        break;
    case A_STARSZA:
        snprintf(t, sizeof t, "Na dysku tego komputera jest starsza wersja: %s.", a);
        zdanie(o, m, t, "Nowa wersja zapisze się obok obecnej. Twoje dane zostają.", NULL, 0);
        break;
    case A_TA_SAMA:
        snprintf(t, sizeof t, "Na dysku jest ta sama wersja: %s.", a);
        zdanie(o, m, t, "Możesz zapisać ją jeszcze raz — obok obecnej, Twoje dane zostają.", NULL, 0);
        break;
    case A_NOWSZA:
        snprintf(t, sizeof t, "Na dysku jest nowsza wersja (%s) niż na pendrivie (%s).", a, b);
        zdanie(o, m, t, "Powrót do starszej wersji jest zablokowany.", NULL, 0);
        break;
    case A_BRAK_K1:
        zdanie(o, m, "Na dysku tego komputera nie ma K1.", "Zainstalujesz go w Instalatorze.", NULL, 0);
        break;
    case A_DWA:
        zdanie(o, m, "Podłączone są dwa pendrive'y z K1.",
               "Odłącz ten, z którego komputer nie startował, i otwórz tę sekcję jeszcze raz.", NULL, 0);
        break;
    case A_TRWA:
        postep(o, m);
        break;
    case A_GOTOWE:
        if (nowe_miejsce)
            snprintf(t, sizeof t, "Gotowe — %s czeka na dysku obok %s.", b, a);
        else
            snprintf(t, sizeof t, "Gotowe — K1 na dysku ma wersję %s.", b);
        snprintf(t2, sizeof t2, "Uruchom komputer ponownie%s.", zrodlo == 1 && dysk_dziala() < 0 ? ", bez pendrive'a" : "");
        zdanie(o, m, t, t2, nowe_miejsce ? "Gdyby nowa wersja nie wystartowała, K1 sam wróci do obecnej." : NULL, 0);
        break;
    default:
        zdanie(o, m, "Nie udało się zaktualizować K1 na dysku.", k1_inst_blad_tekst(wynik), "Dane na dysku są nietknięte.", 1);
        break;
    }
    if (przycisk(&p))
        k1_przycisk(o, m, &p, K1_CZ_TEKST_MOCNA, hover, 1);
}

/* ---- input ---- */

int aktualizacje_najechanie(int32_t x, int32_t y)
{
    struct k1_przycisk p;
    int h = x >= 0 && przycisk(&p) && k1_przycisk_pod(&p, 1, x, y) == 0;
    if (h == hover)
        return 0;
    hover = h;
    return 1;
}

int aktualizacje_klik(int32_t x, int32_t y)
{
    struct k1_przycisk p;
    if (!przycisk(&p) || k1_przycisk_pod(&p, 1, x, y) != 0)
        return 0;
    switch (stan()) {
    case A_STARSZA:
    case A_TA_SAMA:
        k1_log(TAG, "aktualizacje: proszę o aktualizację dysku %d", cel + 1);
        zadanie = A_TRWA;
        procent = 0;
        etap = 0;
        wyslij(k1_komunikat_nowy(K1_KOM_INST_START, (uint32_t)cel + 1, K1_INST_AKTUALIZUJ, 0, 0, 0, 0));
        break;
    case A_GOTOWE:
        k1_log(TAG, "aktualizacje: ponowne uruchamianie");
        wyslij(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, 1, 0, 0, 0, 0));
        break;
    case A_BLAD:
        szukaj();
        break;
    default:
        nieudana_widziana = 1;
        break;
    }
    hover = 0;
    k1_apl_odswiez(&apl);
    loguj();
    return 1;
}

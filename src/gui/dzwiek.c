/* K1 gui — sound (0.12.0 step 1, ADR 0054; the owner's D2: B — its upper part; the list "Grają teraz" comes with the
 * sessions in step 3): the speaker on the top bar shows the state (no card, muted, quiet, loud), its popover "Dźwięk" has the
 * mute, the volume and "Odtwórz próbę" (D3: A — K1's only sound).
 * The channel to the sound service comes from nadzorca on fd 5 (DZWIEK_KANAL; again after the service starts anew): the
 * service's state (DZWIEK_STAN), how much of the sample it played (DZWIEK_ODTWORZONO); gui is a steering client (the volume
 * of all K1, DZWIEK_GLOSNOSC). The sample: made here (k1_dzw_proba), written into our ring (a sealed memfd) as the service
 * takes it. Sender UID of every message checked (k1_odbierz_komunikat). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "gui.h"
#include "k1_dzwiek.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"

int dzw_fd = -1;
static struct {
    int karta, glosnosc, wyciszone;
} st = { K1_DZW_KARTA_SZUKA, K1_DZW_GLOSNOSC_START, 0 };
static int16_t *pierscien; /* our ring, mapped for writing (NULL: not made yet) */
static int pierscien_fd = -1;
static struct {
    int gra, zamknieto;
    uint32_t zapisane, wolne, poz; /* frames written in all, free in the ring, of the sample written */
} pr;
static int hover = -1, wcisniete = -1; /* the control under the pointer / pressed (C_*) */

enum { C_WYCISZ = 0, C_SUWAK, C_PROBA };
#define P 16
#define WYC_X (MARG + P) /* the mute: a round button 36 px */
#define WYC_Y (MARG + 46)
#define SUW_X (MARG + P + 52) /* the slider's track */
#define SUW_W (DZWIEK_OKIENKO_SZER - 2 * P - 52 - 52)
#define SUW_Y (MARG + 64)

static struct k1_przycisk przycisk_proby(void)
{
    struct k1_przycisk p = { MARG + 8, MARG + 110, DZWIEK_OKIENKO_SZER - 16, 36, "Odtwórz próbę", K1_PRZ_CICHY };
    return p;
}

static void wyslij(struct k1_komunikat m)
{
    if (dzw_fd >= 0 && k1_kanal_wyslij(dzw_fd, &m, sizeof m, -1) != 0)
        k1_log(TAG, "dźwięk: wysłanie nieudane: %s", strerror(errno));
}

static void glosnosc(int v, int wyciszone)
{
    st.glosnosc = v < 0 ? 0 : v > K1_DZW_GLOSNOSC_MAKS ? K1_DZW_GLOSNOSC_MAKS : v;
    st.wyciszone = wyciszone;
    wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, st.glosnosc, st.wyciszone, 0, 0, 0));
    gora.brudna = okienko_paska.brudna = 1;
}

/* A new channel from nadzorca (fd 5): the old one and its stream are gone; the service hears our volume. */
void dzwiek_kanal_nowy(int fd)
{
    int nowy = fcntl(fd, F_DUPFD_CLOEXEC, 0); /* the caller closes `fd` */
    if (nowy < 0)
        return;
    if (dzw_fd >= 0)
        close(dzw_fd);
    dzw_fd = nowy;
    pr.gra = 0;
    k1_log(TAG, "dźwięk: kanał do usługi dźwięku");
    glosnosc(st.glosnosc, st.wyciszone);
}

/* The sample into the ring as far as it is free; at its end: close after it plays. */
static void dopisz(void)
{
    while (pr.wolne > 0 && pr.poz < K1_DZW_PROBA_RAMKI) {
        uint32_t od = pr.zapisane % K1_DZW_PIERSCIEN, n = pr.wolne;
        if (n > K1_DZW_PIERSCIEN - od)
            n = K1_DZW_PIERSCIEN - od;
        n = k1_dzw_proba(pierscien + (size_t)od * K1_DZW_KANALY, pr.poz, n);
        pr.poz += n;
        pr.zapisane += n;
        pr.wolne -= n;
        wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_ZAPISANO, 0, (int32_t)n, 0, 0, 0, 0));
    }
    if (pr.poz >= K1_DZW_PROBA_RAMKI && !pr.zamknieto) {
        wyslij(k1_komunikat_nowy(K1_KOM_DZWIEK_ZAMKNIJ, 0, K1_DZW_PO_ODEGRANIU, 0, 0, 0, 0));
        pr.zamknieto = 1;
    }
}

static int zrob_pierscien(void)
{
    void *m;
    if (pierscien != NULL)
        return 0;
    pierscien_fd = k1_memfd_nowy("k1-dzwiek");
    if (pierscien_fd < 0 || ftruncate(pierscien_fd, K1_DZW_PIERSCIEN_BAJTY) != 0 ||
        fcntl(pierscien_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
        return -1;
    m = mmap(NULL, K1_DZW_PIERSCIEN_BAJTY, PROT_READ | PROT_WRITE, MAP_SHARED, pierscien_fd, 0);
    if (m == MAP_FAILED)
        return -1;
    pierscien = m;
    return 0;
}

static void zagraj_probe(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, 0, 0, 0, 0, 0);
    if (dzw_fd < 0 || st.karta != K1_DZW_KARTA_JEST || pr.gra)
        return;
    if (zrob_pierscien() != 0) {
        k1_log(TAG, "dźwięk: pierścień: %s", strerror(errno));
        return;
    }
    if (k1_kanal_wyslij(dzw_fd, &m, sizeof m, pierscien_fd) != 0)
        return;
    memset(&pr, 0, sizeof pr);
    pr.gra = 1;
    pr.wolne = K1_DZW_PIERSCIEN;
    k1_log(TAG, "próba: start (głośność %d%%%s)", st.glosnosc, st.wyciszone ? ", wyciszone" : "");
    dopisz();
    okienko_paska.brudna = 1;
}

static void stan(const struct k1_komunikat *m)
{
    if (m->a != st.karta || m->b != st.glosnosc || m->c != st.wyciszone)
        k1_log(TAG, "dźwięk: karta %d, głośność %d%%%s", m->a, m->b, m->c ? ", wyciszone" : "");
    st.karta = m->a;
    st.glosnosc = m->b;
    st.wyciszone = m->c;
    gora.brudna = okienko_paska.brudna = 1;
}

void dzwiek_odbierz(void)
{
    for (int i = 0; i < 32 && dzw_fd >= 0; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(dzw_fd, K1_UID_DZWIEK, K1_KIER_OD_DZWIEKU, &m, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) { /* the service ended: nadzorca gives a new channel with the new one */
            close(dzw_fd);
            dzw_fd = -1;
            pr.gra = 0;
            return;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (m.typ == K1_KOM_DZWIEK_STAN) {
            stan(&m);
        } else if (m.typ == K1_KOM_DZWIEK_ODTWORZONO && pr.gra) {
            pr.wolne += (uint32_t)m.a;
            if (m.b) {
                pr.gra = 0;
                k1_log(TAG, "próba: %s", pr.poz >= K1_DZW_PROBA_RAMKI ? "zagrana" : "przerwana");
                okienko_paska.brudna = 1;
            } else {
                dopisz();
            }
        }
    }
}

/* The speaker of the state, `ik` and its colour: no card — faint and crossed out; muted or 0 — with a ×; quiet — one wave. */
static enum k1_ikona ikona(void)
{
    if (st.karta == K1_DZW_KARTA_JEST && (st.wyciszone || st.glosnosc == 0))
        return K1_IK_GLOSNIK_WYCISZONY;
    return st.karta == K1_DZW_KARTA_JEST && st.glosnosc < 50 ? K1_IK_GLOSNIK_CICHO : K1_IK_GLOSNIK;
}

void dzwiek_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem)
{
    if (st.karta == K1_DZW_KARTA_BRAK || st.karta == K1_DZW_KARTA_BLAD) {
        k1_ikona(o, K1_IK_GLOSNIK, (float)x + 8, 8, 16, k1_kolor(K1_KREM, 70));
        k1_kreska(o, (float)x + 10, 10, (float)x + 22, 22, 1.4f, k1_kolor(K1_KREM, 160));
        return;
    }
    k1_ikona(o, ikona(), (float)x + 8, 8, 16, krem);
}

static void bez_karty(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const opis[][2] = { [K1_DZW_KARTA_SZUKA] = { "Szukam karty dźwiękowej…", "" },
                                           [K1_DZW_KARTA_BRAK] = { "Ten komputer nie ma", "karty dźwiękowej." },
                                           [K1_DZW_KARTA_BLAD] = { "Karta dźwiękowa przestała grać.", "" } };
    const char *const *t = opis[st.karta == K1_DZW_KARTA_JEST ? K1_DZW_KARTA_BLAD : st.karta];
    k1_ikona(o, K1_IK_GLOSNIK, MARG + P, MARG + 16, 20, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 48, MARG + 31, "Dźwięk", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 58, t[0], k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 76, t[1], k1_kolor(m->tekst2, 255), 0);
}

/* The slider: the track, its part up to the volume in the accent (grey when muted), the knob. */
static void suwak(struct k1_obraz *o, const struct k1_motyw *m)
{
    float f = (float)SUW_W * (float)st.glosnosc / K1_DZW_GLOSNOSC_MAKS;
    k1_zaokraglony(o, SUW_X, SUW_Y - 2, SUW_W, 4, 2, k1_kolor(m->tekst, 30));
    k1_zaokraglony(o, SUW_X, SUW_Y - 2, f, 4, 2, k1_kolor(st.wyciszone ? m->tekst3 : m->akcent, 255));
    k1_cien(o, SUW_X + f - 8, SUW_Y - 8, 16, 16, 8, 1, 4, 70);
    k1_kolo(o, SUW_X + f, SUW_Y, 8, k1_kolor(0xffffff, 255));
    k1_okrag(o, SUW_X + f, SUW_Y, 8, 1.0f, k1_kolor(m->tekst, hover == C_SUWAK || wcisniete == C_SUWAK ? 70 : 30));
    k1_el(K1_EL_PRZYCISK, "Głośność", SUW_X, SUW_Y - 12, SUW_W, 24, 0);
}

void dzwiek_okienko(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p = przycisk_proby();
    char proc[16];
    if (st.karta != K1_DZW_KARTA_JEST) {
        bez_karty(o, m);
        return;
    }
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + P, MARG + 30, "Dźwięk", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + DZWIEK_OKIENKO_SZER - P - k1_tekst_szer(K1_CZ_MALA, "Głośniki"), MARG + 30, "Głośniki",
             k1_kolor(m->tekst2, 255), 0);
    k1_zaokraglony(o, WYC_X, WYC_Y, 36, 36, 18,
                   k1_kolor(st.wyciszone ? m->akcent : hover == C_WYCISZ ? m->najechanie : m->pole, 255));
    if (!st.wyciszone)
        k1_obrys(o, WYC_X, WYC_Y, 36, 36, 18, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, ikona(), WYC_X + 7, WYC_Y + 7, 22, k1_kolor(st.wyciszone ? m->na_akcencie : m->tekst, 255));
    k1_el(K1_EL_PRZYCISK, "Wycisz", WYC_X, WYC_Y, 36, 36, st.wyciszone ? K1_EL_WYBRANY : 0);
    suwak(o, m);
    snprintf(proc, sizeof proc, "%d %%", st.glosnosc);
    k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + DZWIEK_OKIENKO_SZER - P - k1_tekst_szer(K1_CZ_MALA_MOCNA, proc), SUW_Y + 5, proc,
             k1_kolor(st.wyciszone ? m->tekst3 : m->tekst, 255), 0);
    k1_prostokat(o, MARG + P, MARG + 98, DZWIEK_OKIENKO_SZER - 2 * P, 1, k1_kolor(m->tekst, 22));
    k1_przycisk(o, m, &p, K1_CZ_MALA_MOCNA, hover == C_PROBA, !pr.gra);
}

/* The control under (x, y) of the popover's surface, or -1. */
static int pod(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk_proby();
    int32_t dx = x - (WYC_X + 18), dy = y - (WYC_Y + 18);
    if (st.karta != K1_DZW_KARTA_JEST)
        return -1;
    if (dx * dx + dy * dy <= 18 * 18)
        return C_WYCISZ;
    if (x >= SUW_X - 10 && x <= SUW_X + SUW_W + 10 && y >= SUW_Y - 12 && y <= SUW_Y + 12)
        return C_SUWAK;
    return k1_przycisk_pod(&p, 1, x, y) == 0 ? C_PROBA : -1;
}

static void suwak_do(int32_t x)
{
    int v = (int)(((x - SUW_X) * K1_DZW_GLOSNOSC_MAKS + SUW_W / 2) / SUW_W);
    v = v < 0 ? 0 : v > K1_DZW_GLOSNOSC_MAKS ? K1_DZW_GLOSNOSC_MAKS : v;
    if (v != st.glosnosc || st.wyciszone)
        glosnosc(v, 0); /* moving the slider unmutes */
}

void dzwiek_okienko_wskaznik(const struct k1_komunikat *m)
{
    int c = m->c == K1_WSK_WYJSCIE ? -1 : pod(m->a, m->b);
    if (c != hover) {
        hover = c;
        okienko_paska.brudna = 1;
    }
    if (m->d != K1_PRZYCISK_LEWY && m->c != K1_WSK_RUCH)
        return;
    if (m->c == K1_WSK_WCISNIECIE) {
        wcisniete = c;
        if (c == C_SUWAK)
            suwak_do(m->a);
    } else if (m->c == K1_WSK_RUCH && wcisniete == C_SUWAK) {
        suwak_do(m->a);
    } else if (m->c == K1_WSK_PUSZCZENIE) {
        if (wcisniete == C_SUWAK)
            k1_log(TAG, "dźwięk: głośność %d%% (suwak)", st.glosnosc);
        else if (c == C_WYCISZ && wcisniete == C_WYCISZ)
            glosnosc(st.glosnosc, !st.wyciszone);
        else if (c == C_PROBA && wcisniete == C_PROBA)
            zagraj_probe();
        wcisniete = -1;
        okienko_paska.brudna = 1;
    }
}

void dzwiek_okienko_otwarte(int tak)
{
    hover = wcisniete = -1;
    if (tak)
        k1_log(TAG, "okienko dźwięku: karta %d, głośność %d%%%s", st.karta, st.glosnosc, st.wyciszone ? ", wyciszone" : "");
}

/* K1 dzwiek — the microphone (0.13.5 step 1, ADR 0065): who hears it, and what they get.
 *
 * A listener is a client whose row may hear (k1_dzw_klient_slucha; a zone only when nadzorca said so — DZWIEK_KLIENT d):
 *  - the samples (the test program; step 2: a zone's VMM for its guest) — DZWIEK_OTWORZ b = SLUCHA with its own ring, a
 *    memfd sealed against shrinking and growing, which we map for writing only; after each period we write it and say
 *    how many frames and the level (DZWIEK_NAGRANO a, c);
 *  - the level only (Ustawienia's "Powiedz coś": DZWIEK_MIKROFON POZIOM) — DZWIEK_NAGRANO a = 0 with the level ten times a
 *    second; never a sample (D3, T4).
 * The card records only while someone hears it (nagrywanie.c: T4 — never "in case"). Every period: the card's frames →
 * floats (k1_mikrofon.c) → the test tone (the test program only: QEMU's card records silence) → × the level of the
 * microphone → silence while it is muted or gui's security screen is on top (WSTRZYMAJ, T4) → the bar's level → each
 * listener. The steering clients see the microphone in DZWIEK_STAN e (its source, level, mute, held, K1 hears it) — gui
 * draws the sign "mikrofon włączony" from it (D2: A), never from a listener. The log: who hears and how many periods were
 * lost — never a sample or a level in time (T4, niezmiennik 8). */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dzwiek.h"
#include "k1_log.h"
#include "k1_mikrofon.h"

#define POZIOM_CO 5 /* periods (107 ms) between two levels to a client that watches the bar */

static struct sluchacz {
    int rodzaj;           /* the client's row; 0: this slot does not hear */
    uint8_t *pierscien;   /* mapped for writing; NULL: the level only */
    int format;
    uint32_t zapisane;    /* frames we wrote into it in all (modulo 2^32) */
    int poziom;           /* the loudest level since the last NAGRANO to a client that watches the bar */
    int okresy;
} sluchacze[KLIENCI_MAKS];
static struct k1_dzw_mik mik = { K1_DZW_ZRODLO_BRAK, K1_DZW_MIK_START, 0, 0, 0 };
static int ton;           /* the test program's tone in the microphone */
static uint32_t ton_faza;
static int byl_okres;     /* a period came since the card started recording (the log says the first: never a level) */
static int wstrzymal = -1; /* the slot of gui that holds it (its security screen on top), -1: nobody */

int32_t mikrofon_pole(void)
{
    struct k1_dzw_mik m = mik;
    m.zrodlo = nagr_jest() ? kontrolki_zrodlo() : K1_DZW_ZRODLO_BRAK;
    return k1_dzw_mik_pole(&m);
}

static int slucha_ktos(void)
{
    for (int i = 0; i < KLIENCI_MAKS; i++)
        if (sluchacze[i].rodzaj != 0)
            return 1;
    return 0;
}

/* K1 hears it (not a zone): the bit of DZWIEK_STAN e; the steering clients hear when it changes. */
static void zglos(void)
{
    int k1 = 0;
    for (int i = 0; i < KLIENCI_MAKS; i++)
        k1 |= sluchacze[i].rodzaj != 0 && sluchacze[i].rodzaj != K1_DZW_KLIENT_STREFA;
    mik.slucha_k1 = k1;
    klienci_stan_wszystkim();
}

static void koniec(int i, int powiedz)
{
    struct sluchacz *s = &sluchacze[i];
    if (s->rodzaj == 0)
        return;
    if (s->pierscien != NULL)
        munmap(s->pierscien, k1_dzw_pierscien_bajty(s->format));
    if (powiedz)
        klient_wyslij(i, k1_komunikat_nowy(K1_KOM_DZWIEK_NAGRANO, 0, 0, 1, 0, 0, 0));
    k1_log(TAG, "mikrofon: %s nie słucha", k1_dzw_klient_nazwa(s->rodzaj));
    memset(s, 0, sizeof *s);
    if (!slucha_ktos()) {
        nagr_zamknij();
        byl_okres = 0;
    }
    zglos();
}

/* A new listener in slot i: the card records (if it did not yet). 0, or -1: no microphone (it heard so). */
static int nowy(int i, int rodzaj, uint8_t *pierscien, int format)
{
    struct sluchacz *s = &sluchacze[i];
    if (!nagr_jest() || nagr_otworz() != 0) {
        if (pierscien != NULL)
            munmap(pierscien, k1_dzw_pierscien_bajty(format));
        klient_wyslij(i, k1_komunikat_nowy(K1_KOM_DZWIEK_NAGRANO, 0, 0, 1, 0, 0, 0));
        return -1;
    }
    *s = (struct sluchacz){ rodzaj, pierscien, format, 0, 0, 0 };
    k1_log(TAG, "mikrofon: słucha %s (%s)", k1_dzw_klient_nazwa(rodzaj), pierscien != NULL ? "próbki" : "poziom");
    zglos();
    return 0;
}

/* DZWIEK_OTWORZ b = SLUCHA: exactly the ring's size in its format, sealed against shrinking and growing (a shrunk ring
 * would end us with SIGBUS), writable (no write seal). Returns 0, or -1 when the client broke the rules. */
int mikrofon_otworz(int i, int rodzaj, int wolno, int fd, int format)
{
    size_t bajty = k1_dzw_pierscien_bajty(format);
    struct stat st;
    int pieczecie = fd >= 0 ? fcntl(fd, F_GET_SEALS) : -1;
    void *m;
    if (k1_dzw_klient_slucha(rodzaj) != K1_DZW_SLUCHA_PROBKI || !wolno || sluchacze[i].rodzaj != 0)
        return -1; /* no right to the samples (a zone without the switch "Mikrofon"), or it hears already */
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || bajty == 0 || (uint64_t)st.st_size != bajty ||
        pieczecie < 0 || (pieczecie & (F_SEAL_SHRINK | F_SEAL_GROW)) != (F_SEAL_SHRINK | F_SEAL_GROW) ||
        (m = mmap(NULL, bajty, PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        k1_log(TAG, "mikrofon: pierścień %s odrzucony", k1_dzw_klient_nazwa(rodzaj));
        klient_wyslij(i, k1_komunikat_nowy(K1_KOM_DZWIEK_NAGRANO, 0, 0, 1, 0, 0, 0));
        return 0; /* a ring we cannot use: refused, as a playing stream's */
    }
    nowy(i, rodzaj, m, format);
    return 0;
}

/* DZWIEK_ZAMKNIJ b = SLUCHA, or the client's channel ended (then it hears nothing more: powiedz 0). */
void mikrofon_zamknij(int i, int powiedz)
{
    koniec(i, powiedz);
    if (wstrzymal == i) { /* gui ended: its security screen with it */
        wstrzymal = -1;
        mik.wstrzymany = 0;
        klienci_stan_wszystkim();
    }
}

/* DZWIEK_MIKROFON of a client in slot i (its row `rodzaj`): 0, or -1 when it may not ask that. */
int mikrofon_prosba(int i, int rodzaj, const struct k1_komunikat *m)
{
    switch (m->a) {
    case K1_DZW_MIK_USTAW:
        if (!k1_dzw_klient_steruje(rodzaj))
            return -1;
        if (m->b != mik.poziom || m->c != mik.wyciszony)
            k1_log(TAG, "mikrofon: poziom %d%%%s", m->b, m->c ? ", wyciszony" : "");
        mik.poziom = m->b;
        mik.wyciszony = m->c;
        break;
    case K1_DZW_MIK_POZIOM:
        if (k1_dzw_klient_slucha(rodzaj) < K1_DZW_SLUCHA_POZIOM || (sluchacze[i].pierscien != NULL))
            return -1;
        if (m->b && sluchacze[i].rodzaj == 0)
            nowy(i, rodzaj, NULL, 0);
        else if (!m->b)
            koniec(i, 0);
        return 0;
    case K1_DZW_MIK_WSTRZYMAJ:
        if (rodzaj != K1_DZW_KLIENT_GUI)
            return -1;
        wstrzymal = m->b ? i : -1;
        if (mik.wstrzymany != m->b)
            k1_log(TAG, "mikrofon: %s", m->b ? "wstrzymany (ekran zabezpieczeń)" : "znów słychać");
        mik.wstrzymany = m->b;
        break;
    case K1_DZW_MIK_TESTOWY:
        if (rodzaj != K1_DZW_KLIENT_TEST)
            return -1;
        ton = m->b;
        k1_log(TAG, "mikrofon: ton testowy %s", ton ? "włączony" : "wyłączony");
        return 0;
    default: /* SESJA: step 2 */
        return -1;
    }
    klienci_stan_wszystkim();
    return 0;
}

/* The card's recording node from nadzorca (DZWIEK_KARTA NAGRYWANIE). */
void mikrofon_przyjmij(int fd, int nr, int urzadzenie)
{
    if (nagr_przyjmij(fd, nr, urzadzenie) == 0)
        klienci_stan_wszystkim();
}

int mikrofon_fd(void)
{
    return nagr_fd();
}

/* One period to every listener (its ring and the level, or the level ten times a second). */
static void do_sluchaczy(const float *s, int poziom)
{
    for (int i = 0; i < KLIENCI_MAKS; i++) {
        struct sluchacz *l = &sluchacze[i];
        if (l->rodzaj == 0)
            continue;
        if (l->pierscien != NULL) {
            k1_mik_do_pierscienia(l->pierscien, l->format, &l->zapisane, s, K1_DZW_OKRES);
            klient_wyslij(i, k1_komunikat_nowy(K1_KOM_DZWIEK_NAGRANO, 0, (int32_t)K1_DZW_OKRES, 0, poziom, 0, 0));
            continue;
        }
        l->poziom = poziom > l->poziom ? poziom : l->poziom;
        if (++l->okresy >= POZIOM_CO) {
            klient_wyslij(i, k1_komunikat_nowy(K1_KOM_DZWIEK_NAGRANO, 0, 0, 0, l->poziom, 0, 0));
            l->okresy = l->poziom = 0;
        }
    }
}

/* The card has a period (poll: POLLIN on mikrofon_fd). */
void mikrofon_okres(void)
{
    static uint8_t z_karty[K1_DZW_OKRES * 2 * 4];
    static float s[K1_DZW_OKRES * K1_DZW_KANALY];
    int n = nagr_czytaj(z_karty);
    if (n < 0) {
        k1_log(TAG, "mikrofon przestał nagrywać");
        for (int i = 0; i < KLIENCI_MAKS; i++)
            koniec(i, 1);
        nagr_zamknij();
        return;
    }
    if (n == 0)
        return;
    if (!byl_okres)
        k1_log(TAG, "mikrofon: pierwszy okres z karty (ton testowy: %s, wyciszony: %s, wstrzymany: %s)", ton ? "tak" : "nie",
               mik.wyciszony ? "tak" : "nie", mik.wstrzymany ? "tak" : "nie");
    byl_okres = 1;
    k1_mik_z_karty(s, z_karty, K1_DZW_OKRES, nagr_bajty(), nagr_kanaly());
    if (ton)
        k1_mik_ton(s, K1_DZW_OKRES, &ton_faza);
    k1_mik_wzmocnij(s, K1_DZW_OKRES, mik.wyciszony || mik.wstrzymany ? 0.0f : k1_mik_wzmocnienie(mik.poziom));
    do_sluchaczy(s, k1_mik_poziom(s, K1_DZW_OKRES));
}

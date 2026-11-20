/* K1 nadzorca — the graphics broker (0.10.9 step 5, ADR 0049 pkt 2, 7; plan T11, T14, T15, S44). The render node of the
 * card (karta.c, as for the film decoder) goes only to it — never to ekran, an application or a VMM.
 *  - The question at boot: a few seconds after the desktop, one broker (row POSREDNIK, UID 110) says whether the card
 *    can keep the zones apart (fast graphics possible) and ends; Ustawienia → Przeglądarka show it (POSR_KARTA). Ended
 *    without an answer: BLAD with its code.
 *  - The broker of a zone (its row's profile K1_PROFIL_POSREDNIK_<ZONE>, its own UID and cgroup): started with the
 *    zone's VMM, which gets its channel (step 6, only with the switch on); it ends with the VMM (posrednik_strefa_koniec)
 *    or when its channel closes, and is never started again by itself: a crash or a hang (K1_POSR_KONIEC_ZAWIESZENIE)
 *    ends fast graphics of that zone until the zone opens again (T15). In this step the test program asks for it
 *    (TEST_POSREDNIK — with b=1 the test engine: no card).
 *  - T14: its memory counts to the zone (posrednik_pamiec_mib: the rozdzielacz). */
#include "nadzorca.h"

int posrednik_kanal = -1; /* our end of the question's channel */
static struct {
    int stan, kod;
} karta = { K1_POSR_KARTA_SPRAWDZA, 0 };
static int zapytano, wezel_sprawdzony, wyslano = -1;

static void ustaw(int stan, int kod)
{
    karta.stan = stan;
    karta.kod = stan == K1_POSR_KARTA_NIE || stan == K1_POSR_KARTA_BLAD ? kod & 0xff : 0;
    k1_log(TAG, "karta graficzna a szybka grafika: stan %d, kod %d", karta.stan, karta.kod);
    wyslano = -1;
    posrednik_wyslij_ustawieniom();
}

/* Every loop: the question once, 4 s after the desktop started (a second after the film decoder's: one at a time). */
void posrednik_po_starcie(double t)
{
    static double kiedy;
    struct usluga *gui = usluga_profilu(K1_PROFIL_GUI);
    int fd;
    if (zapytano)
        return;
    if (!wezel_sprawdzony) { /* no card of i915: known at once (and Ustawienia never wait) */
        wezel_sprawdzony = 1;
        fd = karta_render();
        if (fd < 0) {
            zapytano = 1;
            ustaw(K1_POSR_KARTA_BRAK, 0);
            return;
        }
        close(fd);
    }
    if (gui == NULL || gui->pid <= 0)
        return;
    if (kiedy == 0) {
        kiedy = t + 4.0;
        return;
    }
    if (t < kiedy)
        return;
    zapytano = 1;
    fd = karta_render();
    if (fd < 0) {
        ustaw(K1_POSR_KARTA_BRAK, 0);
        return;
    }
    posrednik_kanal = karta_uruchom(usluga_profilu(K1_PROFIL_POSREDNIK), 0, 0, fd);
    close(fd);
    if (posrednik_kanal < 0)
        ustaw(K1_POSR_KARTA_BLAD, 0);
}

/* The answer to the question (POSR_UMIE); the broker ends by itself right after. */
void obsluz_posrednika(void)
{
    struct k1_komunikat k;
    enum k1_odbior_wynik w = k1_odbierz_komunikat(posrednik_kanal, K1_UID_POSREDNIK, K1_KIER_OD_POSREDNIKA, &k, NULL);
    if (w == K1_ODB_PUSTO)
        return;
    if (w == K1_ODB_JEST && k.typ == K1_KOM_POSR_UMIE)
        ustaw(k.a, k.b);
    else if (w != K1_ODB_KONIEC)
        k1_log(TAG, "pośrednik grafiki: odrzucono komunikat");
    if (w != K1_ODB_KONIEC && karta.stan == K1_POSR_KARTA_SPRAWDZA)
        return;
    close(posrednik_kanal);
    posrednik_kanal = -1;
}

/* A broker ended: the question without an answer is a failure (its code in Ustawienia); a zone's broker — why. */
void posrednik_koniec(const struct usluga *u, int status)
{
    int kod = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    int nr = k1_profil_posrednika(u->profil);
    if (u->profil == K1_PROFIL_POSREDNIK && karta.stan == K1_POSR_KARTA_SPRAWDZA)
        ustaw(K1_POSR_KARTA_BLAD, kod);
    else if (nr != 0)
        k1_log(TAG, "pośrednik grafiki strefy %d zakończył się (kod %d): %s", nr, kod,
               kod == 0                            ? "koniec strefy albo jej kanału"
               : kod == K1_POSR_KONIEC_ZAWIESZENIE ? "kontekst strefy zawiesił kartę — koniec szybkiej grafiki tej strefy"
               : kod == K1_KOD_SECCOMP             ? "zabronione wywołanie — koniec szybkiej grafiki tej strefy"
                                                   : "awaria — koniec szybkiej grafiki tej strefy");
}

/* Whether the card serves fast graphics, to Ustawienia (fd 4) when it changed or Ustawienia started. */
void posrednik_wyslij_ustawieniom(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_POSR_KARTA, 0, karta.stan, karta.kod, 0, 0, 0);
    if (ustaw_kanal < 0 || ustaw_kanal == wyslano)
        return;
    if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) == 0)
        wyslano = ustaw_kanal;
}

void posrednik_nowy_kanal_ustawien(void)
{
    wyslano = -1;
}

/* The broker of zone nr: the client's end of its channel (the VMM's, or the test program's), or -1. The card's broker
 * only where the card serves fast graphics; the test engine (test) always — with "the card's memory": a sparse memfd
 * of K1_POSR_BUFOR_MAKS bytes it shares as every buffer (step 6: the VMM maps it like a dma-buf; test image only). */
static int pamiec_testowa(void)
{
    int fd = k1_memfd_nowy("posrednik-testowy");
    if (fd >= 0 && ftruncate(fd, (off_t)K1_POSR_BUFOR_MAKS) != 0)
        close(fd), fd = -1;
    return fd;
}

int posrednik_dla_strefy(int nr, int test)
{
    struct usluga *u = karta_usluga_strefy(K1_PROFIL_POSREDNIK, nr);
    int fd = test ? pamiec_testowa() : karta_render(), klient;
    if (u == NULL || (!test && (fd < 0 || karta.stan != K1_POSR_KARTA_UMIE))) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    klient = karta_uruchom(u, nr, test, fd);
    if (fd >= 0)
        close(fd);
    if (klient >= 0)
        karta_oom_tlo(u->pid);
    return klient;
}

void posrednik_strefa_koniec(int nr)
{
    struct usluga *u = karta_usluga_strefy(K1_PROFIL_POSREDNIK, nr);
    if (u != NULL && u->pid > 0)
        kill(u->pid, SIGKILL);
}

int32_t posrednik_pamiec_mib(int nr)
{
    return karta_pamiec_mib(karta_usluga_strefy(K1_PROFIL_POSREDNIK, nr));
}

int posrednik_dziala(int nr)
{
    struct usluga *u = karta_usluga_strefy(K1_PROFIL_POSREDNIK, nr);
    return u != NULL && u->pid > 0;
}

int posrednik_karta_umie(void)
{
    return karta.stan == K1_POSR_KARTA_UMIE;
}

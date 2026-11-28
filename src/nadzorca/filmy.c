/* K1 nadzorca — the film decoder (0.10.9 step 2, ADR 0049 pkt 2, 3; plan T6, T8, S13). The render node of the card is
 * opened here and given to the decoder alone — never to ekran (it keeps the card node, DRM master), an application or a
 * VMM — and only a render node of the i915 driver (karta.c, shared with the graphics broker since step 5).
 *  - The question at boot: a few seconds after the desktop, one decoder (row FILMY, UID 109) says what the card decodes
 *    and ends; Ustawienia → Przeglądarka show it (FILMY_KARTA). Ended without an answer: BLAD with its code.
 *  - The decoder of a zone (its row's profile K1_PROFIL_FILMY_<ZONE>, its own UID and cgroup): since 0.13.2 step 3 (ADR
 *    0063 pkt 4) on request of the zone's VMM (STREFA_POLACZ c=K1_STREFA_FILMY — strefy_siec.c), only for a session with
 *    a graphics broker (its switch on) on a card that decodes, one at a time; the VMM gets its channel and closes it when
 *    no film context is left (the decoder ends by itself, exit 0). A decoder that ended otherwise (a crash, a signal):
 *    no more films by the card until the session opens again. It ends with the VMM (filmy_strefa_koniec). The test program
 *    asks for one too (TEST_FILMY — with b=1 the test decoder: grey frames, no card); so does the test zone in modes 23,
 *    24 (the test decoder, its broker the test engine).
 *  - T8: its memory counts to the zone (filmy_pamiec_mib: the rozdzielacz); the kernel takes it before a K1 service. */
#include "nadzorca.h"

int filmy_kanal = -1; /* our end of the question's channel */
static struct {
    int stan, bity, kod;
} karta = { K1_FILMY_KARTA_SPRAWDZA, 0, 0 };
static int zapytano, wezel_sprawdzony, wyslano = -1;
static struct {
    int dozwolone, test, zablokowane; /* 0.13.2 step 3: what the VMM of this start of the zone may ask for */
} dla_vmm[K1_STREFY_MAKS + 1];

static void ustaw(int stan, int bity, int kod)
{
    karta.stan = stan, karta.bity = stan == K1_FILMY_KARTA_UMIE ? bity : 0, karta.kod = kod & 0xff;
    k1_log(TAG, "karta graficzna a filmy: stan %d, kodeki 0x%x, kod %d", karta.stan, (unsigned)karta.bity, karta.kod);
    wyslano = -1;
    filmy_wyslij_ustawieniom();
}

/* Every loop: the question once, 3 s after the desktop started. */
void filmy_po_starcie(double t)
{
    static double kiedy;
    struct usluga *gui = usluga_profilu(K1_PROFIL_GUI);
    int fd;
    if (zapytano)
        return;
    if (!wezel_sprawdzony) { /* no card of i915: known at once, nothing to ask (and Ustawienia never wait) */
        wezel_sprawdzony = 1;
        fd = karta_render();
        if (fd < 0) {
            zapytano = 1;
            ustaw(K1_FILMY_KARTA_BRAK, 0, 0);
            return;
        }
        close(fd);
    }
    if (gui == NULL || gui->pid <= 0)
        return;
    if (kiedy == 0) {
        kiedy = t + 3.0;
        return;
    }
    if (t < kiedy)
        return;
    zapytano = 1;
    fd = karta_render();
    if (fd < 0) {
        ustaw(K1_FILMY_KARTA_BRAK, 0, 0);
        return;
    }
    filmy_kanal = karta_uruchom(usluga_profilu(K1_PROFIL_FILMY), 0, 0, fd);
    close(fd);
    if (filmy_kanal < 0)
        ustaw(K1_FILMY_KARTA_BLAD, 0, 0);
}

/* The answer to the question (FILMY_UMIE); the decoder ends by itself right after. */
void obsluz_filmy(void)
{
    struct k1_komunikat k;
    enum k1_odbior_wynik w = k1_odbierz_komunikat(filmy_kanal, K1_UID_FILMY, K1_KIER_OD_FILMOW, &k, NULL);
    if (w == K1_ODB_PUSTO)
        return;
    if (w == K1_ODB_JEST && k.typ == K1_KOM_FILMY_UMIE)
        ustaw(k.b, k.a, k.c);
    else if (w != K1_ODB_KONIEC)
        k1_log(TAG, "dekoder filmów: odrzucono komunikat");
    if (w != K1_ODB_KONIEC && karta.stan == K1_FILMY_KARTA_SPRAWDZA)
        return;
    close(filmy_kanal);
    filmy_kanal = -1;
}

/* A decoder ended (zakonczona): the question without an answer is a failure (its code in Ustawienia). */
void filmy_koniec(const struct usluga *u, int status)
{
    int nr = k1_profil_filmow(u->profil);
    if (u->profil == K1_PROFIL_FILMY && karta.stan == K1_FILMY_KARTA_SPRAWDZA)
        ustaw(K1_FILMY_KARTA_BLAD, 0, WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    if (nr >= 1 && nr <= K1_STREFY_MAKS && !(WIFEXITED(status) && WEXITSTATUS(status) == 0) && !dla_vmm[nr].zablokowane) {
        dla_vmm[nr].zablokowane = 1; /* 0.13.2 step 3: a crash — the films of this session go to the processor */
        k1_log(TAG, "strefa %d: dekoder filmów zakończył się błędem (kod %d) — do końca sesji filmy procesorem", nr,
               WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
    }
}

/* What the card decodes, to Ustawienia (fd 4) when it changed or Ustawienia started. */
void filmy_wyslij_ustawieniom(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_FILMY_KARTA, 0, karta.bity, karta.stan,
                                              karta.stan == K1_FILMY_KARTA_NIC || karta.stan == K1_FILMY_KARTA_BLAD
                                                  ? karta.kod : 0, 0, 0);
    if (ustaw_kanal < 0 || ustaw_kanal == wyslano)
        return;
    if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) == 0)
        wyslano = ustaw_kanal;
}

void filmy_nowy_kanal_ustawien(void)
{
    wyslano = -1;
}

/* The decoder of zone nr: the client's end of its channel (the VMM's, or the test program's), or -1. The card's decoder
 * only where the card decodes; the test decoder (test) always. */
int filmy_dla_strefy(int nr, int test)
{
    struct usluga *u = karta_usluga_strefy(K1_PROFIL_FILMY, nr);
    int fd = test ? -1 : karta_render(), klient;
    if (u == NULL || (!test && (fd < 0 || karta.stan != K1_FILMY_KARTA_UMIE))) {
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

/* 0.13.2 step 3: a new start of zone nr — may its VMM ask for a decoder (a graphics broker of the session: `posrednik`;
 * the test zone in modes 23, 24: `test`, the test decoder). */
void filmy_strefa_start(int nr, int posrednik, int test)
{
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return;
    dla_vmm[nr].dozwolone = posrednik && (test || karta.stan == K1_FILMY_KARTA_UMIE);
    dla_vmm[nr].test = test, dla_vmm[nr].zablokowane = 0;
}

/* The VMM of zone nr asks for its decoder (STREFA_POLACZ c=K1_STREFA_FILMY): the client's end, -1 (refused: films on
 * the processor), or -2 (the one before still ends — the VMM closed its channel a moment ago: it asks again soon). */
int filmy_dla_vmm(int nr)
{
    struct usluga *u = karta_usluga_strefy(K1_PROFIL_FILMY, nr);
    int fd;
    if (u != NULL && u->pid > 0 && nr >= 1 && nr <= K1_STREFY_MAKS && dla_vmm[nr].dozwolone && !dla_vmm[nr].zablokowane)
        return -2;
    if (nr < 1 || nr > K1_STREFY_MAKS || !dla_vmm[nr].dozwolone || dla_vmm[nr].zablokowane || u == NULL) {
        k1_log(TAG, "strefa %d: dekoder filmów odmówiony (%s)", nr,
               nr < 1 || nr > K1_STREFY_MAKS || !dla_vmm[nr].dozwolone ? "bez szybkiej grafiki albo karta nie umie"
                                                                       : "poprzedni zakończył się błędem");
        return -1;
    }
    fd = filmy_dla_strefy(nr, dla_vmm[nr].test);
    if (fd >= 0)
        k1_log(TAG, "strefa %d: dekoder filmów na prośbę VMM%s", nr, dla_vmm[nr].test ? " (dekoder testowy)" : "");
    return fd;
}

/* The zone's VMM ended: its decoder ends too. */
void filmy_strefa_koniec(int nr)
{
    struct usluga *u = karta_usluga_strefy(K1_PROFIL_FILMY, nr);
    if (u != NULL && u->pid > 0)
        kill(u->pid, SIGKILL);
}

/* T8: the memory of zone nr's decoder (its cgroup), MiB; 0 when it does not run. */
int32_t filmy_pamiec_mib(int nr)
{
    return karta_pamiec_mib(karta_usluga_strefy(K1_PROFIL_FILMY, nr));
}

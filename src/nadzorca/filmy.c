/* K1 nadzorca — the film decoder (0.10.9 step 2, ADR 0049 pkt 2, 3; plan T6, T8, S13). The render node of the card is
 * opened here and given to the decoder alone — never to ekran (it keeps the card node, DRM master), an application or a
 * VMM — and only a render node of the i915 driver (K1 decodes films with Intel's cards only; another card, a virtual
 * machine: "no card", nothing starts).
 *  - The question at boot: a few seconds after the desktop, one decoder (row FILMY, UID 109) says what the card decodes
 *    and ends; Ustawienia → Przeglądarka show it (FILMY_KARTA). Ended without an answer: BLAD with its code.
 *  - The decoder of a zone (its row's profile K1_PROFIL_FILMY_<ZONE>, its own UID and cgroup): started with the zone's
 *    VMM, which gets its channel (step 3); it ends with the VMM (filmy_strefa_koniec) or when its channel closes. In this
 *    step the test program asks for it (TEST_FILMY — with b=1 the test decoder: grey frames, no card).
 *  - T8: its memory counts to the zone (filmy_pamiec_mib: the rozdzielacz); the kernel takes it before a K1 service. */
#include "nadzorca.h"

#include <libgen.h>

int filmy_kanal = -1; /* our end of the question's channel */
static struct {
    int stan, bity, kod;
} karta = { K1_FILMY_KARTA_SPRAWDZA, 0, 0 };
static int zapytano, wezel_sprawdzony, wyslano = -1;

/* The render node of the i915 driver (read-write), or -1. */
static int wezel_render(void)
{
    for (int i = 128; i < 136; i++) {
        char s[96], cel[96];
        ssize_t n;
        snprintf(s, sizeof s, SYS "/class/drm/renderD%d/device/driver", i);
        n = readlink(s, cel, sizeof cel - 1);
        if (n <= 0)
            continue;
        cel[n] = '\0';
        if (strcmp(basename(cel), "i915") != 0)
            continue;
        snprintf(s, sizeof s, DEV "/dri/renderD%d", i);
        return open(s, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    }
    return -1;
}

/* Starts decoder `u` (argument: the zone, 0 for the question; test: grey frames) with fd 3 = a new channel, fd 4 = the
 * render node or /dev/null. Returns our end of the channel (the client's end: the caller's), or -1. */
static int uruchom_dekoder(struct usluga *u, int strefa, int test, int karta_fd)
{
    int para[2], tak = 1, fds[2];
    if (u == NULL || u->pid > 0 || k1_kanal_utworz(para) != 0)
        return -1;
    fds[0] = para[0];
    fds[1] = karta_fd >= 0 ? karta_fd : zero;
    snprintf(u->argument, sizeof u->argument, "%d", strefa);
    snprintf(u->argument2, sizeof u->argument2, "%s", test ? "t" : "k");
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0 ||
        uruchom(u, &profile[u->profil], fds, 2) <= 0) {
        close(para[0]);
        close(para[1]);
        return -1;
    }
    close(para[0]);
    k1_log(TAG, "dekoder filmów %s (strefa %d%s): pid %d, uid %d", u->nazwa, strefa, test ? ", dekoder testowy" : "",
           (int)u->pid, (int)profile[u->profil].uid);
    return para[1];
}

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
        fd = wezel_render();
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
    fd = wezel_render();
    if (fd < 0) {
        ustaw(K1_FILMY_KARTA_BRAK, 0, 0);
        return;
    }
    filmy_kanal = uruchom_dekoder(usluga_profilu(K1_PROFIL_FILMY), 0, 0, fd);
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
    if (u->profil == K1_PROFIL_FILMY && karta.stan == K1_FILMY_KARTA_SPRAWDZA)
        ustaw(K1_FILMY_KARTA_BLAD, 0, WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
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

static void oom_tlo(pid_t pid)
{
    char s[64];
    int fd;
    snprintf(s, sizeof s, "/rdzen/jadro/proc/%d/oom_score_adj", (int)pid);
    fd = open(s, O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, "800", 3) != 3)
        k1_log(TAG, "dekoder filmów: oom_score_adj: %s", strerror(errno));
    if (fd >= 0)
        close(fd);
}

/* The decoder of zone nr: the client's end of its channel (the VMM's, or the test program's), or -1. The card's decoder
 * only where the card decodes; the test decoder (test) always. */
int filmy_dla_strefy(int nr, int test)
{
    struct usluga *u = nr >= 1 && nr <= K1_STREFY_MAKS ? usluga_profilu(K1_PROFIL_FILMY + nr) : NULL;
    int fd = test ? -1 : wezel_render(), klient;
    if (u == NULL || (!test && (fd < 0 || karta.stan != K1_FILMY_KARTA_UMIE))) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    klient = uruchom_dekoder(u, nr, test, fd);
    if (fd >= 0)
        close(fd);
    if (klient >= 0)
        oom_tlo(u->pid);
    return klient;
}

/* The zone's VMM ended: its decoder ends too. */
void filmy_strefa_koniec(int nr)
{
    struct usluga *u = nr >= 1 && nr <= K1_STREFY_MAKS ? usluga_profilu(K1_PROFIL_FILMY + nr) : NULL;
    if (u != NULL && u->pid > 0)
        kill(u->pid, SIGKILL);
}

/* T8: the memory of zone nr's decoder (its cgroup), MiB; 0 when it does not run. */
int32_t filmy_pamiec_mib(int nr)
{
    struct usluga *u = nr >= 1 && nr <= K1_STREFY_MAKS ? usluga_profilu(K1_PROFIL_FILMY + nr) : NULL;
    char s[96];
    long long b;
    if (u == NULL || u->pid <= 0)
        return 0;
    snprintf(s, sizeof s, K1_CGROUP "/%s/memory.current", profile[u->profil].nazwa);
    b = cgroup_liczba(s, NULL);
    return b > 0 ? (int32_t)(b >> 20) : 0;
}

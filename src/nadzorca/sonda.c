/* K1 nadzorca — 0.10.9 step 1, TRIAL CODE (the trial branch only, never in main): the card probe (src/aplikacje/sonda).
 * Opens for it the render node of the graphics card, the memory counter of its own cgroup (S26) and the battery's
 * power readings (T9 d), and opens its window once, after the desktop, only when a render node exists — in QEMU (no
 * Intel card, so no i915 render node) it never runs, so the tests and the reference pictures stay as they are. */
#include "nadzorca.h"

static int otworz_albo_zero(const char *sciezka)
{
    int fd = open(sciezka, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        fd = fcntl(zero, F_DUPFD_CLOEXEC, 3);
    return fd;
}

/* The probe's descriptors from fd 4: render node (O_RDWR), memory.current of its cgroup, then power_now,
 * current_now, voltage_now of the first battery (/dev/null for what is missing). Returns how many, or -1. */
int otworz_sonde(int *fd, int profil)
{
    static const char *const bateria[] = { "power_now", "current_now", "voltage_now" };
    char sciezka[128];
    int i, n = 0;
    if (k1_klatka_grupa(&profile[profil]) != 0)
        return -1;
    fd[n] = open(DEV "/dri/renderD128", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (fd[n] < 0)
        fd[n] = fcntl(zero, F_DUPFD_CLOEXEC, 3);
    n++;
    snprintf(sciezka, sizeof sciezka, K1_CGROUP "/%s/memory.current", profile[profil].nazwa);
    fd[n++] = otworz_albo_zero(sciezka);
    for (i = 0; i < 3; i++) {
        snprintf(sciezka, sizeof sciezka, SYS "/class/power_supply/BAT0/%s", bateria[i]);
        fd[n] = open(sciezka, O_RDONLY | O_CLOEXEC);
        if (fd[n] < 0) {
            snprintf(sciezka, sizeof sciezka, SYS "/class/power_supply/BAT1/%s", bateria[i]);
            fd[n] = otworz_albo_zero(sciezka);
        }
        n++;
    }
    for (i = 0; i < n; i++)
        if (fd[i] < 0) {
            while (n-- > 0)
                if (fd[n] >= 0)
                    close(fd[n]);
            return -1;
        }
    return n;
}

/* Once: 5 s after the desktop started, if the card has a render node. */
void sonda_okresowe(double t)
{
    static double od;
    static int zrobione;
    struct usluga *gui = usluga_profilu(K1_PROFIL_GUI);
    if (zrobione || gui == NULL || gui->pid <= 0) {
        od = 0;
        return;
    }
    if (od == 0) {
        od = t + 5.0;
        return;
    }
    if (t < od)
        return;
    zrobione = 1;
    if (access(DEV "/dri/renderD128", F_OK) != 0) {
        k1_log(TAG, "sonda: brak węzła render karty — okno sondy się nie otwiera");
        return;
    }
    k1_log(TAG, "sonda: jest węzeł render karty — otwieram okno sondy");
    uruchom_aplikacje(K1_APL_SONDA);
}

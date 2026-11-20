/* K1 nadzorca — the services of the graphics card (0.10.9, ADR 0049 pkt 2): what the film decoder (filmy.c, step 2) and
 * the graphics broker (posrednik.c, step 5) share. The render node of the card is opened here and given only to them —
 * never to ekran (it keeps the card node, DRM master), an application or a VMM — and only a render node of the i915
 * driver (K1 uses Intel's cards only; another card, a virtual machine: "no card", nothing starts). Each of them runs
 * once at boot (the question: what the card can do) and once per zone (its own profile, UID and cgroup: the zone's row
 * + the service's base profile), with fd 3 = a new channel to its client, fd 4 = the render node or /dev/null. */
#include "nadzorca.h"

#include <libgen.h>

int karta_render(void)
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

int karta_uruchom(struct usluga *u, int strefa, int test, int karta_fd)
{
    int para[2], tak = 1, fds[2];
    if (u == NULL || u->pid > 0 || k1_kanal_utworz(para) != 0)
        return -1;
    fds[0] = para[0];
    fds[1] = karta_fd >= 0 ? karta_fd : zero;
    snprintf(u->argument, sizeof u->argument, "%d", strefa);
    /* "k" the card; "t" the test engine for the test program (its client: UID 0); "s" (0.10.9 step 6, the broker only)
     * the test engine for the test zone's VMM (its client: the zone's UID) */
    snprintf(u->argument2, sizeof u->argument2, "%s", test == 2 ? "s" : test ? "t" : "k");
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0 ||
        uruchom(u, &profile[u->profil], fds, 2) <= 0) {
        close(para[0]);
        close(para[1]);
        return -1;
    }
    close(para[0]);
    k1_log(TAG, "%s (strefa %d%s): pid %d, uid %d", u->nazwa, strefa, test ? ", tryb testowy" : "", (int)u->pid,
           (int)profile[u->profil].uid);
    return para[1];
}

void karta_oom_tlo(pid_t pid)
{
    char s[64];
    int fd;
    snprintf(s, sizeof s, "/rdzen/jadro/proc/%d/oom_score_adj", (int)pid);
    fd = open(s, O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, "800", 3) != 3)
        k1_log(TAG, "usługa karty: oom_score_adj: %s", strerror(errno));
    if (fd >= 0)
        close(fd);
}

int32_t karta_pamiec_mib(const struct usluga *u)
{
    char s[96];
    long long b;
    if (u == NULL || u->pid <= 0)
        return 0;
    snprintf(s, sizeof s, K1_CGROUP "/%s/memory.current", profile[u->profil].nazwa);
    b = cgroup_liczba(s, NULL);
    return b > 0 ? (int32_t)(b >> 20) : 0;
}

struct usluga *karta_usluga_strefy(int baza, int nr)
{
    return nr >= 1 && nr <= K1_STREFY_MAKS ? usluga_profilu(baza + nr) : NULL;
}

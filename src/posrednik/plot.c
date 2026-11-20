/* K1 posrednik — the fences (0.10.9 step 5): when the card finished the work of the guest. Each batch gives an out-fence
 * (a sync_file the kernel signals when the batch is done); the last one of each timeline (1 + the batch's engine — as
 * virglrenderer's drm_timeline) is kept. POSR_PLOT asks "tell me when the last batch of this timeline is done": the
 * broker waits on a copy of that fence and answers POSR_PLOT_GOTOWY. When a fence is done, the card is asked whether
 * the context of that batch hung it (GET_RESET_STATS: batch_active) — a hang ends fast graphics of the zone (T15, S39).
 * The test engine gives no fences: every POSR_PLOT is answered at once. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <drm/i915_drm.h>

#include "k1_log.h"
#include "posrednik.h"

static struct {
    int fd;
    uint32_t ctx;
} ostatni[K1_POSR_GOSCIE + 1][K1_POSR_PIERSCIENIE] = {
    [0 ... K1_POSR_GOSCIE] = { [0 ... K1_POSR_PIERSCIENIE - 1] = { -1, 0 } }
};
static struct {
    int fd; /* -1: a free place */
    uint32_t gosc, pierscien, ctx;
    uint64_t nr;
} czeka[K1_POSR_PLOTY_MAKS] = { [0 ... K1_POSR_PLOTY_MAKS - 1] = { -1, 0, 0, 0, 0 } };

void plot_nowy(uint32_t gosc, uint32_t pierscien, int fd, uint32_t ctx)
{
    if (gosc < 1 || gosc > K1_POSR_GOSCIE || pierscien >= K1_POSR_PIERSCIENIE) {
        if (fd >= 0)
            close(fd);
        return;
    }
    if (ostatni[gosc][pierscien].fd >= 0)
        close(ostatni[gosc][pierscien].fd);
    ostatni[gosc][pierscien].fd = fd, ostatni[gosc][pierscien].ctx = ctx;
}

static void gotowy(uint32_t gosc, uint32_t pierscien, uint64_t nr)
{
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_PLOT_GOTOWY, pierscien, k1_kom_niskie(nr), k1_kom_wysokie(nr), (int32_t)gosc, 0,
                             0),
           -1);
}

int plot_czekaj(uint32_t gosc, uint32_t pierscien, uint64_t nr)
{
    if (ostatni[gosc][pierscien].fd < 0) { /* nothing on this timeline: done (virglrenderer: the same) */
        gotowy(gosc, pierscien, nr);
        return 0;
    }
    for (uint32_t i = 0; i < K1_POSR_PLOTY_MAKS; i++)
        if (czeka[i].fd < 0) {
            czeka[i].fd = fcntl(ostatni[gosc][pierscien].fd, F_DUPFD_CLOEXEC, 0);
            if (czeka[i].fd < 0)
                return K1_POSR_BLAD_LIMIT;
            czeka[i].gosc = gosc, czeka[i].pierscien = pierscien, czeka[i].ctx = ostatni[gosc][pierscien].ctx;
            czeka[i].nr = nr;
            return 0;
        }
    return K1_POSR_BLAD_LIMIT;
}

void plot_gosc_koniec(uint32_t gosc)
{
    for (uint32_t p = 0; p < K1_POSR_PIERSCIENIE; p++)
        if (ostatni[gosc][p].fd >= 0)
            close(ostatni[gosc][p].fd), ostatni[gosc][p].fd = -1;
    for (uint32_t i = 0; i < K1_POSR_PLOTY_MAKS; i++)
        if (czeka[i].fd >= 0 && czeka[i].gosc == gosc) { /* nobody waits for them any more: answered, as done */
            gotowy(gosc, czeka[i].pierscien, czeka[i].nr);
            close(czeka[i].fd);
            czeka[i].fd = -1;
        }
}

int plot_deskryptory(struct pollfd *p, int maks)
{
    int n = 0;
    for (uint32_t i = 0; i < K1_POSR_PLOTY_MAKS && n < maks; i++)
        if (czeka[i].fd >= 0)
            p[n++] = (struct pollfd){ czeka[i].fd, POLLIN, 0 };
    return n;
}

/* Did context ctx hang the card (it was guilty in a reset)? */
static int winny(uint32_t ctx)
{
    struct drm_i915_reset_stats s = { .ctx_id = ctx };
    return silnik->ioctl(DRM_IOCTL_I915_GET_RESET_STATS, &s) == 0 && s.batch_active > 0;
}

void plot_gotowe(const struct pollfd *p, int n)
{
    for (int j = 0; j < n; j++) {
        if (!(p[j].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)))
            continue;
        for (uint32_t i = 0; i < K1_POSR_PLOTY_MAKS; i++)
            if (czeka[i].fd == p[j].fd) {
                if (winny(czeka[i].ctx)) {
                    posr_ban(czeka[i].ctx);
                    zawieszona = 1;
                }
                gotowy(czeka[i].gosc, czeka[i].pierscien, czeka[i].nr);
                close(czeka[i].fd);
                czeka[i].fd = -1;
                break;
            }
    }
}

/* K1 nadzorca — why a zone ended (strefy_powod.h). Part of nadzorca. Before 0.10.1 every SIGKILL of a VMM was "the
 * computer ran out of memory" — also when the session's own limit (its sandbox's memory.max) killed it, which on a
 * laptop with plenty of free memory told the user the wrong thing (S3). The kernel tells them apart: the cgroup's
 * "oom" event counts only its own limit (Documentation/admin-guide/cgroup-v2.rst), "oom_kill" any OOM killer. */
#include <signal.h>
#include <sys/wait.h>

#include "k1_komunikat.h"
#include "strefy_powod.h"

int strefa_powod_konca(int status, const struct strefa_zdarzenia *d)
{
    if (WIFEXITED(status)) {
        int k = WEXITSTATUS(status);
        return k == 0 ? 0 : k >= K1_STREFA_KONIEC_OD && k <= K1_STREFA_KONIEC_AWARIA ? k : K1_STREFA_KONIEC_BLAD;
    }
    if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL)
        return K1_STREFA_KONIEC_BLAD;
    if (d == NULL || d->oom < 0 || d->oom_kill < 0) /* unknown: as before 0.10.1 */
        return K1_STREFA_KONIEC_PAMIEC_K1;
    if (d->oom > 0)
        return K1_STREFA_KONIEC_LIMIT;
    return d->oom_kill > 0 ? K1_STREFA_KONIEC_PAMIEC_K1 : K1_STREFA_KONIEC_BLAD;
}

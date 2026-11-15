/* K1 strefa — the watchman: a thread that notices when the main thread stops moving (no poll for STOI_S seconds:
 * the window gets no pictures, "close" from ekran is not seen) and says once where every thread of the machine is —
 * the main thread's step, who holds mz.zamek, each processor (in the guest, at an exit, waiting for the lock, holding
 * it; its last exit and device). Numbers and our own constant names only: nothing the guest wrote (invariant 8).
 * It only looks: it changes nothing and never ends the machine. */
#include <poll.h>
#include <pthread.h>
#include <stdio.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "strefa.h"

#define STOI_S 15.0

static const char *const MIEJSCE[] = { [ST_W_GOSCIU] = "w gościu", [ST_WYJSCIE] = "przy wyjściu",
                                       [ST_CZEKA] = "czeka na zamek", [ST_ZAMEK] = "ma zamek" };

void zamek_wez(const char *kto)
{
    pthread_mutex_lock(&mz.zamek);
    mz.zamek_kto = kto;
}

void zamek_oddaj(void)
{
    mz.zamek_kto = NULL;
    pthread_mutex_unlock(&mz.zamek);
}

void petla_krok(const char *krok)
{
    mz.petla_krok = krok;
    mz.petla_t = k1_teraz();
}

static void opisz(double stoi)
{
    char p[ST_PROCESORY_MAKS * 64] = "";
    const char *kto = mz.zamek_kto;
    size_t n = 0;
    int i;
    for (i = 0; i < mz.n_cpu && i < ST_PROCESORY_MAKS && n < sizeof p; i++) {
        const struct procesor *c = &mz.cpu[i];
        int g = c->gdzie;
        n += (size_t)snprintf(p + n, sizeof p - n, "; procesor %d: %s (wyjście %u, urządzenie %d)", i,
                              g >= 0 && g <= ST_ZAMEK ? MIEJSCE[g] : "?", c->wyjscie, c->urz);
    }
    k1_log(TAG, "stróż: pętla główna stoi od %.0f s (krok: %s; zamek: %s)%s", stoi,
           mz.petla_krok != NULL ? mz.petla_krok : "?", kto != NULL ? kto : "wolny", p);
}

static void *straz(void *arg)
{
    int zgloszone = 0;
    (void)arg;
    while (!mz.koniec) {
        double stoi;
        poll(NULL, 0, 5000);
        stoi = k1_teraz() - mz.petla_t;
        if (stoi < STOI_S)
            zgloszone = 0;
        else if (!zgloszone && !mz.koniec) {
            opisz(stoi);
            zgloszone = 1;
        }
    }
    return NULL;
}

int stroz_start(void)
{
    pthread_t t;
    mz.petla_t = k1_teraz();
    return pthread_create(&t, NULL, straz, NULL) == 0 ? 0 : -1;
}

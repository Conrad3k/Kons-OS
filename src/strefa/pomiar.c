/* K1 strefa — the measurement of the picture's way (pomiar.h). */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <linux/virtio_gpu.h>

#include "k1_log.h"
#include "k1_pomiar.h"
#include "k1_teraz.h"
#include "pomiar.h"
#include "strefa.h"

#define POMIAR_ODSTEP 10.0
#define WEJSCIE_MAKS_US 1000000u /* input without a picture within 1 s: it changed nothing on the screen, not counted */

static pthread_mutex_t zamek = PTHREAD_MUTEX_INITIALIZER;
static struct {
    uint32_t gosc[POMIAR_GOSC_LICZB];
    int gosc_jest;                    /* a line came since the last log */
    struct k1_pomiar przen, okno, ekran, most, wej;
    uint64_t zatw_us, wej_us;         /* the last commit / input still without its answer (0: none) */
    unsigned long klatki, obrazy, mapy[4]; /* copies into the window, the guest's pictures; MAP_BLOB kinds (since start) */
    double t;
} pz;

static void dodaj(struct k1_pomiar *p, uint64_t us)
{
    pthread_mutex_lock(&zamek);
    k1_pomiar_dodaj(p, us);
    pthread_mutex_unlock(&zamek);
}

void pomiar_gosc(const uint32_t *w)
{
    pthread_mutex_lock(&zamek);
    memcpy(pz.gosc, w, sizeof pz.gosc);
    pz.gosc_jest = 1;
    pthread_mutex_unlock(&zamek);
}

void pomiar_przeniesienie(uint64_t us) { dodaj(&pz.przen, us); }
void pomiar_most(uint64_t us) { dodaj(&pz.most, us); }

void pomiar_okno(uint64_t us)
{
    pthread_mutex_lock(&zamek);
    k1_pomiar_dodaj(&pz.okno, us);
    pz.klatki++;
    pthread_mutex_unlock(&zamek);
}

void pomiar_zatwierdzone(void)
{
    pthread_mutex_lock(&zamek);
    pz.zatw_us = k1_pomiar_teraz_us();
    pthread_mutex_unlock(&zamek);
}

void pomiar_ramka(void)
{
    pthread_mutex_lock(&zamek);
    if (pz.zatw_us != 0)
        k1_pomiar_dodaj(&pz.ekran, k1_pomiar_teraz_us() - pz.zatw_us);
    pz.zatw_us = 0;
    pthread_mutex_unlock(&zamek);
}

void pomiar_mapa(uint32_t rodzaj)
{
    pthread_mutex_lock(&zamek);
    pz.mapy[rodzaj < 4 ? rodzaj : 0]++;
    pthread_mutex_unlock(&zamek);
}

void pomiar_wejscie(void)
{
    uint64_t t = k1_pomiar_teraz_us();
    pthread_mutex_lock(&zamek);
    if (pz.wej_us == 0 || t - pz.wej_us > WEJSCIE_MAKS_US) /* the first input since the last picture: the longest wait */
        pz.wej_us = t;
    pthread_mutex_unlock(&zamek);
}

void pomiar_obraz(void)
{
    uint64_t t = k1_pomiar_teraz_us();
    pthread_mutex_lock(&zamek);
    pz.obrazy++;
    if (pz.wej_us != 0 && t - pz.wej_us <= WEJSCIE_MAKS_US)
        k1_pomiar_dodaj(&pz.wej, t - pz.wej_us);
    pz.wej_us = 0;
    pthread_mutex_unlock(&zamek);
}

/* "1,2/3,4 ms" of the guest's pair w[i], w[i + 1] */
static const char *para(char *b, size_t n, const uint32_t *w, int i)
{
    k1_pomiar_opis_liczb(w[i], w[i + 1], b, n);
    return b;
}

static const char *opis(char *b, size_t n, const struct k1_pomiar *p)
{
    k1_pomiar_opis(p, b, n);
    return b;
}

void pomiar_loguj(void)
{
    char a[24], b[24], c[24], d[24], e[24];
    double t = k1_teraz();
    if (t - pz.t < POMIAR_ODSTEP)
        return;
    pthread_mutex_lock(&zamek);
    pz.t = t;
    if (pz.gosc_jest)
        k1_log(TAG, "pomiar strefy (10 s, średnio/95%%): klatki %u, bufory karty %u, pamięci %u; czekanie na kartę %s, "
                    "kopia %s, scena %s, wysłanie %s", pz.gosc[0], pz.gosc[1], pz.gosc[2], para(a, sizeof a, pz.gosc, 3),
               para(b, sizeof b, pz.gosc, 5), para(c, sizeof c, pz.gosc, 7), para(d, sizeof d, pz.gosc, 9));
    if (pz.obrazy > 0 || pz.klatki > 0)
        k1_log(TAG, "pomiar VMM (10 s, średnio/95%%): obrazy gościa %lu, przeniesienie %s; okno %lu klatek, kopia %s, "
                    "ekran %s; most %u próśb, %s; wejście→obraz %s; mapy karty WB %lu, UC %lu, WC %lu", pz.obrazy,
               opis(a, sizeof a, &pz.przen), pz.klatki, opis(b, sizeof b, &pz.okno), opis(c, sizeof c, &pz.ekran),
               pz.most.n, opis(d, sizeof d, &pz.most), opis(e, sizeof e, &pz.wej),
               pz.mapy[VIRTIO_GPU_MAP_CACHE_CACHED], pz.mapy[VIRTIO_GPU_MAP_CACHE_UNCACHED],
               pz.mapy[VIRTIO_GPU_MAP_CACHE_WC]);
    k1_pomiar_zeruj(&pz.przen);
    k1_pomiar_zeruj(&pz.okno);
    k1_pomiar_zeruj(&pz.ekran);
    k1_pomiar_zeruj(&pz.most);
    k1_pomiar_zeruj(&pz.wej);
    pz.gosc_jest = 0;
    pz.klatki = pz.obrazy = 0;
    pthread_mutex_unlock(&zamek);
}

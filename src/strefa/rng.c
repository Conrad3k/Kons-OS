/* K1 strefa — virtio-rng (virtio 1.2, section 5.4): randomness for the guest from K1's kernel (getrandom). */
#include <sys/random.h>

#include <linux/virtio_ids.h>

#include "strefa.h"

#define PROSBA_MAKS 4096u /* bytes per request (Linux asks for up to 64 at a time) */

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w, bylo = 0;
    while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
        uint8_t bufor[PROSBA_MAKS];
        uint64_t n = l.dl_zapis < sizeof bufor ? l.dl_zapis : sizeof bufor;
        ssize_t r = n > 0 ? getrandom(bufor, (size_t)n, 0) : 0;
        if (r < 0)
            r = 0;
        vq_zapisz(&l, 0, bufor, (uint64_t)r);
        vq_oddaj(&u->q[k], u->pamiec, l.glowa, (uint32_t)r);
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
    return w;
}

const struct mmio_ops rng_ops = {
    .id = VIRTIO_ID_RNG,
    .n_kolejek = 1,
    .powiadom = powiadom,
};

/* K1 strefa — virtio-console (virtio 1.2, section 5.3), one port, no features: the guest's console (hvc0). What the
 * guest writes is never shown to the user; in the zone whose row says so (the test zone, K1_STREFY) its lines go to
 * our log — made safe (control characters and invalid UTF-8 → '?') and limited; in every other zone only the
 * number of bytes is counted (invariant 8: a browser's guest may print anything). Nothing is sent to the guest. */
#include <string.h>

#include <linux/virtio_ids.h>

#include "k1_log.h"
#include "k1_rysuj.h"
#include "strefa.h"

#define LINIA_MAKS 160
#define LINIE_MAKS 3000 /* lines of one machine in the log, then only the count */

static char linia[LINIA_MAKS + 1];
static size_t dl;
static unsigned long linie, bajty;

/* Valid UTF-8 stays (Polish letters), every invalid sequence becomes '?'. */
static void oczysc(void)
{
    char b[LINIA_MAKS + 1];
    size_t i = 0, j = 0;
    while (i < dl) {
        uint32_t kod;
        int n = k1_utf8(linia + i, (uint32_t)(dl - i), &kod);
        if (kod == 0xfffd || kod < 0x20 || (kod >= 0x7f && kod < 0xa0))
            b[j++] = '?';
        else {
            memcpy(b + j, linia + i, (size_t)n);
            j += (size_t)n;
        }
        i += (size_t)n;
    }
    memcpy(linia, b, j);
    dl = j;
}

static void wypisz(void)
{
    const char *t = linia;
    oczysc();
    linia[dl] = '\0';
    if (t[0] == '[' && strchr(t, ']') != NULL) /* the guest kernel's "[   0.123456] ": K1's log has its own time */
        t = strchr(t, ']') + 1 + (strchr(t, ']')[1] == ' ');
    if (linie < LINIE_MAKS && mz.opis != NULL && mz.opis->log_goscia)
        k1_log(TAG, "gość: %s", t);
    else if (linie == LINIE_MAKS)
        k1_log(TAG, "gość: dalsze wiersze tylko liczone");
    linie++;
    dl = 0;
}

static void znak(uint8_t c)
{
    bajty++;
    if (c == '\n') {
        wypisz();
        return;
    }
    if (c == '\r')
        return;
    if (dl == LINIA_MAKS)
        wypisz();
    linia[dl++] = (char)(c >= 0x20 && c != 0x7f ? c : '?');
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w, bylo = 0;
    if (k != 1)
        return 0; /* receiveq: buffers for us to write into; we never do */
    while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
        int i;
        uint32_t j;
        for (i = 0; i < l.n_odczyt; i++)
            for (j = 0; j < l.k[i].dl; j++)
                znak(l.k[i].p[j]);
        vq_oddaj(&u->q[k], u->pamiec, l.glowa, 0);
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
    return w;
}

const struct mmio_ops konsola_ops = {
    .id = VIRTIO_ID_CONSOLE,
    .n_kolejek = 2,
    .rozmiar_konfig = 0,
    .powiadom = powiadom,
};

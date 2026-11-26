/* K1 gosc — the test zone's sound (0.12.0 step 3, TRYB_DZWIEK): a tone through the guest kernel's virtio-snd driver, as
 * the browser plays through ALSA — the VMM's sound card takes it to K1's sound service (scripts/test-dzwiek.sh hears it
 * in the WAV file of QEMU's card, clicks the speaker on the window's bar: the session muted — silence — and back). ALSA
 * without alsa-lib, as K1's sound service (src/dzwiek/karta.c): plain writes of periods. 0.12.3 (ADR 0055): in S32_LE —
 * the 32 bits a browser's alsa-lib gives under dmix — so the VMM's conversion into K1's F32 ring is tested on the way.
 * TON_S seconds of TON_HZ, then the end of the machine. Counts in the log only. */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <sound/asound.h>

#include "gosc.h"

#define TON_HZ 480   /* 100 samples a period of the wave at 48 kHz */
#define TON_S 40
#define OKRES 1024
#define WEZEL "/dev/snd/pcmC0D0p"

static void maska(struct snd_pcm_hw_params *p, int param, unsigned bit)
{
    struct snd_mask *m = &p->masks[param - SNDRV_PCM_HW_PARAM_FIRST_MASK];
    memset(m, 0, sizeof *m);
    m->bits[bit / 32] = 1u << (bit % 32);
}

static void przedzial(struct snd_pcm_hw_params *p, int param, unsigned od, unsigned do_)
{
    struct snd_interval *i = &p->intervals[param - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
    memset(i, 0, sizeof *i);
    i->min = od;
    i->max = do_;
    i->integer = 1;
}

static int otworz(void)
{
    struct snd_pcm_hw_params p;
    int fd = -1;
    for (int i = 0; i < 50 && fd < 0; i++) { /* the card may come a moment after /init */
        fd = open(WEZEL, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            usleep(100000);
    }
    if (fd < 0) {
        loguj("dźwięk: brak karty (%s)", strerror(errno));
        return -1;
    }
    memset(&p, 0, sizeof p);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&p.masks[i], 0xff, sizeof p.masks[i]);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
        p.intervals[i].max = ~0u;
    p.rmask = ~0u;
    maska(&p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    maska(&p, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S32_LE);
    maska(&p, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    przedzial(&p, SNDRV_PCM_HW_PARAM_CHANNELS, 2, 2);
    przedzial(&p, SNDRV_PCM_HW_PARAM_RATE, 48000, 48000);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, OKRES, OKRES);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIODS, 6, 6); /* 128 ms: the emulation is slow */
    if (ioctl(fd, (int)SNDRV_PCM_IOCTL_HW_PARAMS, &p) != 0 || ioctl(fd, (int)SNDRV_PCM_IOCTL_PREPARE) != 0) {
        loguj("dźwięk: karta nie przyjęła formatu (%s)", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

void dzwiek_ton(void)
{
    static int32_t okres[OKRES * 2], fala[48000 / TON_HZ];
    int fd = otworz();
    unsigned long zagrane = 0, przerwy = 0, bledy = 0;
    time_t od = time(NULL);
    if (fd < 0)
        return;
    for (unsigned i = 0; i < sizeof fala / sizeof fala[0]; i++) /* one wave, once: the emulation is slow */
        fala[i] = (int32_t)(8000.0 * 65536.0 * sin(2.0 * M_PI * i / (sizeof fala / sizeof fala[0])));
    loguj("dźwięk: gram ton %d Hz przez %d s (32 bity)", TON_HZ, TON_S);
    while (time(NULL) - od < TON_S) {
        struct snd_xferi x;
        for (int i = 0; i < OKRES; i++)
            okres[2 * i] = okres[2 * i + 1] = fala[(zagrane + (unsigned long)i) % (sizeof fala / sizeof fala[0])];
        memset(&x, 0, sizeof x);
        x.buf = okres;
        x.frames = OKRES;
        if (ioctl(fd, (int)SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) == 0) {
            zagrane += OKRES;
        } else if (errno == EPIPE || (errno == EIO && ++bledy < 20)) { /* an underrun (the emulation is slow), a
                                                                         * write the device was late for: again */
            przerwy++;
            ioctl(fd, (int)SNDRV_PCM_IOCTL_PREPARE);
        } else {
            loguj("dźwięk: zapis: %s", strerror(errno));
            break;
        }
    }
    ioctl(fd, (int)SNDRV_PCM_IOCTL_DRAIN);
    close(fd);
    loguj("dźwięk: koniec (zagrane %lu s, przerwy %lu, błędy zapisu %lu)", zagrane / 48000, przerwy, bledy);
}

/* K1 dzwiek — the card's playback node: ALSA without alsa-lib (ADR 0054 T2), the ioctls of the kernel's UAPI header
 * sound/asound.h, as DRM without libdrm. Only what playing needs: INFO (its name for the log), HW_PARAMS (one format, T3),
 * SW_PARAMS, PREPARE, WRITEI_FRAMES, DROP and HW_FREE — the same list the sandbox allows (KLATKA_DZWIEK). */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>

#include "dzwiek.h"
#include "k1_log.h"

static int pcm = -1, spi = 1;
static unsigned long przerwy;

int karta_jest(void)
{
    return pcm >= 0;
}

int karta_spi(void)
{
    return spi;
}

int karta_fd(void)
{
    return pcm >= 0 && !spi ? pcm : -1;
}

unsigned long karta_przerwy(void)
{
    return przerwy;
}

int karta_przyjmij(int fd, int nr, int urzadzenie)
{
    struct snd_pcm_info info;
    memset(&info, 0, sizeof info);
    if (pcm >= 0 || ioctl(fd, (int)SNDRV_PCM_IOCTL_INFO, &info) != 0 || info.stream != SNDRV_PCM_STREAM_PLAYBACK) {
        k1_log(TAG, "karta %d, urządzenie %d: nie przyjmuję (%s)", nr, urzadzenie, pcm >= 0 ? "już jest" : "to nie odtwarzanie");
        close(fd);
        return -1;
    }
    info.name[sizeof info.name - 1] = '\0';
    for (char *c = (char *)info.name; *c; c++)
        if (*c < 0x20 || *c > 0x7e)
            *c = '?'; /* the kernel's name of a driver: printable ASCII in the log */
    pcm = fd;
    spi = 1;
    k1_log(TAG, "karta %d, urządzenie %d: odtwarzanie „%s”", nr, urzadzenie, (const char *)info.name);
    return 0;
}

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

/* Every parameter open, then ours fixed: the kernel refines the rest (the sizes in bytes, the times). */
static int parametry(void)
{
    struct snd_pcm_hw_params p;
    struct snd_pcm_sw_params s;
    memset(&p, 0, sizeof p);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&p.masks[i], 0xff, sizeof p.masks[i]);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
        p.intervals[i].max = ~0u;
    p.rmask = ~0u;
    maska(&p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    maska(&p, SNDRV_PCM_HW_PARAM_FORMAT, SNDRV_PCM_FORMAT_S16_LE);
    maska(&p, SNDRV_PCM_HW_PARAM_SUBFORMAT, SNDRV_PCM_SUBFORMAT_STD);
    przedzial(&p, SNDRV_PCM_HW_PARAM_CHANNELS, K1_DZW_KANALY, K1_DZW_KANALY);
    przedzial(&p, SNDRV_PCM_HW_PARAM_RATE, K1_DZW_CZESTOTLIWOSC, K1_DZW_CZESTOTLIWOSC);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, K1_DZW_OKRES, K1_DZW_OKRES);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIODS, K1_DZW_OKRESY, K1_DZW_OKRESY);
    if (ioctl(pcm, (int)SNDRV_PCM_IOCTL_HW_PARAMS, &p) != 0) {
        k1_log(TAG, "karta nie przyjęła formatu 48 kHz, 16 bitów, stereo: %s", strerror(errno));
        return -1;
    }
    memset(&s, 0, sizeof s);
    s.avail_min = K1_DZW_OKRES;
    s.start_threshold = 2 * K1_DZW_OKRES; /* it starts when two periods wait */
    s.stop_threshold = K1_DZW_OKRES * K1_DZW_OKRESY;
    s.proto = SNDRV_PCM_VERSION;
    if (ioctl(pcm, (int)SNDRV_PCM_IOCTL_SW_PARAMS, &s) != 0) {
        k1_log(TAG, "SW_PARAMS: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int karta_obudz(void)
{
    if (pcm < 0)
        return -1;
    if (!spi)
        return 0;
    if (parametry() != 0 || ioctl(pcm, (int)SNDRV_PCM_IOCTL_PREPARE) != 0)
        return -1;
    spi = 0;
    return 0;
}

int karta_zapisz(const int16_t *ramki)
{
    struct snd_xferi x;
    memset(&x, 0, sizeof x);
    x.buf = (void *)(uintptr_t)ramki;
    x.frames = K1_DZW_OKRES;
    if (pcm < 0 || spi)
        return -1;
    if (ioctl(pcm, (int)SNDRV_PCM_IOCTL_WRITEI_FRAMES, &x) == 0)
        return x.result == (snd_pcm_sframes_t)K1_DZW_OKRES ? 1 : 0; /* a part: the rest of the period is lost, rare */
    if (errno == EAGAIN)
        return 0;
    if (errno == EPIPE) { /* an underrun: nothing came in time — prepare and go on */
        przerwy++;
        return ioctl(pcm, (int)SNDRV_PCM_IOCTL_PREPARE) == 0 ? 0 : -1;
    }
    k1_log(TAG, "zapis do karty: %s", strerror(errno));
    return -1;
}

void karta_usypiaj(void)
{
    if (pcm < 0 || spi)
        return;
    ioctl(pcm, (int)SNDRV_PCM_IOCTL_DROP);
    ioctl(pcm, (int)SNDRV_PCM_IOCTL_HW_FREE);
    spi = 1;
}

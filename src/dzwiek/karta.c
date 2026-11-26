/* K1 dzwiek — the card's playback node: ALSA without alsa-lib (ADR 0054 T2), the ioctls of the kernel's UAPI header
 * sound/asound.h, as DRM without libdrm. Only what playing needs: INFO (its name for the log), HW_PARAMS, SW_PARAMS,
 * PREPARE, WRITEI_FRAMES, DROP and HW_FREE — the same list the sandbox allows (KLATKA_DZWIEK). 0.12.3 (ADR 0055, T1):
 * the format — the rows of `formaty` in order, the first the card takes (HW_PARAMS refuses the others with EINVAL; no
 * new ioctl to ask first). HDA (sound/hda/core/device.c) offers 20 and 24 bits only as S32_LE with a subformat
 * MSBITS_20 / MSBITS_24 (sound/core/pcm_native.c, snd_pcm_hw_rule_subformats): asked for, the card says how many bits it
 * really plays; S32_LE without a subformat (another driver) — its msbits, at most the 24 of a float. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>

#include "dzwiek.h"
#include "k1_log.h"

static int pcm = -1, spi = 1, wybrany = -1, bity_karty = 16;
static unsigned long przerwy;

/* The formats K1 tries, the best first (rule 5: a row each). bity 0: the card's msbits (at most 24). */
static const struct {
    unsigned format, podformat;
    int bajty, bity;
} formaty[] = {
    { SNDRV_PCM_FORMAT_S32_LE, SNDRV_PCM_SUBFORMAT_MSBITS_24, 4, 24 }, /* HDA with a 24-bit converter */
    { SNDRV_PCM_FORMAT_S32_LE, SNDRV_PCM_SUBFORMAT_MSBITS_20, 4, 20 }, /* HDA with a 20-bit one */
    { SNDRV_PCM_FORMAT_S32_LE, SNDRV_PCM_SUBFORMAT_STD, 4, 0 },
    { SNDRV_PCM_FORMAT_S24_3LE, SNDRV_PCM_SUBFORMAT_STD, 3, 24 },
    { SNDRV_PCM_FORMAT_S16_LE, SNDRV_PCM_SUBFORMAT_STD, 2, 16 },      /* every card: as in 0.12.0 */
};
#define FORMATY ((int)(sizeof formaty / sizeof formaty[0]))

int karta_bajty(void)
{
    return wybrany >= 0 ? formaty[wybrany].bajty : 2;
}

int karta_bity(void)
{
    return bity_karty;
}

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

/* Every parameter open, then ours fixed (format row `f`): the kernel refines the rest (the sizes in bytes, the times). */
static int hw_parametry(int f, unsigned *msbits)
{
    struct snd_pcm_hw_params p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&p.masks[i], 0xff, sizeof p.masks[i]);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
        p.intervals[i].max = ~0u;
    p.rmask = ~0u;
    maska(&p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    maska(&p, SNDRV_PCM_HW_PARAM_FORMAT, formaty[f].format);
    maska(&p, SNDRV_PCM_HW_PARAM_SUBFORMAT, formaty[f].podformat);
    przedzial(&p, SNDRV_PCM_HW_PARAM_CHANNELS, K1_DZW_KANALY, K1_DZW_KANALY);
    przedzial(&p, SNDRV_PCM_HW_PARAM_RATE, K1_DZW_CZESTOTLIWOSC, K1_DZW_CZESTOTLIWOSC);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, K1_DZW_OKRES, K1_DZW_OKRES);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIODS, K1_DZW_OKRESY, K1_DZW_OKRESY);
    if (ioctl(pcm, (int)SNDRV_PCM_IOCTL_HW_PARAMS, &p) != 0)
        return -1;
    *msbits = p.msbits;
    return 0;
}

/* The first format of the table the card takes; the log says it when it differs from the last time. */
static int parametry(void)
{
    struct snd_pcm_sw_params s;
    unsigned msbits = 0;
    int f = 0, bylo = wybrany, bity;
    while (f < FORMATY && hw_parametry(f, &msbits) != 0)
        f++;
    if (f == FORMATY) {
        k1_log(TAG, "karta nie przyjęła formatu 48 kHz, stereo (24, 20 ani 16 bitów): %s", strerror(errno));
        return -1;
    }
    bity = formaty[f].bity ? formaty[f].bity : (int)msbits;
    bity_karty = bity < 16 ? 16 : bity > 24 ? 24 : bity;
    wybrany = f;
    if (bylo != f) /* 24 bity, 16 / 20 bitów */
        k1_log(TAG, "karta gra %d %s (%d bajty na próbkę)", bity_karty, bity_karty % 10 >= 2 && bity_karty % 10 <= 4 ?
               "bity" : "bitów", formaty[f].bajty);
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

int karta_zapisz(const uint8_t *ramki)
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

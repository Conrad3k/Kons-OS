/* K1 dzwiek — the card's recording node, the microphone (0.13.5 step 1, ADR 0065): ALSA without alsa-lib, as karta.c.
 * nadzorca gives it with the card (DZWIEK_KARTA NAGRYWANIE: pcmC<k>D<u>c of the same card). It records only while
 * someone hears the microphone (mikrofon.c opens and closes it — T4: the card never records "in case"): HW_PARAMS
 * (48 kHz; S32_LE, else S16_LE; two channels, else one), SW_PARAMS (avail_min one period, start at the first read),
 * PREPARE and one read, which starts it (sound/core/pcm_lib.c: a read of a prepared capture of at least start_threshold
 * frames starts it; without data a non-blocking read says EAGAIN); then a period whenever poll says one waits
 * (READI_FRAMES). An overrun (nobody read in time: XRUN, the read says EPIPE) — PREPARE and on. Nobody hears it: DROP and
 * HW_FREE. The ioctls: INFO, HW_PARAMS, SW_PARAMS, PREPARE, READI_FRAMES, DROP, HW_FREE — KLATKA_DZWIEK. */
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>

#include "dzwiek.h"
#include "k1_log.h"

static int pcm = -1, otwarte, bajty = 2, kanaly = 2;
static unsigned long przepelnienia, puste; /* puste: poll said a period waits, the read had none */

/* What K1 asks the card for, the best first (rule 5: a row each): the format and its bytes, the channels. */
static const struct {
    unsigned format;
    int bajty, kanaly;
} ustawienia[] = {
    { SNDRV_PCM_FORMAT_S32_LE, 4, 2 }, /* HDA: its 20 or 24 bits in a 32-bit word */
    { SNDRV_PCM_FORMAT_S16_LE, 2, 2 },
    { SNDRV_PCM_FORMAT_S32_LE, 4, 1 }, /* a card with one microphone channel */
    { SNDRV_PCM_FORMAT_S16_LE, 2, 1 },
};
#define USTAWIENIA ((int)(sizeof ustawienia / sizeof ustawienia[0]))

int nagr_jest(void)
{
    return pcm >= 0;
}

int nagr_fd(void)
{
    return otwarte ? pcm : -1;
}

int nagr_bajty(void)
{
    return bajty;
}

int nagr_kanaly(void)
{
    return kanaly;
}

unsigned long nagr_przepelnienia(void)
{
    return przepelnienia;
}

int nagr_przyjmij(int fd, int nr, int urzadzenie)
{
    struct snd_pcm_info info;
    memset(&info, 0, sizeof info);
    if (pcm >= 0 || ioctl(fd, (int)SNDRV_PCM_IOCTL_INFO, &info) != 0 || info.stream != SNDRV_PCM_STREAM_CAPTURE) {
        k1_log(TAG, "karta %d, urządzenie %d: nie przyjmuję (%s)", nr, urzadzenie,
               pcm >= 0 ? "mikrofon już jest" : "to nie nagrywanie");
        close(fd);
        return -1;
    }
    info.name[sizeof info.name - 1] = '\0';
    for (char *c = (char *)info.name; *c; c++)
        if (*c < 0x20 || *c > 0x7e)
            *c = '?';
    pcm = fd;
    k1_log(TAG, "karta %d, urządzenie %d: nagrywanie „%s”", nr, urzadzenie, (const char *)info.name);
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

/* Every parameter open, then ours fixed (row `u`); the kernel refines the rest. */
static int hw_parametry(int u)
{
    struct snd_pcm_hw_params p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&p.masks[i], 0xff, sizeof p.masks[i]);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
        p.intervals[i].max = ~0u;
    p.rmask = ~0u;
    maska(&p, SNDRV_PCM_HW_PARAM_ACCESS, SNDRV_PCM_ACCESS_RW_INTERLEAVED);
    maska(&p, SNDRV_PCM_HW_PARAM_FORMAT, ustawienia[u].format);
    przedzial(&p, SNDRV_PCM_HW_PARAM_CHANNELS, (unsigned)ustawienia[u].kanaly, (unsigned)ustawienia[u].kanaly);
    przedzial(&p, SNDRV_PCM_HW_PARAM_RATE, K1_DZW_CZESTOTLIWOSC, K1_DZW_CZESTOTLIWOSC);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, K1_DZW_OKRES, K1_DZW_OKRES);
    przedzial(&p, SNDRV_PCM_HW_PARAM_PERIODS, K1_DZW_OKRESY, K1_DZW_OKRESY);
    return ioctl(pcm, (int)SNDRV_PCM_IOCTL_HW_PARAMS, &p);
}

/* One read of a period into `ramki` (K1_DZW_OKRES frames of at most 2 channels of 4 bytes): the frames read (a whole
 * period, or 0), -1 the card failed. A prepared capture starts here. */
int nagr_czytaj(uint8_t *ramki)
{
    struct snd_xferi x;
    memset(&x, 0, sizeof x);
    x.buf = ramki;
    x.frames = K1_DZW_OKRES;
    if (!otwarte)
        return -1;
    if (ioctl(pcm, (int)SNDRV_PCM_IOCTL_READI_FRAMES, &x) == 0)
        return x.result == (snd_pcm_sframes_t)K1_DZW_OKRES ? (int)K1_DZW_OKRES : (puste++, 0);
    if (errno == EAGAIN) {
        puste++;
        return 0;
    }
    if (errno == EPIPE) { /* an overrun: prepare, and the next read starts it again */
        przepelnienia++;
        return ioctl(pcm, (int)SNDRV_PCM_IOCTL_PREPARE) == 0 ? 0 : -1;
    }
    k1_log(TAG, "odczyt mikrofonu: %s", strerror(errno));
    return -1;
}

/* Someone hears the microphone: the card records. 0, or -1 (it takes none of our formats, or failed). */
int nagr_otworz(void)
{
    static uint8_t pierwszy[K1_DZW_OKRES * 2 * 4];
    struct snd_pcm_sw_params s;
    int u = 0;
    if (pcm < 0)
        return -1;
    if (otwarte)
        return 0;
    while (u < USTAWIENIA && hw_parametry(u) != 0)
        u++;
    if (u == USTAWIENIA) {
        k1_log(TAG, "mikrofon nie przyjął 48 kHz (32 ani 16 bitów, stereo ani mono): %s", strerror(errno));
        return -1;
    }
    bajty = ustawienia[u].bajty;
    kanaly = ustawienia[u].kanaly;
    memset(&s, 0, sizeof s);
    s.avail_min = K1_DZW_OKRES;
    s.start_threshold = 1; /* the first read starts it */
    s.stop_threshold = K1_DZW_OKRES * K1_DZW_OKRESY;
    s.proto = SNDRV_PCM_VERSION;
    if (ioctl(pcm, (int)SNDRV_PCM_IOCTL_SW_PARAMS, &s) != 0 || ioctl(pcm, (int)SNDRV_PCM_IOCTL_PREPARE) != 0) {
        k1_log(TAG, "mikrofon: SW_PARAMS / PREPARE: %s", strerror(errno));
        ioctl(pcm, (int)SNDRV_PCM_IOCTL_HW_FREE);
        return -1;
    }
    otwarte = 1;
    przepelnienia = puste = 0;
    if (nagr_czytaj(pierwszy) < 0) {
        nagr_zamknij();
        return -1;
    }
    k1_log(TAG, "mikrofon nagrywa (%d bitów, %s)", 8 * bajty, kanaly == 2 ? "stereo" : "mono");
    return 0;
}

void nagr_zamknij(void)
{
    if (!otwarte)
        return;
    ioctl(pcm, (int)SNDRV_PCM_IOCTL_DROP);
    ioctl(pcm, (int)SNDRV_PCM_IOCTL_HW_FREE);
    otwarte = 0;
    k1_log(TAG, "mikrofon nie nagrywa (przepełnienia: %lu, odczyty bez okresu: %lu)", przepelnienia, puste);
}

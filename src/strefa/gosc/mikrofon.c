/* K1 gosc — the test zone's microphone (0.13.5 step 2, ADR 0065): TRYB_MIKROFON — the VMM was let hear (as a session with
 * the switch "Mikrofon"), so the guest kernel's virtio-snd card has a capture device (stream 1 → pcmC0D0c): the guest
 * records, as a browser does through ALSA, and says once a second whether it hears K1's microphone — the test program's
 * tone (scripts/test-dzwiek.sh: "ton") — or silence (the sign on the window's bar clicked: the session's microphone muted
 * in K1's sound service). TRYB_MIKROFON_BEZ (NEGATIVE): the VMM was not let hear — the card has a playback device and no
 * capture device at all. ALSA without alsa-lib, as dzwiek.c. Levels as numbers only; then the end of the machine. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <sound/asound.h>

#include "gosc.h"

#define MIK_S 30
#define OKRES 1024
#define NAGRYWANIE "/dev/snd/pcmC0D0c"
#define ODTWARZANIE "/dev/snd/pcmC0D0p"

static int jest(const char *wezel, int razy)
{
    for (int i = 0; i < razy; i++) { /* the card may come a moment after /init */
        if (access(wezel, F_OK) == 0)
            return 1;
        usleep(100000);
    }
    return 0;
}

static int otworz(void)
{
    struct snd_pcm_hw_params p;
    struct snd_pcm_sw_params s;
    int fd = open(NAGRYWANIE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -1;
    memset(&p, 0, sizeof p);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_MASK - SNDRV_PCM_HW_PARAM_FIRST_MASK; i++)
        memset(&p.masks[i], 0xff, sizeof p.masks[i]);
    for (int i = 0; i <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL; i++)
        p.intervals[i].max = ~0u;
    p.rmask = ~0u;
    p.masks[SNDRV_PCM_HW_PARAM_ACCESS - SNDRV_PCM_HW_PARAM_FIRST_MASK] = (struct snd_mask){ { 1u << SNDRV_PCM_ACCESS_RW_INTERLEAVED } };
    p.masks[SNDRV_PCM_HW_PARAM_FORMAT - SNDRV_PCM_HW_PARAM_FIRST_MASK] = (struct snd_mask){ { 1u << SNDRV_PCM_FORMAT_S16_LE } };
    p.intervals[SNDRV_PCM_HW_PARAM_CHANNELS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL] = (struct snd_interval){ .min = 2, .max = 2, .integer = 1 };
    p.intervals[SNDRV_PCM_HW_PARAM_RATE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL] = (struct snd_interval){ .min = 48000, .max = 48000, .integer = 1 };
    p.intervals[SNDRV_PCM_HW_PARAM_PERIOD_SIZE - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL] = (struct snd_interval){ .min = OKRES, .max = OKRES, .integer = 1 };
    p.intervals[SNDRV_PCM_HW_PARAM_PERIODS - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL] = (struct snd_interval){ .min = 6, .max = 6, .integer = 1 };
    memset(&s, 0, sizeof s);
    s.avail_min = OKRES;
    s.start_threshold = 1;
    s.stop_threshold = OKRES * 6;
    s.proto = SNDRV_PCM_VERSION;
    if (ioctl(fd, (int)SNDRV_PCM_IOCTL_HW_PARAMS, &p) != 0 || ioctl(fd, (int)SNDRV_PCM_IOCTL_SW_PARAMS, &s) != 0 ||
        ioctl(fd, (int)SNDRV_PCM_IOCTL_PREPARE) != 0) {
        loguj("mikrofon: karta nie przyjęła formatu (%s)", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* MIK_S seconds of recording: once a second the loudest sample — "ton" over −30 dBFS, else "cisza" — said when it changes. */
static void nagrywaj(int fd)
{
    static int16_t okres[OKRES * 2];
    time_t od = time(NULL), sek = od;
    int szczyt = 0, bylo = -1, przerwy = 0;
    unsigned long ramki = 0, z_tonem = 0, cisza = 0;
    loguj("mikrofon: nagrywam przez %d s", MIK_S);
    while (time(NULL) - od < MIK_S) {
        struct snd_xferi x = { 0, okres, OKRES };
        if (ioctl(fd, (int)SNDRV_PCM_IOCTL_READI_FRAMES, &x) != 0) {
            if (errno != EPIPE || ++przerwy > 50) {
                loguj("mikrofon: odczyt: %s", strerror(errno));
                break;
            }
            ioctl(fd, (int)SNDRV_PCM_IOCTL_PREPARE); /* an overrun (the emulation is slow): again */
            continue;
        }
        ramki += (unsigned long)x.result;
        for (long i = 0; i < 2 * x.result; i++)
            szczyt = okres[i] > szczyt ? okres[i] : -okres[i] > szczyt ? -okres[i] : szczyt;
        if (time(NULL) != sek) {
            int ton = szczyt > 1036; /* −30 dBFS: the tone is −12 dB */
            z_tonem += ton ? 1u : 0u;
            cisza += ton ? 0u : 1u;
            if (ton != bylo)
                loguj("mikrofon: %s (szczyt %d)", ton ? "ton" : "cisza", szczyt);
            bylo = ton;
            szczyt = 0;
            sek = time(NULL);
        }
    }
    loguj("mikrofon: koniec (nagrane %lu s, sekundy z tonem %lu, ciszy %lu, przepełnienia %d)", ramki / 48000, z_tonem,
          cisza, przerwy);
}

void mikrofon(int bez)
{
    int fd;
    if (!jest(ODTWARZANIE, 50)) {
        loguj("mikrofon: brak karty");
        return;
    }
    if (bez) { /* NEGATIVE: without the switch the card has no capture device */
        loguj("mikrofon: %s", jest(NAGRYWANIE, 10) ? "karta MA wejście (źle)" : "karta bez wejścia (dobrze)");
        return;
    }
    fd = jest(NAGRYWANIE, 10) ? otworz() : -1;
    if (fd < 0) {
        loguj("mikrofon: brak wejścia karty (%s)", strerror(errno));
        return;
    }
    nagrywaj(fd);
    ioctl(fd, (int)SNDRV_PCM_IOCTL_DROP);
    close(fd);
}

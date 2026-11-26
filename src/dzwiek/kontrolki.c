/* K1 dzwiek — the card's controls (ADR 0054 T3). A codec starts with its outputs muted or quiet (alsactl, which restores
 * them elsewhere, K1 does not have): the service switches on the playback switches of the outputs and sets their hardware
 * volume to 0 dB (no amplification: the volume of K1 is the service's own, done on the samples). Only the controls of the
 * outputs, by name (the names of the kernel's HDA parser, sound/hda/codecs/generic.c): never an input, a loopback of the
 * microphone or the beep. Reads the card's name for the log. Step 2: the headphones — the card's jack controls
 * ("Headphone Jack": the kernel's HDA parser reports what is in the jack and mutes the speakers itself); the service keeps
 * the controls open, asks for their events (SUBSCRIBE_EVENTS) and reads the jacks again on each. ioctls: CARD_INFO,
 * ELEM_LIST, ELEM_INFO, ELEM_READ, ELEM_WRITE, TLV_READ, SUBSCRIBE_EVENTS — the sandbox allows no other on the controls (no
 * ELEM_ADD: no controls of our own). */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <sound/asound.h>
#include <sound/tlv.h>

#include "dzwiek.h"
#include "k1_log.h"

#define ELEMENTY_MAKS 256
#define GNIAZDA_MAKS 4 /* headphone jacks of one card (front and back at most, in practice) */

static int kfd = -1;                      /* the controls, open while the service runs (step 2) */
static unsigned gniazda[GNIAZDA_MAKS], n_gniazd; /* the numids of the headphone jacks */
static int sluchawki;                     /* something is in one of them */

/* The outputs: "<name> Playback Switch" / "<name> Playback Volume" (sound/hda/codecs/generic.c, the vmaster "Master"). */
static const char *const wyjscia[] = { "Master", "PCM", "Speaker", "Headphone", "Front", "Line Out", "Bass Speaker" };

static void drukowalne(unsigned char *s, size_t n)
{
    s[n - 1] = '\0';
    for (size_t i = 0; s[i]; i++)
        if (s[i] < 0x20 || s[i] > 0x7e)
            s[i] = '?';
}

/* 1 for "<output> Playback Switch" (rodzaj 0) or "<output> Playback Volume" (rodzaj 1). */
static int wyjscie(const unsigned char *nazwa, int rodzaj)
{
    const char *koniec = rodzaj ? " Playback Volume" : " Playback Switch";
    for (size_t i = 0; i < sizeof wyjscia / sizeof wyjscia[0]; i++) {
        size_t n = strlen(wyjscia[i]);
        if (strncmp((const char *)nazwa, wyjscia[i], n) == 0 && strcmp((const char *)nazwa + n, koniec) == 0)
            return 1;
    }
    return 0;
}

/* The value of 0 dB from its dB scale (TLV DB_SCALE: min and step in 1/100 dB), within min..max; without one: max. */
static long zero_db(int fd, const struct snd_ctl_elem_info *in)
{
    unsigned bufor[2 + 8];
    struct snd_ctl_tlv *t = (struct snd_ctl_tlv *)bufor;
    long min = in->value.integer.min, max = in->value.integer.max, v;
    int min_db, krok;
    memset(bufor, 0, sizeof bufor);
    t->numid = in->id.numid;
    t->length = sizeof bufor - sizeof *t;
    if (!(in->access & SNDRV_CTL_ELEM_ACCESS_TLV_READ) || ioctl(fd, (int)SNDRV_CTL_IOCTL_TLV_READ, t) != 0 ||
        t->tlv[0] != SNDRV_CTL_TLVT_DB_SCALE || t->tlv[1] < 8)
        return max;
    min_db = (int)t->tlv[2];
    krok = (int)(t->tlv[3] & SNDRV_CTL_TLVD_DB_SCALE_MASK);
    if (krok <= 0 || min_db >= 0)
        return min_db >= 0 ? min : max;
    v = min + (long)((-min_db) / krok);
    return v > max ? max : v < min ? min : v;
}

/* One control: an output's switch on, its volume at 0 dB. Returns 1 when it was one of ours. */
static int ustaw(int fd, const struct snd_ctl_elem_id *id)
{
    struct snd_ctl_elem_info in;
    struct snd_ctl_elem_value *v;
    int przelacznik, wynik = 0;
    long wartosc;
    memset(&in, 0, sizeof in);
    in.id = *id;
    if (id->iface != SNDRV_CTL_ELEM_IFACE_MIXER || ioctl(fd, (int)SNDRV_CTL_IOCTL_ELEM_INFO, &in) != 0 ||
        !(in.access & SNDRV_CTL_ELEM_ACCESS_WRITE) || in.count == 0 || in.count > 128)
        return 0;
    przelacznik = in.type == SNDRV_CTL_ELEM_TYPE_BOOLEAN && wyjscie(id->name, 0);
    if (!przelacznik && !(in.type == SNDRV_CTL_ELEM_TYPE_INTEGER && wyjscie(id->name, 1)))
        return 0;
    wartosc = przelacznik ? 1 : zero_db(fd, &in);
    v = calloc(1, sizeof *v);
    if (v == NULL)
        return 0;
    v->id = in.id;
    for (unsigned i = 0; i < in.count; i++)
        v->value.integer.value[i] = wartosc;
    if (ioctl(fd, (int)SNDRV_CTL_IOCTL_ELEM_WRITE, v) < 0)
        k1_log(TAG, "kontrolka „%s”: %s", (const char *)id->name, strerror(errno));
    else
        wynik = 1;
    free(v);
    return wynik;
}

/* A headphone jack (step 2): a boolean control of the card (iface CARD) named as k1_dzw_gniazdo_sluchawek says. */
static void gniazdo(int fd, const struct snd_ctl_elem_id *id)
{
    struct snd_ctl_elem_info in;
    memset(&in, 0, sizeof in);
    in.id = *id;
    if (id->iface != SNDRV_CTL_ELEM_IFACE_CARD || n_gniazd >= GNIAZDA_MAKS ||
        !k1_dzw_gniazdo_sluchawek((const char *)id->name) || ioctl(fd, (int)SNDRV_CTL_IOCTL_ELEM_INFO, &in) != 0 ||
        in.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN || !(in.access & SNDRV_CTL_ELEM_ACCESS_READ))
        return;
    gniazda[n_gniazd++] = id->numid;
    k1_log(TAG, "gniazdo słuchawek: „%s”", (const char *)id->name);
}

/* Is something in a headphone jack now? */
static int czytaj_gniazda(void)
{
    struct snd_ctl_elem_value *v = calloc(1, sizeof *v);
    int jest = 0;
    for (unsigned i = 0; v != NULL && i < n_gniazd; i++) {
        memset(v, 0, sizeof *v);
        v->id.numid = gniazda[i];
        if (ioctl(kfd, (int)SNDRV_CTL_IOCTL_ELEM_READ, v) == 0 && v->value.integer.value[0] != 0)
            jest = 1;
    }
    free(v);
    return jest;
}

int kontrolki_fd(void)
{
    return n_gniazd > 0 ? kfd : -1;
}

int kontrolki_wyjscie(void)
{
    return sluchawki ? K1_DZW_WYJSCIE_SLUCHAWKI : K1_DZW_WYJSCIE_GLOSNIKI;
}

int kontrolki_zdarzenia(void)
{
    struct snd_ctl_event ev[8];
    int bylo = sluchawki;
    while (kfd >= 0 && read(kfd, ev, sizeof ev) > 0) /* only that something changed: the jacks are read again */
        ;
    sluchawki = czytaj_gniazda();
    return sluchawki != bylo;
}

void kontrolki_przyjmij(int fd)
{
    int tak = 1;
    struct snd_ctl_card_info karta;
    struct snd_ctl_elem_list lista;
    struct snd_ctl_elem_id *id = calloc(ELEMENTY_MAKS, sizeof *id);
    unsigned ustawione = 0;
    memset(&karta, 0, sizeof karta);
    memset(&lista, 0, sizeof lista);
    if (ioctl(fd, (int)SNDRV_CTL_IOCTL_CARD_INFO, &karta) == 0) {
        drukowalne(karta.name, sizeof karta.name);
        drukowalne(karta.mixername, sizeof karta.mixername);
        k1_log(TAG, "karta %d: „%s”, kodek „%s”", karta.card, (const char *)karta.name, (const char *)karta.mixername);
    }
    lista.space = ELEMENTY_MAKS;
    lista.pids = id;
    if (kfd >= 0) { /* the controls once per service: a second card's are not ours */
        free(id);
        close(fd);
        return;
    }
    if (id == NULL || ioctl(fd, (int)SNDRV_CTL_IOCTL_ELEM_LIST, &lista) != 0) {
        k1_log(TAG, "kontrolki karty: lista niedostępna");
        free(id);
        close(fd);
        return;
    }
    for (unsigned i = 0; i < lista.used && i < ELEMENTY_MAKS; i++) {
        drukowalne(id[i].name, sizeof id[i].name);
        if (ustaw(fd, &id[i])) {
            ustawione++;
            k1_log(TAG, "kontrolka „%s”: włączona (0 dB)", (const char *)id[i].name);
        }
        gniazdo(fd, &id[i]);
    }
    k1_log(TAG, "kontrolki karty: %u z %u ustawione", ustawione, lista.count);
    free(id);
    if (n_gniazd == 0 || fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        ioctl(fd, (int)SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS, &tak) != 0) {
        n_gniazd = 0; /* no headphone jack (or no events of it): the speakers always */
        close(fd);
        return;
    }
    kfd = fd;
    sluchawki = czytaj_gniazda();
    k1_log(TAG, "słuchawki: %s", sluchawki ? "podłączone" : "nie");
}

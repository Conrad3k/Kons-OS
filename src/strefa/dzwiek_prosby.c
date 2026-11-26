/* K1 strefa — the guest's sound card, its control requests (0.12.0 step 3; dzwiek.h). Pure: no system calls; tested on
 * the host (test_virtio_dzwiek) and fuzzed (fuzz_virtio_dzwiek). Everything here comes from the guest: each request is
 * copied out of its memory first (dzwiek.c), its length and every field checked, unknown requests answered "not
 * supported". Byte order: virtio is little endian, as the computers K1 runs on (x86-64) — read with memcpy (no alignment
 * assumed). */
#include "dzwiek.h"

#include <string.h>

#include <linux/virtio_snd.h>

_Static_assert(sizeof(struct virtio_snd_pcm_set_params) == 24, "virtio_snd_pcm_set_params");
_Static_assert(sizeof(struct virtio_snd_query_info) == 16, "virtio_snd_query_info");
_Static_assert(sizeof(struct virtio_snd_pcm_info) == 32, "virtio_snd_pcm_info");
_Static_assert(sizeof(struct virtio_snd_pcm_status) == 8, "virtio_snd_pcm_status");

static uint32_t le32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

static uint32_t naglowek(uint8_t *odp, uint32_t kod)
{
    memcpy(odp, &kod, 4);
    return 4;
}

/* PCM_INFO of stream 0 (the only one): its format, rates and channels. */
static uint32_t informacje(const uint8_t *req, uint32_t n, uint8_t *odp, uint32_t max)
{
    struct virtio_snd_pcm_info in;
    if (n != sizeof(struct virtio_snd_query_info) || le32(req + 4) != 0 || le32(req + 8) != 1 ||
        le32(req + 12) < sizeof in || max < 4 + sizeof in)
        return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
    memset(&in, 0, sizeof in);
    in.formats = 1ull << VIRTIO_SND_PCM_FMT_S16;
    in.rates = 1ull << VIRTIO_SND_PCM_RATE_48000;
    in.direction = VIRTIO_SND_D_OUTPUT;
    in.channels_min = in.channels_max = 2;
    naglowek(odp, VIRTIO_SND_S_OK);
    memcpy(odp + 4, &in, sizeof in);
    return 4 + sizeof in;
}

/* SET_PARAMS: only K1's format; the buffer a whole number of periods, each of whole frames. */
static uint32_t parametry(struct dzw_strumien *s, const uint8_t *req, uint32_t n)
{
    struct virtio_snd_pcm_set_params p;
    if (n != sizeof p)
        return VIRTIO_SND_S_BAD_MSG;
    memcpy(&p, req, sizeof p);
    if (s->stan == DZW_GRA || s->stan == DZW_STOP)
        return VIRTIO_SND_S_BAD_MSG; /* not while it plays or is paused */
    if (p.features != 0 || p.padding != 0)
        return VIRTIO_SND_S_NOT_SUPP;
    if (p.channels != 2 || p.format != VIRTIO_SND_PCM_FMT_S16 || p.rate != VIRTIO_SND_PCM_RATE_48000)
        return VIRTIO_SND_S_NOT_SUPP;
    if (p.period_bytes == 0 || p.period_bytes % 4 != 0 || p.period_bytes > DZW_DANE_MAKS ||
        p.buffer_bytes < p.period_bytes || p.buffer_bytes > DZW_BUFOR_MAKS || p.buffer_bytes % p.period_bytes != 0)
        return VIRTIO_SND_S_BAD_MSG;
    s->bufor = p.buffer_bytes;
    s->okres = p.period_bytes;
    s->stan = DZW_USTAWIONY;
    return VIRTIO_SND_S_OK;
}

/* PREPARE, START, STOP, RELEASE of stream 0 in the states virtio allows (5.14.6.6.1). */
static uint32_t przejscie(struct dzw_strumien *s, uint32_t kod, int *zrob)
{
    switch (kod) {
    case VIRTIO_SND_R_PCM_PREPARE:
        if (s->stan != DZW_USTAWIONY && s->stan != DZW_GOTOWY && s->stan != DZW_ZWOLNIONY)
            return VIRTIO_SND_S_BAD_MSG;
        s->stan = DZW_GOTOWY;
        return VIRTIO_SND_S_OK;
    case VIRTIO_SND_R_PCM_START:
        if (s->stan != DZW_GOTOWY && s->stan != DZW_STOP)
            return VIRTIO_SND_S_BAD_MSG;
        s->stan = DZW_GRA;
        *zrob = DZW_ZROB_START;
        return VIRTIO_SND_S_OK;
    case VIRTIO_SND_R_PCM_STOP:
        if (s->stan != DZW_GRA)
            return VIRTIO_SND_S_BAD_MSG;
        s->stan = DZW_STOP;
        *zrob = DZW_ZROB_STOP;
        return VIRTIO_SND_S_OK;
    default: /* RELEASE */
        if (s->stan != DZW_GOTOWY && s->stan != DZW_STOP)
            return VIRTIO_SND_S_BAD_MSG;
        s->stan = DZW_ZWOLNIONY;
        *zrob = DZW_ZROB_ZWOLNIJ;
        return VIRTIO_SND_S_OK;
    }
}

uint32_t dzw_prosba(struct dzw_strumien *s, const uint8_t *req, uint32_t n, uint8_t *odp, uint32_t max, int *zrob)
{
    uint32_t kod;
    *zrob = DZW_ZROB_NIC;
    if (max < 4)
        return 0;
    if (n < 4 || n > DZW_PROSBA_MAKS)
        return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
    kod = le32(req);
    switch (kod) {
    case VIRTIO_SND_R_PCM_INFO:
        return informacje(req, n, odp, max);
    case VIRTIO_SND_R_PCM_SET_PARAMS:
        return naglowek(odp, n >= 8 && le32(req + 4) == 0 ? parametry(s, req, n) : VIRTIO_SND_S_BAD_MSG);
    case VIRTIO_SND_R_PCM_PREPARE:
    case VIRTIO_SND_R_PCM_START:
    case VIRTIO_SND_R_PCM_STOP:
    case VIRTIO_SND_R_PCM_RELEASE:
        if (n != sizeof(struct virtio_snd_pcm_hdr) || le32(req + 4) != 0)
            return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
        return naglowek(odp, przejscie(s, kod, zrob));
    default: /* jacks, channel maps, controls: this card has none */
        return naglowek(odp, VIRTIO_SND_S_NOT_SUPP);
    }
}

uint32_t dzw_transfer(const uint8_t *naglowek_, uint32_t n_naglowka, uint64_t dane)
{
    if (n_naglowka != sizeof(struct virtio_snd_pcm_xfer) || le32(naglowek_) != 0 || dane % 4 != 0 ||
        dane > DZW_DANE_MAKS)
        return VIRTIO_SND_S_BAD_MSG;
    return VIRTIO_SND_S_OK;
}

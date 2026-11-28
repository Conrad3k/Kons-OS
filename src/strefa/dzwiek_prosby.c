/* K1 strefa — the guest's sound card, its control requests and (0.12.3) its samples into floats (dzwiek.h). Pure: no
 * system calls; tested on
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

#define X_FORMAT(n, ramka) { VIRTIO_SND_PCM_FMT_##n, ramka, #n },
static const struct {
    uint32_t format, ramka;
    const char *nazwa;
} formaty[] = { DZW_FORMATY(X_FORMAT) };
#define FORMATY (sizeof formaty / sizeof formaty[0])

uint32_t dzw_ramka(uint32_t format)
{
    for (size_t i = 0; i < FORMATY; i++)
        if (formaty[i].format == format)
            return formaty[i].ramka;
    return 0;
}

const char *dzw_format_nazwa(uint32_t format)
{
    for (size_t i = 0; i < FORMATY; i++)
        if (formaty[i].format == format)
            return formaty[i].nazwa;
    return "?";
}

void dzw_na_f32(float *cel, const uint8_t *zrodlo, uint32_t ramki, uint32_t format)
{
    for (size_t i = 0; i < (size_t)ramki * 2; i++) {
        if (format == VIRTIO_SND_PCM_FMT_S16) {
            int16_t v;
            memcpy(&v, zrodlo + 2 * i, sizeof v);
            cel[i] = (float)v * (1.0f / 32768.0f);
        } else if (format == VIRTIO_SND_PCM_FMT_S32) {
            int32_t v;
            memcpy(&v, zrodlo + 4 * i, sizeof v);
            cel[i] = (float)v * (1.0f / 2147483648.0f);
        } else if (format == VIRTIO_SND_PCM_FMT_FLOAT) {
            memcpy(&cel[i], zrodlo + 4 * i, sizeof cel[i]);
        } else {
            cel[i] = 0.0f;
        }
    }
}

void dzw_z_f32(uint8_t *cel, const float *zrodlo, uint32_t ramki, uint32_t format)
{
    for (size_t i = 0; i < (size_t)ramki * 2; i++) {
        float v = zrodlo[i] != zrodlo[i] ? 0.0f : zrodlo[i] > 1.0f ? 1.0f : zrodlo[i] < -1.0f ? -1.0f : zrodlo[i];
        if (format == VIRTIO_SND_PCM_FMT_S16) {
            float q = v * 32767.0f;
            int16_t w = (int16_t)(q + (q < 0 ? -0.5f : 0.5f));
            memcpy(cel + 2 * i, &w, sizeof w);
        } else if (format == VIRTIO_SND_PCM_FMT_S32) {
            double q = (double)v * 2147483647.0;
            int32_t w = (int32_t)(q + (q < 0 ? -0.5 : 0.5));
            memcpy(cel + 4 * i, &w, sizeof w);
        } else if (format == VIRTIO_SND_PCM_FMT_FLOAT) {
            memcpy(cel + 4 * i, &v, sizeof v);
        }
    }
}

static uint32_t naglowek(uint8_t *odp, uint32_t kod)
{
    memcpy(odp, &kod, 4);
    return 4;
}

/* PCM_INFO of streams start_id .. start_id + count - 1 of the card's `strumienie`: their format, rates, channels and
 * direction (0 the output; 1 — 0.13.5 step 2 — the input). */
static uint32_t informacje(int strumienie, const uint8_t *req, uint32_t n, uint8_t *odp, uint32_t max)
{
    uint32_t od = n == sizeof(struct virtio_snd_query_info) ? le32(req + 4) : 0, ile = n >= 12 ? le32(req + 8) : 0;
    if (n != sizeof(struct virtio_snd_query_info) || ile < 1 || od >= (uint32_t)strumienie ||
        ile > (uint32_t)strumienie - od || le32(req + 12) < sizeof(struct virtio_snd_pcm_info) ||
        max < 4 + ile * sizeof(struct virtio_snd_pcm_info))
        return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
    naglowek(odp, VIRTIO_SND_S_OK);
    for (uint32_t k = 0; k < ile; k++) {
        struct virtio_snd_pcm_info in;
        memset(&in, 0, sizeof in);
        for (size_t i = 0; i < FORMATY; i++)
            in.formats |= 1ull << formaty[i].format;
        in.rates = 1ull << VIRTIO_SND_PCM_RATE_48000;
        in.direction = od + k == DZW_WEJSCIE ? VIRTIO_SND_D_INPUT : VIRTIO_SND_D_OUTPUT;
        in.channels_min = in.channels_max = 2;
        memcpy(odp + 4 + k * sizeof in, &in, sizeof in);
    }
    return 4 + ile * (uint32_t)sizeof(struct virtio_snd_pcm_info);
}

/* SET_PARAMS: only a format of DZW_FORMATY at 48 kHz, stereo; the buffer a whole number of periods, each of whole frames. */
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
    if (p.channels != 2 || dzw_ramka(p.format) == 0 || p.rate != VIRTIO_SND_PCM_RATE_48000)
        return VIRTIO_SND_S_NOT_SUPP;
    if (p.period_bytes == 0 || p.period_bytes % dzw_ramka(p.format) != 0 || p.period_bytes > DZW_DANE_MAKS ||
        p.buffer_bytes < p.period_bytes || p.buffer_bytes > DZW_BUFOR_MAKS || p.buffer_bytes % p.period_bytes != 0)
        return VIRTIO_SND_S_BAD_MSG;
    s->bufor = p.buffer_bytes;
    s->okres = p.period_bytes;
    s->format = p.format;
    s->ramka = dzw_ramka(p.format);
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

uint32_t dzw_prosba(struct dzw_strumien *s, int strumienie, const uint8_t *req, uint32_t n, uint8_t *odp, uint32_t max,
                    int *zrob, int *ktory)
{
    uint32_t kod, id;
    *zrob = DZW_ZROB_NIC;
    *ktory = DZW_WYJSCIE;
    if (max < 4)
        return 0;
    if (n < 4 || n > DZW_PROSBA_MAKS)
        return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
    kod = le32(req);
    id = n >= 8 ? le32(req + 4) : 0;
    switch (kod) {
    case VIRTIO_SND_R_PCM_INFO:
        return informacje(strumienie, req, n, odp, max);
    case VIRTIO_SND_R_PCM_SET_PARAMS:
        if (n < 8 || id >= (uint32_t)strumienie)
            return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
        *ktory = (int)id;
        return naglowek(odp, parametry(&s[id], req, n));
    case VIRTIO_SND_R_PCM_PREPARE:
    case VIRTIO_SND_R_PCM_START:
    case VIRTIO_SND_R_PCM_STOP:
    case VIRTIO_SND_R_PCM_RELEASE:
        if (n != sizeof(struct virtio_snd_pcm_hdr) || id >= (uint32_t)strumienie)
            return naglowek(odp, VIRTIO_SND_S_BAD_MSG);
        *ktory = (int)id;
        return naglowek(odp, przejscie(&s[id], kod, zrob));
    default: /* jacks, channel maps, controls: this card has none */
        return naglowek(odp, VIRTIO_SND_S_NOT_SUPP);
    }
}

uint32_t dzw_transfer(const uint8_t *naglowek_, uint32_t n_naglowka, uint64_t dane, uint32_t ramka, uint32_t strumien)
{
    if (n_naglowka != sizeof(struct virtio_snd_pcm_xfer) || le32(naglowek_) != strumien || ramka == 0 || dane % ramka != 0 ||
        dane > DZW_DANE_MAKS)
        return VIRTIO_SND_S_BAD_MSG;
    return VIRTIO_SND_S_OK;
}

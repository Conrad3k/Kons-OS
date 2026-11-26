/* K1 — host test: the guest's sound card of a zone's VMM (src/strefa/dzwiek_prosby.c; 0.12.0 step 3, ADR 0054 T5). The
 * driver's way (Linux sound/virtio): PCM_INFO, SET_PARAMS, PREPARE, START, STOP, RELEASE — answered OK in the states
 * virtio allows, BAD_MSG in others; only 48 kHz, stereo and (0.12.3) the formats of DZW_FORMATY — S16, S32, FLOAT —
 * any other "not supported"; every length and field checked; a transfer's head: stream 0, whole frames of the stream's
 * format, not too long; the samples into floats without loss. */
#include <math.h>
#include <string.h>

#include <linux/virtio_snd.h>

#include "dzwiek.h"
#include "k1_test_host.h"

static uint32_t kod(const uint8_t *odp)
{
    uint32_t v;
    memcpy(&v, odp, 4);
    return v;
}

/* A request of `n` bytes from `req`, the answer's code (0 when no answer), what to do in *zrob. */
static uint32_t prosba(struct dzw_strumien *s, const void *req, uint32_t n, int *zrob)
{
    uint8_t odp[DZW_ODPOWIEDZ_MAKS];
    memset(odp, 0, sizeof odp);
    return dzw_prosba(s, req, n, odp, sizeof odp, zrob) >= 4 ? kod(odp) : 0;
}

static uint32_t pcm(struct dzw_strumien *s, uint32_t k, uint32_t strumien, int *zrob)
{
    struct virtio_snd_pcm_hdr h = { { k }, strumien };
    return prosba(s, &h, sizeof h, zrob);
}

static struct virtio_snd_pcm_set_params param(uint8_t format, uint8_t rate, uint8_t kanaly, uint32_t bufor, uint32_t okres)
{
    struct virtio_snd_pcm_set_params p = { .hdr = { { VIRTIO_SND_R_PCM_SET_PARAMS }, 0 }, .buffer_bytes = bufor,
                                           .period_bytes = okres, .channels = kanaly, .format = format, .rate = rate };
    return p;
}

static void informacje(void)
{
    struct dzw_strumien s = { 0 };
    struct virtio_snd_query_info q = { { VIRTIO_SND_R_PCM_INFO }, 0, 1, sizeof(struct virtio_snd_pcm_info) };
    struct virtio_snd_pcm_info in;
    uint8_t odp[DZW_ODPOWIEDZ_MAKS];
    int zrob;
    uint32_t n = dzw_prosba(&s, (const uint8_t *)&q, sizeof q, odp, sizeof odp, &zrob);
    memcpy(&in, odp + 4, sizeof in);
    EXPECT(n == 4 + sizeof in && kod(odp) == VIRTIO_SND_S_OK && zrob == DZW_ZROB_NIC);
    EXPECT(in.formats == (1ull << VIRTIO_SND_PCM_FMT_S16 | 1ull << VIRTIO_SND_PCM_FMT_S32 | 1ull << VIRTIO_SND_PCM_FMT_FLOAT) &&
           in.rates == 1ull << VIRTIO_SND_PCM_RATE_48000 &&
           in.direction == VIRTIO_SND_D_OUTPUT && in.channels_min == 2 && in.channels_max == 2);
    EXPECT(dzw_prosba(&s, (const uint8_t *)&q, sizeof q, odp, 8, &zrob) == 4 && kod(odp) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(dzw_prosba(&s, (const uint8_t *)&q, sizeof q, odp, 3, &zrob) == 0); /* no room for the header */
    q.count = 2; /* one stream only */
    EXPECT(prosba(&s, &q, sizeof q, &zrob) == VIRTIO_SND_S_BAD_MSG);
    q.count = 1;
    q.start_id = 1;
    EXPECT(prosba(&s, &q, sizeof q, &zrob) == VIRTIO_SND_S_BAD_MSG);
    q.start_id = 0;
    q.size = 8;
    EXPECT(prosba(&s, &q, sizeof q, &zrob) == VIRTIO_SND_S_BAD_MSG);
    q.size = sizeof(struct virtio_snd_pcm_info);
    EXPECT(prosba(&s, &q, sizeof q - 1, &zrob) == VIRTIO_SND_S_BAD_MSG);
    q.hdr.code = VIRTIO_SND_R_JACK_INFO;
    EXPECT(prosba(&s, &q, sizeof q, &zrob) == VIRTIO_SND_S_NOT_SUPP); /* no jacks, maps, controls */
    q.hdr.code = VIRTIO_SND_R_CHMAP_INFO;
    EXPECT(prosba(&s, &q, sizeof q, &zrob) == VIRTIO_SND_S_NOT_SUPP);
    EXPECT(prosba(&s, &q, 3, &zrob) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(prosba(&s, &q, DZW_PROSBA_MAKS + 1, &zrob) == VIRTIO_SND_S_BAD_MSG);
}

static void przebieg(void)
{
    struct dzw_strumien s = { 0 };
    struct virtio_snd_pcm_set_params p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 30720, 7680);
    int zrob;
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_PREPARE, 0, &zrob) == VIRTIO_SND_S_BAD_MSG); /* before SET_PARAMS */
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_START, 0, &zrob) == VIRTIO_SND_S_BAD_MSG && zrob == DZW_ZROB_NIC);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_OK && s.stan == DZW_USTAWIONY && s.bufor == 30720 &&
           s.okres == 7680 && s.format == VIRTIO_SND_PCM_FMT_S16 && s.ramka == 4);
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_PREPARE, 1, &zrob) == VIRTIO_SND_S_BAD_MSG); /* stream 1: none */
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_PREPARE, 0, &zrob) == VIRTIO_SND_S_OK && s.stan == DZW_GOTOWY);
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_STOP, 0, &zrob) == VIRTIO_SND_S_BAD_MSG); /* not playing */
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_START, 0, &zrob) == VIRTIO_SND_S_OK && zrob == DZW_ZROB_START && s.stan == DZW_GRA);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG && s.stan == DZW_GRA); /* not while it plays */
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_RELEASE, 0, &zrob) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_STOP, 0, &zrob) == VIRTIO_SND_S_OK && zrob == DZW_ZROB_STOP && s.stan == DZW_STOP);
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_START, 0, &zrob) == VIRTIO_SND_S_OK && s.stan == DZW_GRA); /* a pause ended */
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_STOP, 0, &zrob) == VIRTIO_SND_S_OK);
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_RELEASE, 0, &zrob) == VIRTIO_SND_S_OK && zrob == DZW_ZROB_ZWOLNIJ &&
           s.stan == DZW_ZWOLNIONY);
    EXPECT(pcm(&s, VIRTIO_SND_R_PCM_PREPARE, 0, &zrob) == VIRTIO_SND_S_OK); /* again after RELEASE */
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_OK && s.stan == DZW_USTAWIONY);
}

static void formaty(void)
{
    struct dzw_strumien s = { 0 };
    struct virtio_snd_pcm_set_params p;
    int zrob;
    p = param(VIRTIO_SND_PCM_FMT_U8, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_NOT_SUPP && s.stan == DZW_NIC);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_44100, 2, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_NOT_SUPP);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 1, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_NOT_SUPP);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    p.features = 1;
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_NOT_SUPP);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 0); /* sizes */
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4098);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 4096 * 3 + 4, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 2048, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, DZW_BUFOR_MAKS + DZW_DANE_MAKS, DZW_DANE_MAKS);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 2 * (DZW_DANE_MAKS + 4), DZW_DANE_MAKS + 4);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG && s.stan == DZW_NIC);
    p = param(VIRTIO_SND_PCM_FMT_S16, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    p.hdr.stream_id = 1;
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG);
    /* 0.12.3: the guest's own formats — frames of 8 bytes; a period of half a frame of them is BAD_MSG */
    p = param(VIRTIO_SND_PCM_FMT_S24, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_NOT_SUPP);
    p = param(VIRTIO_SND_PCM_FMT_FLOAT64, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_NOT_SUPP);
    p = param(VIRTIO_SND_PCM_FMT_S32, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4100);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_BAD_MSG && s.stan == DZW_NIC);
    p = param(VIRTIO_SND_PCM_FMT_S32, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_OK && s.ramka == 8 && s.format == VIRTIO_SND_PCM_FMT_S32);
    p = param(VIRTIO_SND_PCM_FMT_FLOAT, VIRTIO_SND_PCM_RATE_48000, 2, 16384, 4096);
    EXPECT(prosba(&s, &p, sizeof p, &zrob) == VIRTIO_SND_S_OK && s.ramka == 8 && s.format == VIRTIO_SND_PCM_FMT_FLOAT);
    EXPECT(dzw_ramka(VIRTIO_SND_PCM_FMT_U8) == 0 && dzw_ramka(255) == 0 &&
           strcmp(dzw_format_nazwa(VIRTIO_SND_PCM_FMT_FLOAT), "FLOAT") == 0 && strcmp(dzw_format_nazwa(99), "?") == 0);
}

static void probki(void)
{
    const int16_t s16[4] = { 32767, -32768, 1, 0 };
    const int32_t s32[4] = { 2147483647, (int32_t)0x80000000u, 256, -256 };
    const float f[4] = { 0.25f, NAN, 7.0f, -0.5f };
    uint8_t b[17];
    float wyj[4];
    memcpy(b + 1, s16, sizeof s16); /* no alignment assumed (UBSan) */
    dzw_na_f32(wyj, b + 1, 2, VIRTIO_SND_PCM_FMT_S16);
    EXPECT(wyj[0] * 32768.0f == 32767.0f && wyj[1] == -1.0f && wyj[2] * 32768.0f == 1.0f && wyj[3] == 0); /* exact */
    memcpy(b + 1, s32, sizeof s32);
    dzw_na_f32(wyj, b + 1, 2, VIRTIO_SND_PCM_FMT_S32);
    EXPECT(wyj[0] == 1.0f && wyj[1] == -1.0f && wyj[2] * 8388608.0f == 1.0f && wyj[3] * 8388608.0f == -1.0f);
    memcpy(b + 1, f, sizeof f);
    dzw_na_f32(wyj, b + 1, 2, VIRTIO_SND_PCM_FMT_FLOAT); /* copied as it is: the sound service checks the numbers */
    EXPECT(wyj[0] == 0.25f && isnan(wyj[1]) && wyj[2] == 7.0f && wyj[3] == -0.5f);
    dzw_na_f32(wyj, b + 1, 2, VIRTIO_SND_PCM_FMT_U8);
    EXPECT(wyj[0] == 0 && wyj[3] == 0);
}

static void transfery(void)
{
    uint8_t h[4] = { 0, 0, 0, 0 }, zly[4] = { 1, 0, 0, 0 };
    EXPECT(dzw_transfer(h, 4, 4096, 4) == VIRTIO_SND_S_OK && dzw_transfer(h, 4, 0, 4) == VIRTIO_SND_S_OK);
    EXPECT(dzw_transfer(h, 4, DZW_DANE_MAKS, 4) == VIRTIO_SND_S_OK);
    EXPECT(dzw_transfer(h, 4, DZW_DANE_MAKS + 4, 4) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(dzw_transfer(h, 4, 4098, 4) == VIRTIO_SND_S_BAD_MSG); /* half a frame */
    EXPECT(dzw_transfer(h, 3, 4096, 4) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(dzw_transfer(zly, 4, 4096, 4) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(dzw_transfer(h, 4, ~0ull, 4) == VIRTIO_SND_S_BAD_MSG);
    EXPECT(dzw_transfer(h, 4, 4100, 8) == VIRTIO_SND_S_BAD_MSG && dzw_transfer(h, 4, 4096, 8) == VIRTIO_SND_S_OK);
    EXPECT(dzw_transfer(h, 4, 4096, 0) == VIRTIO_SND_S_BAD_MSG); /* no SET_PARAMS yet */
}

int main(void)
{
    informacje();
    przebieg();
    formaty();
    transfery();
    probki();
    return K1_TEST_KONIEC("test_virtio_dzwiek");
}

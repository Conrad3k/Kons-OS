/* K1 strefa — the guest's sound card (0.12.0 step 3, ADR 0054 T5): virtio-snd (virtio 1.2, section 5.14) with ONE output
 * stream, 48 kHz, two channels; no input (D1 = A: no microphone), no jacks, no channel maps, no controls. Since 0.12.3
 * (ADR 0055) the stream takes the guest's own format of DZW_FORMATY — the browser (or alsa-lib's dmix under it) no
 * longer converts to 16 bits in the guest — and the VMM turns each into K1's F32 ring without loss. The pure part: the
 * guest's control requests, the head of its transfers, checked field by field, and the samples' conversion
 * (test_virtio_dzwiek, fuzz_virtio_dzwiek). The device: dzwiek.c. */
#ifndef K1_STREFA_DZWIEK_H
#define K1_STREFA_DZWIEK_H

#include <stdint.h>

/* The stream's states (virtio 1.2, 5.14.6.6.1): what the driver did last. */
enum { DZW_NIC = 0, DZW_USTAWIONY, DZW_GOTOWY, DZW_GRA, DZW_STOP, DZW_ZWOLNIONY };

#define DZW_PROSBA_MAKS 64u          /* a control request is 4..24 bytes: longer — BAD_MSG */
#define DZW_ODPOWIEDZ_MAKS 64u       /* the longest answer: the header and one stream's information (36 bytes) */
#define DZW_BUFOR_MAKS (1u << 20)    /* the guest's buffer of the stream at most (bytes; Linux asks for ~30 KiB) */
#define DZW_DANE_MAKS (64u << 10)    /* the samples of one transfer at most (bytes): a period */

struct dzw_strumien {
    int stan;
    uint32_t bufor, okres;           /* bytes, as SET_PARAMS said */
    uint8_t format;                  /* VIRTIO_SND_PCM_FMT_* of DZW_FORMATY, as SET_PARAMS said */
    uint32_t ramka;                  /* bytes of one frame of it (0: no SET_PARAMS yet) */
};

/* The guest's formats the card offers (rule 5: a row each): X(VIRTIO_SND_PCM_FMT_ name, bytes of a frame). Into the F32
 * ring: S16 exactly; S32 rounded to the 24 bits of a float — as many as the best card plays; FLOAT copied as it is (the
 * sound service checks every number: NaN, infinities, beyond ±1.0). */
#define DZW_FORMATY(X) \
    X(S16, 4)          \
    X(S32, 8)          \
    X(FLOAT, 8)
uint32_t dzw_ramka(uint32_t format);    /* bytes of a frame of a format of DZW_FORMATY; 0 for any other */
const char *dzw_format_nazwa(uint32_t format); /* "S16", "S32", "FLOAT"; "?" for any other */
/* `ramki` frames of the guest's samples (`zrodlo`, the format of DZW_FORMATY, no alignment assumed) into floats. */
void dzw_na_f32(float *cel, const uint8_t *zrodlo, uint32_t ramki, uint32_t format);

/* What a request asks the device to do besides answering. */
enum { DZW_ZROB_NIC = 0, DZW_ZROB_START, DZW_ZROB_STOP, DZW_ZROB_ZWOLNIJ };

/* One control request: `req` (n bytes the guest gave) → the answer into `odp` (room for `max` bytes); returns its length
 * (0: no room even for the header — the guest broke the rules), *zrob: DZW_ZROB_*. Every field of the request checked:
 * a wrong one, a request in the wrong state, a format other than K1's — VIRTIO_SND_S_BAD_MSG / _NOT_SUPP, nothing done. */
uint32_t dzw_prosba(struct dzw_strumien *s, const uint8_t *req, uint32_t n, uint8_t *odp, uint32_t max, int *zrob);

/* The head of a transfer (struct virtio_snd_pcm_xfer) and the bytes of samples after it: VIRTIO_SND_S_OK when the device
 * takes them (stream 0, whole frames of `ramka` bytes — the stream's, at most DZW_DANE_MAKS), else VIRTIO_SND_S_BAD_MSG. */
uint32_t dzw_transfer(const uint8_t *naglowek, uint32_t n_naglowka, uint64_t dane, uint32_t ramka);

#endif

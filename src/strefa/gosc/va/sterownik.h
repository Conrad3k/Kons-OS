/* K1 — the VA driver of the browser zone (0.13.2 step 4, ADR 0063 pkt 2, 3): virtio_gpu_drv_video.so, loaded by libva
 * in Firefox's RDD process (and its vaapitest) of a session with films by the card. It speaks no card: every picture
 * goes as one request of the film decoder's format (k1_filmy.h) through the film context of virtio-gpu (capset
 * K1_FILMY) to the VMM, which hands it to the session's film decoder in K1 — that one checks it field by field and
 * decodes it with the card. A surface is a buffer of the card of the zone (a DRM context of the graphics broker, linear
 * NV12): the decoder writes the frame into it (K1_KF_POBIERZ) and Firefox takes it as a dma-buf (vaExportSurfaceHandle).
 * Built against the zone's glibc like our Mesa (scripts/zbuduj-va-strefy.sh); only H.264 of K1_KODEKI.
 * Pure parts (no system calls; test_va_strefy, fuzz_va_strefy): h264.c (VA buffers → the request), klatki.c (VA
 * surfaces → the film's surface numbers), sprawdz.c (the capset and the answers from K1). The rest: lacze.c
 * (virtio-gpu), sterownik.c (VA's entry points), obraz.c (EndPicture), powierzchnie.c (surfaces, buffers, export),
 * zapytania.c (what the driver can do, the unused parts of VA). */
#ifndef K1_VA_STEROWNIK_H
#define K1_VA_STEROWNIK_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include <va/va.h>
#include <va/va_backend.h>

#include "k1_filmy.h"

/* --- h264.c: one picture as a request (k1_filmy.h, family H264) ---------------------------------------------------- */
struct va_dane {      /* one VASliceDataBuffer */
    const uint8_t *p;
    uint32_t n;
};
struct va_obraz {     /* what VA gave between BeginPicture and EndPicture */
    int kodek;                                   /* K1_KODEK_* of the film */
    const VAPictureParameterBufferH264 *obraz;   /* the last picture parameter buffer */
    const VAIQMatrixBufferH264 *macierz;         /* or NULL */
    const VASliceParameterBufferH264 *plastry;   /* every slice, in order */
    const uint32_t *plaster_dane;                /* slice i: its data buffer (index into dane) */
    uint32_t n_plastrow;
    const struct va_dane *dane;
    uint32_t n_danych;
};
/* The film's surface number of a VA surface that holds a decoded picture, or -1 (none: the entry becomes "none"). */
typedef int (*va_numer_fn)(void *ja, VASurfaceID s);
/* The request into `p` (`miejsce` bytes) for the target surface number `cel`: its length, or 0 (VA asked for what the
 * format has no place for — 4:4:4, a slice with part of its data — or it does not fit). */
size_t va_h264_prosba(uint8_t *p, size_t miejsce, const struct va_obraz *w, uint32_t cel, va_numer_fn numer, void *ja);
/* The request's length before it is built (0: too large for the format) */
size_t va_h264_dlugosc(const struct va_obraz *w);

/* --- klatki.c: the film's K1_FILMY_KLATEK_MAKS surface numbers for the VA surfaces FFmpeg uses (its pool grows as it
 * needs: the VA context gets no list of surfaces) -------------------------------------------------------------------- */
#define VA_KLATKI K1_FILMY_KLATEK_MAKS
struct va_klatki {
    VASurfaceID pow[VA_KLATKI];   /* VA_INVALID_SURFACE: free */
    uint8_t zdekodowana[VA_KLATKI];
};
void va_klatki_zacznij(struct va_klatki *k);
/* The number for the target `cel`: its own, a free one, or one whose surface the picture does not use (`uzywane`,
 * `n`: its references); -1 when every number is in use. The number's picture is gone until va_klatki_gotowa. */
int va_klatki_cel(struct va_klatki *k, VASurfaceID cel, const VASurfaceID *uzywane, size_t n);
void va_klatki_gotowa(struct va_klatki *k, int numer, int dobrze);
int va_klatki_numer(const struct va_klatki *k, VASurfaceID s); /* decoded only, else -1 */
void va_klatki_zapomnij(struct va_klatki *k, VASurfaceID s);   /* the surface was destroyed */

/* --- lacze.c: virtio-gpu ----------------------------------------------------------------------------------------- */
struct va_lacze {
    char wezel[32];                 /* the render node (as libva's) */
    struct k1_filmy_zbior zbior;    /* what the card decodes */
    int fd_filmy;                   /* the film context (-1: not open — no VA context) */
    uint8_t *wsp;                   /* its shared memory, VA_WSPOLNA bytes */
    uint32_t numer;
    int fd_drm;                     /* the DRM context of the buffers (-1: not open yet) */
    volatile uint8_t *wsp_drm;
    uint32_t seq, blob;
};
#define VA_WSPOLNA K1_KF_WSPOLNA_MAKS
/* What the card decodes from the capset of the node of libva's `fd`: 0, or -1 (no film context in this session). */
int va_lacze_zacznij(struct va_lacze *l, int fd);
int va_lacze_filmy(struct va_lacze *l);       /* the film context: 0 or -1 */
void va_lacze_filmy_koniec(struct va_lacze *l);
void va_lacze_koniec(struct va_lacze *l);
/* One command; its result (0, K1_FILMY_BLAD_*, K1_KF_BLAD_DEKODER) or VA_BRAK_ODPOWIEDZI. */
#define VA_BRAK_ODPOWIEDZI (-100)
int va_lacze_polecenie(struct va_lacze *l, uint32_t typ, uint32_t film, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
/* A buffer of the card of `dl` bytes: its handle (0: none) and resource number. */
uint32_t va_lacze_bufor(struct va_lacze *l, uint64_t dl, uint32_t *zasob);
void va_lacze_bufor_koniec(struct va_lacze *l, uint32_t uchwyt);
int va_lacze_dmabuf(struct va_lacze *l, uint32_t uchwyt); /* a new dma-buf of the buffer, or -1 */
/* sprawdz.c — pure: is the capset's content one this driver speaks; is the answer the command's */
int va_zbior_dobry(const struct k1_filmy_zbior *z, size_t n);
int va_odpowiedz(const void *wsp, uint32_t numer);

/* --- sterownik.c, powierzchnie.c, zapytania.c ---------------------------------------------------------------------- */
#define VA_KONFIGURACJI 16
#define VA_KONTEKSTOW K1_KF_FILMY
#define VA_ID_KONF 0x10000000u
#define VA_ID_POW 0x20000000u
#define VA_ID_KONT 0x30000000u
#define VA_ID_BUF 0x40000000u
#define VA_ID_OBRAZ 0x50000000u

struct va_konfiguracja {
    int jest;
    VAProfile profil;
};
struct va_powierzchnia {
    int jest;
    uint32_t w, h;          /* the layout: the film's size once a context uses it */
    uint32_t uchwyt, zasob; /* the buffer of the card */
    uint64_t dl;
};
struct va_bufor {
    int jest;
    VABufferType typ;
    uint32_t rozmiar, ile;  /* one element's size, elements */
    uint8_t *dane;
};
struct va_kontekst {
    int jest, kodek;
    uint32_t w, h;
    VASurfaceID cel;        /* between BeginPicture and EndPicture, else VA_INVALID_SURFACE */
    int zepsuty;            /* a RenderPicture failed: EndPicture sends nothing (FFmpeg still calls it) */
    VABufferID *bufory;
    uint32_t n_buforow, miejsce_buforow;
    struct va_klatki klatki;
};
struct va_dane_sterownika {
    pthread_mutex_t zamek;
    struct va_lacze l;
    struct va_konfiguracja konf[VA_KONFIGURACJI];
    struct va_kontekst kont[VA_KONTEKSTOW];
    struct va_powierzchnia *pow;
    uint32_t n_pow;
    struct va_bufor *buf;
    uint32_t n_buf;
};
#define VA_DANE(ctx) ((struct va_dane_sterownika *)(ctx)->pDriverData)

int va_kodek(const struct va_dane_sterownika *d, VAProfile p); /* K1_KODEK_* the card decodes, or 0 */
VAStatus va_status(int wynik);                                  /* K1's result → VA's */
struct va_powierzchnia *va_pow(struct va_dane_sterownika *d, VASurfaceID id);
struct va_bufor *va_buf(struct va_dane_sterownika *d, VABufferID id);
VAStatus va_obraz_koniec(struct va_dane_sterownika *d, uint32_t film, struct va_kontekst *k);
void va_vtable_powierzchnie(struct VADriverVTable *v);
void va_vtable_zapytania(struct VADriverVTable *v);
VAStatus __vaDriverInit_1_0(VADriverContextP ctx);

#endif

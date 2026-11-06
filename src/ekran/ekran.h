/* K1 ekran — state shared by the compositor (main.c) and the KMS part (drm.c). */
#ifndef K1_EKRAN_H
#define K1_EKRAN_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "k1_grafika.h"
#include "k1_komunikat.h"

#define TAG "K1-EKRAN"
#define MAKS_KLIENTOW 12
#define MAKS_POW 48
#define MAKS_PROST 16

struct prost {
    int32_t x, y, w, h;
};

/* A small set of damaged rectangles; on overflow they collapse into their bounding box. */
struct obszar {
    struct prost r[MAKS_PROST];
    int n;
};

struct bufor {
    uint32_t uchwyt, fb, krok_bajty;
    uint64_t rozmiar;
    struct k1_obraz obraz;
    struct obszar brud;   /* what must be copied from the scene before this buffer is shown again */
    int32_t kx, ky;       /* cursor drawn here (top-left), valid if ma_kursor */
    int ma_kursor;
};

struct klient {
    int fd;               /* -1 = free slot */
    uid_t uid;            /* expected sender (from the supervisor) */
    int rola, apl;
    unsigned long odrzucone;
    int zalogowane;       /* rejection log lines written (rate limit) */
};

struct pow {
    int uzyta;
    int kl;               /* client index */
    uint32_t id;          /* the client's surface number 1..K1_POW_MAKS */
    uint32_t okno;        /* global window id (windows only) */
    int rodzaj;
    int32_t x, y, w, h;
    int widoczna, zmin, pokaz;
    const uint32_t *mem;
    size_t rozmiar;
    int polowa;           /* committed half, -1 = nothing yet */
    int czeka_ramka;      /* send RAMKA after the scene used the commit */
    unsigned z;
};

struct stan {
    int fd;
    uint32_t crtc, zlacze;
    struct drm_mode_modeinfo tryb;
    struct bufor buf[2];
    int przedni, flip_w_toku, brak_flip;
    int32_t W, H;
    struct k1_obraz tlo, scena;
    struct obszar nowe;   /* scene areas to recompose */
    uint32_t kursor[2][K1_KURSOR_SZER * K1_KURSOR_WYS];
    int32_t x, y;         /* cursor hotspot */
    float rx, ry;         /* sub-pixel motion remainder (pointer speed) */
    int przyciski;        /* bit per K1_PRZYCISK_* held */
    int wcisniety, zmiana, mody;
    int pod, chwyt, ciagniete, fokus; /* surface indices or -1 */
    int32_t ciag_dx, ciag_dy;
    unsigned z_licznik;
    uint32_t nastepne_okno;
    int ust[K1_UST_MAKS + 1];
    unsigned long klatki, klawisze, ruchy, odrzucone, obcy;
    struct klient kl[MAKS_KLIENTOW];
    struct pow p[MAKS_POW];
};

/* drm.c */
int drm(int fd, unsigned long req, void *arg);
double teraz(void);
void wybierz_wyjscie(struct stan *s);
void utworz_bufor(struct stan *s, struct bufor *b);

#endif

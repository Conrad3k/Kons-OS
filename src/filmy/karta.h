/* K1 filmy — what the parts of the card engine share (karta.c, karta_h264.c). */
#ifndef K1_FILMY_KARTA_H
#define K1_FILMY_KARTA_H

#include <va/va.h>

#include "filmy.h"

struct karta_film {
    VAConfigID cfg;
    VAContextID kontekst;
    VASurfaceID pow[K1_FILMY_KLATEK_MAKS];
    uint32_t n;      /* surfaces made */
    VAImage obraz;   /* linear NV12 the card copies a surface into (pobierz) */
};
extern VADisplay karta_dpy;
extern struct karta_film karta_filmy[K1_FILMY_FILMOW_MAKS + 1];

int karta_blad(VAStatus s); /* K1_FILMY_BLAD_PAMIEC or _KARTA */
/* An H.264 request (checked) into surface `cel`: VA's parameter buffers, then the picture to the card. */
int karta_klatka_h264(int film, const struct k1_filmy_film *f, uint32_t cel, const uint8_t *p,
                      const struct k1_filmy_czesci *c);

#endif

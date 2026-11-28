/* K1 — the host test of the zone's VA driver: what the fake K1 (va_k1_falszywe.c) saw and answered. */
#ifndef K1_VA_K1_FALSZYWE_H
#define K1_VA_K1_FALSZYWE_H

#include "sterownik.h"

struct falsz {
    struct k1_filmy_zbior zbior;   /* the capset the test gives the driver */
    int bez_pamieci;               /* no buffer of the card (the zone's limit) */
    int odmow_pobrania;            /* K1_KF_POBIERZ: the decoder is gone */
    int konteksty, buforow;        /* film contexts open, buffers alive */
    int klatki, pobrane;           /* pictures K1 accepted, frames written into buffers */
    int polecenia[8];              /* by K1_KF_* */
    int ostatni;                   /* the last answer */
    uint8_t prosba[1u << 20];      /* the last request (its first MiB) */
    uint32_t prosba_dl;
};
extern struct falsz falsz;

#endif

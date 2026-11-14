/* K1 strefa — the VMM: one process = one micro machine (ADR 0042 T1). Shared state of its parts:
 * main.c (start, the loop of the main thread, the end), kvm.c (the machine, its processors and their exits),
 * the devices (blk.c, konsola.c, rng.c, wejscie.c, gpu.c, vsock.c), okno.c (the window at ekran).
 *
 * Trust: everything that comes from the guest (its memory, its queues, its register accesses) is hostile — checked
 * field by field (virtq.c, mmio.c and each device); a wrong value ends this machine (koniec()), never K1. The VMM runs
 * in KLATKA_STREFA: its own UID, no paths, no network, only KVM ioctls on the descriptors nadzorca gives it. */
#ifndef K1_STREFA_H
#define K1_STREFA_H

#include <pthread.h>
#include <stdint.h>

#include "k1_uslugi.h"
#include "mmio.h"
#include "start.h"

#define TAG "K1-STREFA"

/* Descriptors from nadzorca (KANAL… order of strefy.c) */
#define FD_EKRAN 3   /* the channel to ekran, role STREFA */
#define FD_KVM 4     /* /dev/kvm */
#define FD_JADRO 5   /* the zone kernel, read only */
#define FD_OBRAZ 6   /* the zone image (SquashFS), read only */

/* The devices of every zone: one row each (readability rule 5) — its MMIO window is MMIO_BAZA + i × MMIO_ROZMIAR and
 * its interrupt line PRZERWANIE_PIERWSZE + i, in this order; the command line of the guest names them in this order
 * too (the first blk device is /dev/vda). X(NAME, ops) */
#define K1_URZADZENIA(X)  \
    X(OBRAZ, blk_ops)     \
    X(KONSOLA, konsola_ops) \
    X(LOSOWOSC, rng_ops)  \
    X(KLAWIATURA, klawiatura_ops) \
    X(MYSZ, mysz_ops)     \
    X(EKRAN, gpu_ops)     \
    X(VSOCK, vsock_ops)
#define X_URZ(n, ops) URZ_##n,
enum { K1_URZADZENIA(X_URZ) URZ_LICZBA };
#undef X_URZ
#define MMIO_BAZA 0xd0000000ull
#define PRZERWANIE_PIERWSZE 5

struct procesor {
    int fd;
    struct kvm_run *run;
    pthread_t watek;
};

struct maszyna {
    const struct k1_strefa_opis *opis;   /* its row of K1_STREFY */
    int kvm, vm;
    struct gosc_pamiec pamiec;
    int n_cpu;
    long rozmiar_run;
    struct procesor cpu[ST_PROCESORY_MAKS];
    pthread_mutex_t zamek;               /* the devices: processor threads and the main thread */
    struct mmio_urz urz[URZ_LICZBA];
    int irqfd[URZ_LICZBA];
    int budzik;                          /* eventfd: wakes the main thread (the end, a picture, a message) */
    uint64_t obraz_sektory;              /* the zone image (FD_OBRAZ) in 512-byte sectors */
    int tryb;                            /* the guest's test mode (argv[2], gosc/gosc.h): 0 none; 1..4 a hostile guest
                                          * drives slot LOSOWOSC itself (3, 4: virtio-blk there) */
    volatile int koniec;
    const char *powod;                   /* why the machine ended (a constant text) */
    unsigned long wyjscia_io, wyjscia_mmio, nieznane;
};
extern struct maszyna mz;

/* The machine ends: the reason goes to the log, the main thread ends the process. Thread-safe; the first reason stays. */
void koniec(const char *powod);
void obudz(void);    /* wakes the main thread */

/* kvm.c */
int kvm_maszyna(uint64_t pamiec_mib, int procesory); /* 0, or -1 (logged) */
int kvm_urzadzenia(void);                            /* the devices of K1_URZADZENIA with their interrupts */
int kvm_procesory(const struct st_wejscie *w);
int kvm_uruchom(void);
void kvm_linia_urzadzen(char *buf, size_t n);        /* "virtio_mmio.device=4K@0x…:5 …" */

/* the devices (each its file) */
const uint32_t *gpu_obraz(uint32_t *w, uint32_t *h); /* gpu.c: the shown resource (under mz.zamek), NULL: none yet */
void wejscie_klawisz(int kod, int wartosc);          /* wejscie.c: a Linux key code, 0 up / 1 down / 2 repeat */
void wejscie_wskaznik(int32_t x, int32_t y, uint32_t w, uint32_t h, int rodzaj, int przycisk); /* K1_WSK_* in a w×h window */
void wejscie_pusc(void);                             /* focus lost: every held key and button up */
void vsock_zamknij(void);                            /* vsock.c: "close" to the guest */
extern const struct mmio_ops blk_ops, konsola_ops, rng_ops, klawiatura_ops, mysz_ops, gpu_ops, vsock_ops;

#endif

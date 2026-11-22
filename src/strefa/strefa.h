/* K1 strefa — the VMM: one process = one micro machine (ADR 0042 T1). Shared state of its parts:
 * main.c (start, the loop of the main thread, the end), kvm.c (the machine, its processors and their exits),
 * the devices (blk.c, konsola.c, rng.c, wejscie.c, gpu.c, vsock.c; dane.c + dysk.c: the disk of a zone that
 * remembers, 0.10.0 step 5), okno.c (the window at ekran).
 *
 * Trust: everything that comes from the guest (its memory, its queues, its register accesses) is hostile — checked
 * field by field (virtq.c, mmio.c and each device); a wrong value ends this machine (koniec()), never K1. The VMM runs
 * in KLATKA_STREFA: its own UID, no paths, no network, only KVM ioctls on the descriptors nadzorca gives it. */
#ifndef K1_STREFA_H
#define K1_STREFA_H

#include <pthread.h>
#include <stdint.h>

#include <linux/virtio_gpu.h>

#include "k1_komunikat.h"
#include "k1_uslugi.h"
#include "lacznik.h"
#include "mmio.h"
#include "start.h"

#define TAG "K1-STREFA"

/* Descriptors from nadzorca (KANAL… order of strefy.c) */
#define FD_EKRAN 3   /* the channel to ekran, role STREFA */
#define FD_KVM 4     /* /dev/kvm */
#define FD_JADRO 5   /* the zone kernel, read only */
#define FD_OBRAZ 6   /* the zone image (SquashFS), read only */
/* fd 7: the channel to nadzorca for the browser's connections (polaczenia.h) */
#define FD_SKARB 8   /* 0.10.0 step 5: a zone that remembers — a channel to skarbiec (the owner: the zone's profile) */
#define FD_WZOR 9    /* …and its empty disk (/rdzen/strefy/dysk.img), read only; a zone that forgets: neither */
#define FD_POSREDNIK 10 /* 0.10.9 step 6: a session with fast graphics on — the channel to its graphics broker; else none
                         * (fd 8, 9 are then placeholders when the zone forgets: dane.c sees no disk) */

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
    X(VSOCK, vsock_ops)   \
    X(DANE, dane_ops)     \
    X(BALON, balon_ops)
#define X_URZ(n, ops) URZ_##n,
enum { K1_URZADZENIA(X_URZ) URZ_LICZBA };
#undef X_URZ
#define MMIO_BAZA 0xd0000000ull
#define PRZERWANIE_PIERWSZE 5

/* Where a processor thread is (stroz.c tells it when the main thread stops). */
enum { ST_W_GOSCIU, ST_WYJSCIE, ST_CZEKA, ST_ZAMEK };

struct procesor {
    int fd;
    struct kvm_run *run;
    pthread_t watek;
    volatile int gdzie;                  /* ST_* */
    volatile unsigned wyjscie;           /* its last exit (KVM_EXIT_*) */
    volatile int urz;                    /* the device of its last MMIO exit (-1: none) */
};

struct maszyna {
    const struct k1_strefa_opis *opis;   /* its row of K1_STREFY */
    int kvm, vm;
    struct gosc_pamiec pamiec;
    int pamiec_fd;                       /* its memfd (0.10.1 step 2: the balloon punches pages out of it) */
    uint32_t pamiec_mib;                 /* its size: the zone's upper limit (argv[3]) */
    int n_cpu;
    long rozmiar_run;
    struct procesor cpu[ST_PROCESORY_MAKS];
    pthread_mutex_t zamek;               /* the devices: processor threads and the main thread */
    struct mmio_urz urz[URZ_LICZBA];
    int irqfd[URZ_LICZBA];
    int budzik;                          /* eventfd: wakes the main thread (the end, a picture, a message) */
    uint64_t obraz_sektory;              /* the zone image (FD_OBRAZ) in 512-byte sectors */
    int tryb;                            /* the guest's test mode (argv[2], gosc/gosc.h): 0 none; 1..4 a hostile guest
                                          * drives slot LOSOWOSC itself (3, 4: virtio-blk there); 5: an honest one that
                                          * counts its start on the disk (0.10.0 step 5) */
    volatile int koniec;
    double start_t;                      /* when the VMM started (k1_teraz) */
    const char *powod;                   /* why the machine ended (a constant text) */
    volatile int kod_goscia;             /* why the guest's program ended by itself (K1_STREFA_KONIEC_*, 0: it did not
                                          * say, or it closed normally) — the VMM's exit code (0.10.0 step 3) */
    volatile int gosc_powiedzial;        /* "koniec …" came (first one counts) */
    volatile int zamykana;               /* ekran asked to close it: the end is the user's (exit code 0) */
    volatile double zamykana_t;          /* when (k1_teraz) — the guest says how long its program took to close */
    int prosba_nadzorcy;                 /* 0.10.2 step 1: nadzorca asked to close it (K1 powers off; main thread) */
    unsigned strony_bez_pamieci;         /* pages of the browser the guest killed for lack of memory */
    unsigned long wyjscia_io, wyjscia_mmio, nieznane;
    volatile double petla_t;             /* the main thread's last step (stroz.c) and its name */
    const char *volatile petla_krok;
    const char *volatile zamek_kto;      /* who holds mz.zamek (a constant name), NULL: nobody */
    char pobierz[LACZNIK_CEL_MAKS + 1];  /* 0.10.5 step 2: the download zone — what it downloads (argv[4], checked) */
};
extern struct maszyna mz;
#define ST_TRYB_ZLOSLIWY 4 /* test modes 1..4 and K1_STREFA_TRYB_BALON_OD..DO: a hostile guest (gosc/zlosliwy.c) */
static inline int zlosliwy_gosc(void)
{
    return (mz.tryb >= 1 && mz.tryb <= ST_TRYB_ZLOSLIWY) ||
           (mz.tryb >= K1_STREFA_TRYB_BALON_OD && mz.tryb <= K1_STREFA_TRYB_BALON_DO) ||
           (mz.tryb >= K1_STREFA_TRYB_VSOCK_OD && mz.tryb <= K1_STREFA_TRYB_VSOCK_DO);
}
/* The device a hostile test guest drives itself (left out of its kernel's command line): vsock in the modes hostile to
 * it (0.10.5 step 5), else slot LOSOWOSC; -1: an honest guest. */
static inline int zlosliwy_slot(void)
{
    if (!zlosliwy_gosc())
        return -1;
    return mz.tryb >= K1_STREFA_TRYB_VSOCK_OD ? URZ_VSOCK : URZ_LOSOWOSC;
}

/* stroz.c: mz.zamek with its holder's name; the main thread's steps; the watchman's thread */
void zamek_wez(const char *kto);
void zamek_oddaj(void);
void petla_krok(const char *krok);
int stroz_start(void);

/* The machine ends: the reason goes to the log, the main thread ends the process. Thread-safe; the first reason stays. */
void koniec(const char *powod);
void obudz(void);    /* wakes the main thread */

/* kvm.c */
int kvm_maszyna(uint64_t pamiec_mib, int procesory); /* 0, or -1 (logged) */
int emulacja_qemu(void);                             /* 1: K1 runs in QEMU's emulation (TCG) — the tests only */
int kvm_urzadzenia(void);                            /* the devices of K1_URZADZENIA with their interrupts */
int kvm_procesory(const struct st_wejscie *w);
int kvm_uruchom(void);
void kvm_linia_urzadzen(char *buf, size_t n);        /* "virtio_mmio.device=4K@0x…:5 …" */

/* powiadomienia.c (0.11.2): an eventfd per queue that KVM signals on the guest's QueueNotify, and the thread that hands
 * them to the devices (under mz.zamek) — the processor does not leave the guest for them */
int powiadomienia_start(void);              /* after kvm_urzadzenia, before kvm_uruchom: 0, or -1 (logged) */
unsigned long powiadomienia_obsluzone(void); /* notifications handled (the log at the end) */

/* the devices (each its file) */
const uint32_t *gpu_obraz(uint32_t *w, uint32_t *h); /* gpu.c: the shown resource (under mz.zamek), NULL: none yet */
void gpu_liczniki(unsigned *n, unsigned *mib);
void gpu_zmiana_ekranu(void);  /* gpu.c, under mz.zamek: the display's size changed (okno.c) — the guest is told */
/* The answer to a 2D command: the header, the display (DISPLAY_INFO) or its EDID (0.11.0 step 1, edid.c). */
union gpu_odp {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_resp_display_info info;
    struct virtio_gpu_resp_edid edid;
};
/* gpu.c: a 2D command of the control queue (gpu_lacze.c: under mz.zamek): 0, or -1 when shorter than its structure */
int gpu_2d(const struct vq_lancuch *l, uint32_t typ, uint32_t *wynik, union gpu_odp *odp);
uint32_t gpu_odp_dl(uint32_t wynik); /* the bytes of the answer of result `wynik` */
int gpu_jest_2d(uint32_t typ, uint32_t id);
/* gpu_lacze.c (0.10.9 step 6): fast graphics — the bridge to the graphics broker (fd 10), its thread, KVM's slots */
int gpu_lacze_start(void);      /* 0 (with or without a broker), -1: the machine cannot start */
int gpu_lacze_jest(void);       /* the device offers 3D (the broker said the card can) */
int gpu_lacze_zasob(uint32_t id); /* a blob resource id exists */
void gpu_lacze_obudz(struct mmio_urz *u);
void gpu_lacze_reset(struct mmio_urz *u);
uint64_t gpu_lacze_cechy(const struct mmio_urz *u);
int gpu_lacze_okno(const struct mmio_urz *u, uint32_t id, uint64_t *baza, uint64_t *dl);
void gpu_lacze_koniec(void);    /* at the end: the numbers to the log */
void wejscie_klawisz(int kod, int wartosc);          /* wejscie.c: a Linux key code, 0 up / 1 down / 2 repeat */
void wejscie_wskaznik(int32_t x, int32_t y, uint32_t w, uint32_t h, int rodzaj, int przycisk); /* K1_WSK_* in a w×h window */
/* the wheel or two fingers at x, y (K1_KOM_PRZEWIN: c, d in 1/120 of a notch, d > 0 further down; e = source) */
void wejscie_przewin(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t dx, int32_t dy, int zrodlo);
void wejscie_podsumowanie(void);                     /* at the end: whether input reached the guest, in the log */
void wejscie_pusc(void);                             /* focus lost: every held key and button up */
void vsock_zamknij(void);                            /* vsock.c: "close" to the guest */
void vsock_linia(const char *l);                     /* vsock.c: a line to the guest's control channel ("…\n") */
void vsock_podsumowanie(void);                       /* vsock.c: the last counters' line (0.10.1 step 1) */
void vsock_liczniki(void);                           /* vsock.c: the counters' line every 5 s (main thread, under the lock) */
/* balon.c (0.10.1 step 2, ADR 0044): the guest's memory as K1 sees it, for nadzorca (STREFA_PAMIEC) and the log */
struct balon_stan {
    uint32_t w_k1_mib;     /* the guest's memory K1 really holds now (the memfd's pages) */
    int64_t dostepna_mib;  /* available in the guest, its own statistics (a hint); -1: none yet */
    uint32_t balon_mib, cel_mib, oddane_mib; /* in the balloon, what K1 asks, handed back to K1 since the start */
};
void balon_cel(uint32_t mib);                /* K1 wants mib MiB in the balloon (STREFA_BALON from nadzorca) */
void balon_krok(void);                       /* once a second: a new copy of the guest's statistics */
void balon_stan(struct balon_stan *s);                       /* vsock.c: the last counters' line (0.10.1 step 1) */
int dane_start(void);   /* dane.c: the disk (fd 8, 9 — or none) and its thread; 0, or -1 */
void dane_koniec(void); /* its thread ends, the dirty pieces are saved (at most 20 s), the numbers to the log */
extern const struct mmio_ops blk_ops, konsola_ops, rng_ops, klawiatura_ops, mysz_ops, gpu_ops, vsock_ops, dane_ops,
    balon_ops;

#endif

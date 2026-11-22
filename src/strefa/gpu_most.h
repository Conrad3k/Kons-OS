/* K1 strefa — the bridge of fast graphics in virtio-gpu (0.10.9 step 6, ADR 0049 pkt 4, 7; plan T12, S40): what the
 * guest's 3D commands mean when its zone has a graphics broker (posrednik). In the zone runs a real driver of the card
 * (Mesa, iris with intel-virtio): it opens a virtio-gpu context with the capset DRM (one per process of the zone that
 * opens the card), sends the bridge's requests in SUBMIT_3D, creates buffers as blob resources and maps them into its
 * memory (the host-visible region). This module checks every command field by field and turns it into what the broker
 * and the machine's memory do — through struct most_lacze (gpu_lacze.c: the broker's channel, the window, KVM's memory
 * slots; the host test: a fake). The requests inside SUBMIT_3D are not read here: the broker copies and checks them.
 * Pure (no system calls of its own): tested on the host (test_gpu_most), fuzzed (fuzz_gpu_most).
 * Trust: the guest is hostile — a command no honest driver sends gets an error reply (the guest's Mesa fails, Firefox
 * draws with the processor), a command shorter than its structure ends the machine (as every device). A buffer is
 * mapped into the guest read-write, never executable (niezmiennik 1: gpu_lacze.c maps without PROT_EXEC). */
#ifndef K1_STREFA_GPU_MOST_H
#define K1_STREFA_GPU_MOST_H

#include <stdint.h>

#include <linux/virtio_gpu.h>

#include "k1_komunikat.h" /* K1_POW_MAKS_BOK, K1_STREFA_* (0.10.11 step 2) */
#include "k1_posrednik.h"

#define MOST_OKNO_BAZA (32ull << 30) /* the host-visible region (virtio-gpu shm id 1) in guest addresses: here for a guest
                                      * up to 28 GiB (below 36 bits: X230), else right above its memory (0.11.3:
                                      * most_okno_baza — a guest of 32 GiB and more would overlap it) */
#define MOST_OKNO_DL (1ull << 30)    /* its length: K1_POSR_PAMIEC of buffers + the shared memories, with room */
#define MOST_MAPY_MAKS 2048u         /* buffers mapped at once (each a memory slot of KVM) */
#define MOST_SLOT_PIERWSZY 2u        /* KVM's slots 0, 1: the guest's memory (kvm.c) */
#define MOST_ODP_MAKS 512u           /* the longest reply (the capset) */
#define MOST_SUBMIT 32u              /* struct virtio_gpu_cmd_submit: the header and the stream's size */
#define MOST_POLECENIE_MAKS (MOST_SUBMIT + K1_POSR_PROSBY_BAJTY) /* the longest command: SUBMIT_3D with its stream */

/* The capset DRM (VIRTIO_GPU_CAPSET_DRM) — Mesa's struct virgl_renderer_capset_drm (src/virtio/virtio-gpu/drm_hw.h,
 * Mesa 26.2.6) as a driver built without amdgpu sees it: 24 bytes, then the union whose largest member (amdgpu's,
 * without its library) has 136 bytes; for i915 (context_type 3) only the PCI part means something. */
struct most_capset {
    uint32_t wire_format_version, version_major, version_minor, version_patchlevel, context_type, pad;
    uint32_t pci_bus, pci_dev, pci_func, pci_revision_id, pci_domain, pci_device_id;
    uint8_t reszta[112];
};
_Static_assert(sizeof(struct most_capset) == 160, "most_capset: Mesa's virgl_renderer_capset_drm");
#define MOST_KONTEKST_I915 3 /* VIRTGPU_DRM_CONTEXT_I915 */
#ifndef VIRTIO_GPU_CAPSET_DRM
#define VIRTIO_GPU_CAPSET_DRM 6 /* linux/virtio_gpu.h since Linux 6.12; the host tests may build with older headers */
#endif

/* A buffer of the card the guest shows as its display (SET_SCANOUT_BLOB, 0.10.11 step 2, ADR 0051): its resource, the
 * picture's size, the bytes of a row and where the picture starts in it. */
struct most_obraz {
    uint32_t res, szer, wys, krok, przes;
};

/* What the bridge needs from outside (each returns 0 or a K1_POSR_BLAD_*; MOST_PADL — the broker is gone). */
#define MOST_PADL (-100)
struct most_lacze {
    void *ja;
    uint8_t *okno;                                                           /* K1_POSR_OKNO bytes: the stream */
    int (*prosby)(void *ja, uint32_t gosc, uint32_t dl);                     /* the stream in okno → POSR_PROSBY */
    int (*wspolna)(void *ja, uint32_t gosc, uint64_t rozmiar, int *fd);      /* a new shared memory → POSR_WSPOLNA */
    int (*blob)(void *ja, uint32_t gosc, uint32_t res, uint64_t blob, uint64_t rozmiar); /* POSR_BLOB */
    int (*bufor)(void *ja, uint32_t res, int *fd, uint32_t *mapa);           /* POSR_BUFOR → the dma-buf */
    void (*zwolnij)(void *ja, uint32_t res);                                 /* POSR_ZWOLNIJ */
    void (*plot)(void *ja, uint32_t gosc, uint32_t pierscien, uint64_t nr);  /* POSR_PLOT */
    void (*gosc_koniec)(void *ja, uint32_t gosc);                            /* POSR_GOSC_KONIEC */
    void (*zamknij)(void *ja, int fd);                                       /* a descriptor of ours goes */
    int (*mapuj)(void *ja, uint32_t slot, uint64_t gpa, int fd, uint64_t rozmiar); /* 0, or -1: not mapped */
    void (*odmapuj)(void *ja, uint32_t slot, uint64_t gpa, uint64_t rozmiar);
    /* 0.10.11 step 2: the guest shows buffer o (its dma-buf fd, ours: borrowed for the call) — 0, or -1: the window
     * cannot (bigger than it); o NULL: the guest shows no buffer of the card any more. obraz_koniec: buffer res is gone. */
    int (*obraz)(void *ja, const struct most_obraz *o, int fd);
    void (*obraz_koniec)(void *ja, uint32_t res);
};

enum { MOST_BRAK = 0, MOST_WSPOLNA = 1, MOST_BUFOR = 2 };
struct most_zasob {
    uint8_t rodzaj;   /* MOST_*: none, the shared memory of a guest context (its blob 0), a buffer of the card */
    uint8_t gosc;     /* the guest context that created it */
    uint8_t mapa;     /* VIRTIO_GPU_MAP_CACHE_* of its mapping */
    uint8_t mapowalny;
    int fd;           /* MOST_WSPOLNA: the memfd (ours); MOST_BUFOR: its dma-buf once the guest showed it (0.10.11) */
    uint32_t slot;    /* mapped: its KVM slot (0: not mapped) */
    uint64_t rozmiar, gpa;
};

/* A command the reply of which waits for a fence of the card (SUBMIT_3D with VIRTIO_GPU_FLAG_FENCE). */
struct most_czeka {
    uint32_t gosc, pierscien;
    uint64_t plot;
};

struct most {
    const struct most_lacze *l;
    struct most_capset caps;
    uint32_t ctx[K1_POSR_GOSCIE + 1]; /* the guest's virtio-gpu context of each guest context (0: free) */
    struct most_zasob zasoby[K1_POSR_ZASOBY_MAKS];
    uint8_t sloty[MOST_MAPY_MAKS];
    uint64_t okno_baza;               /* the host-visible region's guest address (most_okno_baza; most_start: the
                                       * smallest, MOST_OKNO_BAZA) */
    int padl;                         /* the broker ended (a crash, a hang): every 3D command fails, fences are done */
    unsigned long polecenia, bledy, obrazy; /* obrazy: SET_SCANOUT_BLOB accepted (0.10.11 step 2) */
};

/* The bridge with the card the broker described (POSR_UMIE: PCI id, revision, the driver's version). */
void most_start(struct most *m, const struct most_lacze *l, uint32_t pci, uint32_t rewizja, uint32_t wersja);

/* One command of the control queue (the readable part of the chain: p, n bytes; its type is not 2D — gpu.c does those).
 * The reply (its header too; gpu.c copies the fence's fields) into odp, *odp_dl bytes. Returns 0: answer now; 1: answer
 * when the broker says fence *c is done (gpu_lacze.c); -1: a broken guest (*powod) — the machine ends. */
int most_polecenie(struct most *m, const uint8_t *p, uint64_t n, uint8_t *odp, uint32_t *odp_dl, struct most_czeka *c,
                   const char **powod);

/* 0.11.3: where the host-visible region goes for a guest whose memory ends at guest address `koniec_pamieci`:
 * MOST_OKNO_BAZA, or the first whole GiB above that memory when it reaches further. */
uint64_t most_okno_baza(uint64_t koniec_pamieci);

int most_zasob_jest(const struct most *m, uint32_t res); /* a blob resource res exists (2D must not take its number) */
void most_reset(struct most *m);                         /* the device's reset: every context and resource goes */
void most_padl(struct most *m);                          /* the broker is gone: nothing new (mappings stay) */

#endif

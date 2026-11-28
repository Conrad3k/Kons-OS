/* K1 nadzorca — sandbox profiles of the services (ADR 0007). Part of nadzorca (see main.c). */
#include "nadzorca.h"

#include <linux/dma-buf.h>
#include <linux/falloc.h>
#include <sound/asound.h>
#include <sched.h>

static const long sc_wejscie[] = { 0 };
static const struct k1_warunek wa_wejscie[] = { { SYS_ioctl, 1, 0xff00u, 'E' << 8 } }; /* evdev only */
/* ekran: + memfd for the settings it saves in skarbiec (M5d) */
static const long sc_ekran[] = { SYS_memfd_create };
static const struct k1_warunek wa_ekran[] = { { SYS_ioctl, 1, 0xff00u, 'd' << 8 } };   /* DRM only */
/* ekran since 0.10.11 step 2 (ADR 0051): + DMA_BUF_IOCTL_SYNC alone — before and after it reads a zone's buffer of the
 * card (strefa_bufor.c: the card's caches written back before the processor reads) */
static const struct k1_warunek wa_ekranu[] = { { SYS_ioctl, 1, 0xff00u, 'd' << 8 },
                                                { SYS_ioctl, 1, 0xffffffffu, (uint32_t)DMA_BUF_IOCTL_SYNC } };

/* skarbiec (M5d): reads/writes its partition (fd from us), randomness for keys, memfd for data,
 * sysinfo for the Argon2 memory size. No open: Landlock without paths. */
static const long sc_skarbiec[] = { SYS_pread64, SYS_pwrite64, SYS_fdatasync, SYS_getrandom, SYS_memfd_create,
                                    SYS_sysinfo };
/* czytnik (0.8.0 step 3, ADR 0031): reads the stick we open for it (read only), hands files over in sealed memfd. */
static const long sc_czytnik[] = { SYS_pread64, SYS_memfd_create };
/* dekoder (0.8.0 step 4, ADR 0032): reads the file and hands the picture over in sealed memfd — nothing else. */
static const long sc_dekoder[] = { SYS_memfd_create };
/* gui and applications: shared memory for their surfaces, nothing else. */
static const long sc_okna[] = { SYS_memfd_create, SYS_ftruncate };
/* instalator (M5f): the two devices it gets from us, randomness for the GPT GUIDs. No open. */
static const long sc_instalator[] = { SYS_pread64, SYS_pwrite64, SYS_fdatasync, SYS_getrandom };
/* siec (0.9.0, ADR 0039): randomness for the DHCP transaction numbers. It sends and receives on the packet socket we
 * give it (sendto/recvfrom: the base) — no socket, setsockopt, bind or ioctl: it can neither open another socket nor
 * change the filter on this one (SO_LOCK_FILTER), nor set an address (nadzorca does, on its request). */
static const long sc_siec[] = { SYS_getrandom, SYS_memfd_create }; /* 0.9.1: memfd for its data in skarbiec */

/* filmy (0.10.9 step 2, ADR 0049 pkt 3): the card through DRM ioctls on its render node (the only device it gets — the type
 * 'd' is enough, as for ekran; a render node takes no ioctl of the display); getuid/geteuid — libva compares them before
 * it reads its environment (va.c). Its window from the client (mmap, base). No open, no memfd, no socket. Since 0.13.2
 * step 3 also DMA_BUF_IOCTL_SYNC alone (wa_ekranu, as ekran) — around its writes of a frame into a buffer of the card of
 * the zone (a dma-buf from the VMM). */
static const long sc_filmy[] = { SYS_getuid, SYS_geteuid };
/* posrednik (0.10.9 step 5, ADR 0049 pkt 7): the card through DRM ioctls on its render node (type 'd', as filmy: wa_ekran;
 * the narrower list of numbers — the "wrapping" of T15, RYZYKA R100). Its window and the shared memory from the client
 * (mmap, base), the copies of fences (fcntl F_DUPFD_CLOEXEC, base), the dma-buf for the VMM (sendmsg, base). Nothing
 * beyond the base: no open, no memfd, no socket. */
static const long sc_posrednik[] = { 0 };

/* dzwiek (0.12.0 step 1, ADR 0054 T2): the card through the ALSA ioctls it uses, each by its number — on the playback
 * node: its name, the format, the start values, prepare, write frames, stop and free its buffer; on the controls: the
 * card's name, the list of controls, a control's description, value and dB scale, a new value. Not: ELEM_ADD/REPLACE/
 * REMOVE (no controls of its own), TLV_WRITE/COMMAND, POWER, mmap of the status pages (SYNC_PTR), LINK. Its clients'
 * rings from the clients (mmap read only, base). No open, no memfd, no socket. */
static const long sc_dzwiek[] = { 0 };
static const struct k1_warunek wa_dzwiek[] = {
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_INFO },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_HW_PARAMS },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_HW_FREE },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_SW_PARAMS },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_PREPARE },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_WRITEI_FRAMES },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_PCM_IOCTL_DROP },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_CARD_INFO },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_ELEM_LIST },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_ELEM_INFO },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_ELEM_READ },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_ELEM_WRITE },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_TLV_READ },
    { SYS_ioctl, 1, 0xffffffffu, (uint32_t)SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS }, /* step 2: the headphone jack's events */
};

/* the VMM of a zone (0.10.0, ADR 0042 T1, T3): the machine through KVM ioctls only (type 0xAE: on /dev/kvm, the machine
 * and its processors — nadzorca gives it no other device, so the type is enough: S2); guest memory and its window from
 * memfd (ftruncate: RLIMIT_FSIZE is the guest's memory); eventfd for the interrupts (irqfd) and to wake its main
 * thread; a thread per processor (clone with CLONE_THREAD only, futex for their lock); getrandom for virtio-rng;
 * pread64 for the zone kernel and image (read only, from nadzorca). No open, no socket: Landlock and the list. */
/* 0.10.0 step 2 (ADR 0043 T9): shutdown — a connection's half-close; getsockopt only SO_ERROR — whether a TCP socket
 * from nadzorca connected (the VMM never opens a socket itself: no socket, connect, bind) */
static const long sc_strefa[] = { SYS_memfd_create, SYS_ftruncate, SYS_eventfd2, SYS_futex, SYS_getrandom, SYS_pread64,
                                  SYS_shutdown };
/* 0.10.1 step 2 (ADR 0044 T2): fallocate only to punch a hole keeping the size — the balloon gives the guest's free
 * pages back to K1 out of the memfd of its memory (strefa/balon.c); never to grow or to allocate */
static const struct k1_warunek wa_strefa[] = { { SYS_ioctl, 1, 0xff00u, 0xae00u },
                                                { SYS_clone, 0, CLONE_THREAD, CLONE_THREAD },
                                                { SYS_getsockopt, 2, 0xffffffffu, SO_ERROR },
                                                { SYS_fallocate, 1, 0xffffffffu,
                                                  FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE } };

/* The sandbox of each system service: KLATKA_<NAME> for every row of K1_SYSTEM (k1_uslugi.h) — its system calls and
 * limits, with the reason; the name and the UID come from the row. A row without its KLATKA_ does not build. */
#define KLATKA_WEJSCIE .wywolania = sc_wejscie, .warunki = wa_wejscie, .n_warunkow = N(wa_wejscie), .pamiec = 8u << 20, \
                       .pliki = 32
/* ekran: + the wallpaper photo (0.8.0: up to 16 MiB, saved in skarbiec in memfd of 1 MiB) */
#define KLATKA_EKRAN .wywolania = sc_ekran, .n_wywolan = N(sc_ekran), .warunki = wa_ekranu, .n_warunkow = N(wa_ekranu), \
                     .pamiec = 176u << 20, .pliki = 64, .rozmiar_pliku = 2u << 20
/* the boot password screen (M5e): like the shell, no files; password → skarbiec on fd 6. One full-screen
 * panel: RLIMIT_FSIZE (it limits memfd_create+ftruncate too) fits two halves of 4096×2160 (0.7.5). */
#define KLATKA_LOGOWANIE .wywolania = sc_okna, .n_wywolan = N(sc_okna), .pamiec = 96u << 20, .pliki = 32, \
                         .rozmiar_pliku = 72u << 20
/* skarbiec: Argon2id needs up to 256 MiB (ADR 0015); memfd up to a whole file of 16 MiB (0.8.0, ADR 0029; since 0.8.1
 * a piece of 1 MiB, or a whole picture for dekoder of up to 16 MiB; RLIMIT_FSIZE does not apply to the block device:
 * block/fops.c blkdev_write_iter has no rlimit check). 64 files (0.9.0 step 2, was 32): 5 of its own and 16 client
 * channels, and a memfd waiting with a reply for each client whose channel is full (src/skarbiec/zalegle.h). */
#define KLATKA_SKARBIEC .wywolania = sc_skarbiec, .n_wywolan = N(sc_skarbiec), .pamiec = 320u << 20, .pliki = 64, \
                        .rozmiar_pliku = 24u << 20
/* gui: the desktop layer (0.7.5) is one full-screen surface — RLIMIT_FSIZE for two halves of 4096×2160,
 * memory for it and the panels. Its arrangement in skarbiec on fd 6. */
#define KLATKA_GUI .wywolania = sc_okna, .n_wywolan = N(sc_okna), .pamiec = 128u << 20, .pliki = 32, \
                   .rozmiar_pliku = 72u << 20
/* M5f (ADR 0018): the program that writes the disk — no window, no paths, only the source and the
 * target device we open for it; RLIMIT_FSIZE does not apply to block devices (see skarbiec). 0.10.5 step 2 (ADR 0046):
 * memory for the front and the back of a package (at most 8 + 24 MiB, k1_paczka.h) at "Zainstaluj"; RLIMIT_FSIZE —
 * the announcement it writes into the memfd from nadzorca (at most 8 KiB). */
#define KLATKA_INSTALATOR .wywolania = sc_instalator, .n_wywolan = N(sc_instalator), .pamiec = 64u << 20, .pliki = 16, \
                          .rozmiar_pliku = 16u << 10
/* the USB stick reader: no window, no paths, the stick read only; RLIMIT_FSIZE: since 0.8.1 one piece of 1 MiB + its
 * name (the limit of 0.8.0, one file of 16 MiB, kept: ADR 0034 supplement) */
#define KLATKA_CZYTNIK .wywolania = sc_czytnik, .n_wywolan = N(sc_czytnik), .pamiec = 32u << 20, .pliki = 16, \
                       .rozmiar_pliku = 17u << 20
/* the picture decoder (SPEC §7; limits: k1_dekoder.h, 0.8.2 step 3): the file, the pixels of a photo of up to 100 Mpx
 * and the work area of a progressive JPEG (100 Mpx: 0.8 GiB measured) fit in K1_DEK_PAMIEC; RLIMIT_AS above it, so
 * that a picture too large for it fails as "no memory" (malloc) and not as a killed process; cgroup a little more
 * (page tables, the memfd it gives). dekoder.c lowers both on a computer with less than 8 GB. RLIMIT_FSIZE: the
 * largest picture it gives (2560 × 1600 × 4); 20 s of processor time (100 Mpx progressive: 2.3 s on the build
 * computer) */
#define KLATKA_DEKODER .wywolania = sc_dekoder, .n_wywolan = N(sc_dekoder),                                         \
                       .pamiec = (uint64_t)K1_DEK_PAMIEC + K1_DEK_ZAPAS + (64u << 20), .pliki = 8,                 \
                       .rozmiar_pliku = 17u << 20, .czas_cpu = 20, .przestrzen = (uint64_t)K1_DEK_PAMIEC + K1_DEK_ZAPAS

/* siec: the channel, the packet socket and (0.9.0 step 8) up to K1_SIEC_ZRODLA_MAKS UDP sockets to time servers from
 * nadzorca; 0.9.1: the channel to skarbiec, Ustawienia's channel, the EAPOL socket, a memfd for skarbiec — 24
 * descriptors; tens of kilobytes of state (the scan result: 132 KiB); 8 MiB like wejscie. RLIMIT_FSIZE (it limits
 * the memfd it writes too — SIGXFSZ): the remembered networks for skarbiec, 1064 B (pamiec.h) — 64 KiB. */
#define KLATKA_SIEC .wywolania = sc_siec, .n_wywolan = N(sc_siec), .pamiec = 8u << 20, .pliki = 24, \
                    .rozmiar_pliku = 64u << 10

/* the sound service: its clients' rings (64 KiB of S16, 128 KiB of F32 — a zone's; 12 clients: 1.5 MiB mapped at most),
 * its period buffers (0.12.3: floats, 8 KiB each); 16 MiB. Descriptors: its channel, the card's two nodes, 12 clients
 * and a ring while it opens. RLIMIT_FSIZE 0. */
#define KLATKA_DZWIEK .wywolania = sc_dzwiek, .warunki = wa_dzwiek, .n_warunkow = N(wa_dzwiek), .pamiec = 16u << 20, \
                      .pliki = 24, .rozmiar_pliku = 0

/* the film decoder (k1_filmy.h, T8): the card's buffers up to K1_FILMY_PAMIEC (counted in its cgroup: i915 charges them
 * to the process that fills them, mm/shmem.c — S26) + 96 MiB: the code with i965 and libva, the window (25 MiB), its copy
 * of a request (9 MiB), the driver's own buffers. RLIMIT_FSIZE 0: it grows no file. */
#define KLATKA_FILMY .wywolania = sc_filmy, .n_wywolan = N(sc_filmy), .warunki = wa_ekranu, .n_warunkow = N(wa_ekranu), \
                     .pamiec = K1_FILMY_PAMIEC + (96u << 20), .pliki = 16, .rozmiar_pliku = 0

/* the graphics broker (k1_posrednik.h, T14): the card's buffers up to K1_POSR_PAMIEC (charged to the cgroup of the
 * process that fills them — the broker when the card binds them, the VMM when the guest touches them through its mapping:
 * S26, plan T14) + 32 MiB: the code, its copy of a stream (1 MiB), the window and the shared memory (2 MiB), the table of
 * buffers. RLIMIT_FSIZE 0: it grows no file. 16 descriptors + K1_POSR_PLOTY_MAKS copies of fences. */
#define KLATKA_POSREDNIK .wywolania = sc_posrednik, .warunki = wa_ekran, .n_warunkow = N(wa_ekran), \
                         .pamiec = K1_POSR_PAMIEC + (32u << 20), .pliki = 16 + K1_POSR_PLOTY_MAKS, .rozmiar_pliku = 0

const struct k1_profil profile[K1_PROFIL_MAKS + 1] = {
#define X_S(n, profil_, uid_, program_, ...) [K1_PROFIL_##n] = { .nazwa = program_, .uid = uid_, KLATKA_##n },
    K1_SYSTEM(X_S)
#undef X_S
    /* applications (k1_uslugi.h): windows only — what else an application gets comes from us on its
     * channels (k1_uslugi.h, kanaly.h). 32 MiB each; Podgląd 96 MiB (0.8.2, ADR 0037): the picture from dekoder (up to 1600 × 1600,
     * 10 MiB) and the same turned, so that zooming in has pixels to show — two times what fits in 32 MiB with the list
     * of files and the window. */
#define X_APL(n, a_, p_, uid_, program_, ...)                                                                       \
    [K1_PROFIL_##n] = { .nazwa = program_, .uid = uid_, .wywolania = sc_okna, .n_wywolan = N(sc_okna),                \
                        .pamiec = (K1_APL_##n == K1_APL_PODGLAD ? 96u : 32u) << 20, .pliki = 32,                     \
                        .rozmiar_pliku = 16u << 20 },
    K1_APLIKACJE(X_APL)
#undef X_APL
    /* zones (k1_uslugi.h K1_STREFY): a thread per processor, the watchman's (strefa/stroz.c), the disk's (strefa/dane.c,
     * 0.10.0 step 5), the notifications' (strefa/powiadomienia.c, 0.11.2: with an eventfd per queue of each device — at
     * most 9 devices × 4 queues, hence 36 more descriptors), the bridge's of fast graphics (strefa/gpu_lacze.c, 0.10.9
     * step 6 — only with a graphics broker;
     * then nadzorca also adds K1_POSR_PAMIEC to its memory: strefy.c) + 1 task KVM makes in the machine's process itself
     * (the recovery of large pages: a vhost_task,
     * arch/x86/kvm/mmu/mmu.c — counted by the pids controller); the guest's memory + 184 MiB (the resources of
     * virtio-gpu, up to 96 MiB, the window, the VMM itself, KVM's structures; the disk's cache of 16 MiB and its
     * buffer of 4 MiB), RLIMIT_FSIZE = the guest's memory (its memfd; a piece of the disk to skarbiec: 64 KiB) */
#define X_Z(n, nr_, profil_, uid_, obraz_, tytul_, sesja_, pamiec_, procesory_, ...)                                 \
    [profil_] = { .nazwa = "strefa-" #nr_, .uid = uid_, .wywolania = sc_strefa, .n_wywolan = N(sc_strefa),          \
                  .warunki = wa_strefa, .n_warunkow = N(wa_strefa), .pamiec = ((uint64_t)(pamiec_) + 184u) << 20,     \
                  .watki = (procesory_) + 5, .pliki = 64 + K1_STREFA_POLACZENIA_MAKS + 8 + 36,                        \
                  .rozmiar_pliku = (uint64_t)(pamiec_) << 20 },
    K1_STREFY(X_Z)
#undef X_Z
    /* 0.10.9 step 2: the film decoder of each zone — the sandbox of FILMY, its own UID and cgroup */
#define X_F(n, nr_, ...) [K1_PROFIL_FILMY + nr_] = { .nazwa = "filmy-" #nr_, .uid = K1_UID_FILMY_STREF + nr_, KLATKA_FILMY },
    K1_STREFY(X_F)
#undef X_F
    /* 0.10.9 step 5: the graphics broker of each zone — the sandbox of POSREDNIK, its own UID and cgroup */
#define X_P(n, nr_, ...) \
    [K1_PROFIL_POSREDNIK + nr_] = { .nazwa = "posrednik-" #nr_, .uid = K1_UID_POSREDNIK_STREF + nr_, KLATKA_POSREDNIK },
    K1_STREFY(X_P)
#undef X_P
};

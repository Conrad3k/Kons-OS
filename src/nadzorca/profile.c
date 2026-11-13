/* K1 nadzorca — sandbox profiles of the services (ADR 0007). Part of nadzorca (see main.c). */
#include "nadzorca.h"

static const long sc_wejscie[] = { 0 };
static const struct k1_warunek wa_wejscie[] = { { SYS_ioctl, 1, 0xff00u, 'E' << 8 } }; /* evdev only */
/* ekran: + memfd for the settings it saves in skarbiec (M5d) */
static const long sc_ekran[] = { SYS_memfd_create };
static const struct k1_warunek wa_ekran[] = { { SYS_ioctl, 1, 0xff00u, 'd' << 8 } };   /* DRM only */

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
static const long sc_siec[] = { SYS_getrandom };

/* The sandbox of each system service: KLATKA_<NAME> for every row of K1_SYSTEM (k1_uslugi.h) — its system calls and
 * limits, with the reason; the name and the UID come from the row. A row without its KLATKA_ does not build. */
#define KLATKA_WEJSCIE .wywolania = sc_wejscie, .warunki = wa_wejscie, .n_warunkow = N(wa_wejscie), .pamiec = 8u << 20, \
                       .pliki = 32
/* ekran: + the wallpaper photo (0.8.0: up to 16 MiB, saved in skarbiec in memfd of 1 MiB) */
#define KLATKA_EKRAN .wywolania = sc_ekran, .n_wywolan = N(sc_ekran), .warunki = wa_ekran, .n_warunkow = N(wa_ekran), \
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
 * target device we open for it; RLIMIT_FSIZE does not apply to block devices (see skarbiec). */
#define KLATKA_INSTALATOR .wywolania = sc_instalator, .n_wywolan = N(sc_instalator), .pamiec = 16u << 20, .pliki = 16
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
 * nadzorca — 16 descriptors; a few kilobytes of state; 8 MiB like wejscie. */
#define KLATKA_SIEC .wywolania = sc_siec, .n_wywolan = N(sc_siec), .pamiec = 8u << 20, .pliki = 16

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
};

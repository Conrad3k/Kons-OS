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
/* gui and applications: shared memory for their surfaces, nothing else. */
static const long sc_okna[] = { SYS_memfd_create, SYS_ftruncate };
/* instalator (M5f): the two devices it gets from us, randomness for the GPT GUIDs. No open. */
static const long sc_instalator[] = { SYS_pread64, SYS_pwrite64, SYS_fdatasync, SYS_getrandom };

const struct k1_profil profile[K1_PROFIL_MAKS + 1] = {
    [K1_PROFIL_WEJSCIE] = { .nazwa = "wejscie", .uid = K1_UID_WEJSCIE, .wywolania = sc_wejscie, .warunki = wa_wejscie,
                            .n_warunkow = N(wa_wejscie), .pamiec = 8u << 20, .pliki = 32 },
    [K1_PROFIL_EKRAN] = { .nazwa = "ekran", .uid = K1_UID_EKRAN, .wywolania = sc_ekran, .n_wywolan = N(sc_ekran),
                          .warunki = wa_ekran, .n_warunkow = N(wa_ekran), .pamiec = 128u << 20, .pliki = 64,
                          .rozmiar_pliku = 64u << 10 },
    /* the boot password screen (M5e): like the shell, no files; password → skarbiec on fd 6. One full-screen
     * panel: RLIMIT_FSIZE (it limits memfd_create+ftruncate too) fits two halves of 4096×2160 (0.7.5). */
    [K1_PROFIL_LOGOWANIE] = { .nazwa = "logowanie", .uid = K1_UID_LOGOWANIE, .wywolania = sc_okna, .n_wywolan = N(sc_okna),
                              .pamiec = 96u << 20, .pliki = 32, .rozmiar_pliku = 72u << 20 },
    /* skarbiec: Argon2id needs up to 256 MiB (ADR 0015); memfd up to a whole file of 16 MiB (0.8.0, ADR 0029;
     * RLIMIT_FSIZE does not apply to the block device: block/fops.c blkdev_write_iter has no rlimit check). */
    [K1_PROFIL_SKARBIEC] = { .nazwa = "skarbiec", .uid = K1_UID_SKARBIEC, .wywolania = sc_skarbiec,
                             .n_wywolan = N(sc_skarbiec), .pamiec = 320u << 20, .pliki = 32, .rozmiar_pliku = 24u << 20 },
    /* gui: the desktop layer (0.7.5) is one full-screen surface — RLIMIT_FSIZE for two halves of 4096×2160,
     * memory for it and the panels. Its arrangement in skarbiec on fd 6. */
    [K1_PROFIL_GUI] = { .nazwa = "gui", .uid = K1_UID_GUI, .wywolania = sc_okna, .n_wywolan = N(sc_okna),
                        .pamiec = 128u << 20, .pliki = 32, .rozmiar_pliku = 72u << 20 },
    /* M5f (ADR 0018): the program that writes the disk — no window, no paths, only the source and the
     * target device we open for it; RLIMIT_FSIZE does not apply to block devices (see skarbiec). */
    [K1_PROFIL_INSTALATOR] = { .nazwa = "instalator", .uid = K1_UID_INSTALATOR, .wywolania = sc_instalator,
                               .n_wywolan = N(sc_instalator), .pamiec = 16u << 20, .pliki = 16 },
    /* applications (k1_uslugi.h): windows only — what else an application gets comes from us on its
     * channels (uslugi.c) */
#define X_APL(n, a_, p_, uid_, program_, ...)                                                                       \
    [K1_PROFIL_##n] = { .nazwa = program_, .uid = uid_, .wywolania = sc_okna, .n_wywolan = N(sc_okna), .pamiec = 32u << 20, \
                        .pliki = 32, .rozmiar_pliku = 16u << 20 },
    K1_APLIKACJE(X_APL)
#undef X_APL
};

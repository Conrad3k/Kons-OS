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
    [K1_PROFIL_WEJSCIE] = { "wejscie", K1_UID_WEJSCIE, sc_wejscie, 0, wa_wejscie, N(wa_wejscie), NULL,
                            8u << 20, 1, 32, 0 },
    [K1_PROFIL_EKRAN] = { "ekran", K1_UID_EKRAN, sc_ekran, N(sc_ekran), wa_ekran, N(wa_ekran), NULL, 128u << 20, 1, 64,
                          64u << 10 },
    /* skarbiec: Argon2id needs up to 256 MiB (ADR 0015); memfd up to 1 MiB (RLIMIT_FSIZE does not apply to
     * the block device: block/fops.c blkdev_write_iter has no rlimit check). */
    /* the boot password screen (M5e): like the shell, no files; password → skarbiec on fd 6 */
    [K1_PROFIL_LOGOWANIE] = { "logowanie", K1_UID_LOGOWANIE, sc_okna, N(sc_okna), NULL, 0, NULL, 48u << 20, 1, 32,
                              16u << 20 },
    [K1_PROFIL_SKARBIEC] = { "skarbiec", K1_UID_SKARBIEC, sc_skarbiec, N(sc_skarbiec), NULL, 0, NULL, 320u << 20, 1, 32,
                             2u << 20 },
    /* RLIMIT_FSIZE also limits memfd_create+ftruncate: 16 MiB is enough for the largest surface. */
    [K1_PROFIL_GUI] = { "gui", K1_UID_GUI, sc_okna, N(sc_okna), NULL, 0, NULL, 48u << 20, 1, 32, 16u << 20 },
    [K1_PROFIL_ZEGAR] = { "zegar", K1_UID_ZEGAR, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32, 16u << 20 },
    /* Notatki: since M5d no files at all — its note is an object in skarbiec (fd 4 from us). */
    [K1_PROFIL_NOTATKI] = { "notatki", K1_UID_NOTATKI, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32, 16u << 20 },
    [K1_PROFIL_USTAWIENIA] = { "ustawienia", K1_UID_USTAWIENIA, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32,
                               16u << 20 },
    /* Zasoby: like the other apps; its numbers come from us on fd 4 (it reads no files). */
    [K1_PROFIL_ZASOBY] = { "zasoby", K1_UID_ZASOBY, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32, 16u << 20 },
    /* M5f (ADR 0018): the program that writes the disk — no window, no paths, only the source and the
     * target device we open for it; RLIMIT_FSIZE does not apply to block devices (see skarbiec). */
    [K1_PROFIL_INSTALATOR] = { "instalator", K1_UID_INSTALATOR, sc_instalator, N(sc_instalator), NULL, 0, NULL,
                               16u << 20, 1, 16, 0 },
    /* the Instalator window: like the other applications; asks us on fd 4 */
    [K1_PROFIL_INSTALACJA] = { "instalacja", K1_UID_INSTALACJA, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32,
                               16u << 20 },
};

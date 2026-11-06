/* K1 — identities of the native services and applications (ADR 0007).
 *
 * Every service runs with its own UID/GID (no supplementary groups, no capabilities). The numbers
 * are fixed here because there is no user database in K1: the supervisor assigns them, the screen
 * process checks message senders against them (SO_PASSCRED), the in-system test verifies them. */
#ifndef K1_USLUGI_H
#define K1_USLUGI_H

#define K1_UID_WEJSCIE 100
#define K1_UID_EKRAN 101
#define K1_UID_GUI 102
#define K1_UID_ZEGAR 200
#define K1_UID_NOTATKI 201
#define K1_UID_USTAWIENIA 202

/* Roles of clients of the screen process (who is on the other end of a channel). */
enum k1_rola {
    K1_ROLA_WEJSCIE = 1,   /* input events (k1_zdarzenie) */
    K1_ROLA_POWLOKA = 2,   /* the desktop shell (gui): panels, window list, launching apps */
    K1_ROLA_APLIKACJA = 3, /* a native application: windows only */
};
#define K1_ROLA_MAKS 3

/* Native applications (M4). Names and icons are known to the shell; only the number travels. */
enum k1_aplikacja {
    K1_APL_ZEGAR = 1,
    K1_APL_NOTATKI = 2,
    K1_APL_USTAWIENIA = 3,
};
#define K1_APL_MAKS 3

/* Sandbox profiles the supervisor knows (also used for test probes in the test image). */
enum k1_profil_nr {
    K1_PROFIL_WEJSCIE = 1,
    K1_PROFIL_EKRAN = 2,
    K1_PROFIL_GUI = 3,
    K1_PROFIL_ZEGAR = 4,
    K1_PROFIL_NOTATKI = 5,
    K1_PROFIL_USTAWIENIA = 6,
};
#define K1_PROFIL_MAKS 6

/* The only place an application may write (M4; Blob Vault replaces it in M5). */
#define K1_KATALOG_NOTATEK "/dane_uzytkownika/notatki"

/* Exit code of a service that hit a forbidden system call (seccomp, k1_klatka_w_usludze). */
#define K1_KOD_SECCOMP 159

#endif

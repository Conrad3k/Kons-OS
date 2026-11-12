/* K1 — identities of the native services and applications (ADR 0007).
 *
 * Every service runs with its own UID/GID (no supplementary groups, no capabilities). The numbers
 * are fixed here because there is no user database in K1: the supervisor assigns them, the screen
 * process checks message senders against them (SO_PASSCRED), the in-system test verifies them. */
#ifndef K1_USLUGI_H
#define K1_USLUGI_H

/* The system services: one line each — X(NAME, profile, UID, program, name shown in Zasoby). The profile
 * numbers never change: skarbiec stores data by owner = profile number (k1_komunikat.h, K1_WL_*). */
#define K1_SYSTEM(X)                                                                                 \
    X(WEJSCIE, 1, 100, "wejscie", "Wejście (klawiatura, mysz)")                                     \
    X(EKRAN, 2, 101, "ekran", "Ekran (kompozytor okien)")                                           \
    X(GUI, 3, 102, "gui", "Pulpit")                                                                 \
    X(SKARBIEC, 8, 103, "skarbiec", "Skarbiec (dane)")              /* M5d: Blob Vault */           \
    X(LOGOWANIE, 9, 104, "logowanie", "Ekran logowania")            /* M5e: boot password screen */ \
    X(INSTALATOR, 10, 105, "instalator", "Instalator (zapis dysku)") /* M5f: writes the disk, no window */ \
    X(CZYTNIK, 15, 106, "czytnik", "Czytnik pendrive'a")           /* 0.8.0: a USB stick, read only, for Pliki */ \
    X(DEKODER, 16, 107, "dekoder", "Dekoder obrazów")              /* 0.8.0: one picture for Pliki, then it ends */

/* The native applications: one line each — everything else (UIDs, profiles, the Start menu, window names,
 * Zasoby, the sandbox profile, the image contents, the tests) follows from it.
 * X(NAME, application number, profile, UID, program (= directory src/aplikacje/<program>), log tag,
 *   title, description in the Start menu, icon (K1_IK_<icon>), on the bottom bar and the desktop at the first start —
 *   the owner moves them later, 0.7.5; -1: a window of K1 itself, never in the Start menu, ADR 0030). The numbers never change
 *   (the profile is the owner of the application's data in skarbiec); a new application takes the next ones. */
#define K1_APLIKACJE(X)                                                                                         \
    X(ZEGAR, 1, 4, 200, "zegar", "K1-ZEGAR", "Zegar", "Godzina i data", ZEGAR, 1)                               \
    X(NOTATKI, 2, 5, 201, "notatki", "K1-NOTATKI", "Notatki", "Twoje notatki, zaszyfrowane", NOTATKI, 1)        \
    X(USTAWIENIA, 3, 6, 202, "ustawienia", "K1-USTAWIENIA", "Ustawienia", "Wygląd, ekran, mysz, bateria, hasło", \
      USTAWIENIA, 1)                                                                                            \
    X(ZASOBY, 4, 7, 203, "zasoby", "K1-ZASOBY", "Zasoby", "Procesor, pamięć, bateria", ZASOBY, 1)                \
    X(INSTALACJA, 5, 11, 204, "instalacja", "K1-INSTALACJA", "Instalator", "Instalacja, aktualizacja, Secure Boot", \
      DYSK, 0)                                                                                                  \
    X(KALKULATOR, 6, 12, 205, "kalkulator", "K1-KALKULATOR", "Kalkulator", "Działania, procenty, pamięć",          \
      KALKULATOR, 1)                                                                                            \
    X(PLIKI, 7, 13, 206, "pliki", "K1-PLIKI", "Pliki", "Foldery, pliki i Kosz", FOLDER, 1)                       \
    X(WYBOR, 8, 14, 207, "wybor", "K1-WYBOR", "Wybór pliku", "Okno, w którym wybierasz plik dla aplikacji", FOLDER, -1) \
    X(PODGLAD, 9, 17, 208, "podglad", "K1-PODGLAD", "Podgląd", "Zdjęcia i obrazy", OBRAZ, 0)

#define K1_X_UID_S(n, profil, uid, ...) K1_UID_##n = uid,
#define K1_X_UID_A(n, apl, profil, uid, ...) K1_UID_##n = uid,
enum k1_uid { K1_SYSTEM(K1_X_UID_S) K1_APLIKACJE(K1_X_UID_A) };

/* Roles of clients of the screen process (who is on the other end of a channel). */
enum k1_rola {
    K1_ROLA_WEJSCIE = 1,   /* input events (k1_zdarzenie) */
    K1_ROLA_POWLOKA = 2,   /* the desktop shell (gui): panels, window list, launching apps */
    K1_ROLA_APLIKACJA = 3, /* a native application: windows only */
    K1_ROLA_LOGOWANIE = 4, /* the boot password screen (M5e): one full-screen panel, gets every key */
};
#define K1_ROLA_MAKS 4

/* Native applications: only the number travels between gui, nadzorca and ekran. */
#define K1_X_APL(n, apl, ...) K1_APL_##n = apl,
#define K1_X_LICZ(...) +1
enum k1_aplikacja { K1_APLIKACJE(K1_X_APL) };
enum { K1_APL_MAKS = 0 K1_APLIKACJE(K1_X_LICZ) }; /* numbered 1..K1_APL_MAKS (checked in k1_uslugi.c) */

/* Sandbox profiles the supervisor knows (also used for test probes in the test image). */
#define K1_X_PROFIL_S(n, profil, ...) K1_PROFIL_##n = profil,
#define K1_X_PROFIL_A(n, apl, profil, ...) K1_PROFIL_##n = profil,
enum k1_profil_nr { K1_SYSTEM(K1_X_PROFIL_S) K1_APLIKACJE(K1_X_PROFIL_A) };
enum { K1_PROFIL_MAKS = 0 K1_SYSTEM(K1_X_LICZ) K1_APLIKACJE(K1_X_LICZ) }; /* numbered 1..MAKS (checked) */

/* A profile's program name ("zegar") and the name Zasoby shows for it; NULL outside 1..K1_PROFIL_MAKS. */
const char *k1_profil_program(int profil);
const char *k1_profil_nazwa(int profil);
/* The application with that number: its profile, or 0; its UID, or 0. */
int k1_apl_profil(int apl);
int k1_apl_uid(int apl);

/* Exit code of a service that hit a forbidden system call (seccomp, k1_klatka_w_usludze). */
#define K1_KOD_SECCOMP 159

#endif

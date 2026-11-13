/* K1 — identities of the native services and applications (ADR 0007).
 *
 * Every service runs with its own UID/GID (no supplementary groups, no capabilities). The numbers
 * are fixed here because there is no user database in K1: the supervisor assigns them, the screen
 * process checks message senders against them (SO_PASSCRED), the in-system test verifies them. */
#ifndef K1_USLUGI_H
#define K1_USLUGI_H

/* The system services: one line each — everything else follows from it: UIDs and profiles, the services nadzorca
 * starts and restarts, their channels, Zasoby, the image contents, the tests (0.9.0 step 1). The sandbox of each service
 * (its system calls and limits, with the reason for each) is KLATKA_<NAME> in src/nadzorca/profile.c — the build
 * fails without it.
 * X(NAME, profile, UID, program (= /rdzen/system/<program>, log tag K1-<NAME>), name shown in Zasoby,
 *   when it starts: PRZY_STARCIE (and again after it ends) or NA_PROSBE (by its module in nadzorca, never again by itself),
 *   its role at ekran (K1_ROLA_<role>; BRAK: not a client of ekran),
 *   its channels from nadzorca, in this order from fd 3 — after the channel to ekran for a client of ekran
 *   (KANAL_* in src/nadzorca/kanaly.h; skarbiec, ekran and the NA_PROSBE services get theirs from their module)).
 * The profile numbers never change: skarbiec stores data by owner = profile number (k1_komunikat.h, K1_WL_*).
 * The order is the order of starting: skarbiec first (ekran gets its channel to it at start). */
#define K1_SYSTEM(X)                                                                                                    \
    X(SKARBIEC, 8, 103, "skarbiec", "Skarbiec (dane)", PRZY_STARCIE, BRAK, ())                 /* M5d: Blob Vault */  \
    X(EKRAN, 2, 101, "ekran", "Ekran (kompozytor okien)", PRZY_STARCIE, BRAK, ())                                     \
    X(WEJSCIE, 1, 100, "wejscie", "Wejście (klawiatura, mysz)", PRZY_STARCIE, WEJSCIE, (KANAL_WEJSC))                  \
    X(GUI, 3, 102, "gui", "Pulpit", PRZY_STARCIE, POWLOKA, (KANAL_PULPITU, KANAL_BATERII, KANAL_SKARBCA))               \
    /* M5e: the boot password screen, instead of gui while the vault waits for its password */                        \
    X(LOGOWANIE, 9, 104, "logowanie", "Ekran logowania", PRZY_STARCIE, LOGOWANIE,                                      \
      (KANAL_PULPITU, KANAL_BATERII, KANAL_SKARBCA))                                                                    \
    X(INSTALATOR, 10, 105, "instalator", "Instalator (zapis dysku)", NA_PROSBE, BRAK, ()) /* M5f: writes the disk */    \
    X(CZYTNIK, 15, 106, "czytnik", "Czytnik pendrive'a", NA_PROSBE, BRAK, ()) /* 0.8.0: a USB stick, read only */       \
    X(DEKODER, 16, 107, "dekoder", "Dekoder obrazów", NA_PROSBE, BRAK, ())    /* 0.8.0: one picture, then it ends */ \
    /* 0.9.0 (ADR 0039): the address from the router (DHCP) for the card with a cable; nadzorca sets it */          \
    X(SIEC, 18, 108, "siec", "Sieć (adres od routera)", PRZY_STARCIE, BRAK, (KANAL_SIECI))

/* The native applications: one line each — everything else (UIDs, profiles, the Start menu, window names,
 * Zasoby, the sandbox profile, the image contents, the tests) follows from it.
 * X(NAME, application number, profile, UID, program (= directory src/aplikacje/<program>), log tag,
 *   title, description in the Start menu, icon (K1_IK_<icon>), on the bottom bar and the desktop at the first start —
 *   the owner moves them later, 0.7.5; -1: a window of K1 itself, never in the Start menu, ADR 0030 —,
 *   its channels from nadzorca from fd 4 (fd 3 is its channel to ekran; KANAL_* in src/nadzorca/kanaly.h)).
 *   The numbers never change (the profile is the owner of the application's data in skarbiec); a new application
 *   takes the next ones. Applications start on request of gui and are not restarted (the user starts them again). */
#define K1_APLIKACJE(X)                                                                                         \
    X(ZEGAR, 1, 4, 200, "zegar", "K1-ZEGAR", "Zegar", "Godzina i data", ZEGAR, 1, ())                           \
    X(NOTATKI, 2, 5, 201, "notatki", "K1-NOTATKI", "Notatki", "Twoje notatki, zaszyfrowane", NOTATKI, 1,        \
      (KANAL_SKARBCA, KANAL_PLIKU))                                                                             \
    X(USTAWIENIA, 3, 6, 202, "ustawienia", "K1-USTAWIENIA", "Ustawienia", "Wygląd, ekran, mysz, bateria, hasło", \
      USTAWIENIA, 1, (KANAL_USTAWIEN, KANAL_SKARBCA))                                                           \
    X(ZASOBY, 4, 7, 203, "zasoby", "K1-ZASOBY", "Zasoby", "Procesor, pamięć, bateria", ZASOBY, 1, (KANAL_ZASOBOW)) \
    X(INSTALACJA, 5, 11, 204, "instalacja", "K1-INSTALACJA", "Instalator", "Instalacja, aktualizacja, Secure Boot", \
      DYSK, 0, (KANAL_INSTALACJI))                                                                              \
    X(KALKULATOR, 6, 12, 205, "kalkulator", "K1-KALKULATOR", "Kalkulator", "Działania, procenty, pamięć",          \
      KALKULATOR, 1, ())                                                                                        \
    X(PLIKI, 7, 13, 206, "pliki", "K1-PLIKI", "Pliki", "Foldery, pliki i Kosz", FOLDER, 1,                       \
      (KANAL_SKARBCA, KANAL_PLIKU, KANAL_PENDRIVE))                                                             \
    X(WYBOR, 8, 14, 207, "wybor", "K1-WYBOR", "Wybór pliku", "Okno, w którym wybierasz plik dla aplikacji", FOLDER, -1, \
      (KANAL_SKARBCA))                                                                                          \
    X(PODGLAD, 9, 17, 208, "podglad", "K1-PODGLAD", "Podgląd", "Zdjęcia i obrazy", OBRAZ, 0,                    \
      (KANAL_SKARBCA, KANAL_PLIKU, KANAL_DEKODERA))

#define K1_X_UID_S(n, profil, uid, ...) K1_UID_##n = uid,
#define K1_X_UID_A(n, apl, profil, uid, ...) K1_UID_##n = uid,
enum k1_uid { K1_SYSTEM(K1_X_UID_S) K1_APLIKACJE(K1_X_UID_A) };

/* Roles of clients of the screen process (who is on the other end of a channel). */
enum k1_rola {
    K1_ROLA_BRAK = 0,      /* not a client of ekran (K1_SYSTEM: skarbiec, the services nadzorca starts on request) */
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

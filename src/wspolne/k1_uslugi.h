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
    /* 0.9.1 (ADR 0041): + Wi-Fi — the networks around, logging in with a password; the remembered networks in skarbiec */ \
    X(SIEC, 18, 108, "siec", "Sieć (adres od routera, Wi-Fi)", PRZY_STARCIE, BRAK, (KANAL_SIECI, KANAL_SKARBCA))      \
    /* 0.10.9 step 2 (ADR 0049 pkt 2, 3): the film decoder — this row: nadzorca's question at boot, what the card      \
     * decodes (then it ends); the decoder of each zone follows from the zone's row (K1_PROFIL_FILMY_<ZONE>, below) */ \
    X(FILMY, 24, 109, "filmy", "Dekoder filmów (karta graficzna)", NA_PROSBE, BRAK, ())                            \
    /* 0.10.9 step 5 (ADR 0049 pkt 7): the graphics broker — this row: nadzorca's question at boot, whether the card    \
     * can serve fast graphics (then it ends); the broker of each zone follows from the zone's row, as for FILMY. Its    \
     * profile comes right after those of the zones' film decoders (k1_uslugi.c checks it). */                        \
    X(POSREDNIK, 30, 110, "posrednik", "Pośrednik grafiki (karta graficzna)", NA_PROSBE, BRAK, ())

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
      USTAWIENIA, 1, (KANAL_USTAWIEN, KANAL_SKARBCA, KANAL_DO_SIECI, KANAL_AKTUALIZACJI))                       \
    X(ZASOBY, 4, 7, 203, "zasoby", "K1-ZASOBY", "Zasoby", "Procesor, pamięć, bateria", ZASOBY, 1, (KANAL_ZASOBOW)) \
    X(INSTALACJA, 5, 11, 204, "instalacja", "K1-INSTALACJA", "Instalator", "Instalacja na dysku, Secure Boot", \
      DYSK, 0, (KANAL_INSTALACJI))                                                                              \
    X(KALKULATOR, 6, 12, 205, "kalkulator", "K1-KALKULATOR", "Kalkulator", "Działania, procenty, pamięć",          \
      KALKULATOR, 1, ())                                                                                        \
    X(PLIKI, 7, 13, 206, "pliki", "K1-PLIKI", "Pliki", "Foldery, pliki i Kosz", FOLDER, 1,                       \
      (KANAL_SKARBCA, KANAL_PLIKU, KANAL_PENDRIVE))                                                             \
    X(WYBOR, 8, 14, 207, "wybor", "K1-WYBOR", "Wybór pliku", "Okno, w którym wybierasz plik dla aplikacji", FOLDER, -1, \
      (KANAL_SKARBCA))                                                                                          \
    X(PODGLAD, 9, 17, 208, "podglad", "K1-PODGLAD", "Podgląd", "Zdjęcia i obrazy", OBRAZ, 0,                    \
      (KANAL_SKARBCA, KANAL_PLIKU, KANAL_DEKODERA))

/* 0.10.0 (ADR 0042): the zones — each a micro machine with its own kernel, run by its own VMM process (program
 * /rdzen/system/strefa, log tag K1-STREFA); one line each, everything else follows from it: the profile and the UID of
 * its VMM (KLATKA_STREFA in src/nadzorca/profile.c with the row's memory), the frame ekran draws, Zasoby, the tests.
 * X(NAME, zone number, profile, UID, image (/rdzen/strefy/<obraz>.img), title, session ("": none), memory of the guest
 *   in MiB, processors (the most it gets — since 0.11.2 never more than the computer has: strefa/kvm.c), the frame
 *   colour 0xRRGGBB, JEDNORAZOWA (forgets everything when closed) or STALA (its disk in skarbiec — 0.10.0 step 5; the
 *   default: the user changes it in Ustawienia → Przeglądarka, nadzorca keeps the choice),
 *   icon (K1_IK_<icon>) of its mark, 1: the guest's console lines go to K1's log (the test zone only; others: counted),
 *   its line in the Start menu ("": not there; 0.10.0 step 2), 1: it has a window (0: the download zone of 0.10.5 step 2,
 *   ADR 0046 — no window, no display or input devices; it downloads updates and gives K1 the bytes), 1: fast graphics
 *   and films with the card on by default in its session (0.10.9 step 6, the owner's D5: Ogólna yes, Bank and Prywatna
 *   no; the user changes it in Ustawienia → Przeglądarka; only where the card can — ADR 0049), where its files come from
 *   in Pliki (0.11.0 step 2, ADR 0052: "z przeglądarki", with the session's name after " · ")).
 * The numbers never change (a STALA zone's disk in skarbiec belongs to its profile). The test zone's image is only in
 * the test image: the production image has no file for it, nadzorca refuses to start it. 0.10.0 step 2 (ADR 0043, D2):
 * the browser's sessions — each its own zone (machine, colour, UID); since step 5 Ogólna and Bank remember, Prywatna
 * forgets (the test zone remembers: test-strefa checks the disk). */
#define K1_STREFY(X)                                                                                                    \
    X(PROBNA, 1, 19, 300, "probna", "Strefa próbna", "", 128, 1, 0xA8834A, STALA, EKRAN, 1, "", 1, 0,                \
      "ze strefy próbnej")                                                                                      \
    X(OGOLNA, 2, 20, 301, "przegladarka", "Przeglądarka", "Ogólna", 1536, 4, 0x5F8A6A, STALA, PRZEGLADARKA, 0,        \
      "Strony internetowe w osobnej maszynie", 1, 1, "z przeglądarki")                                          \
    X(BANK, 3, 21, 302, "przegladarka", "Przeglądarka", "Bank", 1536, 4, 0x4A6FA5, STALA, PRZEGLADARKA, 0, "", 1, 0,  \
      "z przeglądarki")                                                                                         \
    X(PRYWATNA, 4, 22, 303, "przegladarka", "Przeglądarka", "Prywatna", 1536, 4, 0x8B6AA8, JEDNORAZOWA, PRZEGLADARKA,\
      0, "", 1, 0, "z przeglądarki")                                                                            \
    X(POBIERANIE, 5, 23, 304, "przegladarka", "Pobieranie aktualizacji", "", 384, 1, 0x8D8A84, JEDNORAZOWA,         \
      AKTUALIZACJE, 0, "", 0, 0, "ze strefy pobierania")

/* 0.10.9 step 2 (ADR 0049 pkt 2): the film decoder of each zone — derived from the zone's row: its own UID
 * (K1_UID_FILMY_STREF + the zone's number) and profile (K1_PROFIL_FILMY + the number: its sandbox KLATKA_FILMY, its own
 * cgroup filmy-<nr>, the zone's memory counts it), program filmy; nadzorca starts it with its zone (filmy.c). It owns no
 * data in skarbiec, so its profile number may move when a zone is added. */
#define K1_UID_FILMY_STREF 400
/* 0.10.9 step 5 (ADR 0049 pkt 7): the graphics broker of each zone — the same way: UID K1_UID_POSREDNIK_STREF + the
 * number, profile K1_PROFIL_POSREDNIK + the number (KLATKA_POSREDNIK, cgroup posrednik-<nr>), program posrednik. */
#define K1_UID_POSREDNIK_STREF 410
#define K1_X_UID_S(n, profil, uid, ...) K1_UID_##n = uid,
#define K1_X_UID_A(n, apl, profil, uid, ...) K1_UID_##n = uid,
#define K1_X_UID_Z(n, nr, profil, uid, ...) K1_UID_STREFA_##n = uid,
#define K1_X_UID_F(n, nr, ...) K1_UID_FILMY_##n = K1_UID_FILMY_STREF + nr,
#define K1_X_UID_P(n, nr, ...) K1_UID_POSREDNIK_##n = K1_UID_POSREDNIK_STREF + nr,
enum k1_uid { K1_SYSTEM(K1_X_UID_S) K1_APLIKACJE(K1_X_UID_A) K1_STREFY(K1_X_UID_Z) K1_STREFY(K1_X_UID_F)
                  K1_STREFY(K1_X_UID_P) };

/* Roles of clients of the screen process (who is on the other end of a channel). */
enum k1_rola {
    K1_ROLA_BRAK = 0,      /* not a client of ekran (K1_SYSTEM: skarbiec, the services nadzorca starts on request) */
    K1_ROLA_WEJSCIE = 1,   /* input events (k1_zdarzenie) */
    K1_ROLA_POWLOKA = 2,   /* the desktop shell (gui): panels, window list, launching apps */
    K1_ROLA_APLIKACJA = 3, /* a native application: windows only */
    K1_ROLA_LOGOWANIE = 4, /* the boot password screen (M5e): one full-screen panel, gets every key */
    K1_ROLA_STREFA = 5,    /* the VMM of a zone (0.10.0, ADR 0042): one window, its frame and mark drawn by ekran */
    K1_ROLA_TEST = 6,      /* 0.10.2 step 2 (ADR 0045): the test program (test image only, through TEST_POLACZ) — no
                            * surfaces; asks what is on the screen (OPIS_PYTANIE) */
};
#define K1_ROLA_MAKS 6

/* Native applications: only the number travels between gui, nadzorca and ekran. */
#define K1_X_APL(n, apl, ...) K1_APL_##n = apl,
#define K1_X_LICZ(...) +1
enum k1_aplikacja { K1_APLIKACJE(K1_X_APL) };
enum { K1_APL_MAKS = 0 K1_APLIKACJE(K1_X_LICZ) }; /* numbered 1..K1_APL_MAKS (checked in k1_uslugi.c) */

/* Zones: only the number travels between gui, nadzorca and ekran. */
#define K1_X_STREFA(n, nr, ...) K1_STREFA_##n = nr,
enum k1_strefa_nr { K1_STREFY(K1_X_STREFA) };
enum { K1_STREFY_MAKS = 0 K1_STREFY(K1_X_LICZ) }; /* numbered 1..K1_STREFY_MAKS (checked in k1_uslugi.c) */
enum k1_trwalosc { K1_JEDNORAZOWA = 0, K1_STALA = 1 };
#define K1_STREFA_CEL_MAKS 95 /* 0.10.5: what the download zone downloads, its VMM's argv[4] (src/strefa/lacznik.h) */
#define K1_X_Z_OKNEM(n, nr, profil, uid, obraz, tytul, sesja, pamiec, procesory, kolor, trwalosc, ikona, log, menu, okno, \
                     ...)                                                                                              \
    +(okno)
enum { K1_STREFY_Z_OKNEM = 0 K1_STREFY(K1_X_Z_OKNEM) }; /* the zones with a window (the others: the download zone) */

/* A zone's row as a structure (k1_strefa). */
struct k1_strefa_opis {
    int nr, profil, uid;
    const char *obraz, *tytul, *sesja;
    unsigned pamiec_mib, procesory;
    unsigned kolor;          /* 0xRRGGBB */
    int trwalosc;            /* K1_JEDNORAZOWA / K1_STALA */
    int ikona;               /* K1_IK_* (k1_rysuj.h) */
    int log_goscia;
    int okno;                /* 1: a window at ekran; 0: none (the download zone) */
    int grafika;             /* 0.10.9 step 6 (D5): fast graphics (and films) on by default in its session */
};
/* 1: `profil` is the profile of a zone's VMM (the owner of its disk in skarbiec, 0.10.0 step 5). */
#define K1_X_PROFIL_STREFY(n, nr, profil, ...) || p == (profil)
static inline int k1_profil_strefy(int p)
{
    return 0 K1_STREFY(K1_X_PROFIL_STREFY);
}
/* The zone with number `nr` (1..K1_STREFY_MAKS), or NULL. */
const struct k1_strefa_opis *k1_strefa(int nr);
/* The zone whose VMM has profile `profil`, or NULL. */
const struct k1_strefa_opis *k1_strefa_profilu(int profil);

/* Sandbox profiles the supervisor knows (also used for test probes in the test image). */
#define K1_X_PROFIL_S(n, profil, ...) K1_PROFIL_##n = profil,
#define K1_X_PROFIL_A(n, apl, profil, ...) K1_PROFIL_##n = profil,
#define K1_X_PROFIL_Z(n, nr, profil, ...) K1_PROFIL_STREFA_##n = profil,
#define K1_X_PROFIL_F(n, nr, ...) K1_PROFIL_FILMY_##n = K1_PROFIL_FILMY + nr, /* 0.10.9 step 2: the zone's film decoder */
#define K1_X_PROFIL_P(n, nr, ...) K1_PROFIL_POSREDNIK_##n = K1_PROFIL_POSREDNIK + nr, /* 0.10.9 step 5: its broker */
enum k1_profil_nr { K1_SYSTEM(K1_X_PROFIL_S) K1_APLIKACJE(K1_X_PROFIL_A) K1_STREFY(K1_X_PROFIL_Z) K1_STREFY(K1_X_PROFIL_F)
                        K1_STREFY(K1_X_PROFIL_P) };
enum { K1_PROFIL_MAKS = 0 K1_SYSTEM(K1_X_LICZ) K1_APLIKACJE(K1_X_LICZ) K1_STREFY(K1_X_LICZ) K1_STREFY(K1_X_LICZ)
                           K1_STREFY(K1_X_LICZ) }; /* 1..MAKS */
/* The zone whose film decoder has profile `p` (1..K1_STREFY_MAKS), or 0. */
static inline int k1_profil_filmow(int p)
{
    return p > K1_PROFIL_FILMY && p <= K1_PROFIL_FILMY + K1_STREFY_MAKS ? p - K1_PROFIL_FILMY : 0;
}
/* The zone whose graphics broker has profile `p` (1..K1_STREFY_MAKS), or 0 (0.10.9 step 5). */
static inline int k1_profil_posrednika(int p)
{
    return p > K1_PROFIL_POSREDNIK && p <= K1_PROFIL_POSREDNIK + K1_STREFY_MAKS ? p - K1_PROFIL_POSREDNIK : 0;
}

/* A profile's program name ("zegar") and the name Zasoby shows for it; NULL outside 1..K1_PROFIL_MAKS. */
const char *k1_profil_program(int profil);
const char *k1_profil_nazwa(int profil);
/* The application with that number: its profile, or 0; its UID, or 0. */
int k1_apl_profil(int apl);
int k1_apl_uid(int apl);

/* Exit code of a service that hit a forbidden system call (seccomp, k1_klatka_w_usludze). */
#define K1_KOD_SECCOMP 159

#endif

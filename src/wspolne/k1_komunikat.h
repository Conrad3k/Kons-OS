/* K1 — the message protocol between the supervisor, the screen process and its clients (SPEC §12,
 * ADR 0007 and 0008).
 *
 * One fixed-size 32-byte message for every channel: version, type, length, an object number and
 * five integer parameters, reserved field = 0. Each type belongs to exactly one channel direction
 * and has fixed ranges for every field; k1_komunikat_sprawdz() checks all of them, so receivers only
 * have to check things that depend on their own state (does surface N exist, is a file descriptor
 * attached). Transport: AF_UNIX SOCK_SEQPACKET socketpairs created by the supervisor
 * (src/wspolne/k1_kanal.h) — one send = one message; file descriptors travel with SCM_RIGHTS and
 * sender credentials with SCM_CREDENTIALS. Native byte order (both ends run on one machine).
 * Pure functions, tested on the host with ASan/UBSan and libFuzzer. */
#ifndef K1_KOMUNIKAT_H
#define K1_KOMUNIKAT_H

#include <stddef.h>
#include <stdint.h>

#include "k1_dekoder.h"
#include "k1_dokument.h"
#include "k1_pendrive.h"
#include "k1_siec.h"
#include "k1_wifi.h"

#define K1_KOMUNIKAT_WERSJA 1

/* Channel directions: a type is valid in exactly one of them. */
enum k1_kanal_kierunek {
    K1_KIER_STEROWANIE = 1,  /* nadzorca → ekran (control: new clients) */
    K1_KIER_DO_EKRANU = 2,   /* client (gui, application) → ekran */
    K1_KIER_OD_EKRANU = 3,   /* ekran → client */
    K1_KIER_DO_NADZORCY = 4, /* gui / test program → nadzorca */
    K1_KIER_OD_NADZORCY = 5, /* nadzorca → test program, → Zasoby, → gui (battery); one way */
    K1_KIER_DO_SKARBCA = 6,  /* client (ekran, Notatki, nadzorca) → skarbiec (M5d) */
    K1_KIER_OD_SKARBCA = 7,  /* skarbiec → client */
    K1_KIER_DO_CZYTNIKA = 8, /* Pliki → czytnik (the USB stick reader, 0.8.0 step 3) */
    K1_KIER_OD_CZYTNIKA = 9, /* czytnik → Pliki */
    K1_KIER_DO_DEKODERA = 10, /* Pliki → dekoder (one picture, 0.8.0 step 4) */
    K1_KIER_OD_DEKODERA = 11, /* dekoder → Pliki */
    K1_KIER_DO_SIECI = 12,   /* Ustawienia → siec (0.9.1 step 2: a Wi-Fi password goes straight to siec, not through
                              * nadzorca; one way — siec answers in the list of networks nadzorca passes on) */
};
#define K1_KIER_MAKS 12

enum k1_komunikat_typ {
    /* nadzorca → ekran */
    K1_KOM_KLIENT = 1, /* a=rola, b=aplikacja (rola APLIKACJA: 1..K1_APL_MAKS; rola STREFA: the zone 1..K1_STREFY_MAKS;
                        * else 0), c=uid; + 1 fd */
    /* nadzorca → skarbiec (same direction K1_KIER_STEROWANIE) */
    K1_KOM_SKARB_KLIENT = 2, /* a=owner (K1_WL_*), b=uid of the client, c=1 K1-DANE is on a USB stick, d=MiB the stick
                              * has after K1-DANE (0..K1_PAMIEC_MAKS_MIB; 0.8.1); + 1 fd (skarbiec's end) */
    K1_KOM_POWIEKSZONO = 115, /* nadzorca → skarbiec (0.8.1 step 3, ADR 0033): obiekt=0, a=0 K1-DANE grew to the end of
                               * the stick, K1_SKARB_ARG it cannot (no room, a table K1 did not write), K1_SKARB_IO
                               * writing failed (one valid table stays) */
    K1_KOM_ZEGAR_PRZESUNIETY = 137, /* nadzorca → skarbiec (0.9.0 step 8): the clock was set from the internet; a,b = by
                                     * how many seconds (signed 64 bits: k1_kom_u64; at most ±200 years) — skarbiec moves
                                     * the time of the files in Kosz by as much (their age does not change) */
    K1_KOM_START_POTWIERDZONY = 118, /* nadzorca → skarbiec (0.8.2 step 1c, ADR 0036): all fields 0; the start of this
                                      * version is confirmed (15 s of desktop or password screen, not an A/B try that
                                      * waits) — a vault of format 1 may move to format 2 (a rollback no longer can) */
    /* nadzorca → instalator (M5f, ADR 0018; same direction K1_KIER_STEROWANIE): the job, once */
    K1_KOM_INST_ZADANIE = 3, /* a=K1_INST_* mode, b=logical sector size (512/4096) of the target */

    /* client → ekran (obiekt = surface number 1..K1_POW_MAKS unless noted) */
    K1_KOM_POWIERZCHNIA = 10, /* a=width, b=height, c=kind (K1_POW_*), d,e = x,y (panel; window, desktop: 0); + 1 fd */
    K1_KOM_ZATWIERDZ = 11,    /* a=buffer half 0/1, b,c,d,e = damaged x,y,w,h in the surface (w=h=0: all) */
    K1_KOM_POKAZ = 12,        /* a=1 show / 0 hide (panels) */
    K1_KOM_PRZESUN = 13,      /* a,b = new x,y (panels) */
    K1_KOM_PRZECIAGNIJ = 14,  /* start moving the window with the pressed mouse button */
    K1_KOM_MINIMALIZUJ = 15,  /* hide the window until the shell activates it */
    K1_KOM_USUN = 16,         /* destroy the surface */
    K1_KOM_AKTYWUJ_OKNO = 17, /* shell only: obiekt = window id; show, raise and focus */
    K1_KOM_MINIMALIZUJ_OKNO = 18, /* shell only: obiekt = window id */
    K1_KOM_USTAW = 19,        /* settings application only: obiekt=0, a=key, b=value */
    K1_KOM_ZAMKNIJ_OKNO = 20, /* shell only (0.7.5): obiekt = window id; ekran asks its application to close it */
    K1_KOM_TAPETA_ZDJECIE = 39, /* Pliki only (0.8.0 step 4, ADR 0032): obiekt=0, a,b = width 1..K1_DEK_MAKS_W, height
                                 * 1..K1_DEK_MAKS_H; + 1 sealed memfd of a × b pixels from dekoder — the wallpaper */

    /* ekran → client */
    K1_KOM_WITAJ = 30,      /* obiekt=0, a,b = screen width,height, c=rola, d=aplikacja */
    K1_KOM_WSKAZNIK = 31,   /* obiekt=surface (0: outside the client's surfaces), a,b = x,y, c=K1_WSK_*, d=button,
                             * e=K1_MOD_* bits held (0.8.2 step 6: Ctrl+click, Shift+click) */
    K1_KOM_KLAWISZ = 32,    /* obiekt=surface, a=Linux key code, b=0 up/1 down/2 repeat, c=K1_MOD_* bits; Shift, Ctrl,
                             * Alt and AltGr themselves are sent as keys only to a zone's window (its guest's keyboard) */
    K1_KOM_FOKUS = 33,      /* obiekt=surface, a=1 gained / 0 lost keyboard focus */
    K1_KOM_RAMKA = 34,      /* obiekt=surface: the last commit is on screen, the other half may be drawn */
    K1_KOM_OKNO = 35,       /* shell only: obiekt=window id, a=aplikacja, b=K1_OKNO_* state, c=zone (0.10.0: a zone's
                             * window has a=0, c=its number; an application's c=0) */
    K1_KOM_USTAWIENIE = 36, /* obiekt=0, a=key, b=value (broadcast to all clients) */
    K1_KOM_TRYB = 37,       /* to Ustawienia: obiekt=mode 1..K1_TRYBY_MAKS, a,b = width,height, c=Hz, d=1 preferred */
    K1_KOM_ZAMKNIJ = 38,    /* to an application (0.7.5): obiekt=window surface — close it (as with its × button) */
    /* WITAJ is sent again when the screen size changes (K1_UST_ROZDZIELCZOSC): clients lay out anew. */

    /* gui / test → nadzorca */
    K1_KOM_URUCHOM = 50,      /* gui: a=aplikacja; Pliki only (0.8.0): b=entry to show (0: none), c=K1_PLIKI_* */
    K1_KOM_WYLACZ = 51,       /* gui: a=0 power off, 1 restart, 2 log out (gui only) */
    K1_KOM_TEST_POLACZ = 52,  /* test image only: a=rola, b=aplikacja, c=expected uid → reply + 1 fd */
    K1_KOM_TEST_PROBA = 53,   /* test image only: a=sandbox profile → reply when the probe ends */
    K1_KOM_TEST_SKARB = 123,  /* test image only (0.9.0): a=owner (profile 1..K1_PROFIL_MAKS) → TEST_GNIAZDO + 1 fd: a
                               * channel to skarbiec as that owner, the test program (UID 0) its sender */
    /* Instalator application → nadzorca (M5f, ADR 0018) */
    K1_KOM_INST_DYSKI = 54,   /* obiekt=0: list the disks (reply: INST_DYSK…, INST_LISTA) */
    K1_KOM_INST_START = 55,   /* obiekt=disk 1..K1_INST_DYSKI_MAKS from the last list, a=K1_INST_* mode */
    /* instalator (the writing program) → nadzorca */
    K1_KOM_INST_ETAP = 56,    /* obiekt=0, a=percent 0..100, b=K1_INST_ETAP_* */
    K1_KOM_INST_GOTOWE = 57,  /* obiekt=0, a=result (0 or K1_INST_BLAD_* < 0), b=1 a new slot to try (A/B, 0.7.0) */
    /* nadzorca → Ustawienia (0.6.0): battery history of earlier boots (k1_bat_arch.h) */
    K1_KOM_BATERIA_ARCH = 58, /* obiekt=0, a=length 8..K1_BAT_ARCH_BAJTY, b=sessions 0..K1_BAT_ARCH_SESJE; + 1 sealed memfd */

    /* nadzorca → test */
    K1_KOM_TEST_WYNIK = 60,   /* a=result (exit code, 128+signal, or -1 on error) */
    K1_KOM_TEST_GNIAZDO = 61, /* reply to TEST_POLACZ, TEST_SKARB: + 1 fd (the client end of the new channel) */

    /* nadzorca → Zasoby (once a second; the application never writes to this channel) */
    K1_KOM_ZASOBY_SYSTEM = 62, /* obiekt=0, a=CPU ‰ (all CPUs), b,c = memory used,total MiB, d=CPUs, e=uptime s */
    K1_KOM_ZASOBY_BATERIA = 59, /* obiekt=0, a=1 battery present, b=percent (-1 unknown), c=K1_BAT_*, d=power draw
                                 * mW (-1 unknown, ≤ K1_MOC_MAKS_MW), e=minutes until empty/full (-1 unknown, ≤ 99 h) */
    K1_KOM_ZASOBY_USLUGA = 63, /* obiekt=K1_PROFIL_* of a service, a=CPU ‰ (of all CPUs), b=memory KiB, c=1 running */
    K1_KOM_BATERIA = 64,       /* to gui: obiekt=0, a=1 battery present, b=percent 0..100 (-1 unknown), c=K1_BAT_*,
                                * d=1 on mains power; no battery: a=0, b=-1, c=0 (also to Ustawienia) */
    /* client → skarbiec (M5d, ADR 0015): objects of the client's owner only (the owner comes from nadzorca) */
    K1_KOM_SKARB_CZYTAJ = 70, /* obiekt=object id (K1_OB_*) */
    K1_KOM_SKARB_ZAPISZ = 71, /* obiekt=object id, a=length (0..K1_SKARB_MAKS); + 1 sealed memfd of that size */
    K1_KOM_SKARB_USUN = 72,   /* obiekt=object id */
    K1_KOM_SKARB_ODBLOKUJ = 73, /* login screen only (M5e): obiekt=1, a=password length 1..K1_HASLO_MAKS; + 1 memfd */
    K1_KOM_SKARB_HASLO = 74,    /* Ustawienia only: obiekt=1, a=length; + 1 memfd "current\0new\0" (empty: none) */
    K1_KOM_SKARB_INFO = 75,     /* obiekt=1: the state of the vault (reply: WYNIK with c, d, then MIEJSCE) */
    K1_KOM_SKARB_PODZIAL = 121, /* Ustawienia only (0.8.5 step 3, Miejsce): obiekt=1 — what takes the room for files
                                 * (reply: CZESC for every part, then WYNIK) */
    K1_KOM_SKARB_POWIEKSZ = 113, /* Ustawienia only (0.8.1 step 3, ADR 0033; the user clicked "Użyj całego pendrive'a"):
                                  * obiekt=1 — K1-DANE to the end of its USB stick (reply: WYNIK when done, then MIEJSCE) */
    /* skarbiec → client */
    K1_KOM_SKARB_DANE = 80,   /* obiekt=object id, a=length; + 1 sealed memfd of that size */
    K1_KOM_SKARB_MIEJSCE = 112, /* obiekt=1 (0.8.1 step 3): the room for files, after WYNIK of INFO and POWIEKSZ and after
                                 * DOK_LISTA. a=MiB taken (0..b), b=MiB for files (the vault's region without the
                                 * reserve for the system, or memory without K1-DANE; 0..K1_PAMIEC_MAKS_MIB), c=entries
                                 * (files and folders, Kosz too; 0..K1_DOK_MAKS), d=MiB the stick has after K1-DANE
                                 * (0..K1_PAMIEC_MAKS_MIB: what "Użyj całego pendrive'a" adds), e=1 the files are
                                 * kept in memory — no K1-DANE (0.8.2 step 2: Pliki say "no room in memory"), else 0 */
    K1_KOM_SKARB_CZESC = 122, /* obiekt=part 1..K1_CZESCI_MAKS (0.8.5 step 3, k1_dokument.h), a=KiB it takes
                               * (0..INT32_MAX: 2 TiB, more is sent as INT32_MAX), b=files in it (0..K1_DOK_MAKS; the reserve: 0); one per
                               * part, in order, before WYNIK of PODZIAL — only numbers, never a name */
    K1_KOM_SKARB_WYNIK = 81,  /* obiekt=object id, a=request type (70..75, POWIEKSZ, PODZIAL), b=result (0 or K1_MAG_* < 0),
                               * c=K1_SKARB_TRWALY / _RAM / _ZABLOKOWANY, d: bit 0 password set, bit 1 K1-DANE on USB,
                               * e=seconds until the next password attempt is accepted (0..K1_HASLO_PRZERWA) */
    /* skarbiec → nadzorca, on the control channel (direction K1_KIER_DO_NADZORCY) */
    K1_KOM_SKARB_STAN = 82,   /* a=K1_SKARB_* mode, b=1 password set */
    K1_KOM_POWIEKSZ_DANE = 114, /* (0.8.1 step 3, ADR 0033) obiekt=0: Ustawienia asked (a gesture) and the vault is open —
                                 * grow K1-DANE to the end of its USB stick (reply: POWIEKSZONO) */
    /* nadzorca → Instalator application (M5f) */
    K1_KOM_INST_DYSK = 66,    /* obiekt=1..K1_INST_DYSKI_MAKS, a=size MiB, b=K1_DYSK_* kind, c=Kons K1 on it: 0 no,
                               * 1 yes without a manifest (0.5.9–0.6.1), K1_INST_WERSJA|version (M6a),
                               * d=logical sector size, e=1 in use (its data partition is open now) */
    K1_KOM_INST_LISTA = 67,   /* obiekt=0, a=number of disks sent, b=the system to copy: 1 found (one K1 stick),
                               * 0 none (not started from a K1 stick — e.g. the .iso on it), 2 two K1 sticks;
                               * c=its version (M6a manifest; 0 = none), d=1 signed */
    K1_KOM_INST_POSTEP = 68,  /* obiekt=0, a=percent 0..100, b=K1_INST_ETAP_* */
    K1_KOM_INST_WYNIK = 69,   /* obiekt=0, a=result (0 or K1_INST_BLAD_* < 0), b=K1_INST_* mode, c=1 new slot (A/B) */
    /* nadzorca → Instalator window (0.7.0, ADR 0022): A/B and Secure Boot */
    K1_KOM_INST_AB = 76,      /* obiekt=disk 1..K1_INST_DYSKI_MAKS (after its INST_DYSK), a=version it runs, b=version in the
                               * other slot (0: none), c=slot it runs (1=A, 2=B), d=K1_AB_FL_* flags */
    K1_KOM_SESJA = 79,        /* to gui (fd 5, 0.7.2): obiekt=0, a=1 K1 is installed here (the K1-DANE in use is on
                               * an internal disk: "Wyloguj" makes sense), a=0 a session from a stick or the .iso; b..e=0 */
    K1_KOM_SB_STAN = 77,      /* obiekt=0, a=K1_SB_* Secure Boot, b=K1_SBK_* the K1 key, c=1 release key (0: developer
                               * key — never enrolled), d=K1_SBP_* result of the last SB_KLUCZ */
    /* Instalator window → nadzorca */
    K1_KOM_SB_KLUCZ = 78,     /* obiekt=0: prepare the enrolment of the K1 key (MokNew + MokAuth for MokManager) */
    /* client (Pliki, gui) → skarbiec (0.8.0, ADR 0029): files and folders; what a client may do comes from
     * the table of rights in skarbiec (dokumenty.c). Entry numbers: 1..K1_FOLDERY_MAKS standard folders,
     * K1_DOK_PIERWSZY..K1_DOK_ID_MAKS the user's. Names and content travel in a sealed memfd. Since 0.8.1 (step 4,
     * ADR 0034) content travels in pieces of at most K1_DOK_KAWALEK: a larger file is written as a write in progress
     * (NOWY/ZAPISZ with "more follow", DOK_KAWALEK after each answer, DOK_KONIEC) and read piece by piece. */
    K1_KOM_DOK_SPIS = 84,      /* obiekt=0: the entries this client may see (reply: DOK_LISTA) */
    K1_KOM_DOK_CZYTAJ = 85,    /* obiekt=file, a=at most this many bytes (1..K1_DOK_KAWALEK: a piece; 0.8.0–0.8.1 also a
                                * whole picture for dekoder), b=from piece b (0..K1_DOK_KAWALKI_MAKS-1: byte b ×
                                * K1_DOK_KAWALEK on) (reply: DOK_TRESC) */
    K1_KOM_DOK_NOWY = 86,      /* obiekt=folder, a=K1_DOK_PLIK/FOLDER, b=name length 1..127, c=content length in this
                                * memfd 0..K1_DOK_KAWALEK (folder: 0), d=origin 0 (= K1) or K1_POCH_* (a file, Pliki only:
                                * an import, ADR 0031), e=1 a write in progress: more pieces follow (a file only; then
                                * c=K1_DOK_KAWALEK), else 0; + 1 memfd of b + c bytes: the name, then the content.
                                * Notatki (0.8.0, ADR 0030): obiekt=Dokumenty — the file goes to its folder Dokumenty › Notatki */
    K1_KOM_DOK_ZAPISZ = 87,    /* obiekt=file, a=length 0..K1_DOK_KAWALEK, b=1 a write in progress (then a=K1_DOK_KAWALEK)
                                * else 0; + 1 memfd: the new content (its first piece) */
    K1_KOM_DOK_KAWALEK = 116,  /* (0.8.1 step 4) the next piece of the client's write in progress: obiekt=0,
                                * a=length 1..K1_DOK_KAWALEK (every piece but the last: K1_DOK_KAWALEK); + 1 memfd */
    K1_KOM_DOK_KONIEC = 117,   /* (0.8.1 step 4) obiekt=0, a=0 the write in progress is done (the entry is written now),
                                * a=1 abandoned (its pieces deleted) (reply: DOK_WYNIK; c = the new file's number) */
    K1_KOM_DOK_CZAS = 119,     /* (0.8.2 step 2) Pliki only, after an import: obiekt=a file with an origin other than K1,
                                * a,b=its "changed" time as on the stick (k1_kom_u64: 1..K1_DOK_CZAS_MAKS seconds since
                                * 1970), c..e=0 (reply: DOK_WYNIK) — a second import of the same file is skipped */
    K1_KOM_DOK_NAZWA = 88,     /* obiekt=entry, a=name length 1..127, b=1 a taken name gets " (2)"… (else 0: refused);
                                * + 1 memfd: the new name */
    K1_KOM_DOK_PRZENIES = 89,  /* obiekt=entry, a=the folder it goes to, b=1 a taken name gets " (2)"… (else 0: refused;
                                * 0.8.2 step 6) */
    K1_KOM_DOK_KOSZ = 90,      /* obiekt=entry: into Kosz (a folder with everything inside) */
    K1_KOM_DOK_PRZYWROC = 91,  /* obiekt=entry in Kosz: back to its folder (a=0), or to folder a (0.8.2 step 6: dropped
                                * on it); a taken name gets " (2)"… */
    K1_KOM_DOK_USUN = 92,      /* obiekt=entry in Kosz: deleted for good */
    K1_KOM_DOK_OPROZNIJ = 93,  /* obiekt=0: everything in Kosz deleted for good */
    /* skarbiec → client */
    K1_KOM_DOK_LISTA = 94,     /* obiekt=0, a=length 16..K1_DOK_LISTA_MAKS, b=entries 0..K1_DOK_MAKS; + 1 memfd (k1_dokument.h) */
    K1_KOM_DOK_TRESC = 95,     /* obiekt=file, a=bytes sent 0..K1_DOK_KAWALEK, b=from piece b (as asked), c,d=its whole
                                * size (k1_kom_u64: low, high 32 bits; ≤ K1_DOK_MAKS_BAJTY, ≥ b × K1_DOK_KAWALEK + a);
                                * + 1 memfd of a bytes */
    K1_KOM_DOK_WYNIK = 96,     /* obiekt=entry (0: none), a=request type (84..93, KAWALEK, KONIEC, CZAS), b=result (0,
                                * K1_MAG_* or K1_DOK_BLAD_* < 0), c=the new entry's number (NOWY, KONIEC; else 0) */
    K1_KOM_DOK_ZMIANA = 97,    /* obiekt=0: files changed — ask for the list again (to every client allowed to list) */
    /* client → skarbiec (0.8.0 step 2, ADR 0030): a file for an application, always after a gesture of the user */
    K1_KOM_DOK_OTWORZ = 98,    /* Pliki, gui: obiekt=file, a=application 1..K1_APL_MAKS that opens its type (not Pliki):
                                * it gets this file and is started with it (reply: DOK_WYNIK) */
    K1_KOM_DOK_WYBIERZ = 99,   /* an application: obiekt=0, a=type 0 (any) / 1..K1_TYPY_MAKS, b=family 0 (any) /
                                * 1..K1_RODZINY_MAKS (0.8.2 step 4: Podgląd asks for "obrazy"; not both) — open the file
                                * picker (reply: DOK_WYBRANY when the user is done, DOK_WYNIK at once when it cannot) */
    K1_KOM_DOK_WYBRANO = 100,  /* the file picker only: obiekt=the file picked (0: cancelled) (reply: DOK_WYNIK) */
    /* skarbiec → client */
    K1_KOM_DOK_WYBRANY = 101,  /* to the application that asked: obiekt=the file it may use now (0: the user cancelled) */
    K1_KOM_DOK_PROSBA = 102,   /* to the file picker: obiekt=0, a=application asking 1..K1_APL_MAKS, b=type 0..K1_TYPY_MAKS,
                                * c=family 0..K1_RODZINY_MAKS (not both) */
    /* skarbiec → nadzorca (control channel): start an application (with a file it was given: its fd 5 gets PLIK_OTWORZ) */
    K1_KOM_SKARB_URUCHOM = 83, /* obiekt=file (0: none), a=application 1..K1_APL_MAKS */
    /* nadzorca → an application (its fd 5) and → gui (its fd 5) */
    K1_KOM_PLIK_OTWORZ = 22,   /* to an application: obiekt=file it was given (DOK_OTWORZ) — open it */
    K1_KOM_POKAZ_APL = 23,     /* to gui: obiekt=0, a=application 1..K1_APL_MAKS — show its window (it was asked to open a file) */
    /* nadzorca ↔ Pliki (0.8.0 step 3, ADR 0031; Pliki's fd 6): the USB stick */
    K1_KOM_PENDRIVE = 24,        /* to Pliki: obiekt=0, a=1 a stick (not a K1 one) is plugged in / 0 none */
    K1_KOM_PENDRIVE_OTWORZ = 25, /* from Pliki: obiekt=0 — start czytnik for the stick (reply: PENDRIVE_KANAL, or
                                  * PENDRIVE a=0) */
    K1_KOM_PENDRIVE_KANAL = 26,  /* to Pliki: obiekt=0; + 1 fd: the channel to the new czytnik */
    /* nadzorca ↔ Pliki (0.8.0 step 4, ADR 0032; the same fd 6): a decoder for one picture */
    K1_KOM_DEKODER = 27,         /* from Pliki: obiekt=0 — start dekoder (reply: DEKODER_KANAL) */
    K1_KOM_DEKODER_KANAL = 28,   /* to Pliki: obiekt=0; + 1 fd: the channel to the new dekoder */
    /* Pliki → czytnik (the stick, read only; obiekt = an entry of a list it sent, 0: the main folder) */
    K1_KOM_PEN_SPIS = 103,       /* obiekt=folder, a=its first entry wanted 0..K1_PEN_FOLDER_MAKS-1 (0.8.2 step 2: a large
                                  * folder comes in lists of K1_PEN_WPISOW) (reply: PEN_LISTA) */
    K1_KOM_PEN_CZYTAJ = 104,     /* obiekt=file, a=piece 0..K1_DOK_KAWALKI_MAKS-1 (0.8.1 step 4): its name (piece 0) and
                                  * that piece of the content, for DOK_NOWY / DOK_KAWALEK (reply: PEN_PLIK) */
    /* czytnik → Pliki */
    K1_KOM_PEN_STAN = 105,       /* once at start: obiekt=0, a=12/16/32 the FAT the stick has, or a=0 and b=K1_PEN_BLAD_* */
    K1_KOM_PEN_LISTA = 106,      /* obiekt=folder, a=length 16..K1_PEN_LISTA_MAKS, b=entries 0..K1_PEN_WPISOW, c=1 the
                                  * folder has more, d=its first entry in this list (as asked); + 1 memfd (k1_pendrive.h) */
    K1_KOM_PEN_PLIK = 107,       /* obiekt=file, a=name length (piece 0: 1..127, others: 0), b=bytes of the piece
                                  * 0..K1_DOK_KAWALEK, c=the piece, d,e=the file's size (k1_kom_u64, ≤ K1_DOK_MAKS_BAJTY,
                                  * ≥ c × K1_DOK_KAWALEK + b); + 1 memfd of a + b bytes: the name, then the piece (as
                                  * DOK_NOWY or DOK_KAWALEK want it) */
    K1_KOM_PEN_WYNIK = 108,      /* obiekt=entry, a=request type (PEN_SPIS, PEN_CZYTAJ), b=K1_PEN_BLAD_* (< 0) */
    /* a client (nadzorca/dekoder.c: Pliki) → dekoder (once: dekoder ends after the answer; obiekt = the caller's number,
     * sent back). Since 0.8.2 step 3 the file follows in pieces, as skarbiec gives them. */
    K1_KOM_DEK_OBRAZ = 109,      /* a=file length 1..K1_DEK_MAKS_BAJTY, b,c = the largest width 1..K1_DEK_MAKS_W and
                                  * height 1..K1_DEK_MAKS_H wanted; then DEK_KAWALEK 0, 1, … until a bytes came */
    K1_KOM_DEK_KAWALEK = 120,    /* obiekt as in DEK_OBRAZ, a=piece 0..K1_DEK_MAKS_BAJTY/K1_DOK_KAWALEK-1 (in order),
                                  * b=its bytes 1..K1_DOK_KAWALEK (all but the last: K1_DOK_KAWALEK); + 1 sealed memfd of b
                                  * bytes */
    /* dekoder → Pliki */
    K1_KOM_DEK_PIKSELE = 110,    /* a,b = width 1..K1_DEK_MAKS_W, height 1..K1_DEK_MAKS_H; c,d = the photo's own width ≥ a
                                  * and height ≥ b, turned as seen (0.8.2 step 4: Podgląd shows them; c × d ≤
                                  * K1_DEK_MAKS_PIKSELI); + 1 sealed memfd of a × b pixels (k1_dekoder.h) */
    K1_KOM_DEK_BLAD = 111,       /* a=K1_DEK_BLAD_* (< 0): no picture; b=the photo's Mpx 1..K1_DEK_MPX_MAKS after
                                  * K1_DEK_BLAD_DUZY (0.8.2 step 3), else 0 */
    /* nadzorca → Pliki (0.8.0, its fd 5; after URUCHOM with a place): show it */
    K1_KOM_PLIKI_POKAZ = 21,   /* obiekt=0, a=entry (0: the last place), b=K1_PLIKI_*: 1 Kosz (then a=0), 2 the entry's
                                * name to change (a≠0; 0.8.2 step 5: "Zmień nazwę" on the desktop) */
    K1_KOM_BATERIA_HIST = 65,  /* to Ustawienia: battery history since boot. obiekt=index of the first sample
                                * (0 = history starts again), a=seconds between samples, b..e = up to 16
                                * samples, one byte each (b: samples 0-3 from the lowest byte, …):
                                * percent 0..100, +128 = on mains; 0xFF = no sample (only at the end) */

    /* nadzorca ↔ siec (0.9.0 step 6, ADR 0039; siec's fd 3): obiekt = the card (its kernel number, 1..K1_SIEC_KARTA_MAKS);
     * addresses in host order (k1_siec.h) */
    K1_KOM_SIEC_KABEL = 124,      /* to siec: a cable is in — ask the router for an address. a,b = the card's MAC
                                   * (k1_siec_mac_do_pol: k1_siec_mac_dobry); + 1 fd: a packet socket on the card (IPv4,
                                   * UDP to port 68 only — a filter set and locked by nadzorca) */
    K1_KOM_SIEC_ROZLACZONO = 125, /* to siec: a=K1_SIEC_KABEL_WYJETY / K1_SIEC_KARTA_ZNIKNELA — the address is gone */
    K1_KOM_SIEC_ADRES = 126,      /* from siec: set this address: a=address, b=prefix K1_SIEC_PREFIKS_MIN..MAKS, c=router
                                   * (0: none) — k1_siec_adres_dobry; d=lease seconds K1_SIEC_DZIERZAWA_MIN..MAKS;
                                   * e=the DNS server (DHCP option 6; 0: none, else k1_siec_serwer_dobry; 0.10.0 step 2) */
    K1_KOM_SIEC_ZWOLNIJ = 127,    /* from siec: the lease ended (or the router said no): take the address away */
    K1_KOM_SIEC_WYNIK = 128,      /* to siec: a=request type (SIEC_ADRES, SIEC_ZWOLNIJ, SIEC_ZEGAR), b=0 or K1_SIEC_BLAD_* */
    /* nadzorca → gui (fd 5) and Ustawienia (fd 4): the network as the person sees it (0.9.0 step 7; k1_siec.h) */
    K1_KOM_SIEC_STAN = 129,  /* obiekt=0, a=K1_SIEC_* state 0..K1_SIEC_STAN_MAKS, b=cards in the list 0..K1_SIEC_LISTA_MAKS
                              * (Ustawienia: SIEC_KARTA 0..b-1 follow), c=connected since (CLOCK_MONOTONIC, whole seconds;
                              * > 0 only when POLACZONO), d=the clock set from the internet at (CLOCK_MONOTONIC s; 0: not
                              * yet) */
    K1_KOM_SIEC_KARTA = 130, /* to Ustawienia: obiekt=index 0..K1_SIEC_LISTA_MAKS-1, a=identifier (vendor << 16 | device),
                              * b=K1_SIEC_PRZEWODOWA/WIFI/INNA, c=K1_SIEC_KF_* (KABEL, AKTYWNA: wired with a driver only) */
    /* to Ustawienia, after SIEC_KARTA of a wired card with a driver (the owner's wish: "a bit more about the card") */
    K1_KOM_SIEC_SZCZEGOLY = 131, /* obiekt=index as in SIEC_KARTA, a=link speed Mb/s 0..K1_SIEC_PREDKOSC_MAKS (0: no link
                                  * or unknown), b=K1_SIEC_DUPLEKS_*, c,d=the card's MAC (k1_siec_mac_do_pol;
                                  * k1_siec_mac_dobry) */
    K1_KOM_SIEC_STEROWNIK = 132, /* obiekt=index, a..e=the kernel driver's name, 20 bytes (k1_siec_sterownik_dobry) */
    K1_KOM_SIEC_ADRESY = 133,    /* obiekt=index of the card with the address (only when POLACZONO), a=address, b=prefix,
                                  * c=router (0: none) — k1_siec_adres_dobry */
    /* nadzorca ↔ siec: the clock from the internet (0.9.0 step 8, T5; k1_siec.h); obiekt = the card with the address */
    K1_KOM_SIEC_CZAS_GNIAZDO = 134, /* from siec: a UDP socket to time server a (k1_siec_serwer_dobry), port 123;
                                     * b=source 0..K1_SIEC_ZRODLA_MAKS-1 (comes back in SIEC_GNIAZDO) */
    K1_KOM_SIEC_GNIAZDO = 135,      /* to siec: a=the server, b=the source, c=0 + 1 fd (a UDP socket connected to a:123),
                                     * or c=K1_SIEC_BLAD_* without a fd */
    K1_KOM_SIEC_ZEGAR = 136,        /* from siec: set the clock: a,b = seconds since 1970, UTC (k1_kom_u64;
                                     * K1_SIEC_CZAS_MIN..MAKS), c=nanoseconds 0..999999999, d=sources that agree
                                     * 2..K1_SIEC_ZRODLA_MAKS; answer: SIEC_WYNIK (b: K1_SIEC_BLAD_CZAS — out of bounds or
                                     * too often) */
    /* Wi-Fi (0.9.1 step 1, ADR 0041; k1_wifi.h). nadzorca ↔ siec: the networks around */
    K1_KOM_SIEC_WIFI_WYNIKI = 138, /* to siec: obiekt=the Wi-Fi card (1..K1_SIEC_KARTA_MAKS), a=records 0..K1_WIFI_REKORDY_MAKS,
                                    * b=bytes 0..K1_WIFI_WYNIKI_MAKS; + 1 sealed memfd of b bytes (struct k1_wifi_rekord,
                                    * each followed by its description from the air) */
    K1_KOM_SIEC_WIFI_SIEC = 139,   /* from siec: network obiekt=0..K1_WIFI_LISTA_MAKS-1 of the next list: a=bars 1..4
                                    * (k1_wifi_poziom of c), b=K1_WIFI_* security, c=dBm, d=MHz, e=name length 1..32 */
    K1_KOM_SIEC_WIFI_NAZWA = 140,  /* from siec: obiekt=network*2+part (k1_wifi_nazwa_do_pol): part 0 a..e, part 1 a..c
                                    * (d, e = 0) — the name to show (checked whole by the receiver: k1_wifi_nazwa_dobra) */
    K1_KOM_SIEC_WIFI_LISTA = 141,  /* from siec, after its networks: obiekt=the card of WYNIKI, a=networks 0..LISTA_MAKS,
                                    * b=records rejected 0..K1_WIFI_REKORDY_MAKS; 0.9.1 step 2: c=K1_WIFI_P_* the connection,
                                    * d=its network 0..a-1 (-1: not in the list), e=the remembered ones (bit i: network i) */
    /* nadzorca → Ustawienia (fd 4): Wi-Fi in Ustawienia → Sieć */
    K1_KOM_WIFI_STAN = 142,  /* obiekt=0, a=K1_WIFI_* state 0..K1_WIFI_STAN_MAKS, b=networks 0..LISTA_MAKS (WIFI_SIEC and
                              * WIFI_NAZWA 0..b-1 follow; only when WLACZONE), c=1 looking for networks now, else 0 */
    K1_KOM_WIFI_SIEC = 143,  /* as SIEC_WIFI_SIEC */
    K1_KOM_WIFI_NAZWA = 144, /* as SIEC_WIFI_NAZWA */
    /* Ustawienia → nadzorca (fd 4, 0.9.1: the channel goes both ways) */
    K1_KOM_WIFI_WLACZ = 145,  /* obiekt=0, a=1 Wi-Fi on / 0 off (the radio: soft rfkill) */
    K1_KOM_WIFI_SZUKAJ = 146, /* obiekt=0, all 0: look for networks now (Ustawienia shows the list) */
    /* Joining a Wi-Fi network (0.9.1 step 2, ADR 0041 T3/T4/T6). siec → nadzorca: obiekt = the Wi-Fi card */
    K1_KOM_SIEC_WIFI_SSID = 147,   /* obiekt=part 0/1: the network's name as it is in the air (any bytes; part 0 a..e =
                                    * bytes 0..19, part 1 a..c = 20..31, d=e=0) — for the next SIEC_WIFI_POLACZ */
    K1_KOM_SIEC_WIFI_POLACZ = 148, /* join: a,b=the router's address (k1_siec_mac_do_pol; k1_siec_mac_dobry), c=MHz
                                    * K1_WIFI_MHZ_MIN..MAKS, d=name length 1..32 (the SSID parts before),
                                    * e=K1_WIFI_TRYB_* (open, or WPA2 with the group cipher) */
    K1_KOM_SIEC_WIFI_ROZLACZ = 149, /* leave the network (all 0) */
    K1_KOM_SIEC_WIFI_KLUCZ_CZESC = 150, /* obiekt=part 0/1 of the next key (like SSID: 20 + 12 bytes) — a secret: never
                                         * in a log, wiped after use */
    K1_KOM_SIEC_WIFI_KLUCZ = 151,  /* install the key of the parts: a=1 pairwise (b=0, c=CCMP) / 2 group (b=key id 1..3,
                                    * c=K1_WIFI_SZYFR_*), d,e = its receive counter (RSC bytes 0..3 and 4..5, little
                                    * endian; pairwise: 0) */
    K1_KOM_SIEC_WIFI_OTWORZ = 152, /* the keys are in: let data through (all 0) */
    K1_KOM_SIEC_WIFI_RADIO_USTAW = 155, /* at start, what the person chose before (remembered by siec): a=0 radio off */
    /* 0.10.0 (ADR 0042): the zones — gui (and the test program) → nadzorca, nadzorca → gui (fd 5) */
    K1_KOM_URUCHOM_STREFE = 163, /* obiekt=0, a=zone 1..K1_STREFY_MAKS, b=the guest's test mode 0..K1_STREFA_TRYB_MAKS
                                  * (src/strefa/gosc/gosc.h; not 0: the test program only), c..e=0 */
    K1_KOM_STREFA_STAN = 164,    /* obiekt=0, a=zone, b=K1_STREFA_* why it did not start or ended, c..e=0 (gui says it) */
    /* 0.10.0 step 2 (ADR 0043 T9): the browser's connections — the zone's VMM ↔ nadzorca (VMM's fd 7; the sender's UID
     * is the zone's) */
    K1_KOM_STREFA_POLACZ = 165,   /* from the VMM: obiekt=its connection 1..K1_STREFA_POLACZENIA_MAKS; TCP: a=IPv4 address
                                   * (k1_siec_serwer_dobry), b=port 1..65535, c=K1_STREFA_TCP; the DNS server: a=b=0,
                                   * c=K1_STREFA_DNS; d=e=0 */
    K1_KOM_STREFA_GNIAZDO = 166,  /* to the VMM: obiekt=the connection, a=K1_STREFA_TCP/DNS, b..e=0; + 1 fd: a TCP socket
                                   * connecting (non-blocking) or a UDP socket connected to the DNS server, port 53 */
    K1_KOM_STREFA_ODMOWA = 167,   /* to the VMM: obiekt=the connection, a=K1_STREFA_ODM_* why not, b..e=0 (no fd) */
    /* nadzorca → siec */
    K1_KOM_SIEC_WIFI_EAPOL = 162,     /* obiekt=the card, all 0; + 1 fd: a packet socket for the EAPOL frames on the card
                                       * (WPA2 only; right before SIEC_WIFI_ZDARZENIE LACZE) */
    K1_KOM_SIEC_WIFI_ZDARZENIE = 153, /* a=K1_WIFI_Z_*; LACZE: c,d = our card's address, b=0; NIEUDANE: b=the router's status code
                                       * 1..65535 or -1 (no answer, the kernel refused); ROZLACZONO: b=reason 0..65535;
                                       * KLUCZ, OTWARTE: b=0 or K1_SIEC_BLAD_* */
    K1_KOM_SIEC_WIFI_RADIO = 154,  /* the person switched the radio in Ustawienia: a=1 on / 0 off (siec remembers it) */
    /* Ustawienia → siec (K1_KIER_DO_SIECI; Ustawienia's fd 6, siec's end comes with SIEC_USTAWIENIA) */
    K1_KOM_WIFI_HASLO = 156,       /* obiekt=part 0..3 (k1_wifi_haslo_do_pol): the password for the next POLACZ —
                                    * printable ASCII or 0; a secret: never in a log, wiped after use */
    K1_KOM_WIFI_WYBRANA = 157,     /* obiekt=part 0/1: the name of the network as shown (like WIFI_NAZWA) */
    K1_KOM_WIFI_POLACZ = 158,      /* join the network of the name parts: a=its security (K1_WIFI_*), b=name length 1..32,
                                    * c=password length 0 (open) or K1_WIFI_HASLO_MIN..MAKS */
    K1_KOM_WIFI_ZAPOMNIJ = 159,    /* forget the network of the name parts (and leave it): a=its security, b=name length */
    /* nadzorca → Ustawienia, right after WIFI_STAN (when WLACZONE) */
    K1_KOM_WIFI_POLACZENIE = 160,  /* obiekt=0, a=K1_WIFI_P_*, b=the network of that state 0..b-1 of WIFI_STAN (-1: none —
                                    * also when it is not in the list), c=the remembered networks of the list (bit i: network i) */
    /* nadzorca → siec: the end of Ustawienia's channel (when Ustawienia start) */
    K1_KOM_SIEC_USTAWIENIA = 161,  /* obiekt=0, all 0; + 1 fd (siec's end of K1_KIER_DO_SIECI, sender UID checked) */
};

/* What happened to the connection (SIEC_WIFI_ZDARZENIE a). */
enum k1_wifi_zdarzenie {
    K1_WIFI_Z_LACZE = 1,       /* the kernel took the request: joining (the EAPOL socket comes with it) */
    K1_WIFI_Z_SKOJARZONO = 2,  /* joined the router (the password exchange may start) */
    K1_WIFI_Z_NIEUDANE = 3,    /* joining failed */
    K1_WIFI_Z_ROZLACZONO = 4,  /* the connection ended (the router, the range, our request) */
    K1_WIFI_Z_KLUCZ = 5,       /* the key of SIEC_WIFI_KLUCZ: installed (b=0) or not */
    K1_WIFI_Z_OTWARTE = 6,     /* SIEC_WIFI_OTWORZ: data flows (b=0) or not */
};
#define K1_WIFI_Z_MAKS 6

#define K1_ZEGAR_SKOK_MAKS (200LL * 366 * 86400) /* ZEGAR_PRZESUNIETY: seconds */

/* Surfaces */
#define K1_POW_MAKS 8         /* surfaces per client */
#define K1_POW_MAKS_BOK 4096  /* max width/height of one surface */
#define K1_POZ_MIN (-4096)    /* panel position range */
#define K1_POZ_MAKS 8192
/* OKNO: an application window; PANEL: a panel of the shell or the password screen, above the windows;
 * PULPIT (0.7.5): the shell's desktop layer (icons) — one, at 0,0, right above the wallpaper and below the
 * windows, catching the pointer in every pixel. */
enum { K1_POW_OKNO = 1, K1_POW_PANEL = 2, K1_POW_PULPIT = 3 };

/* Pointer events */
/* What Pliki show when the desktop opens them (URUCHOM c, PLIKI_POKAZ b): the entry's place, Kosz, the entry with its
 * name field open. */
enum { K1_PLIKI_MIEJSCE = 0, K1_PLIKI_KOSZ = 1, K1_PLIKI_NAZWA = 2 };
enum { K1_WSK_RUCH = 1, K1_WSK_WCISNIECIE = 2, K1_WSK_PUSZCZENIE = 3, K1_WSK_WYJSCIE = 4 };
enum { K1_PRZYCISK_BRAK = 0, K1_PRZYCISK_LEWY = 1, K1_PRZYCISK_PRAWY = 2, K1_PRZYCISK_SRODKOWY = 3 };

/* Keyboard modifiers */
#define K1_MOD_SHIFT 1
#define K1_MOD_CTRL 2
#define K1_MOD_ALT 4
#define K1_MOD_ALTGR 8
#define K1_MOD_MASKA 15
#define K1_KLAWISZ_MAKS 0x2ff /* KEY_MAX */

/* 0.10.0 (ADR 0042): why a zone did not start (K1_KOM_STREFA_STAN b); since step 3 (S26) also why it ended without
 * the user closing it: its program said so (PAMIEC: the guest ran out of memory, AWARIA: it crashed — also the VMM's
 * exit code), K1 killed it for lack of memory in the computer (PAMIEC_K1), or it stopped without a word (BLAD) */
enum { K1_STREFA_BEZ_WIRTUALIZACJI = 1, K1_STREFA_ZA_MALO_PAMIECI = 2, K1_STREFA_BRAK_OBRAZU = 3,
       K1_STREFA_KONIEC_PAMIEC = 4, K1_STREFA_KONIEC_PAMIEC_K1 = 5, K1_STREFA_KONIEC_AWARIA = 6, K1_STREFA_KONIEC_BLAD = 7 };
#define K1_STREFA_STAN_MAKS 7
#define K1_STREFA_KONIEC_OD K1_STREFA_KONIEC_PAMIEC /* b >= this: the zone ended (not: did not start) */
#define K1_STREFA_TRYB_MAKS 4 /* the hostile test guests (gosc/zlosliwy.c) */
/* 0.10.0 step 2 (ADR 0043 T9): the zones' connections (STREFA_POLACZ c, STREFA_GNIAZDO a, STREFA_ODMOWA a) */
#define K1_STREFA_POLACZENIA_MAKS 128 /* at once in one zone (the relay in the zone has as many) */
enum { K1_STREFA_TCP = 0, K1_STREFA_DNS = 1 };
enum { K1_STREFA_ODM_ADRES = 1,  /* not allowed (D3: the home network) */
       K1_STREFA_ODM_SIEC = 2,   /* K1 has no network (or no DNS server from the router) */
       K1_STREFA_ODM_LIMIT = 3,  /* too many connections in a short time */
       K1_STREFA_ODM_BLAD = 4 }; /* the socket could not be opened */
#define K1_STREFA_ODM_MAKS 4
/* The number in KLIENT b and WITAJ d: an application (role APLIKACJA) or a zone (role STREFA). */
#define K1_KLIENT_NR_MAKS ((int)K1_APL_MAKS > (int)K1_STREFY_MAKS ? (int)K1_APL_MAKS : (int)K1_STREFY_MAKS)

/* Window states reported to the shell */
enum { K1_OKNO_NOWE = 1, K1_OKNO_AKTYWNE = 2, K1_OKNO_NIEAKTYWNE = 3, K1_OKNO_ZMINIMALIZOWANE = 4, K1_OKNO_ZAMKNIETE = 5 };
#define K1_OKNO_STAN_MAKS 5

/* Settings (key: allowed values) */
enum k1_ustawienie {
    K1_UST_MOTYW = 1,   /* 0 light, 1 dark */
    K1_UST_SEKUNDY = 2, /* clock on the top bar with seconds: 0/1 */
    K1_UST_MYSZ = 3,    /* pointer speed 1..3 */
    K1_UST_TAPETA = 4,  /* wallpaper variant 0..K1_TAPETY-1, or K1_TAPETA_ZDJECIE (only when ekran has a photo) */
    K1_UST_ROZDZIELCZOSC = 5, /* screen mode number 1..K1_TRYBY_MAKS (list: K1_KOM_TRYB) */
    K1_UST_ZDJECIE = 6,       /* 0/1: ekran keeps a wallpaper photo (0.8.0 step 4) — sent by ekran, nobody sets it */
    K1_UST_OTWIERANIE = 7,    /* 7..6+K1_TYPY_MAKS: the default application of file type (key - 6) — one that opens it
                               * (k1_dok_otwiera; 0.8.0, ADR 0030) */
};
#define K1_UST_MAKS (K1_UST_OTWIERANIE + K1_TYPY_MAKS - 1)

/* Wallpaper variants: one palette each in k1_grafika.c, names in Ustawienia (docs/WYGLAD.md). */
#define K1_TAPETY 5
#define K1_TAPETA_ZDJECIE 31 /* the photo set from Pliki (0.8.0 step 4, ADR 0032); never a painted variant */
_Static_assert(K1_TAPETY < K1_TAPETA_ZDJECIE, "k1_komunikat: the photo wallpaper number is taken");

/* Screen modes offered in Ustawienia (distinct sizes, widest first). */
#define K1_TRYBY_MAKS 18
#define K1_TRYB_MIN_SZER 800
#define K1_TRYB_MIN_WYS 600
#define K1_PAMIEC_MAKS_MIB (1 << 24) /* 16 TiB: range check only */

/* skarbiec (M5d): owners (K1_WL_*: K1_PROFIL_* of the client, or the system) and object ids */
#define K1_WL_SYSTEM 100          /* nadzorca itself */
#define K1_OB_MAKS 64             /* object ids 1..64 per owner */
#define K1_OB_USTAWIENIA 1        /* ekran: the settings */
#define K1_OB_NOTATKA 2           /* Notatki: the first note (0.5.8: 1 = the list, 2..33 = notes) */
#define K1_OB_BATERIA 3           /* nadzorca: battery history */
#define K1_OB_ROZMIESZCZENIE 1    /* gui (0.7.5): the desktop arrangement — icons and the bottom bar */
#define K1_OB_ZDJECIE 9           /* ekran (0.8.0 step 4): the wallpaper photo — its size (ekran/zdjecie.c) */
#define K1_OB_ZDJECIE_KAWALEK 10  /* ekran: its pixels, 1 MiB per object: 10..10+K1_ZDJECIE_KAWALKI-1 */
#define K1_ZDJECIE_KAWALKI 12     /* 2560 × 1600 × 3 bytes */
#define K1_SKARB_MAKS (1 << 20)   /* bytes in one object (Blob Vault limit) */
#define K1_SKARB_TRWALY 0         /* saved on the K1-DANE partition */
#define K1_SKARB_RAM 1            /* no (usable) partition: kept in memory until power off */
#define K1_SKARB_ZABLOKOWANY 2    /* the vault needs the password (M5e): nothing readable yet */
#define K1_HASLO_MAKS 256         /* bytes of a password */
#define K1_HASLO_PRZERWA 300      /* longest wait between password attempts, seconds */
#define K1_MAG_WYNIK_MIN (-8)     /* most negative K1_MAG_* result */
#define K1_SKARB_ZLE_HASLO (-1)   /* WYNIK b: wrong password (= K1_MAG_HASLO, magazyn.h) */
#define K1_SKARB_PELNY (-3)       /* WYNIK b: no room left (= K1_MAG_PELNY) */
#define K1_SKARB_IO (-4)          /* WYNIK b, POWIEKSZONO a: writing failed (= K1_MAG_IO) */
#define K1_SKARB_ARG (-8)         /* WYNIK b, POWIEKSZONO a: not possible (= K1_MAG_ARG) */

/* Installer (M5f, ADR 0018) */
#define K1_INST_DYSKI_MAKS 8
enum { K1_INST_INSTALUJ = 1, K1_INST_AKTUALIZUJ = 2 };
#define K1_INST_WERSJA 0x1000000 /* INST_DYSK c: flag "version follows" (major<<16|minor<<8|patch) */
enum { K1_DYSK_SATA = 1, K1_DYSK_NVME = 2, K1_DYSK_VIRTIO = 3, K1_DYSK_INNY = 4 };
enum { K1_INST_ETAP_CZYSZCZENIE = 1, K1_INST_ETAP_KOPIA = 2, K1_INST_ETAP_SPRAWDZANIE = 3, K1_INST_ETAP_PARTYCJE = 4 };
#define K1_INST_ETAP_MAKS 4
#define K1_INST_BLAD_ARG (-1)      /* wrong request, disk gone or changed */
#define K1_INST_BLAD_ZAJETY (-2)   /* the disk is in use */
#define K1_INST_BLAD_MALY (-3)     /* the disk is too small */
#define K1_INST_BLAD_ZAPIS (-4)    /* writing failed */
#define K1_INST_BLAD_SPRAWDZ (-5)  /* what was written reads back differently */
#define K1_INST_BLAD_ZRODLO (-6)   /* no system to copy (not started from a K1 stick) */
#define K1_INST_BLAD_PRZERWANE (-7) /* the writing program ended without a result */
#define K1_INST_BLAD_PODPIS (-8)   /* M6a: no manifest, wrong checksum, or no valid K1 signature */
#define K1_INST_BLAD_STARSZA (-9)  /* M6a: the stick has an older version than the disk */
#define K1_INST_BLAD_MIN (-9)
#define K1_AB_FL_NOWY 1        /* INST_AB d: a new slot waits to be tried on the next start */
#define K1_AB_FL_NIEUDANA 2    /* INST_AB d: the last new slot did not start (GRUB went back) */
enum { K1_SB_WYL = 0, K1_SB_WL = 1, K1_SB_NIEZNANY = 2 };            /* Secure Boot off / on / no UEFI variables */
enum { K1_SBK_BRAK = 0, K1_SBK_JEST = 1, K1_SBK_CZEKA = 2 };         /* key: not enrolled / enrolled / waits for MokManager */
enum { K1_SBP_NIC = 0, K1_SBP_GOTOWE = 1, K1_SBP_BLAD = 2 };          /* the last request: none / written / failed */

/* Battery state (K1_KOM_BATERIA c), from the kernel's power_supply "status". */
enum k1_bateria_stan {
    K1_BAT_NIEZNANY = 0,
    K1_BAT_ROZLADOWANIE = 1, /* Discharging */
    K1_BAT_LADOWANIE = 2,    /* Charging */
    K1_BAT_PELNA = 3,        /* Full */
    K1_BAT_NIE_LADUJE = 4,   /* Not charging (on mains, e.g. charge threshold) */
};
#define K1_BAT_STAN_MAKS 4
/* The battery is low (the top bar and the password screen fill its symbol with terracotta). */
static inline int k1_bateria_niska(int32_t procent, int32_t stan)
{
    return procent >= 0 && procent <= 15 && stan == K1_BAT_ROZLADOWANIE;
}
#define K1_MOC_MAKS_MW 1000000  /* 1 kW: anything above is a firmware error */
#define K1_BAT_MINUT_MAKS (99 * 60)
#define K1_BAT_HIST_MAKS 1440   /* samples of the battery history (nadzorca halves it when full) */
#define K1_BAT_HIST_W_KOM 16    /* samples per K1_KOM_BATERIA_HIST */
#define K1_BAT_ZASILACZ 0x80    /* sample bit: on mains */
#define K1_BAT_BRAK_PROBKI 0xFF

struct k1_komunikat {
    uint8_t wersja;   /* K1_KOMUNIKAT_WERSJA */
    uint8_t typ;      /* enum k1_komunikat_typ */
    uint16_t dlugosc; /* sizeof(struct k1_komunikat) = 32 */
    uint32_t obiekt;
    int32_t a, b, c, d, e;
    uint32_t zarezerwowane; /* must be 0 */
};

_Static_assert(sizeof(struct k1_komunikat) == 32, "k1_komunikat: unexpected size/padding");
_Static_assert(offsetof(struct k1_komunikat, obiekt) == 4, "k1_komunikat: unexpected layout");
_Static_assert(offsetof(struct k1_komunikat, a) == 8, "k1_komunikat: unexpected layout");
_Static_assert(offsetof(struct k1_komunikat, zarezerwowane) == 28, "k1_komunikat: unexpected layout");

/* A size of up to 64 bits in two fields (0.8.1 step 4: files up to K1_DOK_MAKS_BAJTY): low and high 32 bits. */
static inline uint64_t k1_kom_u64(int32_t niskie, int32_t wysokie)
{
    return (uint64_t)(uint32_t)niskie | (uint64_t)(uint32_t)wysokie << 32;
}
static inline int32_t k1_kom_niskie(uint64_t v)
{
    return (int32_t)(uint32_t)v;
}
static inline int32_t k1_kom_wysokie(uint64_t v)
{
    return (int32_t)(uint32_t)(v >> 32);
}

/* Sample `i` (0..15) of a K1_KOM_BATERIA_HIST message. */
static inline unsigned k1_bat_probka(const struct k1_komunikat *k, int i)
{
    const int32_t p[4] = { k->b, k->c, k->d, k->e };
    return ((uint32_t)p[i / 4] >> (8 * (i % 4))) & 0xffu;
}

/* Whether `wartosc` is allowed for setting `klucz` (K1_UST_*): USTAW/USTAWIENIE and the settings
 * ekran reads back from skarbiec (M5d). */
int k1_ustawienie_poprawne(int32_t klucz, int32_t wartosc);

/* Builds a message (all other fields zero). */
struct k1_komunikat k1_komunikat_nowy(enum k1_komunikat_typ typ, uint32_t obiekt, int32_t a, int32_t b, int32_t c,
                                      int32_t d, int32_t e);

/* Validates a received message of `len` bytes arriving on a channel of direction `kierunek`.
 * On success copies it to `out` and returns 0; otherwise returns -1 and zeroes `out`.
 * Never reads beyond `len`, makes no alignment assumptions about `buf`. */
int k1_komunikat_sprawdz(const void *buf, size_t len, enum k1_kanal_kierunek kierunek, struct k1_komunikat *out);

/* Whether a message type carries exactly one file descriptor (SCM_RIGHTS). Types not listed
 * carry none; receivers reject (and close) descriptors attached to any other message. */
int k1_komunikat_z_deskryptorem(uint8_t typ, enum k1_kanal_kierunek kierunek);
/* 1 for a request about files a client sends to skarbiec (K1_KOM_DOK_SPIS..OPROZNIJ, OTWORZ..WYBRANO, KAWALEK, KONIEC). */
int k1_kom_dok_prosba(int typ);

#endif

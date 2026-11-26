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
#include "k1_filmy.h"
#include "k1_pendrive.h"
#include "k1_pliki_strefy.h"
#include "k1_posrednik.h"
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
    K1_KIER_DO_FILMOW = 13,  /* the client (a zone's VMM, nadzorca's question, the test program) → filmy (0.10.9 step 2) */
    K1_KIER_OD_FILMOW = 14,  /* filmy → its client */
    K1_KIER_DO_POSREDNIKA = 15, /* the client (a zone's VMM, nadzorca's question, the test program) → posrednik (0.10.9
                                 * step 5) */
    K1_KIER_OD_POSREDNIKA = 16, /* posrednik → its client */
    K1_KIER_DO_DZWIEKU = 17,    /* a client (gui, the test program) → dzwiek (0.12.0 step 1) */
    K1_KIER_OD_DZWIEKU = 18,    /* dzwiek → its client */
};
#define K1_KIER_MAKS 18

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
    /* 118: START_POTWIERDZONY (0.8.2–0.9.1, the move of a vault of format 1 to format 2) — gone in 0.10.0 */
    K1_KOM_SKARB_WYCZYSC = 168, /* nadzorca → skarbiec (0.10.0 step 5): obiekt=0, a=the owner of a zone (the profile of a
                                 * row of K1_STREFY), b..e=0 — delete every piece of its disk ("Wyczyść dane sesji") */
    K1_KOM_SKARB_WYCZYSZCZONO = 169, /* skarbiec → nadzorca (K1_KIER_DO_NADZORCY): obiekt=0, a=that owner, b=0 or
                                      * K1_MAG_* < 0, c=pieces deleted 0..K1_STREFA_DYSK_KAWALKI, d=e=0 */
    K1_KOM_SKARB_ZWOLNIJ = 222, /* nadzorca → skarbiec (0.10.10 step 1): obiekt=0, a..e=0 — the Instalator installs anew
                                 * on the disk of K1-DANE (the owner typed USUŃ): close the vault and the partition; from
                                 * now on objects only in memory, until the computer is switched off */
    K1_KOM_SKARB_ZWOLNIONO = 223, /* skarbiec → nadzorca (K1_KIER_DO_NADZORCY): obiekt=0, a..e=0 — the partition is closed */
    /* nadzorca → instalator (M5f, ADR 0018; same direction K1_KIER_STEROWANIE): the job, once */
    K1_KOM_INST_ZADANIE = 3, /* a=K1_INST_* mode, b=logical sector size (512/4096) of the target */

    /* client → ekran (obiekt = surface number 1..K1_POW_MAKS unless noted) */
    K1_KOM_POWIERZCHNIA = 10, /* a=width, b=height, c=kind (K1_POW_*), d,e = x,y (panel; desktop: 0); a window: d=0,
                               * e=1 — it REPLACES the window surface obiekt in the size ekran asked for (K1_KOM_ROZMIAR,
                               * 0.10.5 step 4; the window keeps its place, number and focus), e=0 a new one; + 1 fd */
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
    K1_KOM_PRZEWIN = 40,    /* 0.10.0 step 4: the wheel or two fingers over a surface (not only the focused one):
                             * obiekt=surface, a,b = x,y, c,d = across, down in 1/K1_PRZEWIN_ZABEK of a notch (d > 0:
                             * further down, the content goes up; c > 0: to the right; |c|,|d| <= K1_PRZEWIN_MAKS, not
                             * both 0), e = K1_PRZEWIN_KOLKO / K1_PRZEWIN_TOUCHPAD */
    /* WITAJ is sent again when the screen size changes (K1_UST_ROZDZIELCZOSC): clients lay out anew. */

    /* gui / test → nadzorca */
    K1_KOM_URUCHOM = 50,      /* gui: a=aplikacja; Pliki only (0.8.0): b=entry to show (0: none), c=K1_PLIKI_* */
    K1_KOM_WYLACZ = 51,       /* gui: a=0 power off, 1 restart, 2 lock — "Zablokuj", only with a password (gui only) */
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
    K1_KOM_BATERIA_POMIAR = 59, /* also to gui (fd 5, after a change — 0.11.8 step 3: the battery's popover; until then
                                 * ZASOBY_BATERIA): obiekt=0, a=1 battery present, b=percent (-1 unknown), c=K1_BAT_*,
                                 * d=power draw mW (-1 unknown, ≤ K1_MOC_MAKS_MW), e=minutes until empty/full (-1 unknown,
                                 * ≤ 99 h); no battery: a=0, b..e=-1 except c=0 */
    K1_KOM_ZASOBY_KARTA = 224, /* 0.10.10 step 2 (k1_karta_pomiar.h): obiekt=0, a=how busy the card was ‰ (-1 unknown),
                                * b=its clock now MHz, c=its highest clock MHz (-1 unknown, ≤ K1_KARTA_MHZ_MAKS), d=the
                                * card's power draw mW (RAPL "uncore"; -1 unknown, ≤ K1_MOC_MAKS_MW), e=bit nr-1: zone nr
                                * draws with the card (its graphics broker runs) */
    /* 0.10.11 step 2 (ADR 0051): the zone's picture without a copy — a zone's VMM → ekran (role STREFA only) */
    K1_KOM_STREFA_BUFOR = 225, /* obiekt=its window surface, a=the buffer's number 1..K1_STREFA_BUFORY, b=width, c=height
                                * (1..K1_POW_MAKS_BOK), d=bytes of a row 4·b..K1_STREFA_KROK_MAKS (a multiple of 4), e=where
                                * the picture starts in the buffer 0..K1_STREFA_PRZES_MAKS (a multiple of 4); + 1 fd: the
                                * buffer of the card (a dma-buf from the zone's graphics broker), XRGB8888 linear — number a
                                * is that buffer from now on (an earlier one with the number goes) */
    K1_KOM_STREFA_OBRAZ = 226, /* obiekt=its window surface, a=buffer 1..K1_STREFA_BUFORY (sent before), b,c,d,e = damaged
                                * x,y,w,h (w=h=0: all) — the window shows that buffer now, until the next STREFA_OBRAZ or
                                * ZATWIERDZ (its own pixels again); RAMKA when it is on the screen, as after ZATWIERDZ */
    K1_KOM_PELNY_EKRAN = 227,  /* 0.11.0 step 1 (D1): a zone's VMM → ekran (role STREFA only): obiekt=its window surface,
                                * a=1 its guest asks for full screen / 0 it left it, b..e=0 — ekran decides (only right
                                * after the user's gesture in that window, with the focus); its answer is ROZMIAR */
    /* 0.11.0 step 2 (ADR 0052): a file from a zone into Pliki — its VMM ↔ nadzorca (the VMM's fd 7), nadzorca ↔ gui (fd 5
     * and its channel to nadzorca); obiekt of STREFA_PLIK*: the VMM's number of the file 1..K1_PS_NR_MAKS */
    K1_KOM_STREFA_PLIK = 228,         /* VMM → nadzorca: a=b=its size (low, high: 0..K1_PS_ROZMIAR_MAKS), c=the name's length
                                       * 1..K1_DOK_NAZWA-1, d=0 a download / K1's number of a file K1 sent to this zone (a
                                       * changed version of it, step 3), e=0; + 1 fd: a sealed memfd of exactly c bytes, a name of K1
                                       * (the VMM made it one: k1_dok_nazwa_popraw) — the zone has a file to move */
    K1_KOM_STREFA_PLIK_KAWALEK = 229, /* VMM → nadzorca: a=bytes 1..K1_DOK_KAWALEK (all but the last: K1_DOK_KAWALEK), b=its
                                       * number 0..K1_DOK_KAWALKI_MAKS-1, c=1 the last / 0, d=e=0; + 1 fd: a sealed memfd of
                                       * a bytes — only after K1_PLIK_WYSLIJ, the next one only after K1_PLIK_DALEJ */
    K1_KOM_STREFA_PLIK_KONIEC = 230,  /* VMM → nadzorca: a=K1_PS_W_BLAD (the zone broke the rules, its files port closed)
                                       * or K1_PS_W_ZOSTAWIONY (the zone took it back: gone, changed), b..e=0 */
    K1_KOM_STREFA_PLIK_ODP = 231,     /* nadzorca → VMM: a=K1_PLIK_WYSLIJ (send its bytes), K1_PLIK_DALEJ (the next piece),
                                       * K1_PLIK_GOTOWE (its end: b=K1_PS_W_*; b=0 otherwise), c..e=0 */
    K1_KOM_POBRANIE = 232,            /* nadzorca → gui (fd 5): obiekt=zone, a=the file 1..K1_PS_NR_MAKS, b=c=its size
                                       * (low, high: 0..K1_PS_ROZMIAR_MAKS), d=the name's length 1..K1_DOK_NAZWA-1, e=1 a
                                       * changed version of a file K1 sent (step 3: "Zapisać zmiany w oryginale?") / 0; + 1
                                       * fd (as STREFA_PLIK's) — the window asks the user (D2: A) */
    K1_KOM_POBRANIE_DECYZJA = 233,    /* gui (and the test program) → nadzorca: obiekt=zone, a=the file, b=K1_POB_* — "Zapisz
                                       * w Pobranych" (a changed version: "Zapisz" in the original), "Zostaw w przeglądarce"
                                       * / "Nie" (and "OK" of a file too big), "Zapisz jako nowy plik" (a changed one), c..e=0 */
    K1_KOM_POBRANIE_WYNIK = 234,      /* nadzorca → gui (fd 5): obiekt=zone, a=the file, b=K1_PS_W_*, c..e=0 — its window
                                       * goes; when the user chose "Zapisz", gui says why it was not saved */
    /* 0.11.0 step 3 (ADR 0052): a file from Pliki to a zone ("Wyślij do…", "Otwórz w…"); a changed version back */
    K1_KOM_DOK_DO_STREFY = 235,       /* Pliki, gui → skarbiec: obiekt=a visible file, a=zone with a window 1..K1_STREFY_MAKS,
                                       * b=K1_PS_TRYB_* ("Wyślij do…", "Otwórz w…" in a new tab, step 4:
                                       * dropped on its window), c..e=0 — after the user's
                                       * gesture in K1's menu (reply: DOK_WYNIK) */
    K1_KOM_SKARB_DO_STREFY = 236,     /* skarbiec → nadzorca (its control channel): obiekt=the file, a=zone, b=K1_PS_TRYB_*,
                                       * c..e=0; + 1 fd: a sealed memfd of the file's entry (K1_DOK_WPIS_BAJTY) — nadzorca's
                                       * channel K1_WL_DO_STREF may read it now */
    K1_KOM_STREFA_DO_PLIK = 237,      /* nadzorca → VMM (fd 7): obiekt=K1's number of the file 1..K1_PS_NR_MAKS, a=b=its size
                                       * (0..K1_DOK_MAKS_BAJTY), c=the name's length 1..K1_DOK_NAZWA-1, d=K1_PS_TRYB_*, e=0;
                                       * + 1 fd: a sealed memfd of the name */
    K1_KOM_STREFA_DO_KAWALEK = 238,   /* nadzorca → VMM (fd 7): obiekt=the file, a=bytes 1..K1_DOK_KAWALEK, b=the piece's
                                       * number 0..K1_DOK_KAWALKI_MAKS-1, c..e=0; + 1 fd: a sealed memfd of a bytes — only
                                       * after STREFA_DO_ODP DALEJ */
    K1_KOM_STREFA_DO_ODP = 239,       /* VMM → nadzorca (fd 7): obiekt=the file, a=K1_PLIK_DALEJ (the next piece) /
                                       * K1_PLIK_GOTOWE (b=0 in the zone's folder "Z K1", 1 not), c..e=0 */
    /* 0.11.0 step 4 (ADR 0052, T6): dragging between windows — ekran leads it (it knows where the windows are); the
     * source and the target never talk to each other, and the files go through skarbiec (and nadzorca to a zone) */
    K1_KOM_PRZECIAGANIE = 240,        /* Pliki, the shell's desktop → ekran: obiekt=its surface holding the pointer (the left
                                       * button pressed there), a=K1_PZ_* what is dragged, b=how many 1..K1_DOK_MAKS, c=of them
                                       * folders 0..b, d=the first entry (0: none; a standard folder
                                       * 1..K1_FOLDERY_MAKS, a user's entry), e=the folder they
                                       * lie in 0..K1_DOK_ID_MAKS — until the button goes up */
    K1_KOM_PRZECIAGANIE_CEL = 241,    /* the shell's desktop → ekran: obiekt=its surface, a=0 nothing / K1_PC_FOLDER (b=the
                                       * folder 1..K1_DOK_ID_MAKS) / K1_PC_KOSZ (b=0), c..e=0 — where a drop from Pliki on it
                                       * goes (an answer to UPUSZCZANIE UPUSC); ekran tells Pliki (PRZECIAGANIE_KONIEC) */
    K1_KOM_UPUSZCZANIE_CO = 242,      /* ekran → the target (Pliki, the desktop): obiekt=its surface, a..e as PRZECIAGANIE's —
                                       * what comes over it, before its first UPUSZCZANIE */
    K1_KOM_UPUSZCZANIE = 243,         /* ekran → the target: obiekt=its surface, a,b = x,y, c=K1_UP_*, d=K1_MOD_* bits, e=0 */
    K1_KOM_PRZECIAGANIE_GDZIE = 244,  /* ekran → the source: obiekt=its surface, a=1 the pointer is over another window or
                                       * the desktop (the target draws, not the source) / 0 back over its own, b..e=0 */
    K1_KOM_PRZECIAGANIE_KONIEC = 245, /* ekran → the source: obiekt=its surface, a=K1_PC_FOLDER (b=the folder) / K1_PC_KOSZ
                                       * (b=0) / K1_PC_STREFA (b=zone 1..K1_STREFY_MAKS), c=K1_MOD_* bits at the drop, d=e=0 —
                                       * its entries were dropped there: the source does it (Pliki asks, moves, copies) */
    K1_KOM_UPUSC_STREFA = 246,        /* ekran → a zone's VMM: obiekt=its window surface, a,b = x,y in its picture
                                       * 0..K1_PS_POZ_MAKS, c=files 1..K1_PRZ_DO_STREFY_MAKS, d=e=0 — dropped there (the
                                       * copies follow from Pliki: DOK_DO_STREFY "upuść") */
    /* 0.11.0 step 5 (ADR 0052, T8, D5: A): the clipboard — kept by ekran (k1_schowek.h), text only */
    K1_KOM_SCHOWEK_USTAW = 247,       /* a client with the focused window → ekran: obiekt=that window, a=bytes 1..K1_SCHOWEK_MAKS,
                                       * b..e=0; + 1 sealed memfd of them — copied (Ctrl+C in K1; a zone's VMM: copied in it),
                                       * only soon after the user's key or press in that window */
    K1_KOM_SCHOWEK_DAJ = 248,         /* a K1 application with the focused window → ekran: obiekt=that window, a..e=0 — Ctrl+V
                                       * (or "Wklej") right after the user's key or press there; reply SCHOWEK (nothing: empty) */
    K1_KOM_BATERIA_OKIENKO = 250, /* 0.11.8 step 3: gui → nadzorca (its fd 4): obiekt=0, a=K1_BOK_* — the battery's popover
                                   * opened / closed (nadzorca reads the battery every 5 s while it is open), or its button
                                   * "Ustawienia baterii" (open Ustawienia on Bateria); b..e=0 */
    K1_KOM_BATERIA_POKAZ = 251,   /* nadzorca → Ustawienia (its fd 4): obiekt=0, all 0 — show the section Bateria */
    K1_KOM_STREFA_DZWIEK = 47,    /* 0.12.0 step 3 (D6: A): the shell → ekran: obiekt=0, a=the zone 1..K1_STREFY_MAKS,
                                   * b=1 its session plays, c=1 it is muted, d,e=0 — ekran shows the speaker on the
                                   * bar of the zone's window while b = 1 */
    K1_KOM_STREFA_WYCISZ = 48,    /* ekran → the shell (K1_KIER_OD_EKRANU): obiekt=0, a=the zone — the user clicked the
                                   * speaker on its window's bar: mute / unmute its session */
    K1_KOM_GLOSNOSC_POKAZ = 252,  /* 0.12.0 step 2 (D4: A): the shell → ekran (K1_KIER_DO_EKRANU), after a volume key:
                                   * obiekt=0, a=the volume 0..K1_DZW_GLOSNOSC_MAKS, b=1 muted, c=the output
                                   * K1_DZW_WYJSCIE_* (BRAK: no card — then a=b=0), d,e=0 — ekran shows its pill */
    /* 0.12.0 step 1 (ADR 0054): sound — formats and limits: k1_dzwiek.h; obiekt=0 in every one. nadzorca → the sound
     * service (its fd 3, one way: K1_KIER_STEROWANIE) */
    K1_KOM_DZWIEK_KARTA = 4,       /* a=K1_DZW_WEZEL_* (ODTWARZANIE: b=the card, c=its device; KONTROLKI: b=the card, c=0;
                                    * 0..K1_DZW_KARTA_NR_MAKS), d=e=0; + 1 fd: that node of /dev/snd */
    K1_KOM_DZWIEK_KLIENT = 5,      /* a=the client's row 1..K1_DZW_KLIENCI_MAKS, b=its UID 0..65535, c=its zone 1..K1_STREFY_MAKS
                                    * for a client STREFA (step 3), else 0, d,e=0; + 1 fd: the
                                    * service's end of the client's channel (the service checks that UID as the sender) */
    K1_KOM_DZWIEK_BEZ_KARTY = 6,   /* all 0: 5 s after the start there is no card with a playback device */
    /* a client → dzwiek (K1_KIER_DO_DZWIEKU): one stream at a time */
    K1_KOM_DZWIEK_OTWORZ = 7,      /* a=the ring's format K1_DZW_FORMAT_* (0.12.3; 0: S16), b..e=0; + 1 fd: the ring, a memfd
                                    * of exactly k1_dzw_pierscien_bajty(a) sealed against shrinking and growing — a new
                                    * stream (refused or no card: ODTWORZONO a=0, b=1) */
    K1_KOM_DZWIEK_ZAPISANO = 8,    /* a=new frames 1..K1_DZW_PIERSCIEN written after the previous ones (more than the ring
                                    * holds, or no stream: the client loses its channel), b..e=0 */
    K1_KOM_DZWIEK_ZAMKNIJ = 9,     /* a=K1_DZW_PO_ODEGRANIU (the stream ends when what was written played) / K1_DZW_OD_RAZU,
                                    * b..e=0 */
    K1_KOM_DZWIEK_GLOSNOSC = 41,   /* a steering client only (K1_DZW_KLIENCI): a=the volume of all K1 0..K1_DZW_GLOSNOSC_MAKS,
                                    * b=1 muted / 0, c=of which output K1_DZW_WYJSCIE_* (step 2, D5; BRAK: the one
                                    * playing now), d=0 — or (step 3) d=a zone 1..K1_STREFY_MAKS: the volume and the
                                    * mute of its session (c=0), e=0 */
    /* dzwiek → its client (K1_KIER_OD_DZWIEKU) */
    K1_KOM_DZWIEK_STAN = 42,       /* a=K1_DZW_KARTA_*, b=the volume 0..K1_DZW_GLOSNOSC_MAKS, c=1 muted, d=K1_DZW_WYJSCIE_*
                                    * (BRAK unless a=JEST, then not BRAK; step 2: SLUCHAWKI while something is in their
                                    * jack), e=0 — b, c of that output; first, and after every change */
    K1_KOM_DZWIEK_ODTWORZONO = 43, /* a=frames taken from the client's ring since the last 0..K1_DZW_PIERSCIEN (room for as
                                    * many new ones), b=1 its stream ended (a may be 0) / 0 (then a ≥ 1), c..e=0 */
    /* nadzorca → gui (its fd 5) */
    K1_KOM_DZWIEK_SESJA = 46,      /* step 3 (D6: A), to a steering client: a=the zone 1..K1_STREFY_MAKS, b=1 it plays
                                    * (K1_DZW_GRA_S), c=its volume 0..100, d=1 muted, e=0 — after every change */
    K1_KOM_DZWIEK_KANAL = 44,      /* all 0; + 1 fd: gui's end of its channel to dzwiek (again after the service restarts) */
    /* the test program → nadzorca (test image only) */
    K1_KOM_TEST_DZWIEK = 45,       /* all 0 → TEST_GNIAZDO + 1 fd: a channel to dzwiek as the client TEST (its UID 0) */
    K1_KOM_SCHOWEK = 249,             /* ekran → a client: obiekt=its window, a=bytes 1..K1_SCHOWEK_MAKS, b=where it was copied
                                       * (0 K1, a zone's number), c..e=0; + 1 sealed memfd — to an application: the answer to
                                       * SCHOWEK_DAJ; to a zone's VMM: right before Ctrl+V goes to its window (D5: A) */
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
                               * other slot (0: none), c=slot it runs (1=A, 2=B), d=K1_AB_FL_* flags, e=1 the manifest of
                               * the slot it runs is signed (0.10.5) */
    K1_KOM_SESJA = 79,        /* to gui (fd 5, 0.7.2; 0.11.5): obiekt=0, a=1 the session can be locked (the vault is open
                               * and has a password: the Start menu offers "Zablokuj"), a=0 it cannot; b..e=0 */
    K1_KOM_SB_STAN = 77,      /* obiekt=0, a=K1_SB_* Secure Boot, b=K1_SBK_* the K1 key, c=1 release key (0: developer
                               * key — never enrolled), d=K1_SBP_* result of the last SB_KLUCZ */
    /* Instalator window → nadzorca */
    K1_KOM_SB_KLUCZ = 78,     /* obiekt=0: prepare the enrolment of the K1 key (MokNew + MokAuth for MokManager) */
    /* 0.10.5 step 1: updates moved to Ustawienia → Aktualizacje (KANAL_AKTUALIZACJI: Ustawienia's fd 7 — INST_DYSKI,
     * INST_START with K1_INST_AKTUALIZUJ only, WYLACZ a=1 after a finished update; back INST_DYSK, INST_AB, INST_LISTA,
     * INST_POSTEP, INST_WYNIK) */
    K1_KOM_AKT_POKAZ = 185,   /* nadzorca → Ustawienia: obiekt=0, all 0 — show the section Aktualizacje */
    K1_KOM_OTWORZ_AKTUALIZACJE = 186, /* Instalator, gui → nadzorca: obiekt=0, all 0 — open Ustawienia on Aktualizacje */
    K1_KOM_AB_NIEUDANA = 187, /* nadzorca → gui (fd 5): obiekt=0, a=the version that did not start (0: not known), b=the
                               * version that runs (0: not known) — a try A/B did not start, the previous version runs */
    /* 0.10.5 step 2b (ADR 0046): updates through the network — nadzorca → Ustawienia (fd 7) and gui (fd 5) */
    K1_KOM_AKT_STAN = 189,    /* obiekt=0, a=the version that runs (0: not known), b=waiting for "Zainstaluj" (0: none),
                               * c=a newer one in the newest release (0: none), d=the previous one, to go back to (0:
                               * none), e=K1_AKT_FL_* */
    K1_KOM_AKT_ETAP = 190,    /* obiekt=0, a=K1_AKT_E_* what runs now, b=percent 0..100, c=K1_AKT_W_* the last check,
                               * d=when it was (s since 1970, 0: never), e=the last job's result (0 or K1_INST_BLAD_*) */
    K1_KOM_AKT_OPIS = 191,    /* obiekt=0, a=bytes 1..K1_OGL_MAKS, b..e=0; + 1 fd: a sealed memfd with the announcement
                               * (k1_ogloszenie.h) the writing program accepted — what Ustawienia and gui describe */
    K1_KOM_AKT_PROSBA = 192,  /* Ustawienia (fd 7), gui (fd 5: ZAINSTALUJ only) → nadzorca: obiekt=0, a=K1_AKT_P_*,
                               * b..e=0 */
    K1_KOM_ROZMIAR = 193,     /* ekran → a zone's VMM (0.10.5 step 4, T12): obiekt=its window surface, a=width
                               * K1_OKNO_MIN_SZER..K1_POW_MAKS_BOK (until 0.11.0 a multiple of 8), b=height
                               * K1_OKNO_MIN_WYS..K1_POW_MAKS_BOK, c=K1_ROZMIAR_* (0.11.0: also the full screen), d=e=0 —
                               * the picture's new size: the user resized or maximized the window on K1's frame. Only
                               * ekran says it; the zone answers with POWIERZCHNIA e=1 of exactly that size (anything else
                               * is refused) and never asks for a size or a place itself (full screen: PELNY_EKRAN) */
    K1_KOM_WYLACZANIE = 194,  /* nadzorca → ekran (its control channel; 0.10.7 step 6, T7): obiekt=0, a=0 power off / 1 restart,
                               * b..e=0 — K1 is powering off: ekran covers everything with the screen "Wyłączanie" (or
                               * "Ponowne uruchamianie") until the end and gives no input to the windows. Once; not on log out */
    K1_KOM_INST_SIEC = 188,   /* writing program → nadzorca (0.10.5 step 2, after STAN): obiekt=0, a=the version that runs
                               * (1..), b=the version waiting in the waiting room (0: none), c=the newer version to
                               * download (0: none), d=K1_SIEC_FL_*, e=the previous version in the free slot (0: none) */
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
    /* 0.10.0 step 5: the sessions as the user set them (nadzorca keeps them: strefy_ust.c) — nadzorca → gui (fd 5),
     * Ustawienia (fd 4) and ekran (its control channel, K1_KIER_STEROWANIE); after STREFA_NAZWA of every zone */
    K1_KOM_STREFY_STAN = 170,    /* obiekt=0, a=bit nr-1: zone nr remembers (STALA), b=1 the computer has virtualization,
                                  * c=1 the vault is on a disk (else no zone remembers: a=0), d=bit nr-1: zone nr runs,
                                  * e=bit nr-1: zone nr is closing (0.10.2 step 1: its window is gone, its machine ends
                                  * in the background — only a running zone: e ⊆ d) */
    K1_KOM_STREFA_NAZWA = 172,   /* obiekt=zone 1..K1_STREFY_MAKS, a..e=its session's name, K1_STREFA_NAZWA_MAKS bytes
                                  * (k1_strefa_nazwa_dobra); nadzorca → the three above, and Ustawienia → nadzorca: the
                                  * user renamed it */
    /* Ustawienia → nadzorca (fd 4), Ustawienia → Przeglądarka */
    K1_KOM_STREFA_TRWALOSC = 171, /* obiekt=zone, a=K1_STALA / K1_JEDNORAZOWA (from its next start), b..e=0 */
    K1_KOM_STREFA_WYCZYSC = 173,  /* obiekt=zone, all 0: delete its session's data (not while it runs) */
    K1_KOM_STREFA_WYCZYSZCZONO = 174, /* nadzorca → Ustawienia: obiekt=zone, a=K1_STREFA_WYCZ_*, b=pieces deleted
                                       * 0..K1_STREFA_DYSK_KAWALKI, c..e=0 */
    /* 0.10.0 step 2 (ADR 0043 T9): the browser's connections — the zone's VMM ↔ nadzorca (VMM's fd 7; the sender's UID
     * is the zone's) */
    K1_KOM_STREFA_POLACZ = 165,   /* from the VMM: obiekt=its connection 1..K1_STREFA_POLACZENIA_MAKS; TCP: a=IPv4 address
                                   * (k1_siec_serwer_dobry), b=port 1..65535, c=K1_STREFA_TCP; the DNS server: a=b=0,
                                   * c=K1_STREFA_DNS; 0.10.5: the download zone's way to the writing program: a=b=0,
                                   * c=K1_STREFA_POBRANE; d=e=0 */
    K1_KOM_STREFA_GNIAZDO = 166,  /* to the VMM: obiekt=the connection, a=K1_STREFA_TCP/DNS/POBRANE, b..e=0; + 1 fd: a
                                   * TCP socket connecting (non-blocking), a UDP socket connected to the DNS server, port
                                   * 53, or (POBRANE) a stream socket to the writing program */
    K1_KOM_STREFA_ODMOWA = 167,   /* to the VMM: obiekt=the connection, a=K1_STREFA_ODM_* why not, b..e=0 (no fd) */
    /* 0.10.1 step 2 (ADR 0044 T3): the zone's memory and its balloon — on the same channel (VMM's fd 7) */
    K1_KOM_STREFA_PAMIEC = 175,   /* from the VMM, at most once a second: obiekt=0, a=the guest's memory K1 holds now (MiB),
                                   * b=available in the guest by its own statistics (-1: none yet), c=in the balloon,
                                   * d=what K1 asks in it, e=handed back to K1 since the start (MiB; each 0..
                                   * K1_STREFA_PAMIEC_MAKS but e: 0..INT32_MAX) */
    K1_KOM_STREFA_BALON = 176,    /* to the VMM (and from the test program to nadzorca): obiekt=the zone, a=the balloon's
                                   * target 0..K1_STREFA_PAMIEC_MAKS MiB, b..e=0 */
    /* 0.10.2 step 2 (ADR 0045): the screen's description for the tests (k1_opis.h) */
    K1_KOM_OPIS = 182,             /* a client → ekran, only while K1_UST_OPIS = 1: obiekt=its surface, a=records
                                    * 0..K1_OPIS_MAKS, b..e=0; + 1 fd (memfd: k1_opis.h, the surface's elements) */
    K1_KOM_OPIS_PYTANIE = 183,     /* the test client (role TEST) → ekran: obiekt=0, all 0 — what is on the screen? */
    K1_KOM_OPIS_EKRANU = 184,      /* ekran → the test client: obiekt=0, a=records 0..K1_OPIS_EKRAN_MAKS, b, c=the
                                    * screen's size, d=e=0; + 1 fd (memfd: k1_opis.h) */
    /* 0.10.2 step 1: closing in the background — on the same channel (VMM's fd 7) */
    K1_KOM_STREFA_ZAMYKA = 180,    /* from the VMM: obiekt=0, all 0 — the user closed it (its window is gone already), its
                                    * machine ends in the background (the guest saves its session) */
    K1_KOM_STREFA_ZAMKNIJ = 181,   /* to the VMM: obiekt=the zone, all 0 — close as the user would (K1 powers off: nadzorca
                                    * waits for the zones before it stops the rest) */
    /* 0.10.1 step 3 (T6, the owner's D3): the computer's memory for K1 and every zone */
    K1_KOM_STREFA_FOKUS = 177,     /* gui → nadzorca (its fd 4): obiekt=0, a=the zone whose window has the focus
                                    * 0..K1_STREFY_MAKS (0: none), b..e=0 — a hint for the rozdzielacz, nothing more */
    K1_KOM_STREFA_PRZYDZIAL = 178, /* nadzorca → Zasoby (fd 4, once a second) and gui (fd 5, right before MALO_PAMIECI):
                                    * obiekt=a running zone, a=what it uses (the guest's memory K1 holds), b=what it may
                                    * get (its upper limit minus K1's target of its balloon), MiB 0..K1_STREFA_PAMIEC_MAKS;
                                    * c=1 its window has the focus (0, 1); d=e=0 */
    K1_KOM_MALO_PAMIECI = 179,     /* nadzorca → gui (fd 5): obiekt=0, a=MiB K1 has free 0..K1_PAMIEC_MAKS_MIB, b=bit nr-1:
                                    * zone nr is in the window (bits of zones 1..K1_STREFY_MAKS only, not 0), c..e=0 */
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
    /* 0.10.9 step 2 (ADR 0049 pkt 3): the film decoder — its client → filmy (K1_KIER_DO_FILMOW; formats: k1_filmy.h) */
    K1_KOM_FILMY_OKNO = 195,       /* obiekt=0, all 0; + 1 fd: the window, a memfd of K1_FILMY_OKNO bytes sealed against
                                    * shrinking and growing (once; the decoder checks size and seals) */
    K1_KOM_FILMY_OTWORZ = 196,     /* obiekt=film 1..K1_FILMY_FILMOW_MAKS, a=codec 1..K1_KODEKI_MAKS, b,c=width, height
                                    * K1_FILMY_MIN_WH..K1_FILMY_MAKS_W/_H, d=surfaces 1..K1_FILMY_KLATEK_MAKS, e=0 */
    K1_KOM_FILMY_KLATKA = 197,     /* obiekt=film, a=its surface to decode into 0..K1_FILMY_KLATEK_MAKS-1, b=the request's
                                    * length in the window sizeof(k1_filmy_naglowek)..K1_FILMY_PROSBA_MAKS, c..e=0 */
    K1_KOM_FILMY_POBIERZ = 198,    /* obiekt=film, a=surface: its picture into the window (reply FILMY_OBRAZ), b..e=0 */
    K1_KOM_FILMY_ZAMKNIJ = 199,    /* obiekt=film, all 0: the film ends, its buffers go back */
    /* filmy → its client (K1_KIER_OD_FILMOW) */
    K1_KOM_FILMY_UMIE = 200,       /* first, once: obiekt=0, a=bit k-1: codec k of K1_KODEKI the card decodes (only with
                                    * b=UMIE, then not 0), b=K1_FILMY_KARTA_BRAK/NIC/UMIE, c=VA status 0..255 (NIC; else 0),
                                    * d,e=the largest width, height of the card and K1 (UMIE; else 0) */
    K1_KOM_FILMY_WYNIK = 201,      /* obiekt=film 0..K1_FILMY_FILMOW_MAKS (OKNO: 0), a=the request's type (FILMY_OKNO..
                                    * ZAMKNIJ), b=0 or K1_FILMY_BLAD_*, c..e=0 */
    K1_KOM_FILMY_OBRAZ = 202,      /* obiekt=film, a=surface, b,c=width, height (the film's), d=k1_filmy_rzad(b), e=0: the
                                    * frame (NV12) at K1_FILMY_PROSBA_MAKS in the window */
    /* nadzorca → Ustawienia (fd 4): what the card of this computer decodes (Ustawienia → Przeglądarka) */
    K1_KOM_FILMY_KARTA = 203,      /* obiekt=0, a=bits as FILMY_UMIE (only with b=UMIE), b=K1_FILMY_KARTA_*, c=the code
                                    * 0..255 (BLAD: exit code or 128+signal; NIC: VA status; else 0), d=e=0 */
    /* ekran → nadzorca (its control channel, K1_KIER_DO_NADZORCY; 0.10.9 step 2, S3: a new version is confirmed only
     * after its picture showed — with i915 a black screen would otherwise be confirmed) */
    K1_KOM_EKRAN_OBRAZ = 205,      /* obiekt=0, a=1: the first frame is on the screen (a page flip completed; simpledrm:
                                    * the mode set), b..e=0 — once after every start of ekran */
    /* the test program → nadzorca (test image only) */
    K1_KOM_TEST_FILMY = 204,       /* obiekt=0, a=zone 1..K1_STREFY_MAKS, b=0 the card / 1 the test decoder (grey frames,
                                    * no card), c..e=0 → TEST_GNIAZDO + 1 fd: the client's end of a new decoder of that zone */
    /* 0.10.9 step 5 (ADR 0049 pkt 7): the graphics broker — its client → posrednik (K1_KIER_DO_POSREDNIKA; formats and
     * limits: k1_posrednik.h). res_id: the guest's resource number 1..K1_POSR_ZASOBY_MAKS-1. Every request has one
     * answer: POSR_WYNIK, or POSR_DMABUF for POSR_BUFOR done; POSR_PLOT is answered when its fence is done. */
    K1_KOM_POSR_OKNO = 206,        /* obiekt=0, all 0; + 1 fd: the window, a memfd of K1_POSR_OKNO bytes sealed against
                                    * shrinking and growing (once) */
    K1_KOM_POSR_WSPOLNA = 207,     /* obiekt=the guest context 1..K1_POSR_GOSCIE (step 6), a=its size K1_POSR_WSPOLNA_MIN..
                                    * MAKS (a multiple of 4096), b..e=0; + 1 fd: the shared memory of that context's bridge
                                    * (its blob 0), sealed the same way (once per guest context, until POSR_GOSC_KONIEC) */
    K1_KOM_POSR_PROSBY = 208,      /* obiekt=the guest context, a=the stream's length in the window 16..K1_POSR_PROSBY_BAJTY
                                    * (a multiple of 4), b..e=0: do the requests one by one (the answer: c = how many) */
    K1_KOM_POSR_BLOB = 209,        /* obiekt=res_id, a=blob 1.., b,c=the size (k1_kom_u64) 1..K1_POSR_BUFOR_MAKS, d=the guest
                                    * context 1..K1_POSR_GOSCIE, e=0: the buffer that context created as blob a becomes
                                    * resource obiekt */
    K1_KOM_POSR_BUFOR = 210,       /* obiekt=res_id, all 0: share the buffer (answer POSR_DMABUF) */
    K1_KOM_POSR_ZWOLNIJ = 211,     /* obiekt=res_id, all 0: the resource goes, its buffer too */
    K1_KOM_POSR_PLOT = 212,        /* obiekt=the timeline 0..K1_POSR_PIERSCIENIE-1, a,b=the fence's number (k1_kom_u64),
                                    * c=the guest context, d=e=0: answer POSR_PLOT_GOTOWY when the last batch of that
                                    * context's timeline is done */
    /* posrednik → its client (K1_KIER_OD_POSREDNIKA) */
    K1_KOM_POSR_UMIE = 213,        /* first, once: obiekt=0, a=K1_POSR_KARTA_BRAK/NIE/UMIE, b=K1_POSR_NIE_* (NIE; else 0),
                                    * c=the card's PCI id 1..0xffff, d=its revision 0..255, e=the driver's version
                                    * major << 16 | minor << 8 | patch (c..e: UMIE only; else 0) */
    K1_KOM_POSR_WYNIK = 214,       /* obiekt=as the request, a=the request's type (POSR_OKNO..POSR_ZWOLNIJ, GOSC_KONIEC), b=0
                                    * or K1_POSR_BLAD_*, c=PROSBY: the requests done 0..K1_POSR_PROSBY_BAJTY/16 (else 0),
                                    * d=e=0 */
    K1_KOM_POSR_DMABUF = 215,      /* obiekt=res_id, a=K1_POSR_MAPA_*, b,c=the size (k1_kom_u64), d=e=0; + 1 fd: the buffer
                                    * (dma-buf: the VMM maps it into the guest — never executable) */
    K1_KOM_POSR_PLOT_GOTOWY = 216, /* obiekt=the timeline, a,b=the fence's number, c=the guest context, d=e=0 */
    /* nadzorca → Ustawienia (fd 4): whether this computer's card serves fast graphics (Ustawienia → Przeglądarka) */
    K1_KOM_POSR_KARTA = 217,       /* obiekt=0, a=K1_POSR_KARTA_*, b=the code (NIE: K1_POSR_NIE_*; BLAD: exit code or 128 +
                                    * signal 0..255; else 0), c..e=0 */
    /* the test program → nadzorca (test image only) */
    K1_KOM_TEST_POSREDNIK = 218,   /* obiekt=0, a=zone 1..K1_STREFY_MAKS, b=0 the card / 1 the test engine (no card), c..e=0
                                    * → TEST_GNIAZDO + 1 fd: the client's end of a new broker of that zone */
    /* 0.10.9 step 6: its client → posrednik */
    K1_KOM_POSR_GOSC_KONIEC = 219, /* obiekt=the guest context 1..K1_POSR_GOSCIE, all 0: that process of the zone closed the
                                    * card — its contexts, address spaces, shared memory and unnamed buffers go */
    /* 0.10.9 step 6 (D5, D7, D9 „A”): fast graphics and films with the card, a switch of each session (strefy_ust.c) —
     * nadzorca → gui, Ustawienia, Zasoby (K1_KIER_OD_NADZORCY) and ekran (K1_KIER_STEROWANIE), after STREFY_STAN */
    K1_KOM_STREFY_GRAFIKA = 220,   /* obiekt=0, a=bit nr-1: the switch of zone nr's session is on, b=bit nr-1: zone nr runs
                                    * with its graphics broker now (the mark on its frame — D7), c..e=0 */
    /* Ustawienia → nadzorca (fd 4) */
    K1_KOM_STREFA_GRAFIKA = 221,   /* obiekt=zone, a=1 on / 0 off (from its next start), b..e=0 */
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
/* ROZMIAR c: how the zone's window shows (0.10.5 step 4; 0.11.0 step 1: full screen — the whole screen, no K1 frame) */
enum { K1_ROZMIAR_ZWYKLY = 0, K1_ROZMIAR_ZMAKSYMALIZOWANY = 1, K1_ROZMIAR_PELNY = 2 };
/* A zone's window shown from buffers of the card (0.10.11 step 2, ADR 0051 T4): at most this many per window (double or
 * triple buffering of the browser), a row and the start inside a buffer within these limits. */
#define K1_STREFA_BUFORY 4
#define K1_STREFA_KROK_MAKS (4 * K1_POW_MAKS_BOK * 2)
#define K1_STREFA_PRZES_MAKS (1 << 29)
/* 0.11.0 step 2 (ADR 0052): nadzorca's answers to a zone's VMM about a file (STREFA_PLIK_ODP a) */
enum { K1_PLIK_WYSLIJ = 1, K1_PLIK_DALEJ = 2, K1_PLIK_GOTOWE = 3 };
/* POBRANIE_DECYZJA b (0.11.0 steps 2, 3) */
enum { K1_POB_ZOSTAW = 0, K1_POB_ZAPISZ = 1, K1_POB_JAKO_NOWY = 2 };

/* Pointer events */
/* What Pliki show when the desktop opens them (URUCHOM c, PLIKI_POKAZ b): the entry's place, Kosz, the entry with its
 * name field open. */
enum { K1_PLIKI_MIEJSCE = 0, K1_PLIKI_KOSZ = 1, K1_PLIKI_NAZWA = 2 };
enum { K1_WSK_RUCH = 1, K1_WSK_WCISNIECIE = 2, K1_WSK_PUSZCZENIE = 3, K1_WSK_WYJSCIE = 4 };
enum { K1_UP_RUCH = 1, K1_UP_WYJSCIE = 2, K1_UP_UPUSC = 3 }; /* UPUSZCZANIE (0.11.0 step 4): over it, gone, dropped */
enum { K1_PRZYCISK_BRAK = 0, K1_PRZYCISK_LEWY = 1, K1_PRZYCISK_PRAWY = 2, K1_PRZYCISK_SRODKOWY = 3 };

/* Scrolling (K1_KOM_PRZEWIN; the same units and sources as K1_ZD_PRZEWIJANIE in k1_zdarzenie.h) */
#define K1_PRZEWIN_ZABEK 120
#define K1_PRZEWIN_MAKS (64 * K1_PRZEWIN_ZABEK) /* ekran adds up what a client has not taken yet */
enum { K1_PRZEWIN_KOLKO = 1, K1_PRZEWIN_TOUCHPAD = 2 };

/* Keyboard modifiers */
#define K1_MOD_SHIFT 1
#define K1_MOD_CTRL 2
#define K1_MOD_ALT 4
#define K1_MOD_ALTGR 8
#define K1_MOD_MASKA 15
#define K1_KLAWISZ_MAKS 0x2ff /* KEY_MAX */

/* 0.10.0 (ADR 0042): why a zone did not start (K1_KOM_STREFA_STAN b); since step 3 (S26) also why it ended without
 * the user closing it: its program said so (PAMIEC: the guest ran out of memory, AWARIA: it crashed — also the VMM's
 * exit code), K1 killed it for lack of memory in the computer (PAMIEC_K1), or it stopped without a word (BLAD); since
 * 0.10.1 step 1 (S3): the session used all the memory K1 gives it — its sandbox's own limit (LIMIT) */
enum { K1_STREFA_BEZ_WIRTUALIZACJI = 1, K1_STREFA_ZA_MALO_PAMIECI = 2, K1_STREFA_BRAK_OBRAZU = 3,
       K1_STREFA_KONIEC_PAMIEC = 4, K1_STREFA_KONIEC_PAMIEC_K1 = 5, K1_STREFA_KONIEC_AWARIA = 6, K1_STREFA_KONIEC_BLAD = 7,
       K1_STREFA_KONIEC_LIMIT = 8 };
#define K1_STREFA_STAN_MAKS 8
#define K1_STREFA_KONIEC_OD K1_STREFA_KONIEC_PAMIEC /* b >= this: the zone ended (not: did not start) */
/* The guest's test modes (URUCHOM_STREFE b; the test image only, gosc/gosc.h): 1..4 hostile guests (a broken queue —
 * gosc/zlosliwy.c), 5 an honest one that counts its starts on the disk; 0.10.1 step 2 (ADR 0044): 6..8 hostile to the
 * balloon, 9 an honest one that takes memory and gives it back (the balloon's scenario of test-strefa); step 3: 10;
 * 0.10.2 step 1: 11 an honest one with a "window" like the browser's — its first picture at once, "etap okno" later,
 * slow to close (it saves something on its disk, when it has one) */
enum { K1_STREFA_TRYB_DYSK = 5, K1_STREFA_TRYB_BALON_OD = 6, K1_STREFA_TRYB_BALON_DO = 8, K1_STREFA_TRYB_PAMIEC = 9,
       K1_STREFA_TRYB_ROZDZIELACZ = 10 }; /* 0.10.1 step 3: an honest guest that keeps its memory free and waits — its
                                           * balloon is the rozdzielacz's, as a session's (test-strefa) */
enum { K1_STREFA_TRYB_OKNO = 11 };
/* 0.10.5 step 2 (ADR 0046): the download zone in the test of updates — the test server on QEMU's "router" (an address
 * given as numbers is allowed, its certificate is not checked: the signature of K1 is) */
enum { K1_STREFA_TRYB_AKTUALIZACJA = 12 };
/* 0.10.5 step 5 (Raptor Bridge): 13 the test of the bridge from inside the zone (gosc/most.c), 14..15 hostile to vsock —
 * the guest speaks vsock itself (gosc/zlosliwy.c) */
enum { K1_STREFA_TRYB_MOST = 13, K1_STREFA_TRYB_VSOCK_OD = 14, K1_STREFA_TRYB_VSOCK_DO = 15 };
/* 0.10.9 step 6: fast graphics in the test zone — nadzorca gives its VMM a graphics broker with the test engine (no
 * card): 16 an honest guest drives the bridge (gosc/grafika.c), 17 a hostile one (bad requests, a flood, too much
 * memory, a hang of the "card") — the broker refuses or ends, the zone and K1 live */
enum { K1_STREFA_TRYB_GRAFIKA = 16, K1_STREFA_TRYB_GRAFIKA_ZLA = 17 };
/* 0.11.0 step 2 (ADR 0052): files from the zone into Pliki — honest (the test clicks the window) and hostile (the test
 * program says "Zapisz" to each) — scripts/test-pobrane.sh */
enum { K1_STREFA_TRYB_POBRANIE = 18, K1_STREFA_TRYB_POBRANIE_ZLE = 19 };
enum { K1_STREFA_TRYB_ODBIOR = 20 }; /* 0.11.0 step 3: files from K1 to the zone and back changed */
enum { K1_STREFA_TRYB_DZWIEK = 21, K1_STREFA_TRYB_DZWIEK_ZLY = 22 }; /* 0.12.0 step 3: a tone; hostile to the sound card */
#define K1_STREFA_TRYB_MAKS 22
/* 0.10.5 step 4 (D8): the smallest picture of a zone's window the user can drag it to */
#define K1_OKNO_MIN_SZER 640
#define K1_OKNO_MIN_WYS 480
#define K1_STREFA_PAMIEC_MAKS 262144 /* MiB: the largest memory of a zone (0.10.1 step 2, ADR 0044 T5; 0.11.3: 256 GiB,
                                     * was 64 GiB) */
/* 0.10.0 step 2 (ADR 0043 T9): the zones' connections (STREFA_POLACZ c, STREFA_GNIAZDO a, STREFA_ODMOWA a) */
#define K1_STREFA_POLACZENIA_MAKS 128 /* at once in one zone (the relay in the zone has as many) */
enum { K1_STREFA_TCP = 0, K1_STREFA_DNS = 1, K1_STREFA_POBRANE = 2 /* 0.10.5: the download zone's bytes to K1 */ };
enum { K1_STREFA_ODM_ADRES = 1,  /* not allowed (D3: the home network) */
       K1_STREFA_ODM_SIEC = 2,   /* K1 has no network (or no DNS server from the router) */
       K1_STREFA_ODM_LIMIT = 3,  /* too many connections in a short time */
       K1_STREFA_ODM_BLAD = 4 }; /* the socket could not be opened */
#define K1_STREFA_ODM_MAKS 4
/* 0.10.0 step 5: the sessions (STREFA_NAZWA, STREFA_WYCZYSZCZONO a) */
#define K1_STREFA_NAZWA_MAKS 20 /* bytes of a session's name, UTF-8, zero padded */
enum { K1_STREFA_WYCZ_OK = 0, K1_STREFA_WYCZ_DZIALA = 1 /* it runs: close it first */, K1_STREFA_WYCZ_BLAD = 2 };
#define K1_STREFA_WYCZ_MAKS 2
/* A session's name: 1..K1_STREFA_NAZWA_MAKS bytes of UTF-8 then zeros; no control or invisible characters, none
 * that turn the text's direction, no space at either end. 1: good. */
int k1_strefa_nazwa_dobra(const char n[K1_STREFA_NAZWA_MAKS]);
/* `dl` bytes of UTF-8 without control, invisible or direction-turning characters (names; 0.10.2: k1_opis.c). */
int k1_napis_dobry(const char *n, size_t dl);
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
    K1_UST_OPIS = K1_UST_OTWIERANIE + K1_TYPY_MAKS, /* 0/1 (0.10.2 step 2, ADR 0045): a test client is connected —
                               * send ekran your elements (K1_KOM_OPIS); sent by ekran, nobody sets it, never saved */
};
#define K1_UST_MAKS K1_UST_OPIS

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
#define K1_WL_POBRANIA 102        /* nadzorca's channel for files from the zones into Pobrane (0.11.0 step 2, ADR 0052) */
#define K1_WL_DO_STREF 103        /* nadzorca's channels for files sent to a zone and their changed versions (step 3) */
#define K1_OB_MAKS 64             /* object ids 1..64 per owner */
#define K1_OB_USTAWIENIA 1        /* ekran: the settings */
#define K1_OB_NOTATKA 2           /* Notatki: the first note (0.5.8: 1 = the list, 2..33 = notes) */
#define K1_OB_BATERIA 3           /* nadzorca: battery history */
#define K1_OB_ROZMIESZCZENIE 1    /* gui (0.7.5): the desktop arrangement — icons and the bottom bar */
#define K1_OB_GLOSNOSC 2          /* gui (0.12.0 step 2, D5): the volume and the mute of each output (k1_dzwiek.h) */
#define K1_OB_ZDJECIE 9           /* ekran (0.8.0 step 4): the wallpaper photo — its size (ekran/zdjecie.c) */
#define K1_OB_OKNA_STREF 2        /* ekran (0.10.5 step 4): each session's window size and maximized (ekran/rozmiar.c) */
#define K1_OB_ZDJECIE_KAWALEK 10  /* ekran: its pixels, 1 MiB per object: 10..10+K1_ZDJECIE_KAWALKI-1 */
#define K1_ZDJECIE_KAWALKI 12     /* 2560 × 1600 × 3 bytes */
/* 0.10.0 step 5 (T11): the disk of a zone that remembers — pieces of K1_STREFA_DYSK_KAWALEK, piece i is object
 * K1_OB_DYSK + i of the zone's owner (the profile of its row of K1_STREFY); only a zone's VMM may use them (skarbiec
 * checks the owner). A piece never written is read from the zone's empty disk (/rdzen/strefy/dysk.img). */
#define K1_OB_DYSK 65536u
#define K1_STREFA_DYSK_KAWALEK (64u << 10)
#define K1_STREFA_DYSK_KAWALKI 32768u /* 2 GiB */
/* An object id a client may name: 1..K1_OB_MAKS, or a piece of a zone's disk. */
static inline int k1_ob_poprawny(uint32_t ob)
{
    return (ob >= 1 && ob <= K1_OB_MAKS) || (ob >= K1_OB_DYSK && ob - K1_OB_DYSK < K1_STREFA_DYSK_KAWALKI);
}
#define K1_SKARB_MAKS (1 << 20)   /* bytes in one object (Blob Vault limit) */
#define K1_SKARB_TRWALY 0         /* saved on the K1-DANE partition */
#define K1_SKARB_RAM 1            /* no (usable) partition: kept in memory until power off */
#define K1_SKARB_ZABLOKOWANY 2    /* the vault needs the password (M5e): nothing readable yet */
#define K1_HASLO_MAKS 256         /* bytes of a password */
#define K1_HASLO_PRZERWA 300      /* longest wait between password attempts, seconds */
#define K1_MAG_WYNIK_MIN (-8)     /* most negative K1_MAG_* result */
#define K1_SKARB_ZLE_HASLO (-1)   /* WYNIK b: wrong password (= K1_MAG_HASLO, magazyn.h) */
#define K1_SKARB_PELNY (-3)       /* WYNIK b: no room left (= K1_MAG_PELNY) */
#define K1_SKARB_BRAK (-6)        /* WYNIK b: no such object (= K1_MAG_BRAK) */
#define K1_SKARB_IO (-4)          /* WYNIK b, POWIEKSZONO a: writing failed (= K1_MAG_IO) */
#define K1_SKARB_ARG (-8)         /* WYNIK b, POWIEKSZONO a: not possible (= K1_MAG_ARG) */

/* Installer (M5f, ADR 0018) */
#define K1_INST_DYSKI_MAKS 8
enum { K1_INST_INSTALUJ = 1, K1_INST_AKTUALIZUJ = 2 };
/* 0.10.5 step 2 (ADR 0046): the writing program's modes for updates through the network (INST_ZADANIE a; nadzorca
 * only — no window asks for them by INST_START): STAN — the announcement from the zone and the disk's waiting room
 * checked (INST_SIEC out); POBIERZ — the package from the zone into the waiting room; ZAINSTALUJ — the waiting version
 * into the free slot, to try on the next start; WROC — the previous version (in the free slot) to try on the next start */
enum { K1_INST_SIEC_STAN = 3, K1_INST_SIEC_POBIERZ = 4, K1_INST_SIEC_ZAINSTALUJ = 5, K1_INST_SIEC_WROC = 6 };
#define K1_INST_TRYB_MAKS 6
/* INST_SIEC d: what the writing program found */
#define K1_SIEC_FL_NOWA_LINIA 1  /* the version waiting or to download has a new line of the Linux kernel (T3) */
#define K1_SIEC_FL_OGL 2         /* a fresh announcement came and was accepted */
#define K1_SIEC_FL_OGL_ZLE 4     /* a fresh announcement came and was refused (signature, format) */
#define K1_SIEC_FL_BEZ_MIEJSCA 8 /* no waiting room (the K1-EFI partition too small): updates from a stick only */
#define K1_SIEC_FL_PROBA 16      /* a new slot waits to be tried or confirmed: nothing is downloaded or installed */
#define K1_SIEC_FL_MAKS 31
/* 0.10.5 step 2b: what nadzorca says of updates through the network (AKT_STAN e, AKT_ETAP a, c; AKT_PROSBA a) */
#define K1_AKT_FL_NOWA_LINIA 1 /* the newer version has a new line of the Linux kernel (T3) */
#define K1_AKT_FL_PENDRIVE 2   /* K1 runs from a stick (or a disk without a waiting room): "nagraj nowy pendrive" (D5) */
#define K1_AKT_FL_SAM 4        /* "Sprawdzaj sam" is on (D4) */
#define K1_AKT_FL_PROBA 8      /* a new version waits to be tried or confirmed */
#define K1_AKT_FL_DYSK 16      /* K1 runs from a disk with K1 installed */
#define K1_AKT_FL_MAKS 31
enum { K1_AKT_E_NIC = 0, K1_AKT_E_SPRAWDZA = 1, K1_AKT_E_POBIERA = 2, K1_AKT_E_INSTALUJE = 3, K1_AKT_E_WRACA = 4 };
#define K1_AKT_E_MAKS 4
enum { K1_AKT_W_NIC = 0 /* not yet */, K1_AKT_W_OK = 1, K1_AKT_W_BRAK_SIECI = 2 /* K1 has no network */,
       K1_AKT_W_NIEUDANE = 3 /* no announcement came */, K1_AKT_W_ODRZUCONE = 4 /* it came, refused */ };
#define K1_AKT_W_MAKS 4
enum { K1_AKT_P_SPRAWDZ = 1, K1_AKT_P_ZAINSTALUJ = 2, K1_AKT_P_WROC = 3, K1_AKT_P_SAM_WL = 4, K1_AKT_P_SAM_WYL = 5 };
#define K1_AKT_P_MAKS 5
#define K1_OGL_MAKS_BAJTOW 8192 /* AKT_OPIS a: K1_OGL_MAKS (k1_ogloszenie.h; checked there) */
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
#define K1_INST_BLAD_TRWA (-10)    /* 0.10.5: another job runs (asked from the other window) */
#define K1_INST_BLAD_SIEC (-11)    /* 0.10.5 step 2: the download ended too early, or with bytes that are not the package */
#define K1_INST_BLAD_MIEJSCE (-12) /* 0.10.5 step 2: no waiting room on this disk (or nothing waits in it) */
#define K1_INST_BLAD_UKLAD (-13)   /* 0.10.9 step 4b: the disk's K1-EFI has other slots (older K1) — install anew */
#define K1_INST_BLAD_MIN (-13)
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
enum { K1_BOK_ZAMKNIETE = 0, K1_BOK_OTWARTE = 1, K1_BOK_USTAWIENIA = 2 }; /* K1_KOM_BATERIA_OKIENKO a */
#define K1_MOC_MAKS_MW 1000000  /* 1 kW: anything above is a firmware error */
#define K1_KARTA_MHZ_MAKS 5000  /* 0.10.10 step 2: a card's clock above it is a driver's error (ZASOBY_KARTA) */
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

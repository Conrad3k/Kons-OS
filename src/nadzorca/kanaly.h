/* K1 nadzorca — the channels it gives services and applications: one name each (0.9.0 step 1). The rows of K1_SYSTEM
 * and K1_APLIKACJE (k1_uslugi.h) list them by these names, in the order of the descriptors the program gets; uslugi.c
 * opens them when it starts the program (otworz_kanal) and closes our ends when it ends.
 * { kind, our end, who is on the other end (KN_OD), forget what was sent on the previous channel } — struct kanal,
 * nadzorca.h. A new channel: one line here; our end lives in the module that serves it. */
#ifndef K1_NADZORCA_KANALY_H
#define K1_NADZORCA_KANALY_H

#include "nadzorca.h"

#define KANAL_WEJSC { KN_WEJSCIA, NULL, NULL, NULL } /* wejscie: the input devices, one fd each (always last) */
#define KANAL_SKARBCA { KN_SKARB, NULL, NULL, NULL } /* to skarbiec, the program's profile as the owner of its data */
/* gui and the password screen (they share it): requests to us (URUCHOM, WYLACZ) */
#define KANAL_PULPITU { KN_OD, &gui_kanal, &gui_kanal_uid, NULL }
#define KANAL_BATERII { KN_DO, &bateria_kanal, NULL, bateria_nowy_kanal }      /* gui, logowanie: the battery state */
#define KANAL_PLIKU { KN_PLIK, NULL, NULL, NULL } /* an application: a file to open (Pliki: a place; plik_kanal[apl]) */
/* Ustawienia: battery state, history, the network; since 0.9.1 also its requests (Wi-Fi on/off, a look; wifi.c) */
#define KANAL_USTAWIEN { KN_OD, &ustaw_kanal, NULL, ustawienia_nowy_kanal }
#define KANAL_ZASOBOW { KN_DO, &zasoby_kanal, NULL, zasoby_nowy_kanal }        /* Zasoby: the statistics */
#define KANAL_INSTALACJI { KN_OD, &inst_kanal, NULL, inst_nowy_kanal }        /* Instalator: disks, progress */
/* 0.10.5 step 1: Ustawienia → Aktualizacje — the same INST_* as Instalator, only an update (instalacja.c) */
#define KANAL_AKTUALIZACJI { KN_OD, &akt_kanal, NULL, akt_nowy_kanal }
#define KANAL_PENDRIVE { KN_OD, &pen_kanal, NULL, pendrive_nowy_kanal } /* Pliki: the USB stick, a picture decoder */
#define KANAL_DEKODERA { KN_OD, &podglad_kanal, NULL, NULL } /* Podgląd: a picture decoder (dekoder.c; Podgląd only) */
#define KANAL_SIECI { KN_OD, &siec_kanal, NULL, siec_nowy_kanal } /* siec: the card with a cable, its address (siec.c) */
/* 0.9.1 step 2: Ustawienia → siec straight (a Wi-Fi password never passes through us; wifi_polacz.c) */
#define KANAL_DO_SIECI { KN_SIEC, NULL, NULL, NULL }

#endif

/* K1 strefa — what the zone's compositor did with input, as bits only (0.10.0 step 2, CI's diagnosis of the browser
 * zone: where keys stop). The compositor writes the number to PLIK_WEJSCIA when a bit appears; gosc sends it in the
 * line "wejscie <n>" on the control channel each time it changes; the VMM logs the bits as yes/no — never a key. */
#ifndef K1_STREFA_ZNAKI_WEJSCIA_H
#define K1_STREFA_ZNAKI_WEJSCIA_H

#define PLIK_WEJSCIA "/run/k1-wejscie"

enum {
    WEJ_MAPA = 1,              /* the keymap was read */
    WEJ_FOKUS = 2,             /* a wl_keyboard got enter (a window has the keys) */
    WEJ_KLAWISZ = 4,           /* a key came from the guest's keyboard device */
    WEJ_KLAWISZ_OKNO = 8,      /* ... while a window had the keys */
    WEJ_KLAWISZ_WYSLANY = 16,  /* ... and went to a wl_keyboard of that window's client */
    WEJ_PRZYCISK = 32,         /* a pointer button came */
    WEJ_KLATKA_PO_KLAWISZU = 64, /* a client committed a new buffer after a key went to it (it reacts, it draws) */
    WEJ_ENTER = 128,           /* Enter (KEY_ENTER or KEY_KPENTER) went to a window's wl_keyboard */
    WEJ_WSZYSTKIE = 255
};

#endif

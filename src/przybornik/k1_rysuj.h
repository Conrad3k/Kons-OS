/* K1 przybornik — drawing for the native GUI processes (gui, applications), ADR 0008.
 *
 * All drawing goes into premultiplied ARGB8888 images (struct k1_obraz) that the screen process
 * composes. Shapes use signed distance functions for anti-aliased edges; text uses the build-time
 * bitmap fonts (k1_czcionka.h). Pure functions without system calls or allocation — tested on the
 * host with ASan/UBSan (testy/host/test_przybornik.c). */
#ifndef K1_RYSUJ_H
#define K1_RYSUJ_H

#include <stddef.h>
#include <stdint.h>

#include "k1_czcionka.h"
#include "k1_grafika.h"

/* 0xRRGGBB + alpha 0..255 → premultiplied ARGB. */
uint32_t k1_kolor(uint32_t rgb, uint32_t alfa);

void k1_wyczysc(struct k1_obraz *o, uint32_t kolor); /* set every pixel */
/* Blend `kolor` over a rectangle / rounded rectangle (radius r) / its outline (width g). */
void k1_prostokat(struct k1_obraz *o, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kolor);
void k1_zaokraglony(struct k1_obraz *o, float x, float y, float w, float h, float r, uint32_t kolor);
void k1_obrys(struct k1_obraz *o, float x, float y, float w, float h, float r, float g, uint32_t kolor);
void k1_kolo(struct k1_obraz *o, float cx, float cy, float r, uint32_t kolor);
void k1_okrag(struct k1_obraz *o, float cx, float cy, float r, float g, uint32_t kolor);
void k1_kreska(struct k1_obraz *o, float x0, float y0, float x1, float y1, float g, uint32_t kolor);
/* Soft shadow of a rounded rectangle cast `dy` pixels down, drawn only outside the rectangle itself
 * (panels of the shell are translucent: the shadow must not show through them). */
void k1_cien(struct k1_obraz *o, float x, float y, float w, float h, float r, float dy, float rozmycie, uint32_t alfa);

/* Text (UTF-8; invalid sequences are drawn as '?'). `y` is the baseline. Returns the advance in px.
 * With max_szer > 0 the text is cut and ended with "…" so that it fits. */
int32_t k1_tekst(struct k1_obraz *o, enum k1_cz cz, int32_t x, int32_t y, const char *utf8, uint32_t kolor,
                 int32_t max_szer);
int32_t k1_tekst_szer(enum k1_cz cz, const char *utf8);
/* Decodes one UTF-8 code point at s (at most `n` bytes); returns its length (>= 1), *kod = U+FFFD
 * for invalid input. */
int k1_utf8(const char *s, uint32_t n, uint32_t *kod);
/* Appends code point `kod` as UTF-8; returns the number of bytes (0 if it does not fit). */
int k1_utf8_zapisz(uint32_t kod, char *dst, uint32_t miejsce);
/* Characters (not bytes) in the first `n` bytes of UTF-8 `s`. */
size_t k1_utf8_znaki(const char *s, size_t n);
/* Where the character holding byte `p` of UTF-8 `s` starts (p itself when it starts one; 0 at most):
 * for Backspace (k1_utf8_poczatek(s, dl - 1)) and for cutting text without splitting a character. */
size_t k1_utf8_poczatek(const char *s, size_t p);

/* Line icons drawn on a 24×24 grid scaled to `rozmiar` px, stroke 1.5 (at 24 px). */
enum k1_ikona {
    K1_IK_ZEGAR = 1,
    K1_IK_NOTATKI,
    K1_IK_USTAWIENIA,
    K1_IK_GLOSNIK,
    K1_IK_ZASILANIE,
    K1_IK_RESTART,
    K1_IK_ZAMKNIJ,
    K1_IK_MINIMALIZUJ,
    K1_IK_KLODKA,
    K1_IK_INFO,
    K1_IK_MYSZ,
    K1_IK_WYGLAD,
    K1_IK_ZASOBY,
    K1_IK_EKRAN,
    K1_IK_BATERIA,
    K1_IK_DYSK, /* M5f: Instalator */
    K1_IK_KALKULATOR,
    /* 0.8.0 Pliki (ADR 0029) */
    K1_IK_FOLDER,
    K1_IK_PLIK,
    K1_IK_OBRAZ,
    K1_IK_KOSZ,
    K1_IK_KOSZ_PELNY,
    K1_IK_MUZYKA,
    K1_IK_WIDEO,
    K1_IK_POBRANE,
    K1_IK_W_GORE,
    /* 0.9.0 step 7: the network */
    K1_IK_SIEC,           /* a network plug with its cable (connected; Ustawienia → Sieć) */
    K1_IK_SIEC_BEZ_KABLA, /* the plug alone */
    K1_IK_WIFI,
    K1_IK_PRZEGLADARKA, /* 0.10.0 step 2 (D1: icon A): a globe — the browser in its zone */
    K1_IK_AKTUALIZACJE, /* 0.10.5 step 1 (D1: icon B, turned down): an arrow down in a circle — Ustawienia → Aktualizacje */
    K1_IK_MAKSYMALIZUJ, /* 0.10.5 step 4 (D8): a square — the frame of a zone's window */
    K1_IK_PRZYWROC,     /* two squares: a maximized window back to its size */
    K1_IK_KARTA,        /* 0.10.9 step 6 (D7, the owner's choice: a chip) — a zone draws with the card (its frame's mark) */
    K1_IK_GLOSNIK_CICHO,     /* 0.12.0 step 1: the speaker with one wave (the volume under 50 %) */
    K1_IK_GLOSNIK_WYCISZONY, /* the speaker with a × (muted, or the volume 0) */
    K1_IK_MIKROFON,          /* 0.13.5 step 1 (D2: A): a microphone on its stand — the sign "mikrofon włączony" */
    K1_IK_MIKROFON_WYCISZONY, /* the microphone crossed out (muted) */
};
void k1_ikona(struct k1_obraz *o, enum k1_ikona ik, float x, float y, float rozmiar, uint32_t kolor);

/* The battery symbol over the wallpaper (top bar, password screen): a 22×12 body at (x, y) with its
 * terminal, filled to `procent` (no fill when < 0), the fill terracotta when `niska`. `alfa` 0..255. */
void k1_bateria(struct k1_obraz *o, float x, float y, int procent, int niska, uint32_t alfa);

#endif

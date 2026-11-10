/* K1 Notatki — the notes of 0.5.6–0.7.6 moved to files (0.8.0, ADR 0030), once: they were objects of Notatki in
 * skarbiec (object 1 the list "K1N1", count, ids newest first; objects 2..33 the notes; 0.5.6–0.5.7: object 2
 * alone). Every note becomes a file in Dokumenty › Notatki (oldest first, so the newest gets the newest number);
 * its old object is deleted only after skarbiec confirmed the file, the list last — a power cut in between moves
 * nothing twice. Contents never go to the log. Part of Notatki (notatki.h).
 * k1: tymczasowe — usunąć w 0.9.0 (when nobody updates from 0.7.x any more: the owner's notes are moved) */
#include <string.h>

#include "k1_log.h"
#include "k1_skarb_klient.h"
#include "notatki.h"
#include "stare.h"

#define STARY_SPIS 1   /* object: the list */
#define STARA_PIERWSZA 2
#define STARYCH_MAKS 32
#define STARY_ZNAK "K1N1"

static int pozostalo, bledy, byl_spis;

/* The old list: ids in order, or -1 (none / damaged: then the single note of 0.5.6–0.5.7, if any). */
static int stary_spis(int *ids)
{
    unsigned char buf[5 + STARYCH_MAKS + 1];
    int n = k1_skarb_czytaj(skarb, STARY_SPIS, buf, sizeof buf, 3000, &tryb), i, j;
    if (n < 5 || memcmp(buf, STARY_ZNAK, 4) != 0 || buf[4] > STARYCH_MAKS || n != 5 + buf[4])
        return -1;
    for (i = 0; i < buf[4]; i++) {
        ids[i] = buf[5 + i];
        if (ids[i] < STARA_PIERWSZA || ids[i] >= STARA_PIERWSZA + STARYCH_MAKS)
            return -1;
        for (j = 0; j < i; j++)
            if (ids[j] == ids[i])
                return -1;
    }
    byl_spis = 1;
    return buf[4];
}

void stare_przenies(void)
{
    /* all read first: a synchronous read drops other answers of skarbiec, the ones for the files sent too */
    static char tekst[STARYCH_MAKS][MAKS_TEKST + 1];
    int ids[STARYCH_MAKS], dl[STARYCH_MAKS], ile = stary_spis(ids), wyslane = 0;
    if (ile < 0) {
        ids[0] = STARA_PIERWSZA;
        ile = 1;
    }
    for (int i = 0; i < ile; i++) {
        dl[i] = k1_skarb_czytaj(skarb, ids[i], tekst[i], MAKS_TEKST, 3000, &tryb);
        if (dl[i] == -2) {
            k1_log(TAG, "stare notatki: skarbiec nie odpowiada — przeniesienie przy następnym uruchomieniu");
            return;
        }
    }
    for (int i = ile - 1; i >= 0; i--) { /* the oldest first */
        if (dl[i] == 0)
            k1_skarb_usun(skarb, ids[i]); /* an empty note: nothing to keep */
        if (dl[i] > 0 && nowy_plik(tekst[i], (uint32_t)dl[i], 0, ids[i]) == 0) {
            pozostalo++;
            wyslane++;
        }
    }
    if (wyslane > 0)
        k1_log(TAG, "stare notatki: %d do przeniesienia do plików", wyslane);
    else if (byl_spis)
        k1_skarb_usun(skarb, STARY_SPIS);
}

void stare_wynik(int stary, int wynik)
{
    if (wynik == 0)
        k1_skarb_usun(skarb, stary);
    else
        bledy++;
    if (--pozostalo > 0)
        return;
    if (bledy == 0 && byl_spis)
        k1_skarb_usun(skarb, STARY_SPIS);
    k1_log(TAG, "stare notatki przeniesione do plików (błędów: %d)", bledy);
}

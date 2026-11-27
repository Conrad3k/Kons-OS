/* K1 — what the report onto a USB stick did, in words (k1_raport_opis.h). */
#include "k1_raport_opis.h"

#include <stdio.h>

#include "k1_komunikat.h"

const char *k1_raport_opis(int stan, int e, char *b, size_t n)
{
    switch (stan) {
    case K1_RAP_ZAPISUJE:
        return "Zapisuję raport na pendrive…";
    case K1_RAP_ZAPISANY:
        if (e == 0)
            snprintf(b, n, "Raport zapisany na pendrivie: K1RAPORT.TXT. Możesz go wyjąć.");
        else
            snprintf(b, n, "Raport zapisany na pendrivie: K1RAPOR%d.TXT. Możesz go wyjąć.", e);
        return b;
    case K1_RAP_BRAK_PENDRIVE:
        return "Nie widzę pendrive'a. Włóż pendrive (FAT32) i spróbuj jeszcze raz.";
    case K1_RAP_BLAD:
        return e == K1_INST_BLAD_FAT ? "Pendrive nie ma systemu plików FAT32. Użyj innego."
               : e == K1_INST_BLAD_MALY ? "Na pendrivie brakuje miejsca na raport."
                                        : "Nie udało się zapisać raportu. Spróbuj innego pendrive'a.";
    default:
        return NULL;
    }
}

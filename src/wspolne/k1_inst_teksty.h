/* K1 — what the windows of the writing program say (0.10.5 step 1): Instalator (install on a disk) and Ustawienia →
 * Aktualizacje (update K1 on a disk) show the same progress and the same results — one table each, here. */
#ifndef K1_INST_TEKSTY_H
#define K1_INST_TEKSTY_H

#include "k1_komunikat.h"

/* INST_POSTEP b: what the writing program does now. */
static inline const char *k1_inst_etap_tekst(int etap)
{
    static const char *const etapy[K1_INST_ETAP_MAKS + 1] = {
        "Przygotowuję…", "Czyszczę dysk…", "Kopiuję system…", "Sprawdzam zapis…", "Zapisuję tablicę partycji…",
    };
    return etapy[etap >= 1 && etap <= K1_INST_ETAP_MAKS ? etap : 0];
}

/* INST_WYNIK a < 0: why it did not work. */
static inline const char *k1_inst_blad_tekst(int blad)
{
    static const struct {
        int kod;
        const char *tekst;
    } bledy[] = {
        { K1_INST_BLAD_ZAJETY, "Dysk jest teraz używany przez system (jego dane są otwarte)." },
        { K1_INST_BLAD_MALY, "Dysk jest za mały (potrzeba co najmniej 1 GB)." },
        { K1_INST_BLAD_ZAPIS, "Zapis na dysk się nie udał. Dysk może być uszkodzony." },
        { K1_INST_BLAD_SPRAWDZ, "Dysk zwrócił inne dane niż zapisane. Dysk może być uszkodzony." },
        { K1_INST_BLAD_ZRODLO, "Nie znaleziono pendrive'a z K1 (uruchom K1 z pendrive'a albo podłącz go)." },
        { K1_INST_BLAD_PODPIS, "System na pendrivie nie jest podpisaną kopią K1 (albo jest uszkodzony) — nie zapisuję go." },
        { K1_INST_BLAD_STARSZA, "Na pendrivie jest starsza wersja niż na dysku — powrót do starszej wersji jest zablokowany." },
        { K1_INST_BLAD_PRZERWANE, "Program zapisujący zakończył się bez wyniku." },
        { K1_INST_BLAD_TRWA, "Trwa już zapis K1 na dysk w drugim oknie (Instalator albo Ustawienia) — poczekaj na koniec." },
    };
    unsigned i;
    for (i = 0; i < sizeof bledy / sizeof bledy[0]; i++)
        if (bledy[i].kod == blad)
            return bledy[i].tekst;
    return "Dysk zniknął albo się zmienił — sprawdź jeszcze raz.";
}

#endif

/* K1 nadzorca — what each window of the writing program may ask for (inst_prawa.h). */
#include "inst_prawa.h"

int inst_dozwolone(int okno, const struct k1_komunikat *k, int sukces, int sb_czeka)
{
    if (okno != OKNO_INSTALATOR && okno != OKNO_USTAWIENIA)
        return 0;
    switch (k->typ) {
    case K1_KOM_INST_DYSKI:
        return 1;
    case K1_KOM_INST_START:
        return k->a == (okno == OKNO_INSTALATOR ? K1_INST_INSTALUJ : K1_INST_AKTUALIZUJ);
    case K1_KOM_WYLACZ:
        if (k->a == 1)
            return sukces || (okno == OKNO_INSTALATOR && sb_czeka);
        return k->a == 0 && okno == OKNO_INSTALATOR && sukces;
    case K1_KOM_AKT_PROSBA: /* 0.10.5 step 2b: updates through the network — Ustawienia only (gui: obsluz_gui) */
        return okno == OKNO_USTAWIENIA;
    case K1_KOM_SB_KLUCZ:
    case K1_KOM_OTWORZ_AKTUALIZACJE:
        return okno == OKNO_INSTALATOR;
    default:
        return 0;
    }
}

/* K1 nadzorca — what each window of the writing program may ask for (0.10.5 step 1, plan T1): Instalator installs on a
 * disk and turns on Secure Boot; Ustawienia → Aktualizacje only updates K1 on a disk — never wipes one. Pure
 * (test_inst_prawa); instalacja.c asks it for every request. */
#ifndef K1_INST_PRAWA_H
#define K1_INST_PRAWA_H

#include "k1_komunikat.h"

enum { OKNO_INSTALATOR, OKNO_USTAWIENIA, OKIEN };

/* 1 when window `okno` may send `k` (already checked field by field): the list of disks; its own job (INST_START with
 * its mode only); a restart (WYLACZ a=1) only after its finished job (`sukces`) — Instalator also when the K1 key waits
 * for MokManager (`sb_czeka`); power off (a=0) only for Instalator after an install; Secure Boot and the link to
 * Ustawienia — Instalator only; updates through the network (AKT_PROSBA, 0.10.5 step 2b) — Ustawienia only. Everything
 * else: 0. */
int inst_dozwolone(int okno, const struct k1_komunikat *k, int sukces, int sb_czeka);

#endif

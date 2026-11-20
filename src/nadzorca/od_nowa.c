/* K1 nadzorca — the Instalator installs anew on the disk whose K1-DANE skarbiec has (0.10.10 step 1; the owner's choice
 * of 2026-11-20: K1 0.10.7 on a disk cannot be updated to the new layout, and K1 started from a K1 stick keeps using the
 * data of the disk — M5f, ADR 0018 — so the disk was "in use" and the Instalator could not install on it).
 * After the owner typed USUŃ for such a disk (and only with a K1 stick to copy from):
 *   1. every zone ends at once (its VMM, film decoder, graphics broker; the browser's data are going anyway);
 *   2. nadzorca lets go of the zone images it keeps (their dm-verity devices go when the last VMM lets go);
 *   3. skarbiec closes the vault and the partition (SKARB_ZWOLNIJ → SKARB_ZWOLNIONO) and keeps the objects in memory —
 *      the desktop and the Instalator's window go on; from now on no K1-DANE is given to skarbiec again;
 *   4. the install of inst_zapis, tried again every half second until the whole disk opens exclusively (at most 30 s).
 * The running system is in memory (the core, the kernel), so this works also when K1 started from that very disk; the
 * Instalator says then "Wyłącz komputer" and the next start is the new K1. Part of nadzorca (see main.c). */
#include "nadzorca.h"

#define CZEKAJ_S 30.0
#define CO_S 0.5

static struct {
    int czeka;      /* a job waits for the disk */
    int zwolnione;  /* skarbiec closed the partition */
    int okno, nr;   /* the window and the disk of INST_START */
    double do_kiedy, nastepny;
} on;
static int bez_danych; /* from step 3 on: no K1-DANE for skarbiec (otworz_partycje_danych) */

int od_nowa_bez_danych(void)
{
    return bez_danych;
}

static void strefy_koniec_wszystkie(void)
{
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        struct usluga *u = usluga_profilu(k1_strefa(nr)->profil);
        if (u->pid > 0) {
            u->bez_liczenia = 1;
            kill(u->pid, SIGKILL);
        }
    }
}

int od_nowa_start(int okno, int nr)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_ZWOLNIJ, 0, 0, 0, 0, 0, 0);
    if (on.czeka)
        return K1_INST_BLAD_TRWA;
    on.czeka = 1, on.zwolnione = 0, on.okno = okno, on.nr = nr;
    on.do_kiedy = k1_teraz() + CZEKAJ_S, on.nastepny = 0;
    bez_danych = 1;
    k1_log(TAG, "instalator: instalacja od nowa na dysku z danymi (%s) — zamykam sesje i dane", skarb_dane);
    strefy_koniec_wszystkie();
    obrazy_stref_zwolnij();
    if (skarb_ster < 0 || k1_kanal_wyslij(skarb_ster, &k, sizeof k, -1) != 0)
        od_nowa_zwolnione(); /* no skarbiec: nothing holds the partition */
    return 0;
}

void od_nowa_zwolnione(void)
{
    if (!on.czeka)
        return;
    on.zwolnione = 1;
    k1_log(TAG, "instalator: skarbiec oddał partycję %s — dane tylko w pamięci", skarb_dane[0] ? skarb_dane : "(brak)");
    skarb_dane[0] = '\0';
}

/* From the main loop: the install, once the disk is free. */
void od_nowa_krok(double t)
{
    int r;
    if (!on.czeka || !on.zwolnione || t < on.nastepny)
        return;
    r = inst_zapis(on.okno, on.nr, K1_INST_INSTALUJ);
    if (r == K1_INST_BLAD_ZAJETY && t < on.do_kiedy) {
        on.nastepny = t + CO_S; /* a VMM or a dm-verity device still lets go of it */
        return;
    }
    on.czeka = 0;
    if (r != 0) {
        inst_odmowa(on.okno, K1_INST_INSTALUJ, r);
        k1_log(TAG, "instalator: instalacja od nowa nieudana (%d)", r);
    }
}

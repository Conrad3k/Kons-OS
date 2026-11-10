/* K1 nadzorca — the USB stick for Pliki (0.8.0 step 3, ADR 0031): tells Pliki (its fd 6) whether a stick is plugged
 * in, and on its request starts czytnik for it — the whole stick opened read only, a new channel between czytnik
 * (its fd 3) and Pliki (sent with PENDRIVE_KANAL). One czytnik at a time; it is stopped when the stick goes away.
 * We never read the stick ourselves (only sysfs: which disk it is). Part of nadzorca (see main.c). */
#include "nadzorca.h"

int pen_kanal = -1;         /* our end of Pliki's fd 6 */
static char pen_dysk[32];   /* the stick the running czytnik reads */
static int pen_wyslany = -1; /* what Pliki was told last: 1 a stick, 0 none, -1 nothing yet */
static int pen_czeka;       /* Pliki asked while the previous czytnik was still ending */

struct usluga *czytnik(void)
{
    return usluga_profilu(K1_PROFIL_CZYTNIK);
}

static void do_plikow(struct k1_komunikat k, int fd)
{
    if (pen_kanal >= 0 && k1_kanal_wyslij(pen_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "pendrive: wysłanie do Plików nieudane");
}

void pendrive_nowy_kanal(void)
{
    pen_wyslany = -1; /* new Pliki: tell them again */
    pen_czeka = 0;
    pendrive_sprawdz();
}

/* After a disk event (and every few seconds): a stick came or went; czytnik of a stick that went is stopped. */
void pendrive_sprawdz(void)
{
    char dysk[32];
    int jest = k1_pendrive_znajdz(SYS, dysk);
    if (czytnik()->pid > 0 && (!jest || strcmp(dysk, pen_dysk) != 0)) {
        k1_log(TAG, "pendrive: odłączony — zatrzymuję czytnik");
        kill(czytnik()->pid, SIGKILL);
    }
    if (pen_kanal >= 0 && jest != pen_wyslany) {
        do_plikow(k1_komunikat_nowy(K1_KOM_PENDRIVE, 0, jest, 0, 0, 0, 0), -1);
        pen_wyslany = jest;
        k1_log(TAG, "pendrive: %s", jest ? "podłączony (Pliki pokazują go)" : "brak");
    }
}

/* Starts czytnik: fd 3 = its channel to Pliki (each end checks the other: SO_PASSCRED on both), fd 4 = the stick. */
static void otworz(void)
{
    struct usluga *u = czytnik();
    char dysk[32];
    int fds[2], para[2], tak = 1;
    if (u->pid > 0) { /* the previous one first (it ends when Pliki closed its channel, or now) */
        kill(u->pid, SIGKILL);
        pen_czeka = 1;
        return;
    }
    if (!k1_pendrive_znajdz(SYS, dysk)) {
        do_plikow(k1_komunikat_nowy(K1_KOM_PENDRIVE, 0, 0, 0, 0, 0, 0), -1);
        pen_wyslany = 0;
        return;
    }
    fds[1] = otworz_urzadzenie(dysk, O_RDONLY);
    if (fds[1] < 0) {
        k1_log(TAG, "pendrive: nie można otworzyć %s: %s", dysk, strerror(errno));
        do_plikow(k1_komunikat_nowy(K1_KOM_PENDRIVE, 0, 0, 0, 0, 0, 0), -1);
        pen_wyslany = 0;
        return;
    }
    if (k1_kanal_utworz(para) != 0 || setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
        close(fds[1]);
        return;
    }
    fds[0] = para[0];
    if (uruchom(u, &profile[K1_PROFIL_CZYTNIK], fds, 2) > 0) {
        snprintf(pen_dysk, sizeof pen_dysk, "%s", dysk);
        k1_log(TAG, "pendrive: czytnik dla %s (tylko odczyt), pid %d, uid %d", dysk, (int)u->pid,
               (int)profile[K1_PROFIL_CZYTNIK].uid);
        do_plikow(k1_komunikat_nowy(K1_KOM_PENDRIVE_KANAL, 0, 0, 0, 0, 0, 0), para[1]);
    }
    close(para[0]);
    close(para[1]);
    close(fds[1]);
}

/* czytnik ended (zakonczona): a request that waited for it starts the new one now. */
void pendrive_koniec(void)
{
    if (pen_czeka) {
        pen_czeka = 0;
        otworz();
    }
}

void obsluz_pendrive(void)
{
    for (int i = 0; i < 8 && pen_kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(pen_kanal, K1_UID_PLIKI, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(pen_kanal);
            pen_kanal = -1;
            return;
        }
        if (w != K1_ODB_JEST || k.typ != K1_KOM_PENDRIVE_OTWORZ) {
            k1_log(TAG, "pendrive: odrzucono prośbę od Plików");
            continue;
        }
        otworz();
    }
}

/* K1 nadzorca — the picture decoder for Pliki (0.8.0 step 4, ADR 0032): on request of Pliki (DEKODER on their fd 6)
 * a new dekoder process with fd 3 = a new channel, whose other end goes to Pliki (DEKODER_KANAL). One picture per
 * process, one process at a time: a request while one still runs stops it and starts the new one after it ends.
 * We never touch the file or the picture — they go between Pliki and dekoder. Part of nadzorca (see main.c). */
#include "nadzorca.h"

static int dek_czeka; /* Pliki asked while the previous dekoder was still ending */

static struct usluga *dekoder(void)
{
    return usluga_profilu(K1_PROFIL_DEKODER);
}

void dekoder_otworz(void)
{
    struct usluga *u = dekoder();
    int para[2], tak = 1;
    if (u->pid > 0) {
        kill(u->pid, SIGKILL);
        dek_czeka = 1;
        return;
    }
    if (k1_kanal_utworz(para) != 0)
        return;
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) == 0 &&
        uruchom(u, &profile[K1_PROFIL_DEKODER], &para[0], 1) > 0) {
        k1_log(TAG, "dekoder dla Plików: pid %d, uid %d", (int)u->pid, (int)profile[K1_PROFIL_DEKODER].uid);
        pliki_wyslij(k1_komunikat_nowy(K1_KOM_DEKODER_KANAL, 0, 0, 0, 0, 0, 0), para[1]);
    }
    close(para[0]);
    close(para[1]);
}

/* dekoder ended (zakonczona): a request that waited for it starts the new one now. */
void dekoder_koniec(void)
{
    if (dek_czeka) {
        dek_czeka = 0;
        dekoder_otworz();
    }
}

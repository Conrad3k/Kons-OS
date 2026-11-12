/* K1 nadzorca — the picture decoder (0.8.0 step 4, ADR 0032; more clients: 0.8.2 step 3, Podgląd: step 4): on request of
 * a client in the table below, a new dekoder process with fd 3 = a new channel, whose other end goes to the client
 * (DEKODER_KANAL), and the client's application number as its argument (it accepts that application's UID only).
 * One picture per process, one process at a time, at most one request waiting per client: a new request of the same
 * client stops its running dekoder (that picture is no longer wanted), another client's waits for its end. We never
 * touch the file or the picture — they go between the client and dekoder. Part of nadzorca (see main.c). */
#include "nadzorca.h"

int podglad_kanal = -1; /* our end of Podgląd's fd 6: DEKODER in, DEKODER_KANAL out (ADR 0037) */

static void podglad_wyslij(struct k1_komunikat k, int fd)
{
    if (podglad_kanal >= 0 && k1_kanal_wyslij(podglad_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "dekoder: wysłanie do Podglądu nieudane");
}

/* Who may ask for a dekoder, and how the channel reaches them. A new client: one line (and its channel in kanaly[]). */
static const struct klient {
    int apl;
    void (*wyslij)(struct k1_komunikat k, int fd);
} klienci[] = {
    { K1_APL_PLIKI, pliki_wyslij },     /* their fd 6 (pendrive.c) */
    { K1_APL_PODGLAD, podglad_wyslij }, /* its fd 6 (obsluz_podglad) */
};

static int czeka[N(klienci)]; /* a request that waits for the running dekoder to end */
static int dla = -1;          /* the client of the running dekoder (its row) */

static struct usluga *dekoder(void)
{
    return usluga_profilu(K1_PROFIL_DEKODER);
}

/* At most 1/K1_DEK_PAMIEC_RAM of the computer's memory (8 GB and more: the profile's own numbers), never less than
 * K1_DEK_PAMIEC_MIN. */
uint64_t dekoder_pamiec(uint64_t ram, uint64_t profil)
{
    uint64_t r = ram / K1_DEK_PAMIEC_RAM;
    return r < K1_DEK_PAMIEC_MIN ? K1_DEK_PAMIEC_MIN : r < profil ? r : profil;
}

static void uruchom_dla(int i)
{
    struct usluga *u = dekoder();
    struct k1_profil p = profile[K1_PROFIL_DEKODER];
    struct sysinfo si;
    int para[2], tak = 1;
    if (sysinfo(&si) == 0) {
        uint64_t ram = (uint64_t)si.totalram * si.mem_unit, zapas = p.pamiec - p.przestrzen;
        p.przestrzen = dekoder_pamiec(ram, p.przestrzen);
        p.pamiec = p.przestrzen + zapas;
    }
    if (k1_kanal_utworz(para) != 0)
        return;
    snprintf(u->argument, sizeof u->argument, "%d", klienci[i].apl);
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) == 0 && uruchom(u, &p, &para[0], 1) > 0) {
        dla = i;
        k1_log(TAG, "dekoder dla aplikacji %s: pid %d, uid %d, pamięć %llu MiB", k1_profil_nazwa(k1_apl_profil(klienci[i].apl)),
               (int)u->pid, (int)p.uid, (unsigned long long)(p.przestrzen >> 20));
        klienci[i].wyslij(k1_komunikat_nowy(K1_KOM_DEKODER_KANAL, 0, 0, 0, 0, 0, 0), para[1]);
    }
    close(para[0]);
    close(para[1]);
}

void dekoder_otworz(int apl)
{
    struct usluga *u = dekoder();
    int i = 0;
    while (i < (int)N(klienci) && klienci[i].apl != apl)
        i++;
    if (i == (int)N(klienci)) {
        k1_log(TAG, "dekoder: prośba od aplikacji %d, której nie ma w tabeli — odrzucona", apl);
        return;
    }
    if (u->pid > 0) {
        if (dla == i) /* the same client wants another picture: this one is no longer needed */
            kill(u->pid, SIGKILL);
        czeka[i] = 1;
        return;
    }
    uruchom_dla(i);
}

/* dekoder ended (zakonczona): a request that waited starts now — the client after the one just served first. */
void dekoder_koniec(void)
{
    int n = (int)N(klienci);
    for (int k = 1; k <= n; k++) {
        int i = (dla + k + n) % n;
        if (czeka[i]) {
            czeka[i] = 0;
            uruchom_dla(i);
            return;
        }
    }
    dla = -1;
}

/* Podgląd's fd 6: it may only ask for a dekoder. */
void obsluz_podglad(void)
{
    for (int i = 0; i < 8 && podglad_kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(podglad_kanal, K1_UID_PODGLAD, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(podglad_kanal);
            podglad_kanal = -1;
            return;
        }
        if (w == K1_ODB_JEST && k.typ == K1_KOM_DEKODER)
            dekoder_otworz(K1_APL_PODGLAD);
        else
            k1_log(TAG, "dekoder: odrzucono prośbę od Podglądu");
    }
}

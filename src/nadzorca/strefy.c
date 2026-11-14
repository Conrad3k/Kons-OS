/* K1 nadzorca — the zones (0.10.0, ADR 0042 T3): on request of gui (or the test program: with a hostile guest's test
 * mode) the VMM of a zone (program strefa, its row of K1_STREFY: profile, UID, memory) starts in its sandbox with
 *   fd 3 = a new channel to ekran (role STREFA, the zone's number), fd 4 = /dev/kvm, fd 5 = the zone kernel,
 *   fd 6 = the zone image — all opened here, read only but /dev/kvm; the VMM gets no path.
 * One machine per row at a time. Without virtualization (no /dev/kvm: the processor has none, or it is off in the
 * BIOS), without the zone's image or without enough free memory gui is told why (STREFA_STAN) and nothing starts.
 * The VMM's end is an ordinary end of a service started on request (uslugi.c): no restart. Part of nadzorca. */
#include "nadzorca.h"

#define STREFY "/rdzen/strefy"
#define ZAPAS_PAMIECI (64ull << 20) /* free memory K1 keeps besides the zone's own */

static void zglos(int nr, int powod)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_STAN, 0, nr, powod, 0, 0, 0);
    if (bateria_kanal >= 0 && k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "strefa %d: powiadomienie pulpitu nieudane", nr);
}

/* The guest's memory and what the VMM needs fit in what is free now. */
static int jest_pamiec(const struct k1_strefa_opis *z)
{
    struct sysinfo si;
    uint64_t wolne;
    if (sysinfo(&si) != 0)
        return 1;
    wolne = ((uint64_t)si.freeram + si.bufferram) * si.mem_unit;
    return wolne >= ((uint64_t)z->pamiec_mib << 20) + ZAPAS_PAMIECI;
}

/* Opens what the VMM gets (fd[1..3]: kvm, kernel, image); 0, or the K1_STREFA_* reason. */
static int otworz(const struct k1_strefa_opis *z, int *fd)
{
    char sciezka[96];
    fd[1] = open(DEV "/kvm", O_RDWR | O_CLOEXEC);
    if (fd[1] < 0) {
        k1_log(TAG, "strefa %d: brak wirtualizacji (/dev/kvm: %s) — włącz ją w BIOS-ie", z->nr, strerror(errno));
        return K1_STREFA_BEZ_WIRTUALIZACJI;
    }
    snprintf(sciezka, sizeof sciezka, STREFY "/%s.img", z->obraz);
    fd[2] = open(STREFY "/jadro", O_RDONLY | O_CLOEXEC);
    fd[3] = open(sciezka, O_RDONLY | O_CLOEXEC);
    if (fd[2] < 0 || fd[3] < 0) {
        k1_log(TAG, "strefa %d: brak jądra albo obrazu strefy w rdzeniu", z->nr);
        return K1_STREFA_BRAK_OBRAZU;
    }
    if (!jest_pamiec(z)) {
        k1_log(TAG, "strefa %d: za mało wolnej pamięci (potrzeba %u MiB)", z->nr, z->pamiec_mib);
        return K1_STREFA_ZA_MALO_PAMIECI;
    }
    return 0;
}

void strefa_uruchom(int nr, int tryb)
{
    const struct k1_strefa_opis *z = k1_strefa(nr);
    struct usluga *u = z != NULL ? usluga_profilu(z->profil) : NULL;
    int fd[4] = { -1, -1, -1, -1 }, powod, i;
    if (u == NULL)
        return;
    if (u->pid > 0) {
        k1_log(TAG, "strefa %d już działa", nr);
        return;
    }
    powod = otworz(z, fd);
    if (powod == 0) {
        fd[0] = kanal_do_ekranu(K1_ROLA_STREFA, nr, z->uid);
        if (fd[0] < 0)
            k1_log(TAG, "strefa %d: ekran nie działa", nr);
        snprintf(u->argument, sizeof u->argument, "%d", nr);
        snprintf(u->argument2, sizeof u->argument2, "%d", tryb);
        if (fd[0] >= 0 && uruchom(u, &profile[z->profil], fd, 4) > 0)
            k1_log(TAG, "strefa %d (%s): pid %d, uid %d, pamięć %u MiB, procesory %u%s", nr, z->tytul, (int)u->pid, z->uid,
                   z->pamiec_mib, z->procesory, tryb ? ", gość testowy" : "");
        else
            k1_log(TAG, "strefa %d: uruchomienie nieudane", nr);
    } else {
        zglos(nr, powod);
    }
    for (i = 0; i < 4; i++)
        if (fd[i] >= 0)
            close(fd[i]);
}

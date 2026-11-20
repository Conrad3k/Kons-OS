/* K1 nadzorca — the zones (0.10.0, ADR 0042 T3): on request of gui (or the test program: with a hostile guest's test
 * mode) the VMM of a zone (program strefa, its row of K1_STREFY: profile, UID, memory) starts in its sandbox with
 *   fd 3 = a new channel to ekran (role STREFA, the zone's number), fd 4 = /dev/kvm, fd 5 = the zone kernel,
 *   fd 6 = the zone image (since 0.10.9 step 4 the browser's under dm-verity from the system slot: obraz_strefy.c) —
 *   all opened here, read only but /dev/kvm; the VMM gets no path;
 *   fd 7 = its channel to us for the browser's connections (strefy_siec.c; 0.10.0 step 2, ADR 0043 T9),
 *   fd 8, 9 = a zone that remembers (strefy_ust.c; 0.10.0 step 5, T11): a channel to skarbiec with the zone's profile as
 *   the owner (its disk's pieces) and the zone's empty disk (/rdzen/strefy/dysk.img, read only) — else neither;
 *   fd 10 = 0.10.9 step 6: a session with fast graphics on, on a card that can (posrednik.c) — the channel to the zone's
 *   graphics broker (fd 8, 9 then /dev/null when it forgets); its VMM may take K1_POSR_PAMIEC more (the card's buffers
 *   it maps). The test program's modes K1_STREFA_TRYB_GRAFIKA*: a broker with the test engine.
 * One machine per row at a time (one that is closing: strefy_zamyk.c). Without virtualization (no /dev/kvm: the processor has none, or it is off in the
 * BIOS), without the zone's image or without enough free memory gui is told why (STREFA_STAN) and nothing starts.
 * The VMM's end is an ordinary end of a service started on request (uslugi.c): no restart. When the user did not
 * close it, gui says why it ended (0.10.0 step 3, S26: before, the window was simply gone). Part of nadzorca. */
#include "nadzorca.h"
#include "pamiec_stref.h"
#include "strefy_powod.h"

#define STREFY "/rdzen/strefy"
#define ZAPAS_PAMIECI (64ull << 20) /* free memory K1 keeps besides the zone's own */

static char cel_pobierania[K1_STREFA_CEL_MAKS + 1]; /* 0.10.5 step 2b: what the download zone downloads (argv[4]) */

static void zglos(int nr, int powod)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_STAN, 0, nr, powod, 0, 0, 0);
    if (!k1_strefa(nr)->okno) /* the download zone: no window to tell the user about (aktualizacja.c says it) */
        return;
    if (bateria_kanal >= 0 && k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "strefa %d: powiadomienie pulpitu nieudane", nr);
}

/* The zone's upper limit (pamiec_stref.c: D2) into its sandbox's copy: memory.max = the limit + the VMM's own room
 * (VMM_ZAPAS), fixed for the zone's whole life (ADR 0044 T4); RLIMIT_FSIZE = the limit (the memfd of its memory). */
#define VMM_ZAPAS (184ull << 20) /* the resources of virtio-gpu (up to 96 MiB), the window, the VMM, KVM, the disk's cache */
static uint32_t granica_strefy(const struct k1_strefa_opis *z, struct k1_profil *p)
{
    struct sysinfo si;
    uint64_t ram = sysinfo(&si) == 0 ? ((uint64_t)si.totalram * si.mem_unit) >> 20 : 0;
    uint32_t g = pamiec_granica(ram, z->pamiec_mib);
    p->pamiec = ((uint64_t)g << 20) + VMM_ZAPAS;
    p->rozmiar_pliku = (uint64_t)g << 20;
    return g;
}

/* The guest's memory at the start and what the VMM needs fit in what is free now — with what the running sessions'
 * balloons may still give (0.10.1 step 3, S5: on 4 GB a second session can start when the first one is light). */
static int jest_pamiec(const struct k1_strefa_opis *z)
{
    struct sysinfo si;
    long long wolne;
    if (sysinfo(&si) != 0 || si.mem_unit == 0)
        return 1;
    wolne = (long long)si.totalram * si.mem_unit - pamiec_uzyta(&si) + (strefy_moga_oddac() << 20);
    return wolne >= (long long)((uint64_t)z->pamiec_mib << 20) + (long long)ZAPAS_PAMIECI;
}

/* Opens what the VMM gets (fd[1..3]: kvm, kernel, image); 0, or the K1_STREFA_* reason. */
static int otworz(const struct k1_strefa_opis *z, int *fd)
{
    fd[1] = open(DEV "/kvm", O_RDWR | O_CLOEXEC);
    if (fd[1] < 0) {
        k1_log(TAG, "strefa %d: brak wirtualizacji (/dev/kvm: %s) — włącz ją w BIOS-ie", z->nr, strerror(errno));
        return K1_STREFA_BEZ_WIRTUALIZACJI;
    }
    fd[2] = open(STREFY "/jadro", O_RDONLY | O_CLOEXEC);
    fd[3] = obraz_strefy_otworz(z->obraz); /* 0.10.9 step 4: from the slot of this build (obraz_strefy.c) */
    if (fd[2] < 0 || fd[3] < 0) {
        k1_log(TAG, "strefa %d: brak jądra strefy albo jej obrazu", z->nr);
        return K1_STREFA_BRAK_OBRAZU;
    }
    if (!jest_pamiec(z)) {
        k1_log(TAG, "strefa %d: za mało wolnej pamięci (potrzeba %u MiB)", z->nr, z->pamiec_mib);
        return K1_STREFA_ZA_MALO_PAMIECI;
    }
    return 0;
}

/* A zone that remembers: fd[5] the channel to skarbiec, fd[6] the empty disk; 0 when the zone goes without them. */
static int dysk(const struct k1_strefa_opis *z, int *fd)
{
    if (!sesja_pamieta(z->nr))
        return 0;
    fd[5] = kanal_do_skarbca(z->profil, z->uid);
    fd[6] = open(STREFY "/dysk.img", O_RDONLY | O_CLOEXEC);
    if (fd[5] >= 0 && fd[6] >= 0)
        return 2;
    k1_log(TAG, "strefa %d: bez dysku (skarbiec albo pusty dysk strefy niedostępny) — zapomni po zamknięciu", z->nr);
    for (int i = 5; i < 7; i++)
        if (fd[i] >= 0)
            close(fd[i]), fd[i] = -1;
    return 0;
}

/* The memory events of zone nr's sandbox (memory.events: its counters live as long as the cgroup, which nadzorca
 * reuses — so the values at the start are kept and the end compares with them); the peak of its memory in MiB. */
static struct strefa_zdarzenia zdarzenia_od[K1_STREFY_MAKS + 1];

static struct strefa_zdarzenia zdarzenia(int nr)
{
    const struct k1_strefa_opis *z = k1_strefa(nr);
    char sciezka[128];
    struct strefa_zdarzenia d = { -1, -1 };
    if (z == NULL)
        return d;
    snprintf(sciezka, sizeof sciezka, K1_CGROUP "/%s/memory.events", profile[z->profil].nazwa);
    d.oom = cgroup_liczba(sciezka, "oom");
    d.oom_kill = cgroup_liczba(sciezka, "oom_kill");
    return d;
}

static void zdarzenia_start(int nr)
{
    struct strefa_zdarzenia d = zdarzenia(nr);
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return;
    zdarzenia_od[nr] = (struct strefa_zdarzenia){ d.oom > 0 ? d.oom : 0, /* no cgroup yet: a new one starts at 0 */
                                                  d.oom_kill > 0 ? d.oom_kill : 0 };
}

void strefa_uruchom(int nr, int tryb, int z_testu)
{
    const struct k1_strefa_opis *z = k1_strefa(nr);
    struct usluga *u = z != NULL ? usluga_profilu(z->profil) : NULL;
    int fd[8] = { -1, -1, -1, -1, -1, -1, -1, -1 }, powod, i, d, test_grafiki;
    struct k1_profil p;
    uint32_t granica;
    if (u == NULL)
        return;
    if (u->pid > 0) {
        if (!strefa_otworz_po_zamknieciu(nr, tryb, z_testu)) /* 0.10.2 step 1 (D2): it waits for the old machine */
            k1_log(TAG, "strefa %d już działa", nr);
        return;
    }
    powod = otworz(z, fd);
    if (powod == 0) {
        fd[0] = z->okno ? kanal_do_ekranu(K1_ROLA_STREFA, nr, z->uid) : dup(zero); /* no window: no channel to ekran */
        if (fd[0] < 0)
            k1_log(TAG, "strefa %d: ekran nie działa", nr);
        fd[4] = strefa_kanal_nowy(nr);
        p = profile[z->profil];
        granica = granica_strefy(z, &p);
        snprintf(u->argument, sizeof u->argument, "%d", nr);
        snprintf(u->argument2, sizeof u->argument2, "%d", tryb);
        snprintf(u->argument3, sizeof u->argument3, "%u", granica);
        snprintf(u->argument4, sizeof u->argument4, "%s", z->okno ? sesja_nazwa(nr) : cel_pobierania); /* D1; ADR 0046 */
        d = dysk(z, fd);
        test_grafiki = z_testu && (tryb == K1_STREFA_TRYB_GRAFIKA || tryb == K1_STREFA_TRYB_GRAFIKA_ZLA);
        if (test_grafiki || (z->okno && sesja_grafika(nr) && posrednik_karta_umie())) /* 0.10.9 step 6 */
            fd[7] = posrednik_dla_strefy(nr, test_grafiki ? 2 : 0); /* 2: the test engine for the VMM */
        if (fd[7] >= 0) {
            if (d == 0)
                fd[5] = dup(zero), fd[6] = dup(zero), d = -2; /* places of the disk: none (dane.c sees /dev/null) */
            p.pamiec += K1_POSR_PAMIEC;
        }
        zdarzenia_start(nr);
        if (fd[0] >= 0 && fd[4] >= 0 && uruchom(u, &p, fd, fd[7] >= 0 ? 8 : 5 + d) > 0 &&
            (strefa_pamiec_start(nr, granica, z->pamiec_mib, !z_testu || tryb == K1_STREFA_TRYB_ROZDZIELACZ), 1))
            k1_log(TAG, "strefa %d (%s): pid %d, uid %d, pamięć %u MiB na start, granica %u MiB, procesory %u, %s%s%s", nr,
                   z->tytul, (int)u->pid, z->uid, z->pamiec_mib, granica, z->procesory,
                   d > 0 ? "pamięta (dysk w skarbcu)" : "zapomina po zamknięciu", fd[7] >= 0 ? ", szybka grafika" : "",
                   tryb ? ", gość testowy" : "");
        else
            k1_log(TAG, "strefa %d: uruchomienie nieudane", nr), strefa_kanal_koniec(nr), posrednik_strefa_koniec(nr);
    } else {
        zglos(nr, powod);
    }
    for (i = 0; i < 8; i++)
        if (fd[i] >= 0)
            close(fd[i]);
}

/* Why the VMM ended (strefy_powod.c: its exit code — src/strefa/main.c, K1_STREFA_KONIEC_* — or SIGKILL and the
 * sandbox's events: its own limit, or the computer's memory); 0: the user closed it. The log has the numbers. */
int strefa_pobierania(const char *cel, int test)
{
    struct usluga *u = usluga_profilu(k1_strefa(K1_STREFA_POBIERANIE)->profil);
    snprintf(cel_pobierania, sizeof cel_pobierania, "%s", cel);
    strefa_uruchom(K1_STREFA_POBIERANIE, test ? K1_STREFA_TRYB_AKTUALIZACJA : 0, test);
    return u != NULL && u->pid > 0;
}

void strefa_koniec(int nr, int status, int cicho)
{
    struct strefa_zdarzenia d = zdarzenia(nr), r = { -1, -1 };
    const struct k1_strefa_opis *z = k1_strefa(nr);
    char sciezka[128];
    long long szczyt = -1;
    int powod;
    if (z == NULL)
        return;
    if (!z->okno)
        akt_strefa_koniec(); /* 0.10.5 step 2b: the writing program sees the end of the download */
    if (d.oom >= 0 && d.oom_kill >= 0 && zdarzenia_od[nr].oom >= 0 && zdarzenia_od[nr].oom_kill >= 0)
        r = (struct strefa_zdarzenia){ d.oom - zdarzenia_od[nr].oom, d.oom_kill - zdarzenia_od[nr].oom_kill };
    snprintf(sciezka, sizeof sciezka, K1_CGROUP "/%s/memory.peak", profile[z->profil].nazwa);
    szczyt = cgroup_liczba(sciezka, NULL);
    powod = strefa_powod_konca(status, &r);
    if (strefa_pamiec_koniec(nr)) /* K1 closed it itself: the computer's memory (D3 (3)) — and says so */
        powod = K1_STREFA_KONIEC_PAMIEC_K1, cicho = 0;
    k1_log(TAG, "strefa %d: pamięć klatki — szczyt %lld MiB (od startu K1), limit osiągnięty %lld, zabite %lld", nr,
           szczyt >= 0 ? szczyt >> 20 : -1, r.oom, r.oom_kill);
    if (powod == 0)
        return;
    k1_log(TAG, "strefa %d zakończyła się sama (powód %d)%s", nr, powod, cicho ? " — bez okienka" : "");
    if (!cicho)
        zglos(nr, powod);
}

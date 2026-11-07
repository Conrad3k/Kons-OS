/* K1 nadzorca — battery state for gui and Ustawienia, resource usage for Zasoby. Part of nadzorca (see main.c). */
#include "nadzorca.h"

int zasoby_kanal = -1; /* our end of the channel to Zasoby (its fd 4; we only write) */
int bateria_kanal = -1; /* our end of the battery channel to gui (its fd 5; we only write) */
struct k1_bateria bateria_wyslana;
int bateria_znana; /* bateria_wyslana is what gui got on this channel */
/* 0.5.5: battery state and charge history since boot for Ustawienia (its fd 4; we only write) */
int ustaw_kanal = -1;
struct k1_bateria bateria_teraz, ustaw_wyslana;
int bateria_odczytana, ustaw_znana, ustaw_hist_wyslane;
struct k1_bat_historia bat_hist;
double nastepna_probka;
/* Battery (0.5.3, 0.5.5): read from sysfs power_supply every BATERIA_CO s (an ACPI battery talks to
 * the embedded controller), a history sample every bat_hist.interwal s. */
void odczytaj_baterie(double t)
{
    k1_bateria_odczytaj(SYS "/class/power_supply", &bateria_teraz);
    bateria_odczytana = 1;
    if (t >= nastepna_probka) {
        if (k1_bat_hist_dodaj(&bat_hist, &bateria_teraz))
            ustaw_hist_wyslane = 0; /* compacted: Ustawienia gets it again from the start */
        nastepna_probka = t + bat_hist.interwal;
    }
}

struct k1_komunikat komunikat_baterii(const struct k1_bateria *b)
{
    return k1_komunikat_nowy(K1_KOM_BATERIA, 0, b->obecna, b->obecna ? b->procent : -1, b->obecna ? b->stan : 0,
                             b->zasilacz, 0);
}

/* gui's top bar: the state when it changed (the first time after gui started: always). */
void wyslij_baterie(void)
{
    struct k1_komunikat k;
    if (bateria_kanal < 0 || !bateria_odczytana ||
        (bateria_znana && memcmp(&bateria_teraz, &bateria_wyslana, sizeof bateria_teraz) == 0))
        return;
    k = komunikat_baterii(&bateria_teraz);
    if (k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) == 0) {
        bateria_wyslana = bateria_teraz;
        bateria_znana = 1;
    }
}

/* Ustawienia → Bateria: the state when it changed and the history not sent yet, as long as the
 * channel takes it (a full socket buffer: the rest on the next loop). */
void wyslij_ustawieniom(void)
{
    struct k1_komunikat k;
    int ile;
    if (ustaw_kanal < 0 || !bateria_odczytana)
        return;
    if (!ustaw_znana || memcmp(&bateria_teraz, &ustaw_wyslana, sizeof bateria_teraz) != 0) {
        k = komunikat_baterii(&bateria_teraz);
        if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) != 0)
            return;
        ustaw_wyslana = bateria_teraz;
        ustaw_znana = 1;
    }
    while ((ile = k1_bat_hist_komunikat(&bat_hist, ustaw_hist_wyslane, &k)) > 0) {
        if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) != 0)
            return;
        ustaw_hist_wyslane += ile;
    }
}

/* Value of `klucz` ("usage_usec", "file", ...) in a cgroup key-value file, or of the single number
 * in the file when klucz is NULL. -1 if missing. */
long long liczba_z_pliku(const char *sciezka, const char *klucz)
{
    char buf[4096];
    ssize_t n;
    int fd = open(sciezka, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    const char *p = buf;
    size_t kl = klucz ? strlen(klucz) : 0;
    if (fd < 0)
        return -1;
    n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    while (klucz != NULL) {
        if (strncmp(p, klucz, kl) == 0 && p[kl] == ' ') {
            p += kl + 1;
            break;
        }
        p = strchr(p, '\n');
        if (p == NULL)
            return -1;
        p++;
    }
    if (*p < '0' || *p > '9')
        return -1;
    return strtoll(p, NULL, 10);
}

long long cpu_usec(const char *grupa)
{
    char sciezka[128];
    snprintf(sciezka, sizeof sciezka, K1_CGROUP "%s%s/cpu.stat", grupa[0] ? "/" : "", grupa);
    return liczba_z_pliku(sciezka, "usage_usec");
}

int32_t promile(long long delta_usec, double delta_s, long cpus)
{
    double v = (double)delta_usec / (delta_s * 1e6 * (double)cpus) * 1000.0;
    return v < 0 ? 0 : (v > 1000 ? 1000 : (int32_t)(v + 0.5));
}

/* Once a second while Zasoby runs: the whole system, then every service. */
void raport_zasobow(void)
{
    static long long poprz_sys = -1, poprz[K1_PROFIL_MAKS + 1];
    static double poprz_t;
    double t = teraz(), dt = t - poprz_t;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long long uz = cpu_usec(""), plik, shmem, uzyta;
    struct sysinfo si;
    size_t i;
    struct k1_komunikat k;

    if (cpus < 1 || cpus > 4096)
        cpus = 1;
    if (sysinfo(&si) != 0 || si.mem_unit == 0)
        return;
    plik = liczba_z_pliku(K1_CGROUP "/memory.stat", "file");
    shmem = liczba_z_pliku(K1_CGROUP "/memory.stat", "shmem");
    /* used = total - free - page cache that can be dropped (tmpfs data stays "used") */
    uzyta = ((long long)si.totalram - (long long)si.freeram) * (long long)si.mem_unit;
    if (plik >= 0 && shmem >= 0 && plik > shmem)
        uzyta -= plik - shmem;
    if (uzyta < 0)
        uzyta = 0;
    k = k1_komunikat_nowy(K1_KOM_ZASOBY_SYSTEM, 0, poprz_sys >= 0 && uz >= 0 && dt > 0.1 ? promile(uz - poprz_sys, dt, cpus) : 0,
                          (int32_t)(uzyta >> 20), (int32_t)(((long long)si.totalram * si.mem_unit) >> 20), (int32_t)cpus,
                          (int32_t)(si.uptime > INT32_MAX ? INT32_MAX : si.uptime));
    if (k.b > k.c)
        k.b = k.c;
    if (k.c < 1)
        k.c = 1;
    k1_kanal_wyslij(zasoby_kanal, &k, sizeof k, -1);
    for (i = 0; i < N_USLUG; i++) {
        const struct k1_profil *p = &profile[uslugi[i].profil];
        char sciezka[128];
        long long u = cpu_usec(p->nazwa), pam;
        int pr = uslugi[i].profil;
        snprintf(sciezka, sizeof sciezka, K1_CGROUP "/%s/memory.current", p->nazwa);
        pam = uslugi[i].pid > 0 ? liczba_z_pliku(sciezka, NULL) : 0;
        k = k1_komunikat_nowy(K1_KOM_ZASOBY_USLUGA, (uint32_t)pr,
                              uslugi[i].pid > 0 && poprz[pr] > 0 && u >= poprz[pr] && dt > 0.1 ? promile(u - poprz[pr], dt, cpus) : 0,
                              (int32_t)(pam > 0 ? (pam >> 10 > INT32_MAX ? INT32_MAX : pam >> 10) : 0),
                              uslugi[i].pid > 0 ? 1 : 0, 0, 0);
        poprz[pr] = u;
        k1_kanal_wyslij(zasoby_kanal, &k, sizeof k, -1);
    }
    poprz_sys = uz;
    poprz_t = t;
}

/* ---- battery history of earlier boots (0.6.0): kept in skarbiec as owner K1_WL_SYSTEM ---- */

static int arch_kanal = -1;     /* our channel to skarbiec (we are its client K1_WL_SYSTEM) */
static int arch_stan;           /* 0: not asked, 1: read requested, 2: loaded (or none saved) */
static int arch_zapisane;       /* a save of this boot already succeeded (logged once) */
static struct k1_bat_arch arch; /* earlier boots */
static uint32_t arch_start_min; /* when this boot started, minutes since 1970 */
static double arch_nastepny_zapis;
int ustaw_arch_wyslane;         /* Ustawienia has the archive (reset when it starts) */

static void arch_rozlacz(void)
{
    if (arch_kanal >= 0)
        close(arch_kanal);
    arch_kanal = -1;
    arch_stan = 0;
}

/* The running boot + the archive, as stored in skarbiec. */
static size_t arch_bajty(uint8_t *buf, size_t maks)
{
    struct k1_bat_sesja s;
    s.start_min = arch_start_min;
    s.interwal = (uint16_t)bat_hist.interwal;
    s.n = (uint16_t)bat_hist.n;
    s.od = 0;
    return k1_bat_arch_zapisz(&arch, &s, bat_hist.p, arch_start_min + (uint32_t)(od_startu_systemu() / 60.0), buf, maks);
}

static void arch_zapisz(void)
{
    static uint8_t buf[K1_BAT_ARCH_BAJTY];
    size_t n;
    if (arch_kanal < 0 || arch_stan != 2 || bat_hist.n == 0)
        return;
    n = arch_bajty(buf, sizeof buf);
    if (n > 0 && k1_skarb_zapisz(arch_kanal, K1_OB_BATERIA, buf, n) != 0)
        k1_log(TAG, "historia baterii: wysłanie do skarbca nieudane");
}

static void arch_odbierz(void)
{
    static uint8_t buf[K1_BAT_ARCH_BAJTY];
    int i;
    for (i = 0; i < 8 && arch_kanal >= 0; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(arch_kanal, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            arch_rozlacz();
            return;
        }
        if (w != K1_ODB_JEST || k.obiekt != K1_OB_BATERIA) {
            if (fd >= 0)
                close(fd);
            continue;
        }
        if (k.typ == K1_KOM_SKARB_DANE && arch_stan == 1) {
            int ok = k.a <= (int32_t)sizeof buf && k1_memfd_czytaj(fd, buf, (size_t)k.a) == 0 &&
                     k1_bat_arch_czytaj(buf, (size_t)k.a, &arch) == 0;
            arch_stan = 2;
            if (!ok)
                memset(&arch, 0, sizeof arch);
            k1_log(TAG, "historia baterii: %d poprzednich uruchomień%s", arch.n, ok ? "" : " (zapis uszkodzony — od nowa)");
        } else if (k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_CZYTAJ && arch_stan == 1) {
            arch_stan = 2; /* nothing saved yet */
            k1_log(TAG, "historia baterii: brak poprzednich uruchomień");
        } else if (k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_ZAPISZ) {
            if (k.b != 0)
                k1_log(TAG, "historia baterii: zapis w skarbcu nieudany (%d)", (int)k.b);
            else if (!arch_zapisane++)
                k1_log(TAG, "historia baterii zapisana (poprzednich uruchomień: %d)", arch.n);
        }
        if (fd >= 0)
            close(fd);
    }
}

/* Every loop: connect when the vault is open on disk and there is a battery, read the archive once,
 * save this boot at once and then every 5 minutes; give Ustawienia the archive. */
void bateria_archiwum(double t)
{
    if (stan_skarbca != K1_SKARB_TRWALY || skarbiec()->pid <= 0) {
        if (arch_kanal >= 0)
            arch_rozlacz();
        return;
    }
    if (!bateria_odczytana || !bateria_teraz.obecna)
        return;
    if (arch_start_min == 0) {
        struct timespec r;
        clock_gettime(CLOCK_REALTIME, &r);
        arch_start_min = (uint32_t)((r.tv_sec - (time_t)od_startu_systemu()) / 60);
    }
    if (arch_kanal < 0) {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_CZYTAJ, K1_OB_BATERIA, 0, 0, 0, 0, 0);
        arch_kanal = kanal_do_skarbca(K1_WL_SYSTEM, 0);
        if (arch_kanal < 0 || fcntl(arch_kanal, F_SETFL, O_NONBLOCK) != 0 ||
            k1_kanal_wyslij(arch_kanal, &k, sizeof k, -1) != 0) {
            arch_rozlacz();
            return;
        }
        arch_stan = 1;
        arch_nastepny_zapis = t;
    }
    arch_odbierz();
    if (arch_stan == 2 && t >= arch_nastepny_zapis && bat_hist.n > 0) {
        arch_zapisz();
        arch_nastepny_zapis = t + 300.0;
    }
    if (arch_stan == 2 && ustaw_kanal >= 0 && !ustaw_arch_wyslane) {
        static uint8_t buf[K1_BAT_ARCH_BAJTY];
        size_t n = k1_bat_arch_zapisz(&arch, NULL, NULL, arch_start_min, buf, sizeof buf);
        int m = n > 0 ? k1_memfd_z_danymi("k1-bateria", buf, n) : -1;
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_BATERIA_ARCH, 0, (int32_t)n, arch.n, 0, 0, 0);
        if (m >= 0 && k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, m) == 0)
            ustaw_arch_wyslane = 1;
        if (m >= 0)
            close(m);
    }
}

/* Before power off: this boot's history once more, and a moment for skarbiec to write it. */
void bateria_archiwum_koniec(void)
{
    struct pollfd p;
    double koniec = teraz() + 1.5;
    if (arch_kanal < 0 || arch_stan != 2 || bat_hist.n == 0)
        return;
    arch_zapisane = 1; /* no log line: the console may be going away */
    arch_zapisz();
    p.fd = arch_kanal;
    p.events = POLLIN;
    while (teraz() < koniec && poll(&p, 1, 200) >= 0) {
        struct k1_komunikat k;
        int fd = -1;
        if (k1_odbierz_komunikat(arch_kanal, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd) == K1_ODB_JEST) {
            if (fd >= 0)
                close(fd);
            if (k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_ZAPISZ)
                return;
        }
    }
}

/* K1 gosc — the memory of a zone (0.10.0 step 3, S26). The guest has a fixed amount of memory (its row of K1_STREFY)
 * and no disk to swap to; before this step, when a heavy page filled it, the guest kernel's OOM killer took the
 * largest process — often the browser itself — and the machine ended: the window was simply gone.
 *   - compressed memory (zram, the zone kernel's own: jadro/strefa.config): a swap device in the guest's RAM, so rarely
 *     used pages take a third of the room or less (the guest still never uses more than its row says);
 *   - the browser's main process is protected (oom_score_adj -900) and every other process of the browser (its pages)
 *     goes first (+300): out of memory, a page ends ("Karta uległa awarii" in the browser), not the browser;
 *   - the kernel's count of OOM kills (/proc/vmstat oom_kill) tells gosc why the browser ended.
 * Everything here is the guest's own (paths of the zone image), never K1's. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/sysinfo.h>
#include <time.h>
#include <unistd.h>

#include "gosc.h"

#define STRONA 4096u
#define CHRONIONY "-900"
#define NAJPIERW 300

static int zapisz(const char *plik, const char *tekst)
{
    int fd = open(plik, O_WRONLY | O_CLOEXEC), w = -1;
    if (fd >= 0) {
        w = write(fd, tekst, strlen(tekst)) == (ssize_t)strlen(tekst) ? 0 : -1;
        close(fd);
    }
    return w;
}

/* The swap header (include/linux/swap.h, union swap_header): version 1, the last page, "SWAPSPACE2" at the end of the
 * first page — what mkswap writes; the zone image has no mkswap (and no shell to run it). */
static int naglowek(const char *urz, uint64_t bajty)
{
    static unsigned char s[STRONA];
    uint32_t wersja = 1, ostatnia = (uint32_t)(bajty / STRONA - 1);
    int fd = open(urz, O_WRONLY | O_CLOEXEC), w;
    if (fd < 0)
        return -1;
    memset(s, 0, sizeof s);
    memcpy(s + 1024, &wersja, 4);
    memcpy(s + 1028, &ostatnia, 4);
    memcpy(s + STRONA - 10, "SWAPSPACE2", 10);
    w = write(fd, s, sizeof s) == (ssize_t)sizeof s && fsync(fd) == 0 ? 0 : -1;
    close(fd);
    return w;
}

void pamiec_zram(void)
{
    struct sysinfo si;
    uint64_t ram;
    char b[32];
    if (sysinfo(&si) != 0)
        return;
    ram = (uint64_t)si.totalram * si.mem_unit / STRONA * STRONA; /* as much as the guest's RAM (compressed: less) */
    snprintf(b, sizeof b, "%llu", (unsigned long long)ram);
    if (zapisz("/sys/block/zram0/disksize", b) != 0 || naglowek("/dev/zram0", ram) != 0 || swapon("/dev/zram0", 0) != 0) {
        loguj("pamięć kompresowana: nieudana (%s)", strerror(errno));
        return;
    }
    if (zapisz("/proc/sys/vm/swappiness", "100") != 0) /* zram is cheap to reach: use it as eagerly as page cache */
        loguj("swappiness: %s", strerror(errno));
    loguj("pamięć kompresowana: %llu MiB", (unsigned long long)(ram >> 20));
}

/* A small file of the guest's kernel, as text; the length, or -1. */
static ssize_t czytaj(const char *plik, char *b, size_t n)
{
    int fd = open(plik, O_RDONLY | O_CLOEXEC);
    ssize_t r = fd >= 0 ? read(fd, b, n - 1) : -1;
    if (fd >= 0)
        close(fd);
    if (r >= 0)
        b[r] = '\0';
    return r;
}

/* The kernel's count of processes killed for lack of memory, or 0. */
unsigned long pamiec_zabite(void)
{
    char b[8192];
    const char *p;
    if (czytaj("/proc/vmstat", b, sizeof b) <= 0)
        return 0;
    p = strstr(b, "\noom_kill ");
    return p != NULL ? strtoul(p + 10, NULL, 10) : 0;
}

/* The counters for the VMM (0.10.1 step 1, S13), every few seconds: "liczniki <available MiB> <zram MiB> <OOM kills>"
 * — numbers only; the VMM checks each and logs them in one line with its own. */
void pamiec_liczniki(int ster)
{
    char b[2048], l[64];
    const char *p;
    unsigned long dostepna = 0, zram = 0;
    int n;
    if (ster < 0)
        return;
    if (czytaj("/proc/meminfo", b, sizeof b) > 0 && (p = strstr(b, "MemAvailable:")) != NULL)
        dostepna = strtoul(p + 13, NULL, 10) >> 10; /* kB */
    if (czytaj("/sys/block/zram0/mm_stat", b, sizeof b) > 0) { /* orig_data_size compr_data_size mem_used_total … */
        char *k;
        strtoull(b, &k, 10);
        strtoull(k, &k, 10);
        zram = (unsigned long)(strtoull(k, NULL, 10) >> 20);
    }
    n = snprintf(l, sizeof l, "liczniki %lu %lu %lu\n", dostepna, zram, pamiec_zabite());
    if (n > 0 && n < (int)sizeof l && write(ster, l, (size_t)n) != n)
        loguj("kanał sterujący: zapis nieudany");
}

void pamiec_chron(pid_t glowny)
{
    char p[48];
    snprintf(p, sizeof p, "/proc/%d/oom_score_adj", (int)glowny);
    if (zapisz(p, CHRONIONY) != 0)
        loguj("ochrona przeglądarki: %s", strerror(errno));
}

/* Every process of user `uid` but `glowny` (they inherit its protection at fork) goes first when memory runs out. */
void pamiec_kolejnosc(uid_t uid, pid_t glowny)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    while (d != NULL && (e = readdir(d)) != NULL) {
        char p[64], b[16];
        long pid = strtol(e->d_name, NULL, 10);
        struct stat st;
        int fd;
        ssize_t n;
        snprintf(p, sizeof p, "/proc/%ld", pid);
        if (pid <= 1 || pid == glowny || stat(p, &st) != 0 || st.st_uid != uid)
            continue;
        snprintf(p, sizeof p, "/proc/%ld/oom_score_adj", pid);
        fd = open(p, O_RDONLY | O_CLOEXEC);
        n = fd >= 0 ? read(fd, b, sizeof b - 1) : -1;
        if (fd >= 0)
            close(fd);
        if (n <= 0)
            continue;
        b[n] = '\0';
        if (atoi(b) < NAJPIERW) {
            snprintf(b, sizeof b, "%d", NAJPIERW);
            zapisz(p, b);
        }
    }
    if (d != NULL)
        closedir(d);
}

/* TRYB_PAMIEC (0.10.1 step 2, ADR 0044; the test zone, scripts/test-strefa.sh): an honest guest that takes memory and
 * gives it back. (1) PROBA_MIB taken and touched, then freed — the guest kernel reports the free blocks and the VMM
 * hands them back to K1 (its log); (2) a pause while the test program inflates the balloon and lets it go (nadzorca:
 * STREFA_BALON — the guest's kernel does it, not this program); (3) a sudden jump: almost all of its available memory at
 * once, without waiting for anyone — the balloon is empty, the memory is there. Then the machine ends. */
#define PROBA_MIB 64
#define SKOK_PO_S 55
#define SKOK_ZAPAS_MIB 24 /* left free in the jump: the kernel and this program */

static int zajmij(size_t mib)
{
    size_t n = mib << 20, i;
    uint8_t *p = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return -1;
    for (i = 0; i < n; i += STRONA) /* every page touched, and not a zero page */
        memset(p + i, (int)(i / STRONA) | 1, 64);
    munmap(p, n);
    return 0;
}

static unsigned long dostepna_mib(void)
{
    char b[2048];
    const char *p;
    return czytaj("/proc/meminfo", b, sizeof b) > 0 && (p = strstr(b, "MemAvailable:")) != NULL
               ? strtoul(p + 13, NULL, 10) >> 10 : 0;
}

void pamiec_proba(void)
{
    time_t t0 = time(NULL);
    unsigned long d = dostepna_mib(), skok;
    loguj("pamięć: dostępna %lu MiB", d);
    loguj("pamięć: zajęte i zwolnione %d MiB: %s", PROBA_MIB, zajmij(PROBA_MIB) == 0 ? "tak" : "nie");
    for (time_t ost = t0; time(NULL) - t0 < SKOK_PO_S;) { /* busy, not asleep: an idle guest in nested emulation (the */
        sched_yield();                                     /* container's test, no KVM) may wait for its timer too long */
        if (time(NULL) - ost >= 10) {
            ost = time(NULL);
            loguj("pamięć: czekam (%ld s)", (long)(ost - t0));
        }
    }
    d = dostepna_mib();
    skok = d > SKOK_ZAPAS_MIB ? d - SKOK_ZAPAS_MIB : 0;
    loguj("pamięć: skok %lu MiB naraz: %s", skok, zajmij(skok) == 0 ? "żyję" : "nie dostałem");
}

/* TRYB_ROZDZIELACZ (0.10.1 step 3): memory the guest holds but does not need — the page cache of its disk, as a browser
 * keeps files in memory — so that K1's rozdzielacz has something to take by the balloon (the free pages the guest
 * reports are K1's anyway). Reads up to PODRECZNA_MIB of /dev/vdb, never so much that the guest has less than
 * PODRECZNA_ZAPAS free. */
#define PODRECZNA_MIB 64 /* more stalled the whole nested emulation of the container's test (no KVM) */
#define PODRECZNA_ZAPAS 24
void pamiec_podreczna(void)
{
    static char buf[1u << 20];
    int fd = open("/dev/vdb", O_RDONLY | O_CLOEXEC);
    unsigned mib = 0;
    while (fd >= 0 && mib < PODRECZNA_MIB && dostepna_mib() > PODRECZNA_ZAPAS && read(fd, buf, sizeof buf) == sizeof buf)
        mib++;
    if (fd >= 0)
        close(fd);
    loguj("pamięć: w pamięci podręcznej %u MiB z dysku (dostępna %lu MiB)", mib, dostepna_mib());
}

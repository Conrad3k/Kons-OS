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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/swap.h>
#include <sys/sysinfo.h>
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

/* The kernel's count of processes killed for lack of memory, or 0. */
unsigned long pamiec_zabite(void)
{
    char b[8192];
    const char *p;
    int fd = open("/proc/vmstat", O_RDONLY | O_CLOEXEC);
    ssize_t n = fd >= 0 ? read(fd, b, sizeof b - 1) : -1;
    if (fd >= 0)
        close(fd);
    if (n <= 0)
        return 0;
    b[n] = '\0';
    p = strstr(b, "\noom_kill ");
    return p != NULL ? strtoul(p + 10, NULL, 10) : 0;
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

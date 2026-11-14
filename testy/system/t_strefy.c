/* K1 in-system test — zones (0.10.0, ADR 0042): KVM in the K1 kernel.
 *
 * t_kvm: /dev/kvm is there (root only), and a tiny machine runs: a few bytes of 64-bit code in guest memory taken
 * from a memfd without the right to execute (vm.memfd_noexec=2 stays) — the guest writes one byte to an I/O port and
 * stops (KVM_EXIT_IO, KVM_EXIT_HLT). The guest starts in long mode with paging, like the zone kernel the VMM strefa
 * starts (in real mode QEMU 8.2's emulated nested paging gave the guest the wrong memory). */
#include <linux/kvm.h>

#include "k1_test.h"

#define DEV "/rdzen/jadro/dev"
#define GOSC_PAMIEC 0x200000 /* 2 MiB: one large page */
#define GOSC_KOD 0x1000
#define GOSC_PML4 0x2000
#define GOSC_PDPT 0x3000
#define GOSC_PD 0x4000

/* One KVM_RUN; the exit reason, or -1. */
static int uruchom(int vcpu, const struct kvm_run *run)
{
    if (ioctl(vcpu, (int)KVM_RUN, 0) != 0)
        return -1;
    return (int)run->exit_reason;
}

/* The guest: mov al, 0x42; out 0x10, al; hlt */
static const uint8_t kod[] = { 0xb0, 0x42, 0xe6, 0x10, 0xf4 };

/* The processor's features as KVM supports them (long mode among them: without it EFER.LME is refused). */
static int cpuid(int kvm, int vcpu)
{
    static struct {
        struct kvm_cpuid2 c;
        struct kvm_cpuid_entry2 e[128];
    } t;
    t.c.nent = 128;
    return ioctl(kvm, (int)KVM_GET_SUPPORTED_CPUID, &t) == 0 && ioctl(vcpu, (int)KVM_SET_CPUID2, &t) == 0 ? 0 : -1;
}

/* Identity paging of the first 2 MiB (one large page) and 64-bit segments. */
static void tryb_64(uint8_t *p, struct kvm_sregs *s)
{
    struct kvm_segment kodu = { .base = 0, .limit = 0xffffffff, .selector = 8, .type = 11, .present = 1, .s = 1, .l = 1,
                                .g = 1 };
    struct kvm_segment danych = { .base = 0, .limit = 0xffffffff, .selector = 16, .type = 3, .present = 1, .s = 1, .db = 1,
                                  .g = 1 };
    uint64_t w;
    w = GOSC_PDPT | 3;
    memcpy(p + GOSC_PML4, &w, 8);
    w = GOSC_PD | 3;
    memcpy(p + GOSC_PDPT, &w, 8);
    w = 0x83; /* present, writable, large page */
    memcpy(p + GOSC_PD, &w, 8);
    s->cs = kodu;
    s->ds = s->es = s->fs = s->gs = s->ss = danych;
    s->cr3 = GOSC_PML4;
    s->cr4 = 0x20;            /* PAE */
    s->cr0 = 0x80000011;      /* PG, ET, PE */
    s->efer = 0x500;          /* LMA, LME */
}

static void maszyna(int kvm)
{
    struct kvm_userspace_memory_region r = { 0 };
    struct kvm_sregs sregs;
    struct kvm_regs regs = { 0 };
    struct kvm_run *run = MAP_FAILED;
    long rozmiar_run;
    int vm, vcpu = -1, mem, wyjscie = -1, wyjscie2 = -1, bajt = -1;
    uint8_t *p = MAP_FAILED;

    vm = ioctl(kvm, (int)KVM_CREATE_VM, 0);
    check(vm >= 0, "kvm: KVM_CREATE_VM", "errno=%d", errno);
    if (vm < 0)
        return;
    mem = memfd(GOSC_PAMIEC, 1);
    if (mem >= 0)
        p = mmap(NULL, GOSC_PAMIEC, PROT_READ | PROT_WRITE, MAP_SHARED, mem, 0);
    if (p != MAP_FAILED) {
        memcpy(p + GOSC_KOD, kod, sizeof kod);
        r.memory_size = GOSC_PAMIEC;
        r.userspace_addr = (uint64_t)(uintptr_t)p;
    }
    check(p != MAP_FAILED && ioctl(vm, (int)KVM_SET_USER_MEMORY_REGION, &r) == 0,
          "kvm: pamięć gościa z memfd bez prawa wykonania", "errno=%d", errno);
    vcpu = ioctl(vm, (int)KVM_CREATE_VCPU, 0);
    rozmiar_run = ioctl(kvm, (int)KVM_GET_VCPU_MMAP_SIZE, 0);
    if (vcpu >= 0 && rozmiar_run > 0)
        run = mmap(NULL, (size_t)rozmiar_run, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu, 0);
    if (p != MAP_FAILED && run != MAP_FAILED && cpuid(kvm, vcpu) == 0 && ioctl(vcpu, (int)KVM_GET_SREGS, &sregs) == 0) {
        tryb_64(p, &sregs);
        regs.rip = GOSC_KOD;
        regs.rflags = 2;
        if (ioctl(vcpu, (int)KVM_SET_SREGS, &sregs) == 0 && ioctl(vcpu, (int)KVM_SET_REGS, &regs) == 0) {
            wyjscie = uruchom(vcpu, run);
            if (wyjscie == KVM_EXIT_IO && run->io.direction == KVM_EXIT_IO_OUT && run->io.port == 0x10 &&
                run->io.size == 1 && run->io.count == 1)
                bajt = ((const uint8_t *)run)[run->io.data_offset];
            wyjscie2 = uruchom(vcpu, run);
        }
    }
    check(wyjscie == KVM_EXIT_IO && bajt == 0x42 && wyjscie2 == KVM_EXIT_HLT, "kvm: gość działa (port, zatrzymanie)",
          "wyjścia %d/%d, bajt %#x, errno=%d", wyjscie, wyjscie2, bajt, errno);
    if (run != MAP_FAILED)
        munmap(run, (size_t)rozmiar_run);
    if (p != MAP_FAILED)
        munmap(p, GOSC_PAMIEC);
    if (vcpu >= 0)
        close(vcpu);
    if (mem >= 0)
        close(mem);
    close(vm);
}

void t_kvm(void)
{
    struct stat st = { 0 };
    int kvm;
    check(stat(DEV "/kvm", &st) == 0 && S_ISCHR(st.st_mode) && (st.st_mode & 0077) == 0 && st.st_uid == 0,
          "kvm: /rdzen/jadro/dev/kvm tylko dla roota", "mode=%o uid=%u", (unsigned)st.st_mode, (unsigned)st.st_uid);
    kvm = open(DEV "/kvm", O_RDWR | O_CLOEXEC);
    if (kvm < 0) {
        /* no virtualization in this processor (or switched off in the BIOS): the device opens, KVM_CREATE_VM fails */
        check(0, "kvm: otwarcie /dev/kvm", "errno=%d", errno);
        return;
    }
    check(ioctl(kvm, (int)KVM_GET_API_VERSION, 0) == KVM_API_VERSION, "kvm: wersja API 12", "errno=%d", errno);
    maszyna(kvm);
    close(kvm);
}

/* ---- scripts/test-strefa.sh: the test program in "zones" mode — the script adds a small disk with the serial
 * number "k1-test-strefy" (/proc/cmdline is not there: /proc has subset=pid) ---- */

int tryb_strefy(void)
{
    DIR *d = opendir("/rdzen/jadro/sys/block");
    struct dirent *de;
    int jest = 0;
    while (d != NULL && !jest && (de = readdir(d)) != NULL) {
        char p[300], b[64];
        snprintf(p, sizeof p, "/rdzen/jadro/sys/block/%s/serial", de->d_name);
        jest = de->d_name[0] != '.' && read_file(p, b, sizeof b) == 0 && strncmp(b, "k1-test-strefy", 14) == 0;
    }
    if (d != NULL)
        closedir(d);
    return jest;
}

/* The VMM of the test zone is running (its process, seen as root). */
static int strefa_dziala(void)
{
    return find_process("strefa") > 0;
}

/* Waits up to `sekundy` until the zone's VMM runs (1) or no longer runs (0); 1 when it did. */
static int czekaj_strefa(int dziala, int sekundy)
{
    int i;
    for (i = 0; i < sekundy * 10; i++) {
        if (strefa_dziala() == dziala)
            return 1;
        msleep(100);
    }
    return 0;
}

/* The scenario's guest side: the test zone with an honest guest — the script clicks, types, closes it through its
 * badge; then one hostile guest after another (gosc/zlosliwy.c), each must end only its own machine. */
int t_strefy_scenariusz(void)
{
    int tryb, i;
    for (i = 0; i < 600 && find_process("gui") <= 0; i++) /* the zone's window needs ekran, the badge's test gui */
        msleep(100);
    msleep(3000);
    k1_log(TAG, "strefy: tryb testu stref");
    if (absent(DEV "/kvm")) { /* a processor without virtualization: nadzorca must say so, nothing starts */
        zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, K1_STREFA_PROBNA, 0, 0, 0, 0), NULL, 10);
        k1_log(TAG, "strefy: brak wirtualizacji — strefa %s", czekaj_strefa(1, 10) ? "BŁĘDNIE ruszyła" : "nie ruszyła");
    }
    for (tryb = absent(DEV "/kvm") ? K1_STREFA_TRYB_MAKS + 1 : 0; tryb <= K1_STREFA_TRYB_MAKS; tryb++) {
        if (zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, K1_STREFA_PROBNA, tryb, 0, 0, 0), NULL, 10) != 0 ||
            !czekaj_strefa(1, 30)) {
            k1_log(TAG, "strefy: strefa (tryb %d) nie ruszyła", tryb);
            continue;
        }
        k1_log(TAG, "strefy: strefa próbna uruchomiona (tryb %d)", tryb);
        if (!czekaj_strefa(0, tryb == 0 ? 1200 : 120))
            k1_log(TAG, "strefy: strefa (tryb %d) nie skończyła się", tryb);
        else
            k1_log(TAG, "strefy: strefa skończyła się (tryb %d)", tryb);
    }
    k1_log(TAG, "strefy: koniec scenariusza");
    for (;;)
        pause();
}

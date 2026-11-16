/* K1 strefa — the machine in KVM (Documentation/virt/kvm/api.rst): memory from a memfd without the right to execute
 * (vm.memfd_noexec=2; the guest's code runs only in guest mode, through EPT/NPT — ADR 0042 T4), the interrupt
 * controllers and the timer in the kernel (KVM_CREATE_IRQCHIP, KVM_CREATE_PIT2), one irqfd per device, one thread per
 * processor. Exits: MMIO of the devices (under the lock), the keyboard controller's reset (reboot=k: the guest ended),
 * anything else unknown is counted and answered with nothing (reads give 0xff). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/kvm.h>

#include "k1_log.h"
#include "k1_memfd.h"
#include "strefa.h"

#define TSS 0xfffbd000u            /* Intel VT-x needs these 3 pages + 1 below 4 GiB (api.rst: KVM_SET_TSS_ADDR) */
#define MAPA_TOZSAMA 0xfffbc000u
#define PORT_8042 0x64             /* reboot=k: the guest writes 0xfe here to reset the machine */
#define RESET_8042 0xfe
#define CPUID_WPISY 128

/* K1 itself runs in QEMU's emulation (TCG: the hypervisor leaf says "TCGTCGTCGTCG") — the tests without KVM. Its
 * emulated SVM breaks a second processor of the zone (it starts in real mode, without paging: ADR 0042 T10), so a zone
 * gets one there; on a real processor (or KVM under KVM in CI) the row's number. */
static int emulacja_qemu(void)
{
    unsigned a, b, c, d;
    char p[13];
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x40000000u), "c"(0u));
    memcpy(p, &b, 4);
    memcpy(p + 4, &c, 4);
    memcpy(p + 8, &d, 4);
    p[12] = '\0';
    return strcmp(p, "TCGTCGTCGTCG") == 0;
}

int kvm_maszyna(uint64_t pamiec_mib, int procesory)
{
    struct kvm_userspace_memory_region r = { 0 };
    struct kvm_pit_config pit = { 0 };
    uint64_t rozmiar = pamiec_mib << 20;
    int fd;
    void *p;
    if (ioctl(mz.kvm, (int)KVM_GET_API_VERSION, 0) != KVM_API_VERSION) {
        k1_log(TAG, "zła wersja KVM");
        return -1;
    }
    mz.vm = ioctl(mz.kvm, (int)KVM_CREATE_VM, 0);
    mz.rozmiar_run = ioctl(mz.kvm, (int)KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mz.vm < 0 || mz.rozmiar_run <= 0) {
        k1_log(TAG, "KVM_CREATE_VM: %s", strerror(errno));
        return -1;
    }
    fd = k1_memfd_nowy("strefa-pamiec"); /* MFD_NOEXEC_SEAL: never executable in K1 */
    if (fd < 0 || ftruncate(fd, (off_t)rozmiar) != 0) {
        k1_log(TAG, "pamięć strefy (%llu MiB): %s", (unsigned long long)pamiec_mib, strerror(errno));
        return -1;
    }
    p = mmap(NULL, rozmiar, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_NORESERVE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        k1_log(TAG, "mmap pamięci strefy: %s", strerror(errno));
        return -1;
    }
    mz.pamiec.baza = p;
    mz.pamiec.rozmiar = rozmiar;
    r.memory_size = rozmiar;
    r.userspace_addr = (uint64_t)(uintptr_t)p;
    if (ioctl(mz.vm, (int)KVM_SET_USER_MEMORY_REGION, &r) != 0 || ioctl(mz.vm, (int)KVM_SET_TSS_ADDR, TSS) != 0) {
        k1_log(TAG, "pamięć w KVM: %s", strerror(errno));
        return -1;
    }
    {
        uint64_t mapa = MAPA_TOZSAMA;
        if (ioctl(mz.vm, (int)KVM_SET_IDENTITY_MAP_ADDR, &mapa) != 0 || ioctl(mz.vm, (int)KVM_CREATE_IRQCHIP, 0) != 0 ||
            ioctl(mz.vm, (int)KVM_CREATE_PIT2, &pit) != 0) {
            k1_log(TAG, "przerwania w KVM: %s", strerror(errno));
            return -1;
        }
    }
    mz.n_cpu = procesory > 1 && emulacja_qemu() ? 1 : procesory;
    if (mz.n_cpu != procesory)
        k1_log(TAG, "emulacja QEMU (testy): 1 procesor zamiast %d", procesory);
    return 0;
}

/* ---- devices ---- */

static void przerwij(struct mmio_urz *u)
{
    uint64_t jeden = 1;
    int i = (int)(u - mz.urz);
    if (write(mz.irqfd[i], &jeden, sizeof jeden) < 0)
        mz.nieznane++;
}

int kvm_urzadzenia(void)
{
    int i;
#define X_OPS(n, ops) &ops,
    static const struct mmio_ops *const ops[URZ_LICZBA] = { K1_URZADZENIA(X_OPS) };
#undef X_OPS
    for (i = 0; i < URZ_LICZBA; i++) {
        struct kvm_irqfd f = { 0 };
        mz.irqfd[i] = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        f.fd = (uint32_t)mz.irqfd[i];
        f.gsi = (uint32_t)(PRZERWANIE_PIERWSZE + i);
        if (mz.irqfd[i] < 0 || ioctl(mz.vm, (int)KVM_IRQFD, &f) != 0) {
            k1_log(TAG, "irqfd urządzenia %d: %s", i, strerror(errno));
            return -1;
        }
        /* the negative tests 3 and 4 (gosc/zlosliwy.c) need a read-only disk the guest's kernel does not drive */
        mmio_start(&mz.urz[i], i == URZ_LOSOWOSC && mz.tryb >= 3 && mz.tryb <= ST_TRYB_ZLOSLIWY ? &blk_ops : ops[i], NULL,
                   &mz.pamiec, przerwij);
    }
    return 0;
}

void kvm_linia_urzadzen(char *buf, size_t n)
{
    size_t p = 0;
    int i;
    buf[0] = '\0';
    for (i = 0; i < URZ_LICZBA && p < n; i++)
        if (!(zlosliwy_gosc() && i == URZ_LOSOWOSC)) /* a hostile test guest drives that one itself */
            p += (size_t)snprintf(buf + p, n - p, "%svirtio_mmio.device=4K@0x%llx:%d", p ? " " : "",
                              (unsigned long long)(MMIO_BAZA + (uint64_t)i * MMIO_ROZMIAR), PRZERWANIE_PIERWSZE + i);
}

/* ---- processors ---- */

static int cpuid(int fd, int nr)
{
    static struct {
        struct kvm_cpuid2 c;
        struct kvm_cpuid_entry2 e[CPUID_WPISY];
    } t;
    uint32_t i;
    t.c.nent = CPUID_WPISY;
    if (ioctl(mz.kvm, (int)KVM_GET_SUPPORTED_CPUID, &t) != 0)
        return -1;
    for (i = 0; i < t.c.nent; i++) {
        struct kvm_cpuid_entry2 *e = &t.e[i];
        if (e->function == 1) {
            e->ebx = (e->ebx & 0x00ffffffu) | ((uint32_t)nr << 24); /* the initial APIC id */
            e->ecx |= 1u << 31;                                     /* "running under a hypervisor" */
        } else if (e->function == 0xb || e->function == 0x1f) {
            e->edx = (uint32_t)nr; /* the x2APIC id */
        } else if (e->function == 7 && e->index == 0) {
            /* no 5-level paging: a zone has at most 3 GiB, and the kernel's switch to it runs with paging off for a
             * moment (QEMU 8.2's emulated nested paging gives such a guest the wrong memory — ADR 0042 T10) */
            e->ecx &= ~(1u << 16);
        }
    }
    return ioctl(fd, (int)KVM_SET_CPUID2, &t);
}

/* LINT0 ExtINT, LINT1 NMI (as the MP table says) */
static int lint(int fd)
{
    struct kvm_lapic_state l;
    uint32_t v;
    if (ioctl(fd, (int)KVM_GET_LAPIC, &l) != 0)
        return -1;
    memcpy(&v, l.regs + 0x350, 4);
    v = (v & ~0x700u) | (7u << 8);
    memcpy(l.regs + 0x350, &v, 4);
    memcpy(&v, l.regs + 0x360, 4);
    v = (v & ~0x700u) | (4u << 8);
    memcpy(l.regs + 0x360, &v, 4);
    return ioctl(fd, (int)KVM_SET_LAPIC, &l);
}

/* The first processor: long mode, the boot segments, the kernel's entry (start.h). */
static int rejestry(int fd, const struct st_wejscie *w)
{
    struct kvm_segment kod = { .base = 0, .limit = 0xffffffff, .selector = 0x10, .type = 11, .present = 1, .s = 1,
                               .l = 1, .g = 1 };
    struct kvm_segment dane = { .base = 0, .limit = 0xffffffff, .selector = 0x18, .type = 3, .present = 1, .s = 1,
                                .db = 1, .g = 1 };
    struct kvm_sregs s;
    struct kvm_regs r = { 0 };
    if (ioctl(fd, (int)KVM_GET_SREGS, &s) != 0)
        return -1;
    s.cs = kod;
    s.ds = s.es = s.fs = s.gs = s.ss = dane;
    s.gdt.base = w->gdt;
    s.gdt.limit = 4 * 8 - 1;
    s.idt.base = 0;
    s.idt.limit = 0;
    s.cr3 = w->cr3;
    s.cr4 = 0x20;         /* PAE */
    s.cr0 = 0x80000033u;  /* PG, NE, ET, MP, PE */
    s.efer = 0x500;       /* LMA, LME */
    r.rip = w->rip;
    r.rsi = w->rsi;
    r.rflags = 2;
    if (ioctl(fd, (int)KVM_SET_SREGS, &s) != 0)
        return -1;
    return ioctl(fd, (int)KVM_SET_REGS, &r);
}

int kvm_procesory(const struct st_wejscie *w)
{
    int i;
    for (i = 0; i < mz.n_cpu; i++) {
        struct procesor *c = &mz.cpu[i];
        c->fd = ioctl(mz.vm, (int)KVM_CREATE_VCPU, i);
        if (c->fd < 0) {
            k1_log(TAG, "procesor %d: %s", i, strerror(errno));
            return -1;
        }
        c->run = mmap(NULL, (size_t)mz.rozmiar_run, PROT_READ | PROT_WRITE, MAP_SHARED, c->fd, 0);
        if (c->run == MAP_FAILED || cpuid(c->fd, i) != 0 || lint(c->fd) != 0 || (i == 0 && rejestry(c->fd, w) != 0)) {
            k1_log(TAG, "procesor %d: ustawienie: %s", i, strerror(errno));
            return -1;
        }
    }
    return 0;
}

/* ---- exits ---- */

static void io(struct kvm_run *r)
{
    uint8_t *dane = (uint8_t *)r + r->io.data_offset;
    uint64_t n = (uint64_t)r->io.size * r->io.count;
    mz.wyjscia_io++;
    if (r->io.data_offset > (uint64_t)mz.rozmiar_run || n > (uint64_t)mz.rozmiar_run - r->io.data_offset)
        return;
    if (r->io.direction == KVM_EXIT_IO_OUT) {
        if (r->io.port == PORT_8042 && r->io.size == 1 && dane[0] == RESET_8042)
            koniec("gość zakończył pracę (reset)");
        return;
    }
    memset(dane, r->io.port == PORT_8042 ? 0 : 0xff, (size_t)n); /* 8042: "input buffer empty" for reboot=k */
}

static void mmio(struct procesor *c, struct kvm_run *r)
{
    uint64_t a = r->mmio.phys_addr, i = (a - MMIO_BAZA) / MMIO_ROZMIAR;
    const char *powod = NULL;
    int w = 0;
    mz.wyjscia_mmio++;
    if (a < MMIO_BAZA || i >= URZ_LICZBA || r->mmio.len > 8) {
        if (!r->mmio.is_write)
            memset(r->mmio.data, 0xff, sizeof r->mmio.data);
        mz.nieznane++;
        return;
    }
    c->urz = (int)i;
    c->gdzie = ST_CZEKA;
    zamek_wez("procesor: urządzenie");
    c->gdzie = ST_ZAMEK;
    if (r->mmio.is_write)
        w = mmio_pisz(&mz.urz[i], a - MMIO_BAZA - i * MMIO_ROZMIAR, r->mmio.data, r->mmio.len, &powod);
    else
        w = mmio_czytaj(&mz.urz[i], a - MMIO_BAZA - i * MMIO_ROZMIAR, r->mmio.data, r->mmio.len, &powod);
    zamek_oddaj();
    c->gdzie = ST_WYJSCIE;
    if (w != 0)
        koniec(powod != NULL ? powod : "błąd urządzenia");
}

/* KVM_INTERNAL_ERROR_DELIVERY_EV on our MMIO (the devices, the local APIC): KVM did not touch the access — it saw an
 * event "being delivered" (exit_int_info: svm_check_emulate_instruction looks at its type bits, not its VALID bit) and
 * gave up. QEMU 8.2's emulated SVM (the tests, ADR 0042 T10) leaves the type of the event it has just delivered there;
 * real processors leave 0. Nothing is pending then: running the processor again repeats the access, which goes through.
 * A few times in a row at most — a guest that really faults while delivering an event ends. */
#define PONOWIENIA_MAKS 8
static int ponow(struct procesor *c, unsigned *razy)
{
    const struct kvm_run *r = c->run;
    uint64_t gpa = r->internal.ndata >= 4 ? r->internal.data[3] : 0;
    static unsigned long wszystkie;
    if (r->internal.suberror != KVM_INTERNAL_ERROR_DELIVERY_EV || ++*razy > PONOWIENIA_MAKS ||
        !((gpa >= MMIO_BAZA && gpa < MMIO_BAZA + (uint64_t)URZ_LICZBA * MMIO_ROZMIAR) || (gpa >> 12) == (ST_LAPIC >> 12)))
        return 0;
    if (__atomic_fetch_add(&wszystkie, 1, __ATOMIC_RELAXED) == 0)
        k1_log(TAG, "dostęp do urządzenia w trakcie przerwania (emulacja SVM bez czyszczenia exit_int_info) — ponawiam");
    return 1;
}

static void *petla(void *arg)
{
    struct procesor *c = arg;
    unsigned razy = 0;
    c->urz = -1;
    while (!mz.koniec) {
        c->gdzie = ST_W_GOSCIU;
        if (ioctl(c->fd, (int)KVM_RUN, 0) != 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            koniec("KVM_RUN nieudane");
            break;
        }
        if (c->run->exit_reason == KVM_EXIT_INTERNAL_ERROR && ponow(c, &razy))
            continue;
        razy = 0;
        c->gdzie = ST_WYJSCIE;
        c->wyjscie = c->run->exit_reason;
        switch (c->run->exit_reason) {
        case KVM_EXIT_IO:
            io(c->run);
            break;
        case KVM_EXIT_MMIO:
            mmio(c, c->run);
            break;
        case KVM_EXIT_INTR:
            break;
        case KVM_EXIT_SHUTDOWN:
            koniec("gość zakończył pracę (potrójny błąd)");
            break;
        case KVM_EXIT_SYSTEM_EVENT:
            koniec("gość zakończył pracę");
            break;
        default:
            k1_log(TAG, "wyjście procesora gościa %u (%u: %llx %llx %llx %llx)", c->run->exit_reason,
                   c->run->internal.suberror, (unsigned long long)c->run->internal.data[0],
                   (unsigned long long)c->run->internal.data[1], (unsigned long long)c->run->internal.data[2],
                   (unsigned long long)c->run->internal.data[3]);
            koniec("nieobsługiwane wyjście procesora gościa");
            break;
        }
    }
    return NULL;
}

int kvm_uruchom(void)
{
    int i;
    for (i = 0; i < mz.n_cpu; i++)
        if (pthread_create(&mz.cpu[i].watek, NULL, petla, &mz.cpu[i]) != 0) {
            k1_log(TAG, "wątek procesora %d nieudany", i);
            return -1;
        }
    return 0;
}

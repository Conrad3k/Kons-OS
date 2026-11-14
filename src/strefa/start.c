/* K1 strefa — starting the zone kernel without firmware (start.h). */
#include "start.h"

#include <stddef.h>
#include <string.h>

#include <asm/bootparam.h>
#include <asm/e820.h>

#define NAGLOWEK_OD 0x1f1u        /* the setup header in the bzImage file and in the zero page */
#define MAGIA_HDRS 0x53726448u    /* "HdrS" */
#define WERSJA_MIN 0x020cu        /* 2.12: xloadflags (XLF_KERNEL_64) */
#define WEJSCIE_64 0x200u         /* startup_64: 0x200 into the protected-mode kernel */
#define LADOWACZ_INNY 0xffu       /* type_of_loader: undefined boot loader */

_Static_assert(offsetof(struct boot_params, hdr) == NAGLOWEK_OD, "boot_params: nagłówek");
_Static_assert(offsetof(struct boot_params, e820_entries) == 0x1e8, "boot_params: e820_entries");
_Static_assert(offsetof(struct boot_params, e820_table) == 0x2d0, "boot_params: e820_table");
_Static_assert(sizeof(struct boot_params) == 4096, "boot_params: jedna strona");

/* ---- the MP table (Intel MultiProcessor Specification 1.4, chapter 4): packed structures ---- */
struct mp_wskaznik {           /* the floating pointer, "_MP_" */
    char sygnatura[4];
    uint32_t tablica;
    uint8_t dlugosc, wersja, suma, cechy[5];
} __attribute__((packed));
struct mp_naglowek {           /* "PCMP" */
    char sygnatura[4];
    uint16_t dlugosc;
    uint8_t wersja, suma;
    char oem[8], produkt[12];
    uint32_t oem_tablica;
    uint16_t oem_dlugosc, wpisy;
    uint32_t lapic;
    uint16_t ext_dlugosc;
    uint8_t ext_suma, zarezerwowane;
} __attribute__((packed));
struct mp_procesor {
    uint8_t typ, apic_id, apic_wersja, flagi;
    uint32_t sygnatura, cechy;
    uint32_t zarezerwowane[2];
} __attribute__((packed));
struct mp_szyna {
    uint8_t typ, id;
    char nazwa[6];
} __attribute__((packed));
struct mp_ioapic {
    uint8_t typ, id, wersja, flagi;
    uint32_t adres;
} __attribute__((packed));
struct mp_przerwanie {         /* an I/O interrupt (typ 3) or a local one (typ 4) */
    uint8_t typ, rodzaj;
    uint16_t flagi;
    uint8_t szyna, linia_szyny, apic, linia_apic;
} __attribute__((packed));
_Static_assert(sizeof(struct mp_wskaznik) == 16 && sizeof(struct mp_naglowek) == 44 && sizeof(struct mp_procesor) == 20 &&
                   sizeof(struct mp_szyna) == 8 && sizeof(struct mp_ioapic) == 8 && sizeof(struct mp_przerwanie) == 8,
               "tablica MP: rozmiary wpisów");

#define MP_PROCESOR 0
#define MP_SZYNA 1
#define MP_IOAPIC 2
#define MP_PRZERWANIE_IO 3
#define MP_PRZERWANIE_LOK 4
#define MP_INT 0
#define MP_NMI 1
#define MP_EXTINT 3

static uint8_t suma(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    while (n-- > 0)
        s = (uint8_t)(s + *p++);
    return (uint8_t)(0u - s);
}

static size_t dodaj(uint8_t *p, size_t at, const void *wpis, size_t n)
{
    memcpy(p + at, wpis, n);
    return at + n;
}

/* The MP table at ST_MP: `procesory` processors (APIC ids 0..n-1, the first the bootstrap one), one ISA bus, the I/O
 * APIC with id n and its ST_LINIE_PRZERWAN inputs 1:1 (edge, active high: virtio-mmio's irqfd), ExtINT and NMI. */
static void tablica_mp(uint8_t *baza, int procesory)
{
    uint8_t *p = baza + ST_MP;
    struct mp_wskaznik w = { { '_', 'M', 'P', '_' }, ST_MP + sizeof w, 1, 4, 0, { 0 } };
    struct mp_naglowek n = { { 'P', 'C', 'M', 'P' }, 0, 4, 0, { 'K', '1', ' ', ' ', ' ', ' ', ' ', ' ' },
                             { 'S', 'T', 'R', 'E', 'F', 'A', ' ', ' ', ' ', ' ', ' ', ' ' }, 0, 0, 0, ST_LAPIC, 0, 0, 0 };
    struct mp_szyna szyna = { MP_SZYNA, 0, { 'I', 'S', 'A', ' ', ' ', ' ' } };
    struct mp_ioapic io = { MP_IOAPIC, (uint8_t)procesory, 0x11, 1, ST_IOAPIC };
    size_t at = sizeof w + sizeof n;
    int i;
    for (i = 0; i < procesory; i++) {
        struct mp_procesor c = { MP_PROCESOR, (uint8_t)i, 0x14, (uint8_t)(1 | (i == 0 ? 2 : 0)), 0x600, 0x201, { 0, 0 } };
        at = dodaj(p, at, &c, sizeof c);
    }
    at = dodaj(p, at, &szyna, sizeof szyna);
    at = dodaj(p, at, &io, sizeof io);
    for (i = 0; i < ST_LINIE_PRZERWAN; i++) {
        struct mp_przerwanie e = { MP_PRZERWANIE_IO, MP_INT, 0, 0, (uint8_t)i, (uint8_t)procesory, (uint8_t)i };
        at = dodaj(p, at, &e, sizeof e);
    }
    {
        struct mp_przerwanie ext = { MP_PRZERWANIE_LOK, MP_EXTINT, 0, 0, 0, 0xff, 0 };
        struct mp_przerwanie nmi = { MP_PRZERWANIE_LOK, MP_NMI, 0, 0, 0, 0xff, 1 };
        at = dodaj(p, at, &ext, sizeof ext);
        at = dodaj(p, at, &nmi, sizeof nmi);
    }
    n.dlugosc = (uint16_t)(at - sizeof w);
    n.wpisy = (uint16_t)(procesory + 2 + ST_LINIE_PRZERWAN + 2);
    memcpy(p + sizeof w, &n, sizeof n);
    p[sizeof w + offsetof(struct mp_naglowek, suma)] = suma(p + sizeof w, n.dlugosc);
    w.suma = suma((const uint8_t *)&w, sizeof w);
    memcpy(p, &w, sizeof w);
}

/* 1:1 page tables for the first 4 GiB (2 MiB pages) and the GDT: null, null, code 0x10 (64-bit), data 0x18. */
static void tablice(uint8_t *baza)
{
    static const uint64_t gdt[4] = { 0, 0, 0x00af9b000000ffffull, 0x00cf93000000ffffull };
    uint64_t v;
    unsigned i;
    v = ST_PDPT | 3;
    memcpy(baza + ST_PML4, &v, 8);
    for (i = 0; i < 4; i++) {
        v = (ST_PD + 0x1000ull * i) | 3;
        memcpy(baza + ST_PDPT + 8 * i, &v, 8);
    }
    for (i = 0; i < 4 * 512; i++) {
        v = ((uint64_t)i << 21) | 0x83; /* present, writable, 2 MiB */
        memcpy(baza + ST_PD + 8 * i, &v, 8);
    }
    memcpy(baza + ST_GDT, gdt, sizeof gdt);
}

static void e820(struct boot_params *bp, uint64_t rozmiar)
{
    const struct boot_e820_entry t[] = {
        { 0, ST_MP, E820_RAM },
        { ST_MP, ST_NISKA_KONIEC - ST_MP, E820_RESERVED },
        { ST_JADRO, rozmiar - ST_JADRO, E820_RAM },
    };
    memcpy(bp->e820_table, t, sizeof t);
    bp->e820_entries = (uint8_t)(sizeof t / sizeof t[0]);
}

/* Checks the bzImage header into *h: NULL, or why it is not a 64-bit kernel we can start; *od_kodu = where its
 * protected-mode part starts in the file. */
static const char *naglowek(const uint8_t *obraz, size_t dl, struct setup_header *h, size_t *od_kodu)
{
    size_t koniec;
    if (dl < 0x302 || 0x202u + obraz[0x201] < 0x264) /* up to init_size at least; the whole header fits in 0x302 */
        return "plik jądra za krótki";
    koniec = 0x202u + obraz[0x201];
    if (koniec > NAGLOWEK_OD + sizeof *h)
        koniec = NAGLOWEK_OD + sizeof *h;
    memset(h, 0, sizeof *h);
    memcpy(h, obraz + NAGLOWEK_OD, koniec - NAGLOWEK_OD);
    if (h->header != MAGIA_HDRS || h->boot_flag != 0xaa55)
        return "plik nie jest jądrem Linuksa (bzImage)";
    if (h->version < WERSJA_MIN || !(h->xloadflags & XLF_KERNEL_64) || !(h->loadflags & LOADED_HIGH))
        return "jądro bez 64-bitowego wejścia";
    *od_kodu = ((size_t)(h->setup_sects == 0 ? 4 : h->setup_sects) + 1) * 512;
    if (*od_kodu >= dl)
        return "plik jądra bez kodu";
    return NULL;
}

const char *st_przygotuj(const struct gosc_pamiec *m, const uint8_t *obraz, size_t dl, const char *linia, int procesory,
                         struct st_wejscie *w)
{
    struct boot_params bp;
    size_t od_kodu, dl_linii = strlen(linia);
    uint64_t potrzeba;
    const char *powod;
    if (m->rozmiar < ST_PAMIEC_MIN || m->rozmiar > ST_PAMIEC_MAKS || (m->rozmiar & 0xfff) != 0)
        return "zła wielkość pamięci strefy";
    if (procesory < 1 || procesory > ST_PROCESORY_MAKS)
        return "zła liczba procesorów strefy";
    if (dl_linii >= ST_LINIA_MAKS)
        return "linia poleceń za długa";
    memset(&bp, 0, sizeof bp);
    powod = naglowek(obraz, dl, &bp.hdr, &od_kodu);
    if (powod != NULL)
        return powod;
    /* where the kernel decompresses itself: its preferred address + init_size, never below the load address */
    potrzeba = (bp.hdr.pref_address > ST_JADRO ? bp.hdr.pref_address : ST_JADRO) + bp.hdr.init_size;
    if (ST_JADRO + (dl - od_kodu) > m->rozmiar || potrzeba > m->rozmiar)
        return "jądro nie mieści się w pamięci strefy";
    memcpy(m->baza + ST_JADRO, obraz + od_kodu, dl - od_kodu);
    memcpy(m->baza + ST_LINIA, linia, dl_linii + 1);
    bp.hdr.type_of_loader = LADOWACZ_INNY;
    bp.hdr.loadflags = (uint8_t)(bp.hdr.loadflags | LOADED_HIGH);
    bp.hdr.vid_mode = 0xffff;
    bp.hdr.cmd_line_ptr = ST_LINIA;
    bp.hdr.ramdisk_image = bp.hdr.ramdisk_size = 0;
    e820(&bp, m->rozmiar);
    memcpy(m->baza + ST_ZERO, &bp, sizeof bp);
    tablice(m->baza);
    tablica_mp(m->baza, procesory);
    w->rip = ST_JADRO + WEJSCIE_64;
    w->rsi = ST_ZERO;
    w->cr3 = ST_PML4;
    w->gdt = ST_GDT;
    return NULL;
}

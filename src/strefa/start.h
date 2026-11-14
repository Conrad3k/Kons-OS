/* K1 strefa — starting the zone kernel without firmware (ADR 0042 T2): the 64-bit boot protocol of Linux
 * (Documentation/arch/x86/boot.rst, „64-bit BOOT PROTOCOL”). The VMM puts into guest memory: the kernel from its
 * bzImage file, the zero page (struct boot_params: the kernel's own setup header, the memory map, the command line),
 * a GDT with the boot segments 0x10/0x18, page tables mapping the first 4 GiB 1:1 and an MP table (processors and the
 * I/O APIC, no ACPI — S10). Pure: tested on the host (testy/host/test_strefa_start.c). */
#ifndef K1_STREFA_START_H
#define K1_STREFA_START_H

#include <stddef.h>
#include <stdint.h>

#include "virtq.h"

/* Guest physical layout (below 1 MiB: the boot structures; the kernel at 1 MiB) */
#define ST_GDT 0x500u
#define ST_ZERO 0x7000u      /* struct boot_params */
#define ST_PML4 0x9000u
#define ST_PDPT 0xa000u
#define ST_PD 0xb000u        /* 4 pages: 4 GiB in 2 MiB pages */
#define ST_LINIA 0x20000u    /* the command line */
#define ST_LINIA_MAKS 2048u  /* COMMAND_LINE_SIZE of x86 */
#define ST_MP 0x9fc00u       /* the MP table: the last KiB of base memory, where Linux looks for it */
#define ST_NISKA_KONIEC 0xa0000u
#define ST_JADRO 0x100000u   /* the protected-mode kernel (code32_start) */
#define ST_PAMIEC_MIN (64ull << 20)
#define ST_PAMIEC_MAKS (3ull << 30) /* below the devices (MMIO from 0xd0000000) */
#define ST_PROCESORY_MAKS 8
#define ST_IOAPIC 0xfec00000u
#define ST_LAPIC 0xfee00000u
#define ST_LINIE_PRZERWAN 24 /* I/O APIC inputs */

/* Where the first processor starts and what it needs in its registers. */
struct st_wejscie {
    uint64_t rip, rsi;   /* the kernel's 64-bit entry, the zero page */
    uint64_t cr3, gdt;   /* page tables, the GDT (4 entries) */
};

/* Places kernel `obraz` (`dl` bytes of a bzImage) and everything around it into guest memory `m` (zeroed by the caller)
 * for `procesory` processors and command line `linia`. NULL: *w filled; else why the kernel cannot start. */
const char *st_przygotuj(const struct gosc_pamiec *m, const uint8_t *obraz, size_t dl, const char *linia, int procesory,
                         struct st_wejscie *w);

#endif

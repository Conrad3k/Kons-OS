/* K1 — host test of starting the zone kernel (src/strefa/start.c, 0.10.0 step 1, ADR 0042 T2): a made-up bzImage with
 * the fields the 64-bit boot protocol needs goes into guest memory — the kernel at 1 MiB, the zero page with the
 * kernel's own header and the loader's fields, the memory map, the command line, page tables, the GDT, the MP table
 * (checksums, entries); every kernel and memory that cannot start is refused with a reason. */
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <asm/bootparam.h>
#include <asm/e820.h>

#include "k1_test_host.h"
#include "start.h"

#define PAMIEC (64u << 20)
#define OBRAZ 0x4000u

static uint8_t obraz[OBRAZ];

static void jadro(void)
{
    uint16_t v16;
    uint32_t v32;
    uint64_t v64;
    memset(obraz, 0, sizeof obraz);
    obraz[0x1f1] = 4;                       /* setup_sects: the kernel from (4 + 1) × 512 */
    v16 = 0xaa55, memcpy(obraz + 0x1fe, &v16, 2);
    obraz[0x201] = 0x6a;                    /* the header ends at 0x26c */
    memcpy(obraz + 0x202, "HdrS", 4);
    v16 = 0x020f, memcpy(obraz + 0x206, &v16, 2);
    obraz[0x211] = LOADED_HIGH;
    v16 = XLF_KERNEL_64, memcpy(obraz + 0x236, &v16, 2);
    v64 = 0x1000000, memcpy(obraz + 0x258, &v64, 8);
    v32 = 0x100000, memcpy(obraz + 0x260, &v32, 4);
    memcpy(obraz + 0xa00, "kod-jadra", 9);
}

static uint8_t suma(const uint8_t *p, size_t n)
{
    uint8_t s = 0;
    while (n-- > 0)
        s = (uint8_t)(s + *p++);
    return s;
}

static void t_dobry(uint8_t *baza)
{
    const struct gosc_pamiec m = { baza, PAMIEC, 0 };
    struct st_wejscie w;
    struct boot_params bp;
    uint64_t v;
    uint16_t dl_mp, wpisy;
    const uint8_t *mp = baza + ST_MP, *tab;
    jadro();
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "console=hvc0 root=/dev/vda", 2, &w) == NULL);
    EXPECT(w.rip == ST_JADRO + 0x200 && w.rsi == ST_ZERO && w.cr3 == ST_PML4 && w.gdt == ST_GDT);
    EXPECT(memcmp(baza + ST_JADRO, "kod-jadra", 9) == 0);
    EXPECT(strcmp((const char *)baza + ST_LINIA, "console=hvc0 root=/dev/vda") == 0);
    memcpy(&bp, baza + ST_ZERO, sizeof bp);
    EXPECT(bp.hdr.header == 0x53726448 && bp.hdr.type_of_loader == 0xff && (bp.hdr.loadflags & LOADED_HIGH) &&
           bp.hdr.cmd_line_ptr == ST_LINIA && bp.hdr.init_size == 0x100000 && bp.hdr.ramdisk_size == 0);
    EXPECT(bp.e820_entries == 3 && bp.e820_table[0].addr == 0 && bp.e820_table[0].type == E820_RAM &&
           bp.e820_table[1].type == E820_RESERVED && bp.e820_table[2].addr == ST_JADRO &&
           bp.e820_table[2].addr + bp.e820_table[2].size == PAMIEC);
    memcpy(&v, baza + ST_PML4, 8);
    EXPECT(v == (ST_PDPT | 3));
    memcpy(&v, baza + ST_PDPT + 8 * 3, 8);
    EXPECT(v == ((ST_PD + 3 * 0x1000) | 3));
    memcpy(&v, baza + ST_PD + 8 * 5, 8);
    EXPECT(v == ((5ull << 21) | 0x83));
    memcpy(&v, baza + ST_GDT + 16, 8);
    EXPECT(v == 0x00af9b000000ffffull);
    /* the MP table: the pointer and the table add up to 0; 2 processors, the bus, the I/O APIC, 24 + 2 interrupts; the
     * pointer in the last KiB of base memory, the table (0.11.3) at ST_MP_TABELA, in memory e820 reserves */
    EXPECT(memcmp(mp, "_MP_", 4) == 0 && suma(mp, 16) == 0);
    memcpy(&v, mp + 4, 4);
    EXPECT((uint32_t)v == ST_MP_TABELA && bp.e820_table[1].addr == ST_MP_TABELA &&
           bp.e820_table[1].addr + bp.e820_table[1].size == ST_NISKA_KONIEC && bp.e820_table[0].size == ST_MP_TABELA);
    tab = baza + ST_MP_TABELA;
    memcpy(&dl_mp, tab + 4, 2);
    memcpy(&wpisy, tab + 34, 2);
    EXPECT(memcmp(tab, "PCMP", 4) == 0 && suma(tab, dl_mp) == 0 && wpisy == 2 + 1 + 1 + ST_LINIE_PRZERWAN + 2);
    EXPECT(tab[44] == 0 && tab[45] == 0 && tab[47] == 3 && tab[64] == 0 && tab[65] == 1 && tab[67] == 1); /* BSP, AP */
    EXPECT(ST_MP_TABELA + dl_mp <= ST_MP);
}

/* 0.11.3: the most processors — APIC ids 0..253, the I/O APIC 254, the table below the pointer */
static void t_wszystkie_procesory(uint8_t *baza)
{
    const struct gosc_pamiec m = { baza, PAMIEC, 0 };
    struct st_wejscie w;
    uint16_t dl_mp, wpisy;
    const uint8_t *tab = baza + ST_MP_TABELA, *ost, *io;
    memset(baza, 0, PAMIEC);
    jadro();
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", ST_PROCESORY_MAKS, &w) == NULL);
    memcpy(&dl_mp, tab + 4, 2);
    memcpy(&wpisy, tab + 34, 2);
    ost = tab + 44 + (ST_PROCESORY_MAKS - 1) * 20;
    io = tab + 44 + ST_PROCESORY_MAKS * 20 + 8;
    EXPECT(ST_PROCESORY_MAKS == 254 && suma(tab, dl_mp) == 0 && wpisy == ST_PROCESORY_MAKS + 1 + 1 + ST_LINIE_PRZERWAN + 2);
    EXPECT(ost[0] == 0 && ost[1] == 253 && ost[3] == 1 && io[0] == 2 && io[1] == 254);
    EXPECT(ST_MP_TABELA + dl_mp <= ST_MP && suma(baza + ST_MP, 16) == 0);
}

static void t_zle(uint8_t *baza)
{
    struct gosc_pamiec m = { baza, PAMIEC, 0 };
    struct st_wejscie w;
    char dluga[ST_LINIA_MAKS + 8];
    memset(dluga, 'a', sizeof dluga - 1);
    dluga[sizeof dluga - 1] = '\0';
    jadro();
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, dluga, 1, &w) != NULL);
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 0, &w) != NULL);
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", ST_PROCESORY_MAKS + 1, &w) != NULL);
    EXPECT(st_przygotuj(&m, obraz, 0x300, "", 1, &w) != NULL);  /* too short */
    EXPECT(st_przygotuj(&m, obraz, 0xa00, "", 1, &w) != NULL);  /* the header only, no kernel */
    m.rozmiar = 32u << 20;
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL); /* below the minimum */
    m.rozmiar = PAMIEC - 100;
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL); /* not whole pages */
    m.rozmiar = PAMIEC;
    obraz[0x202] = 'X';
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
    jadro();
    obraz[0x236] = 0; /* no 64-bit entry */
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
    jadro();
    obraz[0x206] = 0x0b; /* version 2.11 */
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
    jadro();
    obraz[0x263] = 0x10; /* init_size 256 MiB: does not fit */
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
    jadro();
    obraz[0x201] = 0x20; /* a header too short for init_size */
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
}

/* 0.10.1 step 2 (ADR 0044 T5): a guest larger than the room below the devices — the rest from 4 GiB: the memory map
 * has it, gp_wsk finds it and never lets a range cross the hole. 6 GiB reserved, never touched but below 1 MiB. */
static void t_gora(void)
{
    const uint64_t cala = 6ull << 30;
    uint8_t *baza = mmap(NULL, cala, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    struct gosc_pamiec m = { baza, cala, ST_PAMIEC_MAKS };
    struct st_wejscie w;
    struct boot_params bp;
    if (baza == MAP_FAILED) {
        EXPECT(0);
        return;
    }
    jadro();
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) == NULL);
    memcpy(&bp, baza + ST_ZERO, sizeof bp);
    EXPECT(bp.e820_entries == 4 && bp.e820_table[2].addr + bp.e820_table[2].size == ST_PAMIEC_MAKS &&
           bp.e820_table[3].addr == GP_GORA && bp.e820_table[3].size == cala - ST_PAMIEC_MAKS &&
           bp.e820_table[3].type == E820_RAM);
    EXPECT(gp_wsk(&m, 0x1000, 4096) == baza + 0x1000);
    EXPECT(gp_wsk(&m, ST_PAMIEC_MAKS - 4096, 4096) == baza + ST_PAMIEC_MAKS - 4096);
    EXPECT(gp_wsk(&m, ST_PAMIEC_MAKS - 4096, 8192) == NULL);     /* into the hole */
    EXPECT(gp_wsk(&m, 0xd0000000ull, 4) == NULL);                /* the devices */
    EXPECT(gp_wsk(&m, GP_GORA - 4096, 8192) == NULL);            /* across 4 GiB */
    EXPECT(gp_wsk(&m, GP_GORA, 4096) == baza + ST_PAMIEC_MAKS);
    EXPECT(gp_wsk(&m, GP_GORA + (3ull << 30) - 4096, 4096) == baza + cala - 4096);
    EXPECT(gp_wsk(&m, GP_GORA + (3ull << 30) - 4096, 8192) == NULL); /* past the end */
    EXPECT(gp_wsk(&m, UINT64_MAX - 10, 100) == NULL);
    m.dol = 1ull << 30; /* two parts, but the lower one not the whole room below the devices: refused */
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
    m.dol = ST_PAMIEC_MAKS;
    m.rozmiar = ST_PAMIEC_CALA_MAKS + (2ull << 20); /* above the maximum */
    EXPECT(st_przygotuj(&m, obraz, sizeof obraz, "", 1, &w) != NULL);
    munmap(baza, cala);
}

int main(void)
{
    t_gora();
    uint8_t *baza = calloc(PAMIEC, 1);
    if (baza == NULL)
        return 2;
    t_dobry(baza);
    t_wszystkie_procesory(baza);
    memset(baza, 0, PAMIEC);
    t_zle(baza);
    free(baza);
    return K1_TEST_KONIEC("test_strefa_start");
}

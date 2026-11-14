/* K1 — host test of starting the zone kernel (src/strefa/start.c, 0.10.0 step 1, ADR 0042 T2): a made-up bzImage with
 * the fields the 64-bit boot protocol needs goes into guest memory — the kernel at 1 MiB, the zero page with the
 * kernel's own header and the loader's fields, the memory map, the command line, page tables, the GDT, the MP table
 * (checksums, entries); every kernel and memory that cannot start is refused with a reason. */
#include <stdlib.h>
#include <string.h>

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
    const struct gosc_pamiec m = { baza, PAMIEC };
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
    /* the MP table: the pointer and the table add up to 0; 2 processors, the bus, the I/O APIC, 24 + 2 interrupts */
    EXPECT(memcmp(mp, "_MP_", 4) == 0 && suma(mp, 16) == 0);
    tab = mp + 16;
    memcpy(&dl_mp, tab + 4, 2);
    memcpy(&wpisy, tab + 34, 2);
    EXPECT(memcmp(tab, "PCMP", 4) == 0 && suma(tab, dl_mp) == 0 && wpisy == 2 + 1 + 1 + ST_LINIE_PRZERWAN + 2);
    EXPECT(tab[44] == 0 && tab[45] == 0 && tab[47] == 3 && tab[64] == 0 && tab[65] == 1 && tab[67] == 1); /* BSP, AP */
    EXPECT(ST_MP + 16 + dl_mp <= ST_NISKA_KONIEC);
}

static void t_zle(uint8_t *baza)
{
    struct gosc_pamiec m = { baza, PAMIEC };
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

int main(void)
{
    uint8_t *baza = calloc(PAMIEC, 1);
    if (baza == NULL)
        return 2;
    t_dobry(baza);
    memset(baza, 0, PAMIEC);
    t_zle(baza);
    free(baza);
    return K1_TEST_KONIEC("test_strefa_start");
}

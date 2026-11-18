/* Host test of the update package and its waiting room (0.10.5 step 2, ADR 0046 T4, T7): k1_paczka.c — the header
 * built and read back; manifests of two releases, of other versions or sizes, a slot without room, a front too small,
 * reserved bytes — refused; the front's layout block against the package (slots one after the other right after the
 * front, the back right after them, grubenv inside the front); the waiting room's mark: read back, any changed byte
 * refused, sizes outside the limits refused. */
#include <string.h>

#include "k1_manifest.h"
#include "k1_paczka.h"
#include "k1_test_host.h"
#include "k1_uklad.h"

#define W ((uint32_t)(0 << 16 | 11 << 8 | 0))
#define PRZOD (600u << 10)
#define EFI (280ull << 20)

static void manifest(uint8_t *b, uint32_t format, uint32_t wersja, uint64_t fat, uint8_t wydanie)
{
    struct k1_manifest m;
    memset(&m, 0, sizeof m);
    m.format = format;
    m.wersja = wersja;
    m.fat_bajty = fat;
    k1_wersja_tekst(wersja, m.tekst, sizeof m.tekst);
    memset(m.wydanie, wydanie, sizeof m.wydanie);
    memset(m.skrot, 3, sizeof m.skrot);
    k1_manifest_zbuduj(&m, NULL, NULL, b);
}

static void wzor(struct k1_paczka *p)
{
    memset(p, 0, sizeof *p);
    p->wersja = W;
    p->slot_bajty = 122u << 20;
    p->efi_bajty = EFI;
    p->przod_bajty = PRZOD;
    p->tyl_od = PRZOD + 2ull * K1_SLOT_BAJTY;
    p->tyl_bajty = 4u << 20;
    manifest(p->man_slot, K1_MANIFEST_SLOT, W, p->slot_bajty, 0x22);
    manifest(p->man_start, K1_MANIFEST_START, W, EFI, 0x22);
}

static void naglowek(void)
{
    static uint8_t b[K1_PACZKA_NAGLOWEK], c[K1_PACZKA_NAGLOWEK];
    struct k1_paczka p, r;
    size_t i;
    wzor(&p);
    EXPECT(k1_paczka_zbuduj(&p, b) == 0);
    EXPECT(k1_paczka_czytaj(b, &r) == 0 && r.wersja == W && r.slot_bajty == p.slot_bajty && r.tyl_bajty == p.tyl_bajty &&
           r.slot.format == K1_MANIFEST_SLOT && r.start.format == K1_MANIFEST_START);
    EXPECT(k1_paczka_slot_od() == K1_PACZKA_NAGLOWEK && k1_paczka_przod_od(&r) == K1_PACZKA_NAGLOWEK + (122ull << 20) &&
           k1_paczka_bajty(&r) == K1_PACZKA_NAGLOWEK + (122ull << 20) + PRZOD + (4u << 20));
    for (i = 56; i < K1_PACZKA_NAGLOWEK; i += 101) { /* reserved bytes and the manifests' */
        memcpy(c, b, sizeof c);
        c[i] ^= 1;
        EXPECT(k1_paczka_czytaj(c, &r) != 0 || (i >= K1_PACZKA_MAN_OFF && i < K1_PACZKA_MAN_OFF + 2 * K1_MANIFEST_BAJTY));
    }
    memcpy(c, b, sizeof c);
    c[0] = 'X';
    EXPECT(k1_paczka_czytaj(c, &r) != 0);
    memcpy(c, b, sizeof c);
    c[12] = 1; /* flags */
    EXPECT(k1_paczka_czytaj(c, &r) != 0);

    wzor(&p); /* the boot files' manifest of another release */
    manifest(p.man_start, K1_MANIFEST_START, W, EFI, 0x23);
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* of another version */
    manifest(p.man_slot, K1_MANIFEST_SLOT, W + 1, p.slot_bajty, 0x22);
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* both manifests of one kind */
    manifest(p.man_start, K1_MANIFEST_SLOT, W, p.slot_bajty, 0x22);
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* the slot's size not the manifest's */
    p.slot_bajty += 4096;
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* the K1-EFI's size not the manifest's */
    p.efi_bajty += 1u << 20;
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* slots not right after the front */
    p.tyl_od += 512;
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* the back beyond the K1-EFI */
    p.tyl_bajty = EFI - p.tyl_od + 512;
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* a front without room for the layout block */
    p.przod_bajty = 1024;
    p.tyl_od = 1024 + 2ull * K1_SLOT_BAJTY;
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
    wzor(&p); /* a front larger than any K1-EFI's */
    p.przod_bajty = K1_PACZKA_PRZOD_MAKS + 512;
    p.tyl_od = p.przod_bajty + 2ull * K1_SLOT_BAJTY;
    EXPECT(k1_paczka_zbuduj(&p, b) != 0);
}

static void uklad(void)
{
    static uint8_t przod[PRZOD];
    struct k1_paczka p;
    struct k1_uklad u, r;
    wzor(&p);
    memset(przod, 0, sizeof przod);
    u.fat_bajty = EFI;
    u.grubenv_off = PRZOD - 8192;
    u.slot_bajty = K1_SLOT_BAJTY;
    u.slot_off[1] = PRZOD; /* as zbuduj-usb.sh lays it out: grubenv, B, A */
    u.slot_off[0] = PRZOD + K1_SLOT_BAJTY;
    k1_uklad_zbuduj(&u, przod + K1_UKLAD_OFF);
    EXPECT(k1_paczka_uklad(&p, przod, &r) == 0 && r.slot_off[0] == u.slot_off[0]);
    u.slot_off[0] = PRZOD; /* A first: as good */
    u.slot_off[1] = PRZOD + K1_SLOT_BAJTY;
    k1_uklad_zbuduj(&u, przod + K1_UKLAD_OFF);
    EXPECT(k1_paczka_uklad(&p, przod, &r) == 0);
    u.slot_off[1] = PRZOD + K1_SLOT_BAJTY + 4096; /* a gap between the slots */
    k1_uklad_zbuduj(&u, przod + K1_UKLAD_OFF);
    EXPECT(k1_paczka_uklad(&p, przod, &r) != 0);
    u.slot_off[1] = PRZOD + K1_SLOT_BAJTY;
    u.grubenv_off = PRZOD + 2ull * K1_SLOT_BAJTY; /* grubenv in the back */
    k1_uklad_zbuduj(&u, przod + K1_UKLAD_OFF);
    EXPECT(k1_paczka_uklad(&p, przod, &r) != 0);
    u.grubenv_off = PRZOD - 8192;
    u.fat_bajty = EFI - (1u << 20); /* another K1-EFI */
    k1_uklad_zbuduj(&u, przod + K1_UKLAD_OFF);
    EXPECT(k1_paczka_uklad(&p, przod, &r) != 0);
    memset(przod + K1_UKLAD_OFF, 0, K1_UKLAD_BAJTY); /* no layout block */
    EXPECT(k1_paczka_uklad(&p, przod, &r) != 0);
}

static void poczekalnia(void)
{
    uint8_t b[K1_POCZ_ZNAK], c[K1_POCZ_ZNAK];
    struct k1_pocz z, r;
    size_t i;
    EXPECT(k1_pocz_od(EFI) == EFI && k1_pocz_od(EFI + 1) == EFI + (1u << 20) && k1_pocz_od(1) == 1u << 20);
    memset(&z, 0, sizeof z);
    z.wersja = W;
    z.paczka_bajty = 133u << 20;
    z.ogl_bajty = 700;
    memset(z.sha256, 5, 32);
    memset(z.wydanie, 6, 16);
    k1_pocz_zbuduj(&z, b);
    EXPECT(k1_pocz_czytaj(b, &r) == 0 && r.wersja == W && r.paczka_bajty == z.paczka_bajty && r.ogl_bajty == 700 &&
           memcmp(r.sha256, z.sha256, 32) == 0 && memcmp(r.wydanie, z.wydanie, 16) == 0);
    for (i = 0; i < sizeof b; i += 13) {
        memcpy(c, b, sizeof c);
        c[i] ^= 0x80;
        EXPECT(k1_pocz_czytaj(c, &r) != 0);
    }
    memset(c, 0, sizeof c); /* zeroed: nothing waits */
    EXPECT(k1_pocz_czytaj(c, &r) != 0);
    z.ogl_bajty = 0;
    k1_pocz_zbuduj(&z, b);
    EXPECT(k1_pocz_czytaj(b, &r) != 0);
    z.ogl_bajty = 700;
    z.paczka_bajty = 100;
    k1_pocz_zbuduj(&z, b);
    EXPECT(k1_pocz_czytaj(b, &r) != 0);
    z.paczka_bajty = (512ull << 20) + 1;
    k1_pocz_zbuduj(&z, b);
    EXPECT(k1_pocz_czytaj(b, &r) != 0);
}

int main(void)
{
    naglowek();
    uklad();
    poczekalnia();
    return K1_TEST_KONIEC("test_paczka");
}

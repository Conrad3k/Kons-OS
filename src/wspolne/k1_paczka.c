/* K1 — the update package and its waiting room. See k1_paczka.h. */
#include "k1_paczka.h"
#include "k1_bajty.h"
#include "k1_ogloszenie.h"

#include <string.h>

#include "monocypher.h"

static int zera(const uint8_t *b, size_t od, size_t do_)
{
    for (; od < do_; od++)
        if (b[od] != 0)
            return 0;
    return 1;
}

/* The sizes and the two manifests agree with each other and with the version; the slots (between the front and the
 * back) a whole number of MiB from K1_SLOT_MIN to K1_SLOT_MAKS, the system before its slot's manifests (0.10.9 step 4b). */
static int zgodne(const struct k1_paczka *p)
{
    const struct k1_manifest *s = &p->slot, *t = &p->start;
    uint64_t slot = p->tyl_od > p->przod_bajty ? (p->tyl_od - p->przod_bajty) / 2 : 0;
    return p->wersja != 0 && p->tyl_od > p->przod_bajty && (p->tyl_od - p->przod_bajty) % 2 == 0 && slot >= K1_SLOT_MIN &&
           slot <= K1_SLOT_MAKS && slot % K1_SLOT_MIB == 0 && p->slot_bajty <= k1_slot_man_off(slot) && s->format == K1_MANIFEST_SLOT && t->format == K1_MANIFEST_START && s->wersja == p->wersja &&
           t->wersja == p->wersja && memcmp(s->wydanie, t->wydanie, sizeof s->wydanie) == 0 &&
           p->slot_bajty == s->fat_bajty && p->efi_bajty == t->fat_bajty && p->przod_bajty >= K1_UKLAD_OFF + K1_UKLAD_BAJTY &&
           p->przod_bajty <= K1_PACZKA_PRZOD_MAKS && p->przod_bajty % 512 == 0 && p->tyl_od % 512 == 0 &&
           p->tyl_od <= p->efi_bajty &&
           p->tyl_bajty <= p->efi_bajty - p->tyl_od && p->tyl_bajty <= K1_PACZKA_TYL_MAKS;
}

int k1_paczka_czytaj(const uint8_t *b, struct k1_paczka *p)
{
    memset(p, 0, sizeof *p);
    if (memcmp(b, "K1A1", 4) != 0 || k1_c32(b + 4) != 1 || k1_c32(b + 12) != 0 || !zera(b, 56, K1_PACZKA_MAN_OFF) ||
        !zera(b, K1_PACZKA_MAN_OFF + 2 * K1_MANIFEST_BAJTY, K1_PACZKA_NAGLOWEK))
        goto zle;
    p->wersja = k1_c32(b + 8);
    p->slot_bajty = k1_c64(b + 16);
    p->efi_bajty = k1_c64(b + 24);
    p->przod_bajty = k1_c64(b + 32);
    p->tyl_od = k1_c64(b + 40);
    p->tyl_bajty = k1_c64(b + 48);
    memcpy(p->man_slot, b + K1_PACZKA_MAN_OFF, K1_MANIFEST_BAJTY);
    memcpy(p->man_start, b + K1_PACZKA_MAN_OFF + K1_MANIFEST_BAJTY, K1_MANIFEST_BAJTY);
    if (k1_manifest_czytaj(p->man_slot, &p->slot) != 0 || k1_manifest_czytaj(p->man_start, &p->start) != 0 || !zgodne(p))
        goto zle;
    return 0;
zle:
    memset(p, 0, sizeof *p);
    return -1;
}

int k1_paczka_uklad(const struct k1_paczka *p, const uint8_t *przod, struct k1_uklad *u)
{
    uint64_t a, b;
    if (k1_uklad_czytaj(przod + K1_UKLAD_OFF, p->efi_bajty, u) != 0)
        return -1;
    a = u->slot_off[0] < u->slot_off[1] ? u->slot_off[0] : u->slot_off[1];
    b = u->slot_off[0] < u->slot_off[1] ? u->slot_off[1] : u->slot_off[0];
    if (u->fat_bajty != p->efi_bajty || a != p->przod_bajty || b != a + u->slot_bajty || b + u->slot_bajty != p->tyl_od ||
        u->grubenv_off + K1_GRUBENV_BAJTY > p->przod_bajty)
        return -1;
    return 0;
}

int k1_paczka_zbuduj(struct k1_paczka *p, uint8_t *b)
{
    if (k1_manifest_czytaj(p->man_slot, &p->slot) != 0 || k1_manifest_czytaj(p->man_start, &p->start) != 0 || !zgodne(p))
        return -1;
    memset(b, 0, K1_PACZKA_NAGLOWEK);
    memcpy(b, "K1A1", 4);
    k1_z32(b + 4, 1);
    k1_z32(b + 8, p->wersja);
    k1_z64(b + 16, p->slot_bajty);
    k1_z64(b + 24, p->efi_bajty);
    k1_z64(b + 32, p->przod_bajty);
    k1_z64(b + 40, p->tyl_od);
    k1_z64(b + 48, p->tyl_bajty);
    memcpy(b + K1_PACZKA_MAN_OFF, p->man_slot, K1_MANIFEST_BAJTY);
    memcpy(b + K1_PACZKA_MAN_OFF + K1_MANIFEST_BAJTY, p->man_start, K1_MANIFEST_BAJTY);
    return 0;
}

#define SKROT_OFF (K1_POCZ_ZNAK - 16u)

int k1_pocz_czytaj(const uint8_t *b, struct k1_pocz *z)
{
    uint8_t s[16];
    memset(z, 0, sizeof *z);
    crypto_blake2b(s, sizeof s, b, SKROT_OFF);
    if (memcmp(b, "K1W1", 4) != 0 || (k1_c32(b + 4) != 1 && k1_c32(b + 4) != 2) || k1_c32(b + 12) != 0 ||
        k1_c32(b + 28) != 0 || !zera(b, k1_c32(b + 4) == 1 ? 80 : 96, SKROT_OFF) || memcmp(s, b + SKROT_OFF, sizeof s) != 0)
        goto zle;
    z->widziane = k1_c64(b + 80);
    z->potwierdzone = k1_c64(b + 88);
    z->wersja = k1_c32(b + 8);
    z->paczka_bajty = k1_c64(b + 16);
    z->ogl_bajty = k1_c32(b + 24);
    memcpy(z->sha256, b + 32, 32);
    memcpy(z->wydanie, b + 64, 16);
    if (z->wersja == 0 || z->wersja > 0xffffffu || z->paczka_bajty < K1_PACZKA_NAGLOWEK ||
        z->paczka_bajty > (512ull << 20) || z->ogl_bajty == 0 || z->ogl_bajty > K1_POCZ_PACZKA_OFF - K1_POCZ_OGL_OFF)
        goto zle;
    return 0;
zle:
    memset(z, 0, sizeof *z);
    return -1;
}

void k1_pocz_zbuduj(const struct k1_pocz *z, uint8_t *b)
{
    memset(b, 0, K1_POCZ_ZNAK);
    memcpy(b, "K1W1", 4);
    k1_z32(b + 4, 2);
    k1_z64(b + 80, z->widziane);
    k1_z64(b + 88, z->potwierdzone);
    k1_z32(b + 8, z->wersja);
    k1_z64(b + 16, z->paczka_bajty);
    k1_z32(b + 24, z->ogl_bajty);
    memcpy(b + 32, z->sha256, 32);
    memcpy(b + 64, z->wydanie, 16);
    crypto_blake2b(b + SKROT_OFF, 16, b, SKROT_OFF);
}

int k1_pocz_zwloka(struct k1_pocz *z, uint64_t teraz)
{
    if (z->potwierdzone != 0)
        return 0;
    if (z->widziane == 0 || teraz + 3600u < z->widziane) { /* not seen yet, or seen "in the future": from now */
        z->widziane = teraz;
        return 1;
    }
    if (teraz >= z->widziane + K1_OGL_ZWLOKA) {
        z->potwierdzone = teraz != 0 ? teraz : 1;
        return 1;
    }
    return 0;
}

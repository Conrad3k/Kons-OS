/* K1 — what a K1-EFI partition holds (see k1_efi.h). */
#include "k1_efi.h"

#include <string.h>

#include "k1_io.h"

static void manifest(int fd, uint64_t off, uint32_t format, struct k1_manifest *m)
{
    uint8_t b[K1_MANIFEST_BAJTY];
    if (k1_czytaj_calosc(fd, b, sizeof b, off) != 0 || k1_manifest_czytaj(b, m) != 0 || m->format != format)
        memset(m, 0, sizeof *m);
}

int k1_efi_czytaj(int fd, uint64_t rozmiar, struct k1_efi *e)
{
    uint8_t b[K1_UKLAD_BAJTY], g[K1_GRUBENV_BAJTY];
    int i;
    memset(e, 0, sizeof *e);
    if (rozmiar >= K1_UKLAD_OFF + K1_UKLAD_BAJTY && k1_czytaj_calosc(fd, b, sizeof b, K1_UKLAD_OFF) == 0 &&
        k1_uklad_czytaj(b, rozmiar, &e->uklad) == 0) {
        e->format = 2;
        e->env_ok = k1_czytaj_calosc(fd, g, sizeof g, e->uklad.grubenv_off) == 0 && k1_grubenv_czytaj(g, &e->env) == 0;
        if (!e->env_ok)
            memset(&e->env, 0, sizeof e->env);
        e->dziala = e->env.slot == 'b' ? 1 : 0; /* GRUB: anything but "b" is "a" */
        for (i = 0; i < 2; i++) {
            manifest(fd, e->uklad.slot_off[i] + k1_slot_man_off(e->uklad.slot_bajty), K1_MANIFEST_SLOT, &e->slot[i]);
            if (e->slot[i].fat_bajty > k1_slot_man_off(e->uklad.slot_bajty)) /* an image over its own manifest */
                memset(&e->slot[i], 0, sizeof e->slot[i]);
        }
        return 0;
    }
    return -1;
}

const struct k1_manifest *k1_efi_dziala(const struct k1_efi *e)
{
    static const struct k1_manifest zaden;
    return e->format == 2 ? &e->slot[e->dziala] : &zaden;
}

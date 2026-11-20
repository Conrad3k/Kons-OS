/* K1 posrednik — the card (0.10.9 step 5): DRM ioctls on the render node from nadzorca (fd 4), and what the card can do.
 * Fast graphics needs a card that keeps the zones apart by itself (ADR 0049 pkt 7, T15, S38): every client of the render
 * node in its own address space (full PPGTT: I915_PARAM_HAS_ALIASING_PPGTT >= 2 — Skylake yes, Ivy Bridge (X230) no),
 * the registers of each context its own (HAS_CONTEXT_ISOLATION), pinned addresses (EXEC_SOFTPIN — iris needs them;
 * K1 lets no relocation through) and out-fences (EXEC_FENCE). One failed check = no fast graphics (fail closed). */
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include <drm/drm.h>
#include <drm/i915_drm.h>

#include "posrednik.h"

static int karta_ioctl(unsigned long nr, void *arg)
{
    for (;;) {
        if (ioctl(FD_KARTA, (int)nr, arg) == 0)
            return 0;
        if (errno != EINTR && errno != EAGAIN)
            return errno > 0 ? errno : EIO;
    }
}

const struct silnik silnik_karty = { "karta", karta_ioctl };

static int param(int nr)
{
    int v = 0;
    struct drm_i915_getparam gp = { .param = nr, .value = &v };
    return silnik->ioctl(DRM_IOCTL_I915_GETPARAM, &gp) == 0 ? v : -1;
}

int posr_karta(int *powod, int *pci, int *rewizja, int *wersja)
{
    char nazwa[16] = { 0 };
    struct drm_version v = { .name_len = sizeof nazwa - 1, .name = nazwa };
    if (silnik->ioctl(DRM_IOCTL_VERSION, &v) != 0)
        return K1_POSR_KARTA_BRAK; /* /dev/null (no card): not a DRM device */
    if (strcmp(nazwa, "i915") != 0 || v.version_major < 0 || v.version_major > 255 || v.version_minor < 0 ||
        v.version_minor > 255 || v.version_patchlevel < 0 || v.version_patchlevel > 255)
        return *powod = K1_POSR_NIE_WERSJA, K1_POSR_KARTA_NIE;
    if (param(I915_PARAM_HAS_ALIASING_PPGTT) < 2)
        return *powod = K1_POSR_NIE_PPGTT, K1_POSR_KARTA_NIE;
    if (param(I915_PARAM_HAS_CONTEXT_ISOLATION) <= 0)
        return *powod = K1_POSR_NIE_IZOLACJA, K1_POSR_KARTA_NIE;
    if (param(I915_PARAM_HAS_EXEC_SOFTPIN) <= 0)
        return *powod = K1_POSR_NIE_SOFTPIN, K1_POSR_KARTA_NIE;
    if (param(I915_PARAM_HAS_EXEC_FENCE) <= 0)
        return *powod = K1_POSR_NIE_PLOTY, K1_POSR_KARTA_NIE;
    *pci = param(I915_PARAM_CHIPSET_ID) & 0xffff;
    *rewizja = param(I915_PARAM_REVISION) & 0xff;
    *wersja = v.version_major << 16 | v.version_minor << 8 | v.version_patchlevel;
    if (*pci == 0)
        return *powod = K1_POSR_NIE_WERSJA, *pci = *rewizja = *wersja = 0, K1_POSR_KARTA_NIE;
    return K1_POSR_KARTA_UMIE;
}

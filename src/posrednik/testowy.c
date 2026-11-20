/* K1 posrednik — the test engine (0.10.9 step 5, plan S44): no card. QEMU has no Intel card, so the tests of the
 * broker's way (the window, the shared memory, the checks, the replies, the memory limit, the end after a hang) run with
 * this engine; only the card is missing. Chosen by nadzorca only on request of the test program (TEST_POSREDNIK b=1 —
 * the test image), never for a zone's VMM: it draws nothing, so it could not stand in for the card anyway.
 * It answers like a card of Skylake that keeps the zones apart; a buffer is a number (no memory, no dma-buf: POSR_BUFOR
 * fails with K1_POSR_BLAD_KARTA), a batch gives no fence, and a batch of length K1_POSR_TEST_ZAWIESZENIE "hangs the card"
 * (EIO: the context is banned) — the test sees the broker end with K1_POSR_KONIEC_ZAWIESZENIE. */
#include <errno.h>
#include <string.h>

#include <drm/drm.h>
#include <drm/i915_drm.h>

#include "posrednik.h"

static uint32_t uchwyty, konteksty, przestrzenie;

static int getparam(struct drm_i915_getparam *g)
{
    switch (g->param) {
    case I915_PARAM_CHIPSET_ID:
        *g->value = 0x1916; /* HD 520 (IdeaPad 110-15ISK) */
        break;
    case I915_PARAM_REVISION:
        *g->value = 7;
        break;
    case I915_PARAM_HAS_ALIASING_PPGTT:
        *g->value = 3; /* full, 48 bits */
        break;
    default:
        *g->value = 1;
        break;
    }
    return 0;
}

static int testowy_ioctl(unsigned long nr, void *arg)
{
    switch (nr) {
    case DRM_IOCTL_VERSION: {
        struct drm_version *v = arg;
        v->version_major = 1, v->version_minor = 6, v->version_patchlevel = 0;
        if (v->name != NULL && v->name_len >= 4)
            memcpy(v->name, "i915", 4);
        return 0;
    }
    case DRM_IOCTL_I915_GETPARAM:
        return getparam(arg);
    case DRM_IOCTL_I915_QUERY: {
        struct drm_i915_query *q = arg;
        struct drm_i915_query_item *it = (struct drm_i915_query_item *)(uintptr_t)q->items_ptr;
        it->length = -EINVAL; /* a card of nothing: no topology */
        return 0;
    }
    case DRM_IOCTL_I915_GEM_CREATE_EXT:
        ((struct drm_i915_gem_create_ext *)arg)->handle = ++uchwyty;
        return 0;
    case DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT:
        ((struct drm_i915_gem_context_create_ext *)arg)->ctx_id = ++konteksty;
        return 0;
    case DRM_IOCTL_I915_GEM_CONTEXT_CREATE:
        ((struct drm_i915_gem_context_create *)arg)->ctx_id = ++konteksty;
        return 0;
    case DRM_IOCTL_I915_GEM_VM_CREATE:
        ((struct drm_i915_gem_vm_control *)arg)->vm_id = ++przestrzenie;
        return 0;
    case DRM_IOCTL_I915_GEM_EXECBUFFER2_WR: {
        struct drm_i915_gem_execbuffer2 *x = arg;
        if (x->batch_len == K1_POSR_TEST_ZAWIESZENIE)
            return EIO;
        x->rsvd2 = (uint64_t)(uint32_t)-1 << 32; /* no out-fence */
        return 0;
    }
    case DRM_IOCTL_PRIME_HANDLE_TO_FD:
        return ENODEV;
    default: /* GEM_CLOSE, BUSY, the simple ioctls: done, nothing to say */
        return 0;
    }
}

const struct silnik silnik_testowy = { "silnik testowy", testowy_ioctl };

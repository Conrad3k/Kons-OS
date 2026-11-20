#!/bin/sh
# Biblioteki dekodera filmów (usługa filmy, 0.10.9 krok 2, ADR 0049 pkt 3): libdrm (rdzeń + intel), libva (rdzeń + DRM)
# i sterownik intel-vaapi-driver (i965) — statycznie z naszym musl, bez dlopen (filmy podaje własne dlopen/dlsym, które
# zwracają funkcję startową i965 — src/filmy/obce.c). Źródła: src/zewnetrzne/va/zrodla.txt (pakiety Debiana, sumy).
# Wzór: skrypt sondy z kroku 1 (historia PR-a #181), bez FFmpeg. Kod obcy: nasze flagi utwardzenia (SPEC §11) bez naszego
# -Werror i bez naszego zestawu ostrzeżeń (jak Wuffs) — ostrzeżenia liczone osobno (build/va/ostrzezenia-*.txt, S5).
# Wynik: build/va/pref/{include,lib}, build/va/funkcja-startowa.txt; make woła go, gdy zmieni się tabela albo skrypt.
. "$(dirname "$0")/wspolne.sh"

B=$K1_BUDOWA/va
P=$B/pref
MUSL=$K1_BUDOWA/musl/bin/musl-gcc
POB=$K1_BUDOWA/pobrane/va
TABELA=$K1_REPO/src/zewnetrzne/va/zrodla.txt
[ -x "$MUSL" ] || blad "brak $MUSL — najpierw scripts/zbuduj-musl.sh"
rm -rf "$B"
mkdir -p "$B/zr" "$P/include" "$P/lib" "$POB"

while read -r sciezka suma reszta; do
    case $sciezka in '' | '#'*) continue ;; esac
    [ -n "$suma" ] && [ -z "$reszta" ] || blad "src/zewnetrzne/va/zrodla.txt: zły wiersz „$sciezka”"
    plik=$POB/$(basename "$sciezka")
    if [ ! -f "$plik" ]; then
        curl -fsSL --retry 3 --connect-timeout 30 --speed-limit 2048 --speed-time 60 --max-time 600 -o "$plik.tmp" \
            "https://deb.debian.org/debian/pool/main/$sciezka" || blad "nie pobrano $sciezka"
        mv "$plik.tmp" "$plik"
    fi
    echo "$suma  $plik" | sha256sum -c --quiet - || { rm -f "$plik"; blad "zła suma kontrolna $(basename "$plik")"; }
    tar xf "$plik" -C "$B/zr"
done <"$TABELA"

# Our hardening flags (SPEC §11) without -Werror: foreign code. Its warnings with -Wall -Wextra -Wformat=2 are counted.
OBCE="-std=gnu11 -O2 -ffunction-sections -fdata-sections -fPIE -fstack-protector-strong -fstack-clash-protection
 -D_FORTIFY_SOURCE=3 -ftrivial-auto-var-init=zero -frandom-seed=k1-va -ffile-prefix-map=$B/zr=.
 -isystem $K1_REPO/src/zewnetrzne/fortify-headers/include -isystem $K1_NAGLOWKI/include -w"
OSTRZ="-std=gnu11 -O2 -fPIE -Wall -Wextra -Wformat=2 -fsyntax-only -isystem $K1_NAGLOWKI/include"

kompiluj() { # library name, output .a, cflags, sources...
    nazwa=$1 wynik=$2 flagi=$3
    shift 3
    : >"$B/ostrzezenia-$nazwa.txt"
    for z in "$@"; do
        o=$B/obj/$nazwa/$(basename "$z" .c).o
        mkdir -p "$(dirname "$o")"
        # shellcheck disable=SC2086
        "$MUSL" $OBCE $flagi -c "$z" -o "$o" || blad "$nazwa: $(basename "$z") się nie kompiluje"
        # shellcheck disable=SC2086
        "$MUSL" $OSTRZ $flagi "$z" 2>>"$B/ostrzezenia-$nazwa.txt" || true
        ar rcsD "$wynik" "$o"
    done
    info "va: $nazwa — $# plików, $(grep -c 'warning:' "$B/ostrzezenia-$nazwa.txt" || true) ostrzeżeń (-Wall -Wextra)"
}

# --- libdrm: the ioctl wrappers and libdrm_intel (the GEM buffer manager i965 is built on) ---
D=$B/zr/libdrm-2.4.134
cat >"$D/config.h" <<'EOF'
#define HAVE_LIBDRM_ATOMIC_PRIMITIVES 1
#define HAVE_LIB_ATOMIC_OPS 0
#define HAVE_SECURE_GETENV 1
#define HAVE_SYS_SYSCTL_H 0
#define HAVE_SYS_SELECT_H 1
#define HAVE_ALLOCA_H 1
#define MAJOR_IN_SYSMACROS 1
#define HAVE_OPEN_MEMSTREAM 1
#define HAVE_VISIBILITY 1
#define UDEV 0
EOF
# libdrm_intel needs libpciaccess only for drm_intel_get_aperture_sizes, which i965 never calls: a stub that finds nothing.
mkdir -p "$B/zaslepki"
cat >"$B/zaslepki/pciaccess.h" <<'EOF'
#include <stdint.h>
struct pci_mem_region { uint64_t size; };
struct pci_device { struct pci_mem_region regions[6]; };
static inline int pci_system_init(void) { return -1; }
static inline void pci_system_cleanup(void) {}
static inline struct pci_device *pci_device_find_by_slot(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
static inline int pci_device_probe(struct pci_device *p) { (void)p; return -1; }
EOF
python3 -I "$D/gen_table_fourcc.py" "$D/include/drm/drm_fourcc.h" "$D/generated_static_table_fourcc.h"
DF="-include $D/config.h -I$D -I$D/include/drm -I$D/intel -I$B/zaslepki"
kompiluj libdrm "$P/lib/libdrm.a" "$DF" "$D/xf86drm.c" "$D/xf86drmHash.c" "$D/xf86drmRandom.c" "$D/xf86drmMode.c"
kompiluj libdrm_intel "$P/lib/libdrm_intel.a" "$DF" "$D/intel/intel_bufmgr.c" "$D/intel/intel_bufmgr_gem.c" \
    "$D/intel/intel_decode.c" "$D/intel/mm.c"
mkdir -p "$P/include/libdrm"
cp "$D"/include/drm/*.h "$D/xf86drm.h" "$D/xf86drmMode.h" "$D"/intel/intel_bufmgr.h "$D"/intel/intel_aub.h \
    "$D"/intel/intel_debug.h "$P/include/libdrm/"

# --- libva: the core and the DRM display (vaGetDisplayDRM); the X11 authentication is not built (obce.c: a stub) ---
V=$(echo "$B"/zr/intel-libva-*)
[ -d "$V" ] || blad "brak źródeł libva"
VMAJ=$(sed -n "s/^va_api_major_version = \([0-9]*\)/\1/p" "$V/meson.build")
VMIN=$(sed -n "s/^va_api_minor_version = \([0-9]*\)/\1/p" "$V/meson.build")
VMIC=$(sed -n "s/^va_api_micro_version = \([0-9]*\)/\1/p" "$V/meson.build")
sed -e "s/@VA_API_MAJOR_VERSION@/$VMAJ/; s/@VA_API_MINOR_VERSION@/$VMIN/; s/@VA_API_MICRO_VERSION@/$VMIC/" \
    -e "s/@VA_API_VERSION@/$VMAJ.$VMIN.$VMIC/" "$V/va/va_version.h.in" >"$V/va/va_version.h"
# The paths libva would look at: none exists in K1 (and filmy opens no file — obce.c).
cat >"$V/config.h" <<EOF
#define VA_DRIVERS_PATH "/k1-filmy"
#define HAVE_SECURE_GETENV 1
#define VERSION "$VMAJ.$VMIN.$VMIC"
#define VA_API_VERSION "$VMAJ.$VMIN.$VMIC"
EOF
VF="-include $V/config.h -I$V -I$V/va -I$P/include/libdrm -DSYSCONFDIR=\"/k1-filmy\" -DHAVE_VA_DRM=1"
kompiluj libva "$P/lib/libva.a" "$VF" "$V/va/va.c" "$V/va/va_str.c" "$V/va/va_trace.c" "$V/va/va_compat.c" \
    "$V/va/drm/va_drm.c" "$V/va/drm/va_drm_auth.c" "$V/va/drm/va_drm_utils.c"
mkdir -p "$P/include/va"
cp "$V"/va/*.h "$V"/va/drm/va_drm.h "$V"/va/drm/va_drm_utils.h "$P/include/va/"

# --- intel-vaapi-driver (i965): its init function under its versioned name (our dlsym gives it to libva) ---
# Debian removed the shader binaries without source (+dfsg1); its patches 0001-0004 drop the encoders and the
# post-processing that used them (decoding stays; 0005 touches only meson). The sources: the patched Makefile.sources.
I=$B/zr/intel-vaapi-driver-2.4.1
for l in 0001-Don-t-build-non-free-files 0002-Disable-post-processing-shaders 0003-Disable-decoding \
    0004-fix-exporting-buffers-with-3-planes; do
    patch -s -d "$I" -p1 <"$B/zr/debian/patches/$l.patch" || blad "i965: łatka $l"
done
cat >"$I/src/config.h" <<EOF
#define VERSION "2.4.1"
#define VA_DRIVER_INIT_FUNC __vaDriverInit_${VMAJ}_${VMIN}
#define INTEL_DRIVER_MAJOR_VERSION 2
#define INTEL_DRIVER_MINOR_VERSION 4
#define INTEL_DRIVER_MICRO_VERSION 1
#define INTEL_DRIVER_PRE_VERSION 0
#define HAVE_HYBRID_CODEC 0
#define HAVE_LOG2F 1
EOF
sed 's/@INTEL_DRIVER_GIT_VERSION@//' "$I/src/intel_version.h.in" >"$I/src/intel_version.h"
IF="-DHAVE_CONFIG_H -I$I/src -I$I -I$P/include -I$P/include/libdrm -DPTHREADS"
ZRODLA_I965=$(sed -n '/^source_c/,/NULL/p' "$I/src/Makefile.sources" | tr -d '\\' | tr -s ' \t' '\n' |
    grep '\.c$' | grep -v dso_utils | sed "s|^|$I/src/|")
# shellcheck disable=SC2086
kompiluj i965 "$P/lib/libi965.a" "$IF" $ZRODLA_I965
echo "__vaDriverInit_${VMAJ}_${VMIN}" >"$B/funkcja-startowa.txt"
du -k "$P"/lib/*.a | while read -r k f; do info "va: $(basename "$f") $k KiB"; done
touch "$B/gotowe"

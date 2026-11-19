#!/bin/sh
# 0.10.9 step 1 (TRIAL CODE — stays on the trial branch, never in main): the libraries of the card probe
# (src/aplikacje/sonda): libdrm (core + intel), libva (core + drm), intel-vaapi-driver (i965) and FFmpeg (the H.264
# decoder with the VA-API hwaccel) — statically with our musl, without dlopen (the probe supplies its own
# dlopen/dlsym that return i965's init function). Sources from Debian (sums from the signed .dsc files).
# Result: build/sonda/pref/{include,lib}; warnings of the foreign code: build/sonda/ostrzezenia-*.txt (S5).
set -eu
cd "$(dirname "$0")/.."
. scripts/wspolne.sh

B=$PWD/build/sonda
P=$B/pref
MUSL=$PWD/build/musl/bin/musl-gcc
POB=$PWD/build/pobrane
[ -x "$MUSL" ] || { echo "brak $MUSL — najpierw make"; exit 1; }
[ -f "$B/gotowe" ] && { echo "[sonda] biblioteki już są ($B)"; exit 0; }
mkdir -p "$B" "$P/include" "$P/lib" "$POB"

pobierz() { # pool path, file, sha256
    plik=$POB/$2
    if [ ! -f "$plik" ]; then
        curl -fsSL --connect-timeout 30 --speed-limit 2048 --speed-time 60 -o "$plik.tmp" \
            "https://deb.debian.org/debian/pool/main/$1/$2"
        mv "$plik.tmp" "$plik"
    fi
    echo "$3  $plik" | sha256sum -c - >/dev/null
    tar xf "$plik" -C "$B/zr"
}
rm -rf "$B/zr"
mkdir -p "$B/zr"
pobierz libd/libdrm libdrm_2.4.134.orig.tar.xz ac5e74d157830eb8bee44c6a6bf3ad49774ef0dd2a72bdad74a8f20308b52a95
pobierz libv/libva libva_2.24.1.orig.tar.gz ed24e260a46bb669660b7b5ee993fe17dd436f7874f17de5620305dd69ced8b1
pobierz i/intel-vaapi-driver intel-vaapi-driver_2.4.1+dfsg1.orig.tar.xz \
    c684a2190d826b032b58743a563af385c906be6d79c74aac629274cf180fd558
pobierz i/intel-vaapi-driver intel-vaapi-driver_2.4.1+dfsg1-2.debian.tar.xz \
    b17a409a1ee0f154c114b030ab377a47c2f2098dc3f823408f7a3db9bb32cf45
pobierz f/ffmpeg ffmpeg_8.1.2.orig.tar.xz 464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c

# Our hardening flags (SPEC §11) without -Werror and our warning set: foreign code; its warnings are counted (S5).
OBCE="-Os -ffunction-sections -fdata-sections -fPIE -fstack-protector-strong -fstack-clash-protection -D_FORTIFY_SOURCE=3 -ftrivial-auto-var-init=zero
 -isystem $PWD/src/zewnetrzne/fortify-headers/include -isystem $PWD/build/naglowki/include -ffile-prefix-map=$B/zr=. -w"
OSTRZ="-O2 -fPIE -Wall -Wextra -Wformat=2 -fsyntax-only -isystem $PWD/build/naglowki/include"

kompiluj() { # library name, output .a, cflags, sources...
    nazwa=$1 wynik=$2 flagi=$3
    shift 3
    rm -f "$wynik"
    : >"$B/ostrzezenia-$nazwa.txt"
    for z in "$@"; do
        o=$B/obj/$nazwa/$(basename "$z" .c).o
        mkdir -p "$(dirname "$o")"
        # shellcheck disable=SC2086
        "$MUSL" $OBCE $flagi -c "$z" -o "$o"
        # shellcheck disable=SC2086
        "$MUSL" $OSTRZ $flagi "$z" 2>>"$B/ostrzezenia-$nazwa.txt" || true
        ar rcs "$wynik" "$o"
    done
    echo "[sonda] $nazwa: $(grep -c 'warning:' "$B/ostrzezenia-$nazwa.txt" || true) ostrzeżeń (-Wall -Wextra -Wformat=2)"
}

# --- libdrm: the core (ioctl wrappers) and libdrm_intel (GEM buffer manager i965 is built on) ---
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
# libdrm_intel needs libpciaccess only for drm_intel_get_aperture_sizes, which i965 never calls: a stub.
mkdir -p "$B/stub"
cat >"$B/stub/pciaccess.h" <<'EOF'
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
DF="-include $D/config.h -I$D -I$D/include/drm -I$D/intel -I$B/stub"
kompiluj libdrm "$P/lib/libdrm.a" "$DF" "$D/xf86drm.c" "$D/xf86drmHash.c" "$D/xf86drmRandom.c" "$D/xf86drmMode.c"
kompiluj libdrm_intel "$P/lib/libdrm_intel.a" "$DF" "$D/intel/intel_bufmgr.c" "$D/intel/intel_bufmgr_gem.c" \
    "$D/intel/intel_decode.c" "$D/intel/mm.c"
mkdir -p "$P/include/libdrm"
cp "$D"/include/drm/*.h "$D/xf86drm.h" "$D/xf86drmMode.h" "$D"/intel/intel_bufmgr.h "$D"/intel/intel_aub.h \
    "$D"/intel/intel_debug.h "$P/include/libdrm/"

# --- libva: the core and the DRM display (vaGetDisplayDRM) ---
V=$(echo "$B"/zr/intel-libva-*)
VMAJ=$(sed -n "s/^va_api_major_version = \([0-9]*\)/\1/p" "$V/meson.build")
VMIN=$(sed -n "s/^va_api_minor_version = \([0-9]*\)/\1/p" "$V/meson.build")
VMIC=$(sed -n "s/^va_api_micro_version = \([0-9]*\)/\1/p" "$V/meson.build")
sed -e "s/@VA_API_MAJOR_VERSION@/$VMAJ/; s/@VA_API_MINOR_VERSION@/$VMIN/; s/@VA_API_MICRO_VERSION@/$VMIC/" \
    -e "s/@VA_API_VERSION@/$VMAJ.$VMIN.$VMIC/" "$V/va/va_version.h.in" >"$V/va/va_version.h"
cat >"$V/config.h" <<EOF
#define VA_DRIVERS_PATH "/k1-sonda"
#define HAVE_SECURE_GETENV 1
#define VERSION "$VMAJ.$VMIN.$VMIC"
#define VA_API_VERSION "$VMAJ.$VMIN.$VMIC"
EOF
VF="-include $V/config.h -I$V -I$V/va -I$P/include/libdrm -DSYSCONFDIR=\"/k1-sonda\" -DHAVE_VA_DRM=1"
kompiluj libva "$P/lib/libva.a" "$VF" "$V/va/va.c" "$V/va/va_str.c" "$V/va/va_trace.c" "$V/va/va_compat.c" \
    "$V/va/drm/va_drm.c" "$V/va/drm/va_drm_auth.c" "$V/va/drm/va_drm_utils.c"
mkdir -p "$P/include/va"
cp "$V"/va/*.h "$V"/va/drm/va_drm.h "$V"/va/drm/va_drm_utils.h "$P/include/va/"

# --- intel-vaapi-driver (i965): the init function under its versioned name, called by our dlsym ---
# Debian removed the shader binaries without source (+dfsg1); its patches 0001-0004 drop the encoders and the
# post-processing kernels that used them (decoding stays). The sources: the patched Makefile.sources list.
I=$B/zr/intel-vaapi-driver-2.4.1
for l in 0001-Don-t-build-non-free-files 0002-Disable-post-processing-shaders 0003-Disable-decoding \
    0004-fix-exporting-buffers-with-3-planes; do
    patch -s -d "$I" -p1 <"$B/zr/debian/patches/$l.patch"
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

# --- FFmpeg: only what the probe needs — the H.264 parser and decoder with the VA-API hwaccel, small ---
F=$B/zr/ffmpeg-8.1.2
mkdir -p "$B/ffmpeg-obj"
mkdir -p "$B/pkg"
for pc in libva libva-drm; do
    cat >"$B/pkg/$pc.pc" <<EOF
prefix=$P
Name: $pc
Description: libva (K1 probe, static)
Version: $VMAJ.$VMIN.$VMIC
Cflags: -I$P/include
Libs: -L$P/lib -lva -ldrm
EOF
done
(
    cd "$B/ffmpeg-obj"
    PKG_CONFIG_PATH=$B/pkg PKG_CONFIG_LIBDIR=$B/pkg "$F/configure" --prefix="$P" --cc="$MUSL" \
        --enable-static --disable-shared --enable-pic --disable-programs --disable-doc --disable-network \
        --disable-autodetect --disable-everything --disable-pthreads --disable-debug --disable-avdevice \
        --disable-avfilter --disable-swresample --disable-swscale --enable-vaapi \
        --enable-decoder=h264 --enable-parser=h264 --disable-avformat --enable-small \
        --enable-hwaccel=h264_vaapi \
        --extra-cflags="-fstack-protector-strong -fstack-clash-protection -ffunction-sections -fdata-sections" --extra-ldflags="-static" >"$B/ffmpeg-configure.log"
    make -j"$(nproc)" >"$B/ffmpeg-make.log" 2>&1
    make install >>"$B/ffmpeg-make.log" 2>&1
)
echo "[sonda] ffmpeg: $(grep -c 'warning:' "$B/ffmpeg-make.log" || true) ostrzeżeń (jego własne flagi)"
du -sh "$P/lib"/*.a | sed 's/^/[sonda] /'
touch "$B/gotowe"

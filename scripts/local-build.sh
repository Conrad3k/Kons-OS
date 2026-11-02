#!/bin/bash
# Buduje ISO Kons-OS lokalnie w kontenerze Claude (albo na dowolnym Linuksie z debootstrap),
# bez Dockera: w chroocie z Debianem 13 + live-build.
#
#   sudo scripts/local-build.sh          # pełny build ISO
#   sudo scripts/local-build.sh shell    # powłoka w środowisku budowania
#
# Ruch do Debiana idzie przez scripts/mirror-proxy.py (HTTP -> HTTPS), bo w kontenerze
# Claude działa tylko HTTPS przez proxy. Wymaga dostępu do deb.debian.org i security.debian.org.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILDER="${KONS_BUILDER:-/srv/kons-builder}"
PORT="${KONS_MIRROR_PORT:-3142}"
MIRROR="http://127.0.0.1:${PORT}/debian/"

[ "$(id -u)" -eq 0 ] || { echo "Uruchom jako root." >&2; exit 1; }

# 1. Mirror-proxy
if ! curl -fsS -o /dev/null "${MIRROR}dists/trixie/Release" 2>/dev/null; then
    python3 "$REPO/scripts/mirror-proxy.py" "$PORT" >/tmp/kons-mirror-proxy.log 2>&1 &
    for _ in $(seq 20); do curl -fsS -o /dev/null "${MIRROR}dists/trixie/Release" 2>/dev/null && break; sleep 0.5; done
fi
curl -fsS -o /dev/null "${MIRROR}dists/trixie/Release" || { echo "Mirror-proxy nie działa (brak dostępu do deb.debian.org?)" >&2; exit 1; }

# 2. Środowisko budowania (Debian 13 + live-build), tworzone raz
if [ ! -x "$BUILDER/usr/bin/lb" ]; then
    if [ ! -d "$BUILDER/etc" ]; then
        debootstrap --variant=minbase --keyring=/usr/share/keyrings/debian-archive-keyring.gpg \
            trixie "$BUILDER" "$MIRROR"
    fi
    echo "deb $MIRROR trixie main" > "$BUILDER/etc/apt/sources.list"
    chroot "$BUILDER" apt-get update
    chroot "$BUILDER" env DEBIAN_FRONTEND=noninteractive apt-get install -y live-build
fi

# 3. Montowania
mkdir -p "$BUILDER/build"
mounts=(proc sys dev dev/pts build)
cleanup() {
    for ((i=${#mounts[@]}-1; i>=0; i--)); do
        mountpoint -q "$BUILDER/${mounts[$i]}" && umount -l "$BUILDER/${mounts[$i]}" || true
    done
}
trap cleanup EXIT
mountpoint -q "$BUILDER/proc"    || mount -t proc proc "$BUILDER/proc"
mountpoint -q "$BUILDER/sys"     || mount -t sysfs sys "$BUILDER/sys"
mountpoint -q "$BUILDER/dev"     || mount --bind /dev "$BUILDER/dev"
mountpoint -q "$BUILDER/dev/pts" || mount --bind /dev/pts "$BUILDER/dev/pts"
mountpoint -q "$BUILDER/build"   || mount --bind "$REPO" "$BUILDER/build"

# 4. Build
ENVV=(KONS_MIRROR="$MIRROR" KONS_MIRROR_SECURITY="http://127.0.0.1:${PORT}/debian-security/")
if [ "${1:-}" = "shell" ]; then
    chroot "$BUILDER" env "${ENVV[@]}" bash -c "cd /build && exec bash"
else
    chroot "$BUILDER" env "${ENVV[@]}" bash -c "cd /build && ./build.sh"
fi

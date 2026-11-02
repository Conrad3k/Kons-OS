#!/bin/bash
# Buduje obraz ISO Kons-OS przy pomocy Debian live-build.
# Uruchamiaj na Debianie 13 (trixie) jako root:  sudo ./build.sh
set -euo pipefail

cd "$(dirname "$0")"

if [ "$(id -u)" -ne 0 ]; then
    echo "Uruchom jako root: sudo ./build.sh" >&2
    exit 1
fi

if ! command -v lb >/dev/null; then
    echo "Brak live-build. Zainstaluj: apt install live-build" >&2
    exit 1
fi

# Wersja systemu (z pliku VERSION)
KONS_VERSION="$(cat VERSION)"
export KONS_VERSION
mkdir -p config/includes.chroot/etc
echo "$KONS_VERSION" > config/includes.chroot/etc/kons-version

# Sprzątanie po poprzednim buildzie (konfiguracja w config/ zostaje)
lb clean --purge || true

lb config \
    --mode debian \
    --distribution trixie \
    --architectures amd64 \
    --linux-flavours amd64 \
    --archive-areas "main contrib non-free non-free-firmware" \
    --mirror-bootstrap "http://deb.debian.org/debian/" \
    --mirror-binary "http://deb.debian.org/debian/" \
    --security true \
    --updates true \
    --binary-images iso-hybrid \
    --bootloaders "grub-efi,syslinux" \
    --debian-installer none \
    --firmware-chroot true \
    --iso-application "Kons-OS" \
    --iso-publisher "Kons-OS; https://github.com/conrad3k/kons-os" \
    --iso-volume "KONS-OS ${KONS_VERSION}" \
    --image-name "kons-os-${KONS_VERSION}" \
    --bootappend-live "boot=live components quiet locales=pl_PL.UTF-8 keyboard-layouts=pl timezone=Europe/Warsaw username=kons user-fullname=Kons hostname=kons-os" \
    --apt-recommends true \
    --cache true

lb build 2>&1 | tee build.log

ls -lh ./*.iso

#!/bin/sh
# Instaluje narzędzia potrzebne do budowy i testów K1 (Ubuntu 24.04: kontener deweloperski i CI).
# Wersje pakietów zapisuje build w build/wynik/srodowisko.txt (zob. docs/ZALEZNOSCI.md).
set -eu
SUDO=; [ "$(id -u)" -eq 0 ] || SUDO=sudo
PAKIETY="build-essential gcc-13-plugin-dev flex bison bc libelf-dev libssl-dev rsync cpio xz-utils
 curl gnupg musl-tools musl-dev squashfs-tools qemu-system-x86 ovmf clang libclang-rt-18-dev python3
 pkg-config libfreetype-dev zlib1g-dev fonts-ibm-plex grub-common grub-efi-amd64-bin grub-pc-bin xorriso mtools fdisk acpica-tools cryptsetup-bin sbsigntool faketime python3-virt-firmware"
$SUDO apt-get update -qq
# shellcheck disable=SC2086
# sudo resets the environment: pass DEBIAN_FRONTEND explicitly (no debconf prompts in CI).
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $PAKIETY </dev/null
echo "[k1] narzędzia zainstalowane"

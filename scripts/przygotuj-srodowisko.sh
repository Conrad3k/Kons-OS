#!/bin/sh
# Instaluje narzędzia potrzebne do budowy i testów K1 (Ubuntu 24.04: kontener deweloperski i CI).
# Wersje pakietów zapisuje build w build/wynik/srodowisko.txt (zob. docs/ZALEZNOSCI.md).
set -eu
SUDO=; [ "$(id -u)" -eq 0 ] || SUDO=sudo
PAKIETY="build-essential gcc-13-plugin-dev flex bison bc libelf-dev libssl-dev rsync cpio xz-utils
 curl gnupg musl-tools musl-dev squashfs-tools qemu-system-x86 ovmf clang libclang-rt-18-dev python3
 pkg-config libfreetype-dev zlib1g-dev fonts-ibm-plex grub-common grub-efi-amd64-bin grub-pc-bin xorriso mtools fdisk acpica-tools cryptsetup-bin sbsigntool faketime python3-virt-firmware python3-pil"
APT_OPCJE="-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30"

# GitHub runners take packages from a list of mirrors (apt-mirrors.txt). On 2026-11-11 the first one
# (azure.archive.ubuntu.com) did not resolve and the step hung until its time limit: a mirror whose
# name does not resolve is left out while at least one other does (K1_LUSTRA: another list, for a test).
lustra() {
    plik=${K1_LUSTRA:-/etc/apt/apt-mirrors.txt}
    [ -f "$plik" ] || return 0
    dobre=$(mktemp)
    ile=0
    while IFS= read -r wiersz; do
        host=${wiersz#*://}
        host=${host%%/*}
        if [ -z "$wiersz" ] || [ "${wiersz#\#}" != "$wiersz" ]; then
            printf '%s\n' "$wiersz" >>"$dobre"
        elif getent hosts "$host" >/dev/null 2>&1; then
            printf '%s\n' "$wiersz" >>"$dobre"
            ile=$((ile + 1))
        else
            echo "[uwaga] lustro pakietów $host nie odpowiada — pomijam je"
        fi
    done <"$plik"
    if [ "$ile" -gt 0 ]; then
        $SUDO cp "$dobre" "$plik"
    else
        echo "[uwaga] żadne lustro pakietów nie odpowiada — zostawiam listę bez zmian"
    fi
    rm -f "$dobre"
}

# A failed index is an error (--error-on=any), not a warning: three tries, then on with what there is.
odswiez() {
    for proba in 1 2 3; do
        # shellcheck disable=SC2086
        if $SUDO apt-get update -qq --error-on=any $APT_OPCJE; then
            return 0
        fi
        echo "[uwaga] spis pakietów nie pobrany (próba $proba z 3)"
        [ "$proba" -eq 3 ] || sleep 15
    done
}

lustra
odswiez
# shellcheck disable=SC2086
# sudo resets the environment: pass DEBIAN_FRONTEND explicitly (no debconf prompts in CI).
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $APT_OPCJE $PAKIETY </dev/null
echo "[k1] narzędzia zainstalowane"

#!/bin/sh
# Instaluje narzędzia potrzebne do budowy i testów K1 (Ubuntu 24.04: kontener deweloperski i CI).
# Wersje pakietów zapisuje build w build/wynik/srodowisko.txt (zob. docs/ZALEZNOSCI.md).
#
# Zmienne (CI):
#   K1_APT_ZAPAS  katalog z kopią pobranych pakietów i spisu pakietów (actions/cache w k1.yml):
#                 przed instalacją wraca do apt, po udanej instalacji zapisuje się na nowo;
#                 bez sieci instalacja idzie tylko z tej kopii.
#   K1_LUSTRA, K1_LUSTRO_ZAPASOWE  inna lista luster i inne lustro zapasowe (do testu skryptu).
set -eu
SUDO=; [ "$(id -u)" -eq 0 ] || SUDO=sudo
PAKIETY="build-essential gcc-13-plugin-dev flex bison bc libelf-dev libssl-dev rsync cpio xz-utils
 curl gnupg squashfs-tools qemu-system-x86 ovmf clang libclang-rt-18-dev python3
 pkg-config libfreetype-dev zlib1g-dev fonts-ibm-plex grub-common grub-efi-amd64-bin grub-pc-bin xorriso mtools fdisk acpica-tools cryptsetup-bin sbsigntool faketime python3-virt-firmware python3-pil dosfstools zstd e2fsprogs"
APT_OPCJE="-o Acquire::Retries=2 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20"
ZAPAS=${K1_APT_ZAPAS:-}
# A full Ubuntu mirror outside ubuntu.com (kernel.org): another name and another operator, so an outage of
# Ubuntu's names does not take it down too. apt checks Ubuntu's signature on every index either way.
ZAPASOWE=${K1_LUSTRO_ZAPASOWE:-https://mirrors.edge.kernel.org/ubuntu/}
BEZ_SIECI=0

# GitHub runners take packages from a list of mirrors (apt-mirrors.txt). On 2026-11-11 first
# azure.archive.ubuntu.com and then all three Ubuntu names did not resolve and the step hung until its
# time limit. A mirror whose name does not resolve is left out; when none resolves, the kernel.org mirror
# is used; when that does not resolve either, there is no network for apt (BEZ_SIECI=1).
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
    if [ "$ile" -eq 0 ]; then
        host=${ZAPASOWE#*://}
        host=${host%%/*}
        if getent hosts "$host" >/dev/null 2>&1; then
            echo "[uwaga] żadne lustro Ubuntu nie odpowiada — biorę pakiety z lustra zapasowego $host"
            printf '%s\tpriority:1\n' "$ZAPASOWE" >>"$dobre"
            ile=1
        else
            echo "[uwaga] żadne lustro pakietów nie odpowiada (także zapasowe $host)"
            BEZ_SIECI=1
        fi
    fi
    if [ "$ile" -gt 0 ]; then
        $SUDO cp "$dobre" "$plik"
    fi
    rm -f "$dobre"
}

# With a copy (K1_APT_ZAPAS) apt downloads straight into it (Dir::Cache::archives): the default place may be
# emptied after every install (Docker images do that), the copy's own directory is not.
ARCHIWUM=
if [ -n "$ZAPAS" ]; then
    mkdir -p "$ZAPAS/listy" "$ZAPAS/pakiety/partial"
    ARCHIWUM="-o Dir::Cache::archives=$ZAPAS/pakiety/"
fi

# The package index from the cache goes back to apt (an index that fits the packages in the copy).
zapas_wczytaj() {
    [ -n "$ZAPAS" ] || return 0
    ile=$(find "$ZAPAS/pakiety" -maxdepth 1 -name '*.deb' | wc -l)
    [ "$ile" -gt 0 ] || return 0
    $SUDO find "$ZAPAS/listy" -maxdepth 1 -type f -exec cp -t /var/lib/apt/lists/ {} +
    echo "[k1] kopia pakietów z pamięci podręcznej: $ile pakietów"
}

# After a good install: the index is copied, packages no longer in it are dropped, the copy belongs to the
# user (actions/cache reads it).
zapas_zapisz() {
    [ -n "$ZAPAS" ] || return 0
    # shellcheck disable=SC2086
    $SUDO apt-get autoclean -qq $ARCHIWUM >/dev/null
    rm -rf "$ZAPAS/listy"
    mkdir -p "$ZAPAS/listy"
    find /var/lib/apt/lists -maxdepth 1 -type f ! -name lock -exec cp -t "$ZAPAS/listy/" {} +
    $SUDO rm -rf "$ZAPAS/pakiety/partial" "$ZAPAS/pakiety/lock"
    $SUDO chown -R "$(id -u):$(id -g)" "$ZAPAS"
    echo "[k1] kopia pakietów zapisana: $(find "$ZAPAS/pakiety" -name '*.deb' | wc -l) pakietów"
}

# A failed index is an error (--error-on=any), not a warning: two tries of at most 150 s each, then on with
# what there is (the index from the cache, if any).
odswiez() {
    for proba in 1 2; do
        # shellcheck disable=SC2086
        if $SUDO timeout 150 apt-get update -qq --error-on=any $APT_OPCJE; then
            return 0
        fi
        echo "[uwaga] spis pakietów nie pobrany (próba $proba z 2)"
        [ "$proba" -eq 2 ] || sleep 10
    done
}

lustra
zapas_wczytaj
BEZ_POBIERANIA=
if [ "$BEZ_SIECI" -eq 1 ]; then
    if [ -n "$ZAPAS" ] && [ -n "$(find "$ZAPAS/pakiety" -maxdepth 1 -name '*.deb')" ]; then
        echo "[uwaga] bez sieci — instaluję tylko z kopii pakietów"
        BEZ_POBIERANIA=--no-download
    else
        echo "[k1] BŁĄD: żadne lustro pakietów nie odpowiada i nie ma kopii pakietów — spróbuj później" >&2
        exit 1
    fi
else
    odswiez
fi
# shellcheck disable=SC2086
# sudo resets the environment: pass DEBIAN_FRONTEND explicitly (no debconf prompts in CI).
$SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends $BEZ_POBIERANIA \
    $APT_OPCJE $ARCHIWUM $PAKIETY </dev/null
zapas_zapisz
echo "[k1] narzędzia zainstalowane"

# Wspólne ustawienia skryptów K1 (dołączane przez `. scripts/wspolne.sh`).
set -eu
K1_REPO=$(cd "$(dirname "$0")/.." && pwd)
K1_BUDOWA=${K1_BUDOWA:-$K1_REPO/build}
K1_WYNIK=$K1_BUDOWA/wynik
K1_WERSJA=$(cat "$K1_REPO/VERSION")
. "$K1_REPO/jadro/wersja.env"
K1_JADRO_ZRODLA=$K1_BUDOWA/zrodla/linux-$K1_JADRO_WERSJA
K1_JADRO_OBJ=$K1_BUDOWA/jadro-obj
K1_NAGLOWKI=$K1_BUDOWA/naglowki
# Stały znacznik czasu = powtarzalne wyniki (data ostatniego commita, a bez gita: 0).
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$K1_REPO" log -1 --format=%ct 2>/dev/null || echo 0)}
export SOURCE_DATE_EPOCH
K1_CPU=$(nproc 2>/dev/null || echo 2)
info() { printf '[k1] %s\n' "$*"; }
blad() { printf '[k1] BŁĄD: %s\n' "$*" >&2; exit 1; }
# Test battery for QEMU (testy/acpi/bateria.asl → ACPI table): $1 = state (1 discharging,
# 2 charging), $2 = AC adapter online (0/1), $3 = output .aml file.
bateria_aml() {
    sed "s/@STAN@/$1/; s/@ZASILACZ@/$2/" "$K1_REPO/testy/acpi/bateria.asl" > "${3%.aml}.asl"
    iasl -p "${3%.aml}" "${3%.aml}.asl" >/dev/null 2>&1 || blad "iasl: nie udało się skompilować ${3%.aml}.asl"
}
# Signed EFI boot loader from Ubuntu (0.5.6): shim (signed by Microsoft) + GRUB for removable media
# (gcdx64, signed by Canonical; finds its medium by /.disk/info, reads /boot/grub/grub.cfg). Our own
# unsigned GRUB images were flagged by Windows Defender as "Trojan:Win32/Sabsik!ml". The packages
# are only unpacked (apt checks their hashes against the signed archive), never installed.
# Sets K1_EFI (directory with shimx64.efi.signed.latest, gcdx64.efi.signed and wersje.txt).
efi_podpisane() {
    K1_EFI=$K1_BUDOWA/pobrane/efi
    [ -f "$K1_EFI/gcdx64.efi.signed" ] && [ -f "$K1_EFI/shimx64.efi.signed.latest" ] && return 0
    mkdir -p "$K1_EFI"
    (cd "$K1_EFI" && rm -f ./*.deb && apt-get download shim-signed grub-efi-amd64-signed >/dev/null 2>&1) \
        || blad "nie udało się pobrać shim-signed / grub-efi-amd64-signed (apt)"
    rm -rf "$K1_EFI/x"; mkdir -p "$K1_EFI/x"
    for deb in "$K1_EFI"/*.deb; do dpkg-deb -x "$deb" "$K1_EFI/x"; done
    cp "$K1_EFI/x/usr/lib/shim/shimx64.efi.signed.latest" "$K1_EFI/x/usr/lib/grub/x86_64-efi-signed/gcdx64.efi.signed" \
        "$K1_EFI/"
    (cd "$K1_EFI" && ls ./*.deb > wersje.txt)
}

# A FAT image of `$2` MiB at `$1` holding the files given as "source:destination" pairs; created
# before mformat (otherwise mformat writes uninitialised memory into the last sector), fixed volume
# serial `$3`, file dates = SOURCE_DATE_EPOCH: the same inputs give the same image.
obraz_fat() {
    plik=$1 mib=$2 numer=$3; shift 3
    rm -f "$plik"; truncate -s "${mib}M" "$plik"
    mformat -i "$plik" -T $((mib * 2048)) -N "$numer" -v K1-EFI :: || blad "mformat $plik"
    for para in "$@"; do
        zrodlo=${para%%:*} cel=${para#*:}
        katalog=$(dirname "$cel")
        d=; for czesc in $(echo "$katalog" | tr '/' ' '); do
            d=$d/$czesc; mmd -D s -i "$plik" "::$d" 2>/dev/null || true
        done
        cp "$zrodlo" "$K1_BUDOWA/fat.tmp" && touch -d "@$SOURCE_DATE_EPOCH" "$K1_BUDOWA/fat.tmp"
        mcopy -m -i "$plik" "$K1_BUDOWA/fat.tmp" "::$cel" || blad "mcopy $cel"
    done
    rm -f "$K1_BUDOWA/fat.tmp"
}

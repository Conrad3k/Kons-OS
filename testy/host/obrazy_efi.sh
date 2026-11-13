#!/bin/sh
# Obrazy K1-EFI do testu wymiany plików startowych (test_start, 0.9.0 krok 3, ADR 0038), zrobione zwykłymi narzędziami
# (mtools) — tak, jak buduje je scripts/zbuduj-usb.sh (FAT32, klastry 4 KiB, najpierw grubenv, miejsce B, miejsce A,
# potem pliki startowe), ale małe (miejsca po 64 KiB):
#   dysk.img      „stary” K1 na dysku: stare pliki startowe, grubenv i miejsca z treścią dysku
#   pendrive.img  „nowy” K1: inne pliki startowe (inne rozmiary, jeden plik więcej, długa nazwa), inna treść grubenv
#                 i miejsc (ich treść nie jest częścią plików startowych)
#   inny.img      jak pendrive, ale klastry 8 KiB (inny układ — wymiana niemożliwa)
#   pelny.img     jak dysk, ale bez wolnego miejsca obok starych plików
#   miejsca.txt   przesunięcia grubenv, miejsca A i B (bajty od początku partycji; takie same w każdym obrazie)
#   nowe/         nowe pliki startowe (do porównania po wymianie, mtools)
# Użycie: obrazy_efi.sh KATALOG          — obrazy
#         obrazy_efi.sh KATALOG sprawdz  — po teście: wynik.img (dysk po wymianie) czytany przez mtools i fsck.fat
set -eu
OUT=$1
export LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1
Z=$OUT/zrodla N=$OUT/nowe

wzor() { # wzor PLIK BAJTÓW ZIARNO
    python3 -I -c 'import sys; n,z=int(sys.argv[2]),int(sys.argv[3]); open(sys.argv[1],"wb").write(bytes((i*z+7)%251 for i in range(n)))' "$1" "$2" "$3"
}

obraz() { # obraz PLIK KLASTER_SEKTORÓW MiB ZIARNO_MIEJSC PLIK:ŚCIEŻKA...
    plik=$1 c=$2 mib=$3 z=$4; shift 4
    rm -f "$plik"; truncate -s "${mib}M" "$plik"
    mformat -i "$plik" -T $((mib * 2048)) -N 4b315553 -v K1-EFI -F -c "$c" ::
    wzor "$Z/grubenv" 1024 "$z"; wzor "$Z/b.img" 65536 $((z + 1)); wzor "$Z/a.img" 65536 $((z + 2))
    mmd -i "$plik" ::/boot ::/boot/grub ::/k1
    mcopy -i "$plik" "$Z/grubenv" ::/boot/grub/grubenv
    mcopy -i "$plik" "$Z/b.img" ::/k1/b.img
    mcopy -i "$plik" "$Z/a.img" ::/k1/a.img
    for para in "$@"; do
        zrodlo=${para%%:*} cel=${para#*:}
        d=; for czesc in $(dirname "$cel" | tr '/' ' '); do d=$d/$czesc; mmd -D s -i "$plik" "::$d" 2>/dev/null || true; done
        mcopy -i "$plik" "$zrodlo" "::$cel"
    done
}

miejsca() { # bytes from the partition start of grubenv, slot A, slot B (as zbuduj-usb.sh finds them)
    python3 -I - "$1" <<'PY'
import subprocess, sys, re
img = sys.argv[1]
b = open(img, "rb").read(64)
bps, spc, res, nfat = int.from_bytes(b[11:13], "little"), b[13], int.from_bytes(b[14:16], "little"), b[16]
fatsz = int.from_bytes(b[36:40], "little")
def od(p):
    w = subprocess.run(["mshowfat", "-i", img, "::" + p], capture_output=True, text=True, check=True).stdout
    k = int(re.search(r"<(\d+)", w).group(1))
    return (res + nfat * fatsz + (k - 2) * spc) * bps
print(od("/boot/grub/grubenv"), od("/k1/a.img"), od("/k1/b.img"))
PY
}

if [ "${2:-}" = sprawdz ]; then
    W=$OUT/wynik.img
    fsck.fat -n "$W" >/dev/null || { echo "test_start: fsck.fat znalazł błędy w wynik.img"; fsck.fat -n "$W"; exit 1; }
    for p in EFI/BOOT/BOOTX64.EFI EFI/BOOT/grubx64.efi EFI/BOOT/mmx64.efi boot/grub/grub.cfg boot/grub/start.png \
        .disk/info "EFI/BOOT/Długa nazwa — pliku.txt"; do
        mtype -i "$W" "::/$p" | cmp -s - "$N/$(basename "$p")" || { echo "test_start: ::/$p po wymianie inny niż nowy"; exit 1; }
    done
    mtype -i "$W" ::/boot/grub/grubenv | cmp -s - "$OUT/dysk-grubenv" || { echo "test_start: grubenv dysku zmieniony"; exit 1; }
    mtype -i "$W" ::/k1/a.img | cmp -s - "$OUT/dysk-a.img" || { echo "test_start: miejsce A dysku zmienione"; exit 1; }
    if mtype -i "$W" ::/EFI/BOOT/stary.efi >/dev/null 2>&1; then echo "test_start: stary plik został"; exit 1; fi
    echo "test_start: wynik.img — fsck.fat bez błędów, mtools czyta nowe pliki startowe, grubenv i miejsca dysku nietknięte"
    exit 0
fi

rm -rf "$Z" "$N"; mkdir -p "$Z" "$N"
wzor "$Z/BOOTX64.EFI" 12000 3; wzor "$Z/grubx64.efi" 20000 5; wzor "$Z/stary.efi" 3000 9
printf 'set k1=stary\n' > "$Z/grub.cfg"; printf 'Kons K1 0.8.7\n' > "$Z/info"
obraz "$OUT/dysk.img" 8 40 11 "$Z/BOOTX64.EFI:/EFI/BOOT/BOOTX64.EFI" "$Z/grubx64.efi:/EFI/BOOT/grubx64.efi" \
    "$Z/stary.efi:/EFI/BOOT/stary.efi" "$Z/grub.cfg:/boot/grub/grub.cfg" "$Z/info:/.disk/info"
cp "$Z/grubenv" "$OUT/dysk-grubenv"; cp "$Z/a.img" "$OUT/dysk-a.img"
obraz "$OUT/pelny.img" 8 40 11 "$Z/BOOTX64.EFI:/EFI/BOOT/BOOTX64.EFI" "$Z/grubx64.efi:/EFI/BOOT/grubx64.efi" \
    "$Z/grub.cfg:/boot/grub/grub.cfg" "$Z/info:/.disk/info"
wolne=$(mdir -i "$OUT/pelny.img" :: | sed -n 's/^ *\([0-9 ]*\) bytes free.*/\1/p' | tr -d ' ')
wzor "$Z/duzy" $((wolne - 40960)) 13 # 10 clusters stay free: less than the new boot files need
mcopy -i "$OUT/pelny.img" "$Z/duzy" ::/zajete.bin
rm -f "$Z/duzy"

wzor "$N/BOOTX64.EFI" 15000 4; wzor "$N/grubx64.efi" 9000 6; wzor "$N/mmx64.efi" 5000 8; wzor "$N/start.png" 6000 10
printf 'set k1=nowy\nset k1_slot=a\n' > "$N/grub.cfg"; printf 'Kons K1 0.9.0\n' > "$N/info"
printf 'długa nazwa\n' > "$N/Długa nazwa — pliku.txt"
NOWE="$N/BOOTX64.EFI:/EFI/BOOT/BOOTX64.EFI $N/grubx64.efi:/EFI/BOOT/grubx64.efi $N/mmx64.efi:/EFI/BOOT/mmx64.efi"
# shellcheck disable=SC2086 # the list above has no spaces in its paths
obraz "$OUT/pendrive.img" 8 40 21 $NOWE "$N/grub.cfg:/boot/grub/grub.cfg" "$N/start.png:/boot/grub/start.png" \
    "$N/info:/.disk/info" "$N/Długa nazwa — pliku.txt:/EFI/BOOT/Długa nazwa — pliku.txt"
# shellcheck disable=SC2086
obraz "$OUT/inny.img" 16 40 21 $NOWE "$N/grub.cfg:/boot/grub/grub.cfg" "$N/info:/.disk/info"
a=$(miejsca "$OUT/dysk.img"); b=$(miejsca "$OUT/pendrive.img"); c=$(miejsca "$OUT/pelny.img")
[ "$a" = "$b" ] && [ "$a" = "$c" ] || { echo "obrazy_efi: grubenv i miejsca w różnych miejscach ($a / $b / $c)"; exit 1; }
echo "$a" > "$OUT/miejsca.txt"

#!/bin/sh
# Obrazy do testu aktualizacji przez sieć (test_siec, 0.10.5 krok 2, ADR 0046), zrobione jak w scripts/zbuduj-usb.sh
# (FAT32, klastry 4 KiB, grubenv, miejsce B, miejsce A, potem pliki startowe; manifesty przez narzedzia/podpisz):
#   nowa.img     K1-EFI wydania 0.11.0 (miejsce A: system 0.11.0, nowe pliki startowe) — z niej paczka i ogłoszenie
#   dysk.img     K1-EFI na dysku: działa miejsce A (0.10.9), w miejscu B poprzednia 0.10.8, stare pliki startowe;
#                partycja 512 MiB (FAT 300 MiB — za nią poczekalnia)
#   paczka.k1a, ogloszenie.bin   wydanie 0.11.0 (narzedzia/wydanie), podpisane kluczem testowym (K1_KLUCZ_PODPISU)
#   obcy.bin     to samo ogłoszenie podpisane innym kluczem; stare.bin — ogłoszenie wersji 0.10.8
# Użycie: obrazy_paczki.sh KATALOG BUDOWA_OBJ (podpisz i wydanie są w BUDOWA_OBJ)
set -eu
OUT=$1 OBJ=$2
export LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1
rm -rf "$OUT"; mkdir -p "$OUT/z"
Z=$OUT/z

wzor() { # wzor PLIK BAJTÓW ZIARNO
    python3 -I -c 'import sys; n,z=int(sys.argv[2]),int(sys.argv[3]); open(sys.argv[1],"wb").write(bytes((i*z+7)%251 for i in range(n)))' "$1" "$2" "$3"
}

slot() { # slot PLIK WERSJA ZIARNO — a slot of 128 MiB with a "system" of 1 MiB and its signed manifest
    rm -f "$1"; wzor "$1" 1048576 "$3"; truncate -s 128M "$1"
    "$OBJ/podpisz" slot "$1" "$2" "$(printf '%032x' "$3")" 1048576 >/dev/null
}

miejsce() { # bytes from the start of the image of a file's first cluster
    python3 -I - "$1" "$2" <<'PY'
import subprocess, sys, re
img, p = sys.argv[1], sys.argv[2]
b = open(img, "rb").read(64)
bps, spc, res, nfat = int.from_bytes(b[11:13], "little"), b[13], int.from_bytes(b[14:16], "little"), b[16]
fatsz = int.from_bytes(b[36:40], "little")
w = subprocess.run(["mshowfat", "-i", img, "::" + p], capture_output=True, text=True, check=True).stdout
k = int(re.search(r"<(\d+)", w).group(1))
print((res + nfat * fatsz + (k - 2) * spc) * bps)
PY
}

efi() { # efi PLIK A.img B.img ZIARNO_PLIKÓW — the K1-EFI image (300 MiB) with its layout block and boot manifest
    plik=$1 a=$2 b=$3 z=$4
    rm -f "$plik"; truncate -s 300M "$plik"
    mformat -i "$plik" -T $((300 * 2048)) -N 4b315553 -v K1-EFI -F -c 8 ::
    "$OBJ/podpisz" grubenv "$Z/grubenv"
    wzor "$Z/boot" $((200000 + z * 1000)) "$z"; wzor "$Z/grub" $((90000 + z * 3000)) $((z + 1)); printf 'Kons K1 %s\n' "$z" > "$Z/info"
    mmd -i "$plik" ::/boot ::/boot/grub ::/k1 ::/EFI ::/EFI/BOOT ::/.disk
    mcopy -i "$plik" "$Z/grubenv" ::/boot/grub/grubenv
    mcopy -i "$plik" "$b" ::/k1/b.img
    mcopy -i "$plik" "$a" ::/k1/a.img
    mcopy -i "$plik" "$Z/boot" ::/EFI/BOOT/BOOTX64.EFI
    mcopy -i "$plik" "$Z/grub" ::/EFI/BOOT/grubx64.efi
    mcopy -i "$plik" "$Z/info" ::/.disk/info
    "$OBJ/podpisz" uklad "$plik" "$(miejsce "$plik" /boot/grub/grubenv)" "$(miejsce "$plik" /k1/a.img)" \
        "$(miejsce "$plik" /k1/b.img)" >/dev/null
    "$OBJ/podpisz" start "$plik" >/dev/null
}

slot "$Z/nowa-a.img" 0.11.0 11; truncate -s 128M "$Z/pusty.img"
efi "$OUT/nowa.img" "$Z/nowa-a.img" "$Z/pusty.img" 5
slot "$Z/dysk-a.img" 0.10.9 9; slot "$Z/dysk-b.img" 0.10.8 8
efi "$OUT/dysk.img" "$Z/dysk-a.img" "$Z/dysk-b.img" 3
truncate -s 512M "$OUT/dysk.img" # the partition: 512 MiB, the FAT 300 MiB
"$OBJ/wydanie" paczka "$OUT/nowa.img" "$OUT/paczka.k1a" >/dev/null
printf '0.11.0\topis\tPliki ze stref: kopia zostaje w Plikach.\n0.10.9\tpoprawki\n' > "$Z/opisy"
"$OBJ/wydanie" ogloszenie "$OUT/paczka.k1a" "$Z/opisy" 1763400000 "$OUT/ogloszenie.bin" >/dev/null
K1_KLUCZ_PODPISU=AgICAgICAgICAgICAgICAgICAgICAgICAgICAgICAgI= "$OBJ/wydanie" ogloszenie "$OUT/paczka.k1a" "$Z/opisy" 1763400000 \
    "$OUT/obcy.bin" >/dev/null
# an announcement of 0.10.8: the package's header names another version — built from a slot of that version
slot "$Z/stara-a.img" 0.10.8 8; efi "$Z/stara.img" "$Z/stara-a.img" "$Z/pusty.img" 3
"$OBJ/wydanie" paczka "$Z/stara.img" "$Z/stara.k1a" >/dev/null
printf '' > "$Z/bez"
"$OBJ/wydanie" ogloszenie "$Z/stara.k1a" "$Z/bez" 1763300000 "$OUT/stare.bin" >/dev/null
rm -rf "$Z"

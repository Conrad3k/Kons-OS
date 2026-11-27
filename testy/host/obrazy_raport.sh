#!/bin/sh
# Obrazy pendrive'ów do testu raportu trybu ratunkowego (test_raport, 0.13.0 krok 3, ADR 0058) — zrobione zwykłymi
# narzędziami (mkfs.fat, sfdisk, mtools), jak sformatowałby je inny system:
#   fat32-mbr.img  64 MiB, tablica MBR, FAT32, kilka plików i jeden usunięty (wolne miejsce w środku katalogu)
#   fat16.img      32 MiB bez tablicy partycji, FAT16, pusty
#   fat12.img      1,44 MB FAT12 — raportu na nim nie piszemy (odmowa)
#   pelny.img      8 MiB FAT16 zapełniony jednym plikiem — brak miejsca
# Po teście (drugi argument „sprawdz”): fsck.fat bez błędów, mtools czyta raporty bajt w bajt (wzór jak w test_raport).
# Użycie: obrazy_raport.sh KATALOG [sprawdz]
set -eu
OUT=$1
export LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1
FSCK=$(command -v fsck.fat || echo /usr/sbin/fsck.fat)
MKFS=$(command -v mkfs.fat || echo /usr/sbin/mkfs.fat)
wzor() { # PLIK BAJTÓW NR — bajt i = (i*7 + nr) mod 256
    python3 -I -c 'import sys; n=int(sys.argv[2]); k=int(sys.argv[3]); open(sys.argv[1],"wb").write(bytes((i*7+k)%256 for i in range(n)))' "$1" "$2" "$3"
}
if [ "${2:-}" = sprawdz ]; then
    dd if="$OUT/fat32-mbr.img" of="$OUT/czesc.img" bs=512 skip=2048 status=none
    for o in "$OUT/czesc.img" "$OUT/fat16.img"; do
        "$FSCK" -n "$o" >"$OUT/fsck.txt" 2>&1 || { cat "$OUT/fsck.txt"; echo "fsck.fat: błędy w $o"; exit 1; }
    done
    # fat32-mbr: three reports (K1RAPORT, K1RAPOR1, K1RAPOR2) of 40 000, 1, 65 536 bytes; fat16: one of 5000
    i=0
    for p in K1RAPORT.TXT:40000 K1RAPOR1.TXT:1 K1RAPOR2.TXT:65536; do
        wzor "$OUT/wzor" "${p#*:}" "$i"
        mtype -i "$OUT/czesc.img" "::${p%%:*}" > "$OUT/odczyt"
        cmp -s "$OUT/wzor" "$OUT/odczyt" || { echo "fat32-mbr: ${p%%:*} inny niż zapisany"; exit 1; }
        i=$((i + 1))
    done
    wzor "$OUT/wzor" 5000 0
    mtype -i "$OUT/fat16.img" ::K1RAPORT.TXT > "$OUT/odczyt"
    cmp -s "$OUT/wzor" "$OUT/odczyt" || { echo "fat16: K1RAPORT.TXT inny niż zapisany"; exit 1; }
    mtype -i "$OUT/czesc.img" ::JEDEN.TXT > "$OUT/odczyt" && cmp -s "$OUT/odczyt" "$OUT/jeden" ||
        { echo "fat32-mbr: plik, który był, się zmienił"; exit 1; }
    echo "obrazy_raport: fsck.fat bez błędów, mtools czyta raporty bajt w bajt, inne pliki nietknięte"
    exit 0
fi
rm -rf "$OUT"; mkdir -p "$OUT"
truncate -s 64M "$OUT/fat32-mbr.img"
printf 'label: dos\nstart=2048, type=c\n' | sfdisk -q "$OUT/fat32-mbr.img" >/dev/null
truncate -s 63M "$OUT/czesc.img"
"$MKFS" -F 32 -s 1 -n PENDRIVE "$OUT/czesc.img" >/dev/null
wzor "$OUT/jeden" 3000 9
wzor "$OUT/dwa" 100 3
cp "$OUT/jeden" "$OUT/JEDEN.TXT"; cp "$OUT/dwa" "$OUT/DWA.TXT"
mcopy -i "$OUT/czesc.img" "$OUT/JEDEN.TXT" "$OUT/DWA.TXT" ::
mdel -i "$OUT/czesc.img" ::DWA.TXT
dd if="$OUT/czesc.img" of="$OUT/fat32-mbr.img" bs=512 seek=2048 conv=notrunc status=none
truncate -s 32M "$OUT/fat16.img"
"$MKFS" -F 16 -n PENDRIVE "$OUT/fat16.img" >/dev/null
truncate -s 1440K "$OUT/fat12.img"
"$MKFS" -F 12 "$OUT/fat12.img" >/dev/null
truncate -s 8M "$OUT/pelny.img"
"$MKFS" -F 16 -s 2 "$OUT/pelny.img" >/dev/null
WOLNE=$(mdir -i "$OUT/pelny.img" :: | python3 -I -c 'import re,sys; print(re.search(r"([\d ]+) bytes free", sys.stdin.read()).group(1).replace(" ", ""))')
wzor "$OUT/DUZY.BIN" "$WOLNE" 1
mcopy -i "$OUT/pelny.img" "$OUT/DUZY.BIN" ::

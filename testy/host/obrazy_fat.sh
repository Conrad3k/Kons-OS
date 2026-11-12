#!/bin/sh
# Obrazy pendrive'ów z FAT do testu czytnika (test_fat, 0.8.0 krok 3, ADR 0031), zrobione zwykłymi narzędziami
# (mtools, sfdisk) — tak, jak formatuje je inny system, a nie nasz własny kod:
#   fat12.img      bez tablicy partycji (jak dyskietka), polskie długie nazwy, plik ukryty, folder DCIM/100K1
#   fat16-mbr.img  tablica MBR, FAT16, duży plik, plik w kawałkach (pofragmentowany)
#   fat32-gpt.img  tablica GPT, FAT32, zdjęcie JPEG, folder z 1100 plikami
#   fat32-maly.img FAT32 z mniej niż 65 525 klastrami (mformat -F -c 8, jak próba T6 planu 0.8.2), bez partycji,
#                  folder z 3000 plików (plan 0.8.2, krok 2: L13, L14)
#   ntfs-mbr.img   tablica MBR z partycją NTFS (śmieci) — czytnik ma odmówić
# Treść plików: bajt i = (i*31 + 7) mod 251 (test liczy to samo). Użycie: obrazy_fat.sh KATALOG
set -eu
OUT=$1
export LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1
mkdir -p "$OUT"
Z=$OUT/zrodla
rm -rf "$Z"; mkdir -p "$Z"
wzor() { # wzor PLIK BAJTÓW [POCZĄTEK]
    python3 -I -c 'import sys; n=int(sys.argv[2]); p=bytes.fromhex(sys.argv[3]); d=bytes((i*31+7)%251 for i in range(n)); open(sys.argv[1],"wb").write(p+d[len(p):])' "$1" "$2" "${3:-}"
}

# FAT12 bez partycji
rm -f "$OUT/fat12.img"
mformat -C -i "$OUT/fat12.img" -f 1440 -v PENDRIVE ::
printf 'abc\n' > "$Z/Zdjęcie Łódź.txt"
wzor "$Z/KROTKA.TXT" 1500
wzor "$Z/OBRAZ.PNG" 700 89504e470d0a1a0a
wzor "$Z/UKRYTY.TXT" 10
mcopy -i "$OUT/fat12.img" "$Z/Zdjęcie Łódź.txt" "$Z/KROTKA.TXT" "$Z/UKRYTY.TXT" ::
mattrib -i "$OUT/fat12.img" +h ::/UKRYTY.TXT
mmd -i "$OUT/fat12.img" ::/DCIM ::/DCIM/100K1
mcopy -i "$OUT/fat12.img" "$Z/OBRAZ.PNG" ::/DCIM/100K1/

# FAT16 w partycji MBR (typ 0x0e) od 1 MiB
rm -f "$OUT/czesc.img" "$OUT/fat16-mbr.img"
mformat -C -i "$OUT/czesc.img" -T 65536 ::
wzor "$Z/duzy.bin" 307200
wzor "$Z/a.bin" 8192
wzor "$Z/b.bin" 8192
wzor "$Z/frag.bin" 40000
mcopy -i "$OUT/czesc.img" "$Z/a.bin" "$Z/b.bin" ::
mdel -i "$OUT/czesc.img" ::/a.bin
mcopy -i "$OUT/czesc.img" "$Z/frag.bin" "$Z/duzy.bin" ::
truncate -s 40M "$OUT/fat16-mbr.img"
printf 'label: dos\nstart=2048, size=65536, type=e\n' | sfdisk -q "$OUT/fat16-mbr.img" >/dev/null
dd if="$OUT/czesc.img" of="$OUT/fat16-mbr.img" bs=512 seek=2048 conv=notrunc status=none

# FAT32 w partycji GPT (Microsoft basic data) od 1 MiB
rm -f "$OUT/czesc.img" "$OUT/fat32-gpt.img"
mformat -C -i "$OUT/czesc.img" -F -c 1 -T 98304 ::
wzor "$Z/zdjecie.jpg" 20000 ffd8ffe0
mcopy -i "$OUT/czesc.img" "$Z/zdjecie.jpg" ::
mmd -i "$OUT/czesc.img" ::/Duzo
mkdir -p "$Z/duzo"
python3 -I -c 'import sys
for i in range(1100): open(f"{sys.argv[1]}/p{i:04d}.txt", "w").write(str(i))' "$Z/duzo"
mcopy -i "$OUT/czesc.img" "$Z"/duzo/* ::/Duzo/
truncate -s 64M "$OUT/fat32-gpt.img"
printf 'label: gpt\nstart=2048, size=98304, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7\n' | sfdisk -q "$OUT/fat32-gpt.img" >/dev/null
dd if="$OUT/czesc.img" of="$OUT/fat32-gpt.img" bs=512 seek=2048 conv=notrunc status=none
rm -f "$OUT/czesc.img"

# FAT32 „mały”: 64 MiB, klaster 4 KiB → ok. 16 000 klastrów (z samej liczby wyszedłby FAT16)
rm -f "$OUT/fat32-maly.img"
mformat -C -i "$OUT/fat32-maly.img" -F -c 8 -T 131072 ::
mmd -i "$OUT/fat32-maly.img" ::/Telefon
rm -rf "$Z/telefon"; mkdir -p "$Z/telefon"
python3 -I -c 'import sys
for i in range(3000): open(f"{sys.argv[1]}/IMG_2026{i:04d}_zdjecie.jpg", "w").write(str(i))' "$Z/telefon"
mcopy -i "$OUT/fat32-maly.img" "$Z"/telefon/* ::/Telefon/
wzor "$Z/zdjecie.jpg" 20000 ffd8ffe0
mcopy -i "$OUT/fat32-maly.img" "$Z/zdjecie.jpg" ::

# NTFS (tylko typ w MBR i śmieci w partycji): nie FAT
rm -f "$OUT/ntfs-mbr.img"
truncate -s 8M "$OUT/ntfs-mbr.img"
printf 'label: dos\nstart=2048, type=7\n' | sfdisk -q "$OUT/ntfs-mbr.img" >/dev/null
printf '\353R\220NTFS    ' | dd of="$OUT/ntfs-mbr.img" bs=512 seek=2048 conv=notrunc status=none
rm -rf "$Z"

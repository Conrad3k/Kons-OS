#!/bin/sh
# Składa obraz K1 na pendrive z miejscem na dane (M5d, ADR 0016; A/B od 0.7.0, ADR 0022):
#   build/wynik/k1-<wersja>-usb.img (+ .img.xz do pobrania)
# GPT:  1. „K1-EFI”  (FAT32, 1600 MiB — do 0.10.9 krok 4 280 MiB; format 2 — src/wspolne/k1_uklad.h): shim + GRUB (z Ubuntu),
#          MokManager, grub.cfg, grubenv, dwa miejsca na system: /k1/a.img (k1.efi tej wersji i za nim obraz strefy
#          przeglądarki z drzewem dm-verity — 0.10.9 krok 4, ADR 0050) i /k1/b.img, po 768 MiB (do kroku 4b po 128 MiB)
#       2. „K1-DANE” (64 MiB, same zera): zaszyfrowany magazyn tworzy skarbiec przy pierwszym starcie; wcześniej
#          nadzorca powiększa pustą K1-DANE do końca pendrive'a (0.8.0, ADR 0033, src/nadzorca/powieksz.c).
# Instalacja kopiuje K1-EFI na dysk (bez miejsca B); aktualizacja zapisuje nieużywane miejsce (A/B) i — gdy się
# zmieniły — pliki startowe obok starych (0.9.0, ADR 0038). W miejscu A: manifest systemu i manifest plików startowych.
# Aktualizacje przez sieć (0.10.5 krok 2, ADR 0046): z gotowej K1-EFI także paczka aktualizacji
# (build/wynik/k1-<wersja>.k1a: miejsce A i pliki startowe) i jej ogłoszenie (build/wynik/k1-ogloszenie.bin: wersja,
# skrót paczki, opisy wersji z CHANGELOG.md — scripts/opisy-wersji.py), podpisane tym samym kluczem co manifesty.
# Od 0.13.0 (ADR 0056) dwa ogłoszenia: k1-ogloszenie.bin (format 1 — dla K1 do 0.12.x, nigdy się nie zmienia)
# i k1-ogloszenie2.bin (format 2 z czasem podpisu — czyta je K1 od 0.13; workflow „Ogłoszenie dnia” podpisuje je co dzień
# od nowa, więc nie ma go w SHA256SUMS: chroni je własny podpis).
# Wymaga: scripts/zbuduj.sh (jądro, programy, obrazy, k1.efi).
. "$(dirname "$0")/wspolne.sh"

USB=$K1_BUDOWA/usb
WYJSCIE=$K1_WYNIK/k1-$K1_WERSJA-usb.img
# 0.10.9 step 4b (ADR 0050, the owner's D10 "A": more room, with a reserve): slots of 768 MiB (128 MiB before), the
# K1-EFI 1600 MiB = two slots + 64 MiB for the boot files (an update writes the new ones beside the old ones)
SLOT_MIB=768
ESP_MIB=1600
DANE_MIB=64
PODPISZ=$K1_BUDOWA/obj/podpisz
WYDANIE=$K1_BUDOWA/obj/wydanie
[ -f "$K1_WYNIK/k1.efi" ] && [ -f "$K1_WYNIK/wydanie" ] && [ -f "$K1_BUDOWA/gen/start.png" ] && [ -x "$PODPISZ" ] && [ -x "$WYDANIE" ] \
    || blad "najpierw zbuduj system: scripts/zbuduj.sh"
for n in apt-get dpkg-deb mformat mmd mcopy mshowfat sfdisk xz truncate od; do command -v $n >/dev/null || blad "brak $n (scripts/przygotuj-srodowisko.sh)"; done

info "obraz USB $WYJSCIE"
rm -rf "$USB"; mkdir -p "$USB"

# Slot A: an inner FAT with /k1.efi (GRUB opens it with loopback), right after it the browser's zone image with its
# dm-verity tree (0.10.9 step 4, ADR 0050: it ends where the slot's image ends — k1_obraz_strefy.h), its manifest in
# the slot's last MiB. The manifest's digest covers both: an update copies them together.
STREFA=$K1_WYNIK/strefa-przegladarka.img
[ -f "$STREFA" ] || blad "brak obrazu strefy $STREFA (scripts/zbuduj-strefe.sh)"
EFI_MIB=$(( ($(stat -c %s "$K1_WYNIK/k1.efi") + 2 * 1048576 - 1) / 1048576 ))
obraz_fat "$USB/slot-a.fat" "$EFI_MIB" 4b315341 "$K1_WYNIK/k1.efi:/k1.efi"
cat "$USB/slot-a.fat" "$STREFA" > "$USB/a.img"
SLOT_BAJTY=$(stat -c %s "$USB/a.img")
MIEJSCE=$(( (SLOT_MIB - 1) * 1048576 )) # up to the manifests in the slot's last MiB
[ "$SLOT_BAJTY" -le "$MIEJSCE" ] ||
    blad "k1.efi i obraz strefy nie mieszczą się w miejscu A/B: brakuje $(( (SLOT_BAJTY - MIEJSCE + 1023) / 1024 )) KiB"
info "miejsce A ($SLOT_MIB MiB): k1.efi $((EFI_MIB)) MiB + obraz strefy $(( $(stat -c %s "$STREFA") >> 20 )) MiB, zapas $(( (MIEJSCE - SLOT_BAJTY) >> 20 )) MiB"
truncate -s ${SLOT_MIB}M "$USB/a.img"
"$PODPISZ" slot "$USB/a.img" "$K1_WERSJA" "$(cat "$K1_WYNIK/wydanie")" "$SLOT_BAJTY" || blad "manifest slotu A"
truncate -s ${SLOT_MIB}M "$USB/b.img" # slot B: empty until the first update
"$PODPISZ" grubenv "$USB/grubenv" || blad "grubenv"

{ grub_poczatek; cat <<CFG; } > "$USB/grub.cfg"
# A/B (0.7.0, ADR 0022): system jest w /k1/a.img albo /k1/b.img (w środku jeden plik: k1.efi — jądro z rdzeniem,
# podpisane kluczem K1). grubenv: k1_slot = potwierdzone miejsce; k1_nowy = do wypróbowania raz (po aktualizacji);
# k1_start = co uruchomiono teraz; k1_proba = ostatnia próba, dopóki K1 jej nie potwierdzi albo nie zgłosi.
# M5f: everything from the partition GRUB itself was loaded from — with K1 on the disk and a K1 stick
# plugged in, its search by /.disk/info may find the other one first.
regexp --set=1:k1_dysk '^\(([^)]+)\)' "\$cmdpath"
if [ -n "\$k1_dysk" -a -e "(\$k1_dysk)/k1/a.img" ]; then set root=\$k1_dysk; fi
background_image -m stretch /boot/grub/start.png
echo ""
load_env --file /boot/grub/grubenv k1_slot k1_nowy k1_proba
if [ "\$k1_slot" != b ]; then set k1_slot=a; fi
set k1_start=\$k1_slot
if [ "\$k1_nowy" = a -o "\$k1_nowy" = b ]; then
    set k1_start=\$k1_nowy
    set k1_proba=\$k1_nowy
fi
set k1_nowy=
save_env --file /boot/grub/grubenv k1_start k1_nowy k1_proba
loopback k1sys /k1/\${k1_start}.img
linux (k1sys)/k1.efi
boot
# Here only when the system could not be loaded (damaged, or not signed with a key this computer trusts).
# A new slot that does not load: straight back to the confirmed one (K1 tells the owner after the start).
if [ "\$k1_start" != "\$k1_slot" ]; then
    loopback -d k1sys
    set k1_start=\$k1_slot
    save_env --file /boot/grub/grubenv k1_start
    loopback k1sys /k1/\${k1_start}.img
    linux (k1sys)/k1.efi
    boot
fi
terminal_output console
set color_normal=white/black
echo "Kons K1 $K1_WERSJA nie moze wystartowac."
echo "System na tym nosniku jest uszkodzony albo Secure Boot nie zna klucza K1."
echo "Wlacz komputer z pendrive'a K1: Instalator naprawi system (dane zostana)."
echo "Albo wylacz Secure Boot w BIOS-ie (F1 na ThinkPadzie) i wpisz klucz K1 jeszcze raz."
sleep 600
CFG

# The EFI partition: FAT32 with 4 KiB clusters; the order of the files fixes where they are —
# grubenv, slot B, slot A, then the rest (the boot files). The same inputs give the same image (obraz_fat).
efi_podpisane
ESP=$USB/esp.img
printf 'Kons K1 %s\n' "$K1_WERSJA" > "$USB/info"
K1_FAT_OPCJE="-F -c 8" obraz_fat "$ESP" "$ESP_MIB" 4b315553 "$USB/grubenv:/boot/grub/grubenv" "$USB/b.img:/k1/b.img" \
    "$USB/a.img:/k1/a.img" "$K1_EFI/shimx64.efi.signed.latest:/EFI/BOOT/BOOTX64.EFI" \
    "$K1_EFI/gcdx64.efi.signed:/EFI/BOOT/grubx64.efi" "$K1_EFI/mmx64.efi:/EFI/BOOT/mmx64.efi" \
    "$USB/info:/.disk/info" "$USB/grub.cfg:/boot/grub/grub.cfg" "$K1_BUDOWA/gen/start.png:/boot/grub/start.png" \
    "$K1_WYNIK/k1-sb.cer:/k1/klucz.cer"

# Where the three files are: FAT32 boot sector (bytes/sector, sectors/cluster, reserved, FATs, FAT size)
# and each file's cluster run (mshowfat "<first-last>"); every file must be one contiguous run.
pole() { od -An -tu"$2" -j"$1" -N"$2" "$ESP" | tr -d ' '; }
BPS=$(pole 11 2) SPC=$(pole 13 1) RES=$(pole 14 2) NFAT=$(pole 16 1) FATSZ=$(pole 36 4)
miejsce() { # $1 = path in the image, $2 = expected size → byte offset
    zakres=$(mshowfat -i "$ESP" "::$1" | sed -n 's/.*<\([0-9]*\)-\([0-9]*\)>$/\1 \2/p; s/.*<\([0-9]*\)>$/\1 \1/p')
    set -- "$1" "$2" $zakres
    [ $# -eq 4 ] || blad "$1 nie leży w jednym kawałku w K1-EFI"
    [ $(( ($4 - $3 + 1) * SPC * BPS )) -ge "$2" ] || blad "$1: zły rozmiar w K1-EFI"
    echo $(( (RES + NFAT * FATSZ + ($3 - 2) * SPC) * BPS ))
}
GENV=$(miejsce /boot/grub/grubenv 1024) A=$(miejsce /k1/a.img $((SLOT_MIB << 20))) B=$(miejsce /k1/b.img $((SLOT_MIB << 20)))
"$PODPISZ" uklad "$ESP" "$GENV" "$A" "$B" $((SLOT_MIB << 20)) || blad "blok układu K1-EFI"
# The manifest of the boot files (ADR 0038): the digest of every file but grubenv and the slots, signed, in slot A.
"$PODPISZ" start "$ESP" || blad "manifest plików startowych (podpisz)"

# 0.10.5 step 2 (ADR 0046): the update package and its signed announcement, from the finished K1-EFI
PACZKA=$K1_WYNIK/k1-$K1_WERSJA.k1a
OGL=$K1_WYNIK/k1-ogloszenie.bin
OGL2=$K1_WYNIK/k1-ogloszenie2.bin
python3 -I "$K1_REPO/scripts/opisy-wersji.py" "$K1_REPO/CHANGELOG.md" "$K1_WERSJA" > "$USB/opisy.txt" || blad "opisy wersji (CHANGELOG.md)"
NOWA_LINIA=; python3 -I "$K1_REPO/scripts/opisy-wersji.py" --nowa-linia "$K1_REPO/CHANGELOG.md" "$K1_WERSJA" && NOWA_LINIA=nowa-linia
"$WYDANIE" paczka "$ESP" "$PACZKA" || blad "paczka aktualizacji"
"$WYDANIE" ogloszenie "$PACZKA" "$USB/opisy.txt" "$SOURCE_DATE_EPOCH" "$OGL" "$OGL2" $NOWA_LINIA || blad "ogłoszenie wydania"

# The disk: GPT with fixed GUIDs, the EFI partition copied in, K1-DANE left as zeros.
rm -f "$WYJSCIE" "$WYJSCIE.xz"
truncate -s $(((1 + ESP_MIB + DANE_MIB + 1) * 1024 * 1024)) "$WYJSCIE"
sfdisk -q "$WYJSCIE" >/dev/null <<TAB || blad "sfdisk"
label: gpt
label-id: 4B310000-0000-4000-8000-000000000001
start=2048, size=$((ESP_MIB * 2048)), type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, uuid=4B310000-0000-4000-8000-0000000000E1, name="K1-EFI"
start=$(((1 + ESP_MIB) * 2048)), size=$((DANE_MIB * 2048)), type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, uuid=4B310000-0000-4000-8000-0000000000D1, name="K1-DANE"
TAB
dd if="$ESP" of="$WYJSCIE" bs=1M seek=1 conv=notrunc,sparse status=none # the empty slot B: no room taken
xz -T0 -6 -k "$WYJSCIE"
(cd "$K1_WYNIK" && sha256sum "k1-$K1_WERSJA-usb.img.xz" > "k1-$K1_WERSJA-usb.img.xz.sha256" &&
    sed -i '/  k1-.*\.k1a$\|  k1-ogloszenie\.bin$/d' SHA256SUMS && sha256sum "k1-$K1_WERSJA.k1a" k1-ogloszenie.bin >> SHA256SUMS)
info "gotowe: $WYJSCIE ($(du -k "$WYJSCIE" | cut -f1) KiB), do pobrania: .img.xz ($(du -k "$WYJSCIE.xz" | cut -f1) KiB)"

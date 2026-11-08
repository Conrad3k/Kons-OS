#!/bin/sh
# Składa obraz ISO K1 do uruchomienia w maszynie wirtualnej (ADR 0010):
#   build/wynik/k1-<wersja>.iso — UEFI: shim + GRUB podpisane (Ubuntu); BIOS: GRUB El Torito → k1.efi.
# Od 0.7.0 (ADR 0022) cały system to jeden podpisany plik k1.efi: jądro z wbudowaną linią poleceń i initramfs
# z obrazem rdzenia (k1.rdzen=wbudowany, sprawdzanym przez dm-verity), więc obraz nie potrzebuje sterowników
# dysków ani napędów CD. Powtarzalne: te same wejścia = ten sam plik.
# Wymaga: scripts/zbuduj.sh (jądro, programy, obrazy, k1.efi).
. "$(dirname "$0")/wspolne.sh"

ISO=$K1_BUDOWA/iso
WYJSCIE=$K1_WYNIK/k1-$K1_WERSJA.iso
[ -f "$K1_WYNIK/k1.efi" ] || blad "najpierw zbuduj system: scripts/zbuduj.sh"
for n in grub-mkimage xorriso mformat mcopy apt-get dpkg-deb; do command -v $n >/dev/null || blad "brak $n (scripts/przygotuj-srodowisko.sh)"; done

info "obraz ISO $WYJSCIE"
rm -rf "$ISO"; mkdir -p "$ISO/k1" "$ISO/boot/grub/i386-pc" "$ISO/.disk"
# 0.7.0 (ADR 0022): the whole system is one signed file, k1.efi (command line and core inside)
cp "$K1_WYNIK/k1.efi" "$ISO/k1/"
{ grub_poczatek; cat <<CFG; } > "$ISO/boot/grub/grub.cfg"
background_image -m stretch /boot/grub/start.png
echo ""
menuentry "Kons K1 $K1_WERSJA" {
    linux /k1/k1.efi
}
CFG
printf 'Kons K1 %s\n' "$K1_WERSJA" > "$ISO/.disk/info"
cp "$K1_BUDOWA/gen/start.png" "$ISO/boot/grub/start.png"

# UEFI: shim + GRUB signed by Microsoft / Canonical (efi_podpisane, wspolne.sh) in a FAT image.
efi_podpisane
obraz_fat "$ISO/efi.img" 8 4b314953 "$K1_EFI/shimx64.efi.signed.latest:/EFI/BOOT/BOOTX64.EFI" \
    "$K1_EFI/gcdx64.efi.signed:/EFI/BOOT/grubx64.efi"
# BIOS: GRUB's El Torito image (not a PE file, nothing to sign) with the modules built in; it finds
# the disc by /.disk/info like the UEFI one.
cat > "$K1_BUDOWA/iso-bios.cfg" <<'CFG'
search --no-floppy --file --set=root /.disk/info
set prefix=($root)/boot/grub
configfile $prefix/grub.cfg
CFG
grub-mkimage -O i386-pc-eltorito -o "$ISO/boot/grub/i386-pc/eltorito.img" -c "$K1_BUDOWA/iso-bios.cfg" -p /boot/grub \
    biosdisk iso9660 part_msdos part_gpt search search_fs_file configfile normal linux echo test all_video gfxterm vbe \
    video_bochs png gfxterm_background >"$K1_BUDOWA/iso.log" 2>&1 || { cat "$K1_BUDOWA/iso.log"; blad "grub-mkimage (BIOS)"; }

find "$ISO" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
rm -f "$WYJSCIE"
# Hybrid image (CD and USB stick, BIOS and UEFI) like grub-mkrescue, but with the signed EFI files and
# fixed dates/GUIDs: the same inputs give the same ISO.
DATA=$(date -u -d "@$SOURCE_DATE_EPOCH" +%Y%m%d%H%M%S00)
xorriso -as mkisofs -graft-points --modification-date="$DATA" -V K1 \
    -b boot/grub/i386-pc/eltorito.img -no-emul-boot -boot-load-size 4 -boot-info-table --grub2-boot-info \
    --grub2-mbr /usr/lib/grub/i386-pc/boot_hybrid.img --efi-boot efi.img -efi-boot-part --efi-boot-image \
    --protective-msdos-label -o "$WYJSCIE" -r --sort-weight 0 / --sort-weight 1 /boot "$ISO" \
    -- -volume_date all_file_dates "=$SOURCE_DATE_EPOCH" -volume_date uuid "$DATA" \
    -boot_image any gpt_disk_guid=volume_date_uuid >>"$K1_BUDOWA/iso.log" 2>&1 || { cat "$K1_BUDOWA/iso.log"; blad "xorriso"; }
(cd "$K1_WYNIK" && sha256sum "k1-$K1_WERSJA.iso" > "k1-$K1_WERSJA.iso.sha256")
info "gotowe: $WYJSCIE ($(du -k "$WYJSCIE" | cut -f1) KiB)"

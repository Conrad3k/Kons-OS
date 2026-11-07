#!/bin/sh
# Składa obraz ISO K1 do uruchomienia w maszynie wirtualnej albo z pendrive'a (ADR 0010, 0016):
#   build/wynik/k1-<wersja>.iso — UEFI: shim + GRUB podpisane (Ubuntu); BIOS: GRUB El Torito
#   → jądro + initramfs + rdzen.cpio. Powtarzalne: te same wejścia = ten sam plik.
# rdzen.cpio zawiera rdzen.squashfs jako /rdzen.squashfs; k1-initramfs montuje go przez loop
# (k1.rdzen=wbudowany), więc obraz nie potrzebuje sterowników dysków ani napędów CD.
# Obraz z GRUB-em jest tymczasowy: w M10 zastąpi go podpisany UKI z Secure Boot.
# Wymaga: scripts/zbuduj.sh (jądro, programy, obrazy).
. "$(dirname "$0")/wspolne.sh"

GEN_CPIO=$K1_JADRO_OBJ/usr/gen_init_cpio
ISO=$K1_BUDOWA/iso
WYJSCIE=$K1_WYNIK/k1-$K1_WERSJA.iso
[ -f "$K1_WYNIK/rdzen.squashfs" ] && [ -x "$GEN_CPIO" ] || blad "najpierw zbuduj system: scripts/zbuduj.sh"
for n in grub-mkimage xorriso mformat mcopy apt-get dpkg-deb; do command -v $n >/dev/null || blad "brak $n (scripts/przygotuj-srodowisko.sh)"; done

info "obraz ISO $WYJSCIE"
rm -rf "$ISO"; mkdir -p "$ISO/k1" "$ISO/boot/grub/i386-pc" "$ISO/.disk"
cp "$K1_WYNIK/bzImage" "$K1_WYNIK/initramfs.cpio" "$ISO/k1/"
printf 'file /rdzen.squashfs %s 0400 0 0\n' "$K1_WYNIK/rdzen.squashfs" > "$K1_BUDOWA/rdzen-iso.lista"
"$GEN_CPIO" -t "$SOURCE_DATE_EPOCH" "$K1_BUDOWA/rdzen-iso.lista" > "$ISO/k1/rdzen.cpio"
# quiet: the kernel's EFI stub prints nothing over the boot screen ("EFI stub: Loaded initrd…");
# loglevel=7 keeps the kernel's messages on the serial console as before (tests, diagnosis).
# M6b: k1.rdzen_skrot = the dm-verity root hash of rdzen.squashfs (initramfs refuses a changed image).
CMDLINE="$(sed 's/k1\.rdzen=[^ ]*/k1.rdzen=wbudowany/' "$K1_REPO/jadro/cmdline") k1.rdzen_skrot=$(cat "$K1_WYNIK/rdzen.squashfs.skrot") quiet loglevel=7"
cat > "$ISO/boot/grub/grub.cfg" <<CFG
# Kons K1 $K1_WERSJA — start bez menu. Tryb graficzny ustawia GRUB (UEFI GOP albo VESA),
# jądro przejmuje go sterownikiem simpledrm. 1366x768 = ekran ThinkPada X230 (firmware laptopa
# nie oferuje 1280x800 — za dużo linii); nie jako pierwszy: karty emulowane (QEMU) pokazują ten
# tryb krzywo (szerokość zaokrąglona do 1360).
set timeout=0
set timeout_style=hidden
insmod all_video
insmod gfxterm
set gfxmode=1280x800,1366x768,1280x720,1024x768,auto
set gfxpayload=keep
insmod png
# Boot screen (0.5.8): the K1 mark on black; the kernel keeps it until the desktop's first frame.
# GRUB draws the picture only when something is written (echo); black text on black (set after
# terminal_output, which resets the colours) hides its cursor.
terminal_output gfxterm
set color_normal=black/black
background_image -m stretch /boot/grub/start.png
echo ""
menuentry "Kons K1 $K1_WERSJA" {
    linux /k1/bzImage $CMDLINE
    initrd /k1/initramfs.cpio /k1/rdzen.cpio
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

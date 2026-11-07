#!/bin/sh
# Składa obraz K1 na pendrive z miejscem na dane (M5d, ADR 0016):
#   build/wynik/k1-<wersja>-usb.img (+ .img.xz do pobrania)
# GPT:  1. „K1-EFI”  (FAT, 48 MiB): shim + GRUB (podpisane, z Ubuntu) + jądro + initramfs + rdzen.cpio
#       2. „K1-DANE” (64 MiB, same zera): zaszyfrowany magazyn tworzy skarbiec przy pierwszym starcie.
# Tylko UEFI (ThinkPad X230: UEFI, Secure Boot wyłączony — K1 jest niepodpisany do M10).
# Rdzeń jest w initramfs (k1.rdzen=wbudowany, jak w ISO), więc partycja danych to jedyne, co K1 czyta
# z pendrive'a. Wymaga: scripts/zbuduj.sh (jądro, programy, obrazy).
. "$(dirname "$0")/wspolne.sh"

GEN_CPIO=$K1_JADRO_OBJ/usr/gen_init_cpio
USB=$K1_BUDOWA/usb
WYJSCIE=$K1_WYNIK/k1-$K1_WERSJA-usb.img
ESP_MIB=48
DANE_MIB=64
[ -f "$K1_WYNIK/rdzen.squashfs" ] && [ -f "$K1_BUDOWA/gen/start.png" ] && [ -x "$K1_BUDOWA/obj/podpisz" ] && [ -x "$GEN_CPIO" ] || blad "najpierw zbuduj system: scripts/zbuduj.sh"
for n in apt-get dpkg-deb mformat mmd mcopy sfdisk xz truncate; do command -v $n >/dev/null || blad "brak $n (scripts/przygotuj-srodowisko.sh)"; done

info "obraz USB $WYJSCIE"
rm -rf "$USB"; mkdir -p "$USB"
printf 'file /rdzen.squashfs %s 0400 0 0\n' "$K1_WYNIK/rdzen.squashfs" > "$USB/rdzen.lista"
"$GEN_CPIO" -t "$SOURCE_DATE_EPOCH" "$USB/rdzen.lista" > "$USB/rdzen.cpio"
# quiet: the kernel's EFI stub prints nothing over the boot screen ("EFI stub: Loaded initrd…");
# loglevel=7 keeps the kernel's messages on the serial console as before (tests, diagnosis).
CMDLINE="$(sed 's/k1\.rdzen=[^ ]*/k1.rdzen=wbudowany/' "$K1_REPO/jadro/cmdline") quiet loglevel=7"

# Boot loader: shim + GRUB signed by Microsoft / Canonical, from Ubuntu (efi_podpisane, wspolne.sh).
# K1 itself is not signed yet (M10): Secure Boot must stay off.
efi_podpisane
cat > "$USB/grub.cfg" <<CFG
# Kons K1 $K1_WERSJA (pendrive) — start bez menu; obraz z firmware (GOP) przejmuje simpledrm.
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
# M5f: everything from the partition GRUB itself was loaded from — with K1 on the disk and a K1 stick
# plugged in, its search by /.disk/info may find the other one first.
regexp --set=1:k1_dysk '^\(([^)]+)\)' "\$cmdpath"
if [ -n "\$k1_dysk" -a -e "(\$k1_dysk)/k1/bzImage" ]; then set root=\$k1_dysk; fi
background_image -m stretch /boot/grub/start.png
echo ""
menuentry "Kons K1 $K1_WERSJA" {
    linux /k1/bzImage $CMDLINE
    initrd /k1/initramfs.cpio /k1/rdzen.cpio
}
CFG

# EFI partition (FAT): the same inputs give the same image (obraz_fat, wspolne.sh)
ESP=$USB/esp.img
printf 'Kons K1 %s\n' "$K1_WERSJA" > "$USB/info"
# FAT: 47 MiB; the last MiB of the partition holds the manifest (version, checksum, signature — M6a,
# ADR 0021) at 47 MiB, written by narzedzia/podpisz.c (key from K1_KLUCZ_PODPISU, else unsigned).
obraz_fat "$ESP" $((ESP_MIB - 1)) 4b315553 "$K1_EFI/shimx64.efi.signed.latest:/EFI/BOOT/BOOTX64.EFI" \
    "$K1_EFI/gcdx64.efi.signed:/EFI/BOOT/grubx64.efi" "$USB/info:/.disk/info" "$USB/grub.cfg:/boot/grub/grub.cfg" \
    "$K1_BUDOWA/gen/start.png:/boot/grub/start.png" \
    "$K1_WYNIK/bzImage:/k1/bzImage" "$K1_WYNIK/initramfs.cpio:/k1/initramfs.cpio" "$USB/rdzen.cpio:/k1/rdzen.cpio"

truncate -s "${ESP_MIB}M" "$ESP"
"$K1_BUDOWA/obj/podpisz" obraz "$ESP" "$K1_WERSJA" || blad "manifest obrazu systemu (podpisz)"

# The disk: GPT with fixed GUIDs, the EFI partition copied in, K1-DANE left as zeros.
rm -f "$WYJSCIE" "$WYJSCIE.xz"
truncate -s $(((1 + ESP_MIB + DANE_MIB + 1) * 1024 * 1024)) "$WYJSCIE"
sfdisk -q "$WYJSCIE" >/dev/null <<TAB || blad "sfdisk"
label: gpt
label-id: 4B310000-0000-4000-8000-000000000001
start=2048, size=$((ESP_MIB * 2048)), type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, uuid=4B310000-0000-4000-8000-0000000000E1, name="K1-EFI"
start=$(((1 + ESP_MIB) * 2048)), size=$((DANE_MIB * 2048)), type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, uuid=4B310000-0000-4000-8000-0000000000D1, name="K1-DANE"
TAB
dd if="$ESP" of="$WYJSCIE" bs=1M seek=1 conv=notrunc status=none
xz -T0 -6 -k "$WYJSCIE"
(cd "$K1_WYNIK" && sha256sum "k1-$K1_WERSJA-usb.img.xz" > "k1-$K1_WERSJA-usb.img.xz.sha256")
info "gotowe: $WYJSCIE ($(du -k "$WYJSCIE" | cut -f1) KiB), do pobrania: .img.xz ($(du -k "$WYJSCIE.xz" | cut -f1) KiB)"

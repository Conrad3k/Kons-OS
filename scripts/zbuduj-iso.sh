#!/bin/sh
# Składa obraz ISO K1 do uruchomienia w maszynie wirtualnej albo z pendrive'a (ADR 0010):
#   build/wynik/k1-<wersja>.iso — GRUB (UEFI i BIOS) → jądro + initramfs + rdzen.cpio
# rdzen.cpio zawiera rdzen.squashfs jako /rdzen.squashfs; k1-initramfs montuje go przez loop
# (k1.rdzen=wbudowany), więc obraz nie potrzebuje sterowników dysków ani napędów CD.
# Obraz z GRUB-em jest tymczasowy: w M10 zastąpi go podpisany UKI z Secure Boot.
# Wymaga: scripts/zbuduj.sh (jądro, programy, obrazy).
. "$(dirname "$0")/wspolne.sh"

GEN_CPIO=$K1_JADRO_OBJ/usr/gen_init_cpio
ISO=$K1_BUDOWA/iso
WYJSCIE=$K1_WYNIK/k1-$K1_WERSJA.iso
[ -f "$K1_WYNIK/rdzen.squashfs" ] && [ -x "$GEN_CPIO" ] || blad "najpierw zbuduj system: scripts/zbuduj.sh"
command -v grub-mkrescue >/dev/null || blad "brak grub-mkrescue (scripts/przygotuj-srodowisko.sh)"

info "obraz ISO $WYJSCIE"
rm -rf "$ISO"; mkdir -p "$ISO/k1" "$ISO/boot/grub"
cp "$K1_WYNIK/bzImage" "$K1_WYNIK/initramfs.cpio" "$ISO/k1/"
printf 'file /rdzen.squashfs %s 0400 0 0\n' "$K1_WYNIK/rdzen.squashfs" > "$K1_BUDOWA/rdzen-iso.lista"
"$GEN_CPIO" -t "$SOURCE_DATE_EPOCH" "$K1_BUDOWA/rdzen-iso.lista" > "$ISO/k1/rdzen.cpio"
CMDLINE=$(sed 's/k1\.rdzen=[^ ]*/k1.rdzen=wbudowany/' "$K1_REPO/jadro/cmdline")
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
terminal_output gfxterm
menuentry "Kons K1 $K1_WERSJA" {
    linux /k1/bzImage $CMDLINE
    initrd /k1/initramfs.cpio /k1/rdzen.cpio
}
CFG
find "$ISO" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
rm -f "$WYJSCIE"
grub-mkrescue -o "$WYJSCIE" "$ISO" -- -volid K1 >"$K1_BUDOWA/iso.log" 2>&1 || { cat "$K1_BUDOWA/iso.log"; blad "grub-mkrescue"; }
(cd "$K1_WYNIK" && sha256sum "k1-$K1_WERSJA.iso" > "k1-$K1_WERSJA.iso.sha256")
info "gotowe: $WYJSCIE ($(du -k "$WYJSCIE" | cut -f1) KiB)"

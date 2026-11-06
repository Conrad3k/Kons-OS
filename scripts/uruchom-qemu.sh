#!/bin/sh
# Uruchamia K1 w QEMU. Log z portu szeregowego trafia do pliku, monitor QEMU do gniazda unix
# (do zrzutów ekranu: scripts/zrzut-ekranu.sh). Działa na pierwszym planie do wyłączenia maszyny.
#
#   scripts/uruchom-qemu.sh [--obraz PLIK | --bez-dysku] [--cmdline "..."] [--log PLIK]
#                           [--monitor GNIAZDO] [--czas SEKUNDY]
#   scripts/uruchom-qemu.sh --iso PLIK [--uefi] [--vga std|virtio] ...   (start z obrazu ISO, ADR 0010)
#
# Domyślnie: build/wynik/rdzen.squashfs, linia poleceń z jadro/cmdline, log build/qemu/serial.log.
# Podgląd na żywo: tail -f build/qemu/serial.log
. "$(dirname "$0")/wspolne.sh"

OBRAZ=$K1_WYNIK/rdzen.squashfs
CMDLINE=$(cat "$K1_REPO/jadro/cmdline")
LOG=$K1_BUDOWA/qemu/serial.log
MONITOR=$K1_BUDOWA/qemu/monitor.sock
CZAS=0
ISO= UEFI= VGA=std
while [ $# -gt 0 ]; do
    case $1 in
        --obraz) OBRAZ=$2; shift 2 ;;
        --bez-dysku) OBRAZ=; shift ;;
        --cmdline) CMDLINE=$2; shift 2 ;;
        --log) LOG=$2; shift 2 ;;
        --monitor) MONITOR=$2; shift 2 ;;
        --czas) CZAS=$2; shift 2 ;;
        --iso) ISO=$2; shift 2 ;;
        --uefi) UEFI=1; shift ;;
        --vga) VGA=$2; shift 2 ;;
        *) blad "nieznana opcja: $1" ;;
    esac
done
mkdir -p "$(dirname "$LOG")" "$(dirname "$MONITOR")"
rm -f "$LOG" "$MONITOR"

# KVM, jeśli jest (np. runner CI z /dev/kvm); inaczej emulacja TCG (wolniej, ale działa wszędzie).
if [ -w /dev/kvm ]; then AKCEL="-accel kvm -cpu host"; else AKCEL="-accel tcg -cpu max"; fi
DYSK=
[ -n "$OBRAZ" ] && DYSK="-drive file=$OBRAZ,if=virtio,format=raw,readonly=on"

if [ -n "$ISO" ]; then
    # Like a VM user: firmware → GRUB from the CD → kernel; virtio-vga has no K1 driver → simpledrm.
    FIRMWARE=; [ -n "$UEFI" ] && FIRMWARE="-bios /usr/share/ovmf/OVMF.fd"
    set -- -machine q35 $AKCEL -m 512M -smp 2 -nodefaults -no-reboot $FIRMWARE \
        -vga "$VGA" -display none -nic none \
        -serial "file:$LOG" -monitor "unix:$MONITOR,server,nowait" -cdrom "$ISO"
else
set -- -machine q35 $AKCEL -m 256M -smp 2 -nodefaults -no-reboot \
    -vga std -display none -nic none \
    -serial "file:$LOG" -monitor "unix:$MONITOR,server,nowait" \
    -kernel "$K1_WYNIK/bzImage" -initrd "$K1_WYNIK/initramfs.cpio" -append "$CMDLINE" $DYSK
fi
if [ "$CZAS" -gt 0 ]; then
    exec timeout --foreground "$CZAS" qemu-system-x86_64 "$@"
else
    exec qemu-system-x86_64 "$@"
fi

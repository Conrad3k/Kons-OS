#!/bin/sh
# Uruchamia K1 w QEMU. Log z portu szeregowego trafia do pliku, monitor QEMU do gniazda unix
# (do zrzutów ekranu: scripts/zrzut-ekranu.sh), a obok niego gniazdo QMP <GNIAZDO>.qmp (0.10.5: klawisze po kolei —
# Alt przytrzymany przy Tab, testy/qemu/k1qemu.py: klawisze). Działa na pierwszym planie do wyłączenia maszyny.
#
#   scripts/uruchom-qemu.sh [--obraz PLIK | --bez-dysku] [--obraz-strefy PLIK|-] [--cmdline "..."] [--log PLIK]
#                           [--monitor GNIAZDO] [--czas SEKUNDY]
#                           [--maszyna ",i8042=off"] [--qemu "-device ..."]   (dyski, USB — M5a)
#                           [--pamiec MiB] [--emulacja] [--bez-wirtualizacji]   (strefy — 0.10.0: pamięć K1 w QEMU,
#                           emulacja zamiast KVM — QEMU udaje wtedy wirtualizację AMD; procesor bez VT-x/AMD-V)
#   scripts/uruchom-qemu.sh --iso PLIK [--uefi] [--vga std|virtio] ...   (start z obrazu ISO, ADR 0010; bez --iso: jądro
#                           testów przez UEFI z obrazem z firmware — 0.13.0 krok 4, ekran awarii)
#   scripts/uruchom-qemu.sh --pendrive PLIK ...   (obraz USB jako pamięć USB, start przez UEFI — M5d)
#   scripts/uruchom-qemu.sh --dysk-uefi PLIK ...  (dysk SATA, start z niego przez UEFI — zainstalowany K1, M5f)
#   ... --zmienne-uefi PLIK                      (z --pendrive/--dysk-uefi: OVMF z obsługą Secure Boot i trwałymi
#                                                 zmiennymi UEFI w PLIKU — klucze, MOK, stan SB; M6d, 0.7.0)
#
# Domyślnie: build/wynik/rdzen.squashfs, linia poleceń z jadro/cmdline, log build/qemu/serial.log.
# Z --obraz (rdzeń na /dev/vda) obraz strefy przeglądarki nie leży w miejscu A/B (0.10.9 krok 4, ADR 0050): dostaje go
# drugi dysk virtio (adres 0x1c, tylko do odczytu) — build/wynik/strefa-przegladarka.img, inny plik (--obraz-strefy PLIK)
# albo żaden (--obraz-strefy -). Nadzorca bierze dysk virtio dokładnie wielkości obrazu, jeśli zgadza się ze skrótem w rdzeniu.
# Podgląd na żywo: tail -f build/qemu/serial.log
. "$(dirname "$0")/wspolne.sh"

OBRAZ=$K1_WYNIK/rdzen.squashfs
OBRAZ_STREFY=$K1_WYNIK/strefa-przegladarka.img
CMDLINE=$(cat "$K1_REPO/jadro/cmdline")
LOG=$K1_BUDOWA/qemu/serial.log
MONITOR=$K1_BUDOWA/qemu/monitor.sock
CZAS=0
ISO= UEFI= VGA=std MASZYNA= DODATKI= PENDRIVE= DYSK_UEFI= ZMIENNE= PAMIEC= EMULACJA= BEZ_WIRT=
while [ $# -gt 0 ]; do
    case $1 in
        --obraz) OBRAZ=$2; shift 2 ;;
        --bez-dysku) OBRAZ=; shift ;;
        --obraz-strefy) OBRAZ_STREFY=$2; shift 2 ;;
        --cmdline) CMDLINE=$2; shift 2 ;;
        --log) LOG=$2; shift 2 ;;
        --monitor) MONITOR=$2; shift 2 ;;
        --czas) CZAS=$2; shift 2 ;;
        --iso) ISO=$2; shift 2 ;;
        --pendrive) PENDRIVE=$2; shift 2 ;;
        --dysk-uefi) DYSK_UEFI=$2; shift 2 ;;
        --uefi) UEFI=1; shift ;;
        --zmienne-uefi) ZMIENNE=$2; shift 2 ;;
        --vga) VGA=$2; shift 2 ;;
        --maszyna) MASZYNA=$2; shift 2 ;;
        --qemu) DODATKI=$2; shift 2 ;;
        --pamiec) PAMIEC=$2; shift 2 ;;
        --emulacja) EMULACJA=1; shift ;;
        --bez-wirtualizacji) BEZ_WIRT=1; shift ;;
        *) blad "nieznana opcja: $1" ;;
    esac
done
mkdir -p "$(dirname "$LOG")" "$(dirname "$MONITOR")"
rm -f "$LOG" "$MONITOR" "$MONITOR.qmp"

# KVM, jeśli jest (np. runner CI z /dev/kvm); inaczej emulacja TCG (wolniej, ale działa wszędzie).
if [ -w /dev/kvm ] && [ -z "$EMULACJA" ]; then AKCEL="-accel kvm -cpu host"; else AKCEL="-accel tcg -cpu max"; fi
# 0.10.0 (ADR 0042): a processor without virtualization (the zones must say so instead of starting)
[ -n "$BEZ_WIRT" ] && AKCEL="$AKCEL,vmx=off,svm=off"
# The memory of the machine: 1 GiB from firmware (k1.efi carries the core in memory — with the browser zone's image
# 121 MiB since 0.10.0 step 2; 512 MiB no longer boots it), 256 MiB without (bzImage + initramfs); --pamiec for the zones.
P512=${PAMIEC:-1024}M P256=${PAMIEC:-256}M
DYSK=
[ -n "$OBRAZ" ] && DYSK="-drive file=$OBRAZ,if=virtio,format=raw,readonly=on"
if [ -n "$OBRAZ" ] && [ "$OBRAZ_STREFY" != - ] && [ -f "$OBRAZ_STREFY" ]; then
    DYSK="$DYSK -drive if=none,id=obraz-strefy,file=$OBRAZ_STREFY,format=raw,readonly=on -device virtio-blk-pci,drive=obraz-strefy,addr=0x1c"
fi
# M6b: the image's dm-verity root hash (OBRAZ.skrot from the build) unless the command line has one
# already (negative tests pass their own — or none).
if [ -n "$OBRAZ" ] && [ -f "$OBRAZ.skrot" ] && ! echo "$CMDLINE" | grep -q 'k1\.rdzen_skrot='; then
    CMDLINE="$CMDLINE k1.rdzen_skrot=$(cat "$OBRAZ.skrot")"
fi

# UEFI firmware: OVMF without stored variables, or (M6d) the Secure Boot build with a variable store in a
# file (SMM protects it, as on a real computer): Secure Boot on or off depends on what that file holds.
FW="-bios /usr/share/ovmf/OVMF.fd"
if [ -n "$ZMIENNE" ]; then
    MASZYNA="$MASZYNA,smm=on"
    FW="-global driver=cfi.pflash01,property=secure,value=on
        -drive if=pflash,format=raw,unit=0,file=/usr/share/OVMF/OVMF_CODE_4M.secboot.fd,readonly=on
        -drive if=pflash,format=raw,unit=1,file=$ZMIENNE"
fi
if [ -n "$DYSK_UEFI" ]; then
    # An installed K1 (M5f): UEFI firmware boots the SATA disk (AHCI of q35, like the X230's disk).
    set -- -machine "q35$MASZYNA" $AKCEL -m $P512 -smp 2 -nodefaults -no-reboot $FW \
        -vga virtio -display none -nic none -serial "file:$LOG" -monitor "unix:$MONITOR,server,nowait" \
        -drive if=none,id=dysk,file="$DYSK_UEFI",format=raw -device ide-hd,bus=ide.0,drive=dysk,bootindex=1
elif [ -n "$PENDRIVE" ]; then
    # Like the ThinkPad: UEFI firmware boots the stick (usb-storage on xHCI); picture from GOP → simpledrm.
    set -- -machine "q35$MASZYNA" $AKCEL -m $P512 -smp 2 -nodefaults -no-reboot $FW \
        -vga virtio -display none -nic none -serial "file:$LOG" -monitor "unix:$MONITOR,server,nowait" \
        -device qemu-xhci,id=xhci -drive if=none,id=pendrive,file="$PENDRIVE",format=raw \
        -device usb-storage,bus=xhci.0,drive=pendrive,bootindex=1
elif [ -n "$ISO" ]; then
    # Like a VM user: firmware → GRUB from the CD → kernel; virtio-vga has no K1 driver → simpledrm.
    FIRMWARE=; [ -n "$UEFI" ] && FIRMWARE="-bios /usr/share/ovmf/OVMF.fd"
    set -- -machine "q35$MASZYNA" $AKCEL -m $P512 -smp 2 -nodefaults -no-reboot $FIRMWARE \
        -vga "$VGA" -display none -nic none \
        -serial "file:$LOG" -monitor "unix:$MONITOR,server,nowait" -cdrom "$ISO"
else
# 0.13.0 step 4 (ADR 0059): --uefi --vga virtio — the kernel started by UEFI firmware with its picture (simpledrm, as on a
# laptop before i915): the screen of a critical error in the initramfs has a picture to draw on (bochs has none until ekran)
FIRMWARE=; [ -n "$UEFI" ] && FIRMWARE="-bios /usr/share/ovmf/OVMF.fd"
set -- -machine "q35$MASZYNA" $AKCEL -m $P256 -smp 2 -nodefaults -no-reboot $FIRMWARE \
    -vga "$VGA" -display none -nic none \
    -serial "file:$LOG" -monitor "unix:$MONITOR,server,nowait" \
    -kernel "$K1_WYNIK/bzImage" -initrd "$K1_WYNIK/initramfs.cpio" -append "$CMDLINE" $DYSK
fi
set -- "$@" -qmp "unix:$MONITOR.qmp,server,nowait"
# shellcheck disable=SC2086
[ -n "$DODATKI" ] && set -- "$@" $DODATKI
if [ "$CZAS" -gt 0 ]; then
    exec timeout --foreground "$CZAS" qemu-system-x86_64 "$@"
else
    exec qemu-system-x86_64 "$@"
fi

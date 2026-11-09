#!/bin/sh
# Zapisuje zrzut ekranu działającej maszyny QEMU (przez monitor, polecenie screendump) jako PNG.
#   scripts/zrzut-ekranu.sh [PLIK.png] [GNIAZDO_MONITORA]
# (To samo, co robią testy: testy/qemu/k1qemu.py.)
. "$(dirname "$0")/wspolne.sh"
WYJSCIE=${1:-$K1_BUDOWA/qemu/ekran.png}
MONITOR=${2:-$K1_BUDOWA/qemu/monitor.sock}
[ -S "$MONITOR" ] || blad "brak gniazda monitora QEMU: $MONITOR (czy maszyna działa?)"
rozmiar=$(python3 -I "$K1_REPO/testy/qemu/k1qemu.py" zrzut "$WYJSCIE" "$MONITOR") || blad "QEMU nie zapisał zrzutu ekranu"
echo "[k1] zrzut ekranu: $WYJSCIE $rozmiar"

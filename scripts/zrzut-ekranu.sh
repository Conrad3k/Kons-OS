#!/bin/sh
# Zapisuje zrzut ekranu działającej maszyny QEMU (przez monitor, polecenie screendump) jako PNG.
#   scripts/zrzut-ekranu.sh [PLIK.png] [GNIAZDO_MONITORA]
. "$(dirname "$0")/wspolne.sh"
WYJSCIE=${1:-$K1_BUDOWA/qemu/ekran.png}
MONITOR=${2:-$K1_BUDOWA/qemu/monitor.sock}
[ -S "$MONITOR" ] || blad "brak gniazda monitora QEMU: $MONITOR (czy maszyna działa?)"
PPM=$(mktemp --suffix=.ppm)
python3 -I - "$MONITOR" "$PPM" <<'PY'
import os, socket, sys, time
sock_path, ppm = sys.argv[1:3]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
s.settimeout(5)
def drain():
    data = b""
    try:
        while not data.endswith(b"(qemu) "):
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    return data
drain()
s.sendall(f"screendump {ppm}\n".encode())
drain()
s.close()
for _ in range(50):
    if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
        break
    time.sleep(0.1)
PY
[ -s "$PPM" ] || blad "QEMU nie zapisał zrzutu ekranu"
rozmiar=$(python3 -I "$K1_REPO/scripts/ppm-na-png.py" "$PPM" "$WYJSCIE") || { rm -f "$PPM"; exit 1; }
echo "[k1] zrzut ekranu: $WYJSCIE $rozmiar"
rm -f "$PPM"

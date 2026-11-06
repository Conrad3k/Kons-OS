#!/bin/sh
# Zapisuje zrzut ekranu działającej maszyny QEMU (przez monitor, polecenie screendump) jako PNG.
#   scripts/zrzut-ekranu.sh [PLIK.png] [GNIAZDO_MONITORA]
. "$(dirname "$0")/wspolne.sh"
WYJSCIE=${1:-$K1_BUDOWA/qemu/ekran.png}
MONITOR=${2:-$K1_BUDOWA/qemu/monitor.sock}
[ -S "$MONITOR" ] || blad "brak gniazda monitora QEMU: $MONITOR (czy maszyna działa?)"
PPM=$(mktemp --suffix=.ppm)
python3 -I - "$MONITOR" "$PPM" "$WYJSCIE" <<'PY'
import os, re, socket, struct, sys, time, zlib
sock_path, ppm, png = sys.argv[1:4]
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
raw = open(ppm, "rb").read()
# PPM P6: "P6 <w> <h> <max>" + exactly one whitespace byte + pixel data
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", raw)
assert m, "nieobsługiwany format PPM"
w, h, data = int(m.group(1)), int(m.group(2)), raw[m.end():]
rows = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
with open(png, "wb") as f:
    f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
print(f"[k1] zrzut ekranu: {png} ({w}x{h})")
PY
rm -f "$PPM"

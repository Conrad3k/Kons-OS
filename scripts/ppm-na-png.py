"""PPM (P6) → PNG without external libraries. Usage: python3 -I scripts/ppm-na-png.py IN.ppm OUT.png"""
import re, struct, sys, zlib

def ppm_na_png(raw: bytes, out_path: str) -> tuple:
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", raw)
    if not m:
        raise ValueError("nieobsługiwany format PPM")
    w, h, data = int(m.group(1)), int(m.group(2)), raw[m.end():]
    rows = b"".join(b"\x00" + data[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(out_path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    return w, h

if __name__ == "__main__":
    print(ppm_na_png(open(sys.argv[1], "rb").read(), sys.argv[2]))

#!/bin/sh
# Test etapu M2 (powtarzalny): ekran i wejście w działającym K1 (obraz produkcyjny).
# 1. start → pierwsza klatka (tapeta + logo), 2. mysz (monitor QEMU: mouse_move),
# 3. klawiatura (sendkey: strzałki przesuwają kursor) i przycisk myszy (zmiana koloru kursora).
# Każdy krok: zapis w logu ekranu + zrzut ekranu sprawdzany automatycznie (kolory pikseli).
# Zrzuty: build/testy/m2-*.png (dowód dla człowieka). Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
LOG=$TESTY/m2-ekran.log MON=$TESTY/m2-ekran.sock
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$LOG" "$MON" "$TESTY"/m2-*.png

"$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" &
QPID=$!
r=0
python3 -I - "$LOG" "$MON" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" "$CZAS" <<'PY' || r=$?
import re, socket, struct, subprocess, sys, time, zlib
log, mon, out, zrzut_sh, limit = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5])
start = time.time(); wyniki = []

def czekaj(wzor, od=0):
    while time.time() - start < limit:
        try: tekst = open(log, encoding="utf-8", errors="replace").read()
        except FileNotFoundError: tekst = ""
        m = list(re.finditer(wzor, tekst))
        if len(m) > od: return m[-1]
        if "Kernel panic" in tekst: break
        time.sleep(0.5)
    return None

def monitor(*cmds):
    s = socket.socket(socket.AF_UNIX); s.connect(mon); s.settimeout(2)
    for c in ("",) + cmds:
        s.sendall(c.encode() + b"\n"); time.sleep(0.3)
        try: s.recv(65536)
        except socket.timeout: pass
    s.close()

def png(path):  # our own PNGs: RGB8, filter 0 on every row
    raw = open(path, "rb").read(); pos = 8; idat = b""; w = h = 0
    while pos < len(raw):
        n, t = struct.unpack(">I4s", raw[pos:pos + 8]); d = raw[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b"IHDR": w, h = struct.unpack(">II", d[:8])
        elif t == b"IDAT": idat += d
    data = zlib.decompress(idat)
    return w, h, lambda x, y: tuple(data[y * (w * 3 + 1) + 1 + x * 3: y * (w * 3 + 1) + 4 + x * 3])

def zrzut(nazwa):
    p = f"{out}/m2-{nazwa}.png"
    subprocess.run([zrzut_sh, p, mon], check=True, stdout=subprocess.DEVNULL)
    return png(p)

def sprawdz(nazwa, warunek, opis=""):
    wyniki.append(warunek); print(f"  [{'OK' if warunek else 'BŁĄD'}]    {nazwa}" + ("" if warunek else f" — {opis}"))

krem = lambda c: c[0] > 200 and c[1] > 195 and c[2] > 180
akcent = lambda c: c[0] > 170 and c[1] < 150 and c[2] < 120 and c[0] > c[1] > c[2]
ciemny = lambda c: max(c) < 60
WZOR = r"K1-EKRAN: kursor (\d+),(\d+) przycisk (\S+)"

tryb = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
klatka = czekaj(r"K1-EKRAN: pierwsza klatka")
k0 = czekaj(WZOR)
sprawdz("ekran: tryb i pierwsza klatka w logu", bool(tryb and klatka and k0))
if not (tryb and klatka and k0): sys.exit(1)
W, H = int(tryb.group(1)), int(tryb.group(2)); x0, y0 = int(k0.group(1)), int(k0.group(2))
time.sleep(1)
w, h, px = zrzut("1-start")
sprawdz(f"zrzut 1: rozmiar {w}x{h} = tryb {W}x{H}", (w, h) == (W, H))
sprawdz("zrzut 1: logo (terakota w tarczy)", akcent(px(W // 2, int(0.535 * H))), str(px(W // 2, int(0.535 * H))))
sprawdz("zrzut 1: ciemna tapeta w rogach", ciemny(px(3, 3)) and ciemny(px(W - 4, 3)), str(px(3, 3)))
sprawdz("zrzut 1: kursor w miejscu z logu", krem(px(x0 + 3, y0 + 9)), str(px(x0 + 3, y0 + 9)))

monitor(*["mouse_move -60 -30"] * 4)
x1, y1 = x0 - 240, y0 - 120
m = czekaj(rf"K1-EKRAN: kursor {x1},{y1} ")
sprawdz(f"mysz: kursor przesunięty o (-240,-120) → {x1},{y1}", m is not None)
time.sleep(1)
w, h, px = zrzut("2-mysz")
sprawdz("zrzut 2: kursor w nowym miejscu", krem(px(x1 + 3, y1 + 9)), str(px(x1 + 3, y1 + 9)))
sprawdz("zrzut 2: w starym miejscu znów tapeta (brak śladu)", not krem(px(x0 + 3, y0 + 9)), str(px(x0 + 3, y0 + 9)))

monitor(*["sendkey right"] * 3 + ["mouse_button 1"])
x2 = x1 + 60
m = czekaj(rf"K1-EKRAN: kursor {x2},{y1} przycisk wciśnięty")
sprawdz(f"klawiatura: 3 x strzałka w prawo → {x2},{y1}; przycisk myszy wciśnięty", m is not None)
time.sleep(1)
w, h, px = zrzut("3-klawiatura-przycisk")
sprawdz("zrzut 3: kursor w kolorze akcentu (przycisk wciśnięty)", akcent(px(x2 + 3, y1 + 9)), str(px(x2 + 3, y1 + 9)))
monitor("mouse_button 0")

tekst = open(log, encoding="utf-8", errors="replace").read()
sprawdz("brak 'Kernel panic' i błędów usług", "Kernel panic" not in tekst and "BŁĄD" not in tekst)
sprawdz("brak odrzuconych i zgubionych zdarzeń przy poprawnym wejściu",
        re.findall(r"odrzucone (\d+)\)", tekst)[-1:] == ["0"] and "pominięto" not in tekst)
monitor("quit")
sys.exit(0 if all(wyniki) else 1)
PY
kill $QPID 2>/dev/null || true; wait $QPID 2>/dev/null || true
if [ $r -eq 0 ]; then info "M2: wszystko zaliczone (zrzuty: $TESTY/m2-*.png)"; else echo "[k1] M2: BŁĄD (log: $LOG)"; fi
exit $r

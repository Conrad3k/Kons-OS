#!/bin/sh
# Test etapu M5 (powtarzalny), krok M5a: sprzęt — wejście przez USB w działającym K1 (obraz produkcyjny).
# Maszyna BEZ kontrolera PS/2 (i8042=off), tylko klawiatura i mysz USB na kontrolerze xHCI:
# 1. start → wejscie znajduje klawiaturę i mysz USB, 2. mysz USB przesuwa kursor (monitor QEMU),
# 3. klawiatura USB (strzałki przesuwają kursor, gdy żadne okno nie ma fokusu),
# 4. podłączenie drugiej myszy w trakcie pracy (device_add), odłączenie (device_del) i ponowne
#    podłączenie — system zauważa zmiany, nic się nie zawiesza.
# Dyski (SATA, NVMe, pendrive) i kontrolery EHCI/OHCI sprawdza test w systemie (scripts/test-m1.sh).
# Zrzut: build/testy/m5-usb.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
LOG=$TESTY/m5-usb.log MON=$TESTY/m5-usb.sock
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$LOG" "$MON" "$TESTY"/m5-*.png

echo "[k1] test M5a: klawiatura i mysz USB (bez PS/2), podłączanie w trakcie pracy"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" --maszyna ",i8042=off" \
    --qemu "-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0" &
QPID=$!
r=0
python3 -I - "$LOG" "$MON" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" "$CZAS" <<'PY' || r=$?
import re, socket, struct, subprocess, sys, time, zlib
log, mon, out, zrzut_sh, limit = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5])
start = time.time(); wyniki = []

def tekst():
    try: return open(log, encoding="utf-8", errors="replace").read()
    except FileNotFoundError: return ""

def czekaj(wzor, od=0, sekundy=None):
    koniec = time.time() + sekundy if sekundy else start + limit
    while time.time() < min(koniec, start + limit):
        t = tekst(); m = list(re.finditer(wzor, t))
        if len(m) > od: return m[-1]
        if "Kernel panic" in t: break
        time.sleep(0.5)
    return None

def ile(wzor): return len(re.findall(wzor, tekst()))

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

def sprawdz(nazwa, warunek, opis=""):
    wyniki.append(bool(warunek)); print(f"  [{'OK' if warunek else 'BŁĄD'}]    {nazwa}" + ("" if warunek else f" — {opis}"))

krem = lambda c: c[0] > 200 and c[1] > 195 and c[2] > 180
GOTOWE = r"K1-WEJSCIE: gotowe \((\d+) urządzeń\)"
KURSOR = r"K1-EKRAN: kursor (\d+),(\d+) przycisk"

def urzadzenia(n, od=0, sekundy=60):
    """Waits until wejscie (re)started with n devices, at least `od` times before."""
    t_end = time.time() + sekundy
    while time.time() < t_end:
        m = re.findall(GOTOWE, tekst())
        if m and int(m[-1]) == n and m.count(str(n)) > od: return True
        time.sleep(0.5)
    return False

def kursor():
    m = list(re.finditer(KURSOR, tekst()))
    return (int(m[-1].group(1)), int(m[-1].group(2))) if m else None

tryb = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
sprawdz("ekran: pierwsza klatka", tryb and czekaj(r"K1-EKRAN: pierwsza klatka") and czekaj(KURSOR))
if not tryb: sys.exit(1)
sprawdz("brak kontrolera PS/2 (maszyna tylko z USB)", "i8042: PNP: No PS/2 controller found" in tekst())
sprawdz("wejscie: klawiatura i mysz USB (2 urządzenia)", urzadzenia(2, sekundy=90),
        str(re.findall(GOTOWE, tekst())))
time.sleep(1)
x0, y0 = kursor()

monitor(*["mouse_move -60 -30"] * 4)
x1, y1 = x0 - 240, y0 - 120
sprawdz(f"mysz USB: kursor przesunięty o (-240,-120) → {x1},{y1}", czekaj(rf"K1-EKRAN: kursor {x1},{y1} ", sekundy=20),
        str(kursor()))
monitor(*["sendkey right"] * 3)
x2 = x1 + 60
sprawdz(f"klawiatura USB: 3 x strzałka w prawo → {x2},{y1}", czekaj(rf"K1-EKRAN: kursor {x2},{y1} ", sekundy=20),
        str(kursor()))

# hotplug: a second mouse appears, disappears, appears again (same event number)
n_nowe = ile(r"nowe urządzenie wejścia")
monitor("device_add usb-mouse,id=mysz2,bus=xhci.0")
sprawdz("podłączenie myszy w trakcie pracy → wejscie z 3 urządzeniami",
        czekaj(r"nowe urządzenie wejścia", od=n_nowe, sekundy=30) and urzadzenia(3, sekundy=30))
monitor("device_del mysz2")
sprawdz("odłączenie myszy: nadzorca i wejscie to zauważają",
        czekaj(r"urządzenie wejścia odłączone \(event\d+\)", sekundy=30) and czekaj(r"K1-WEJSCIE: urządzenie odłączone", sekundy=30))
monitor("device_add usb-mouse,id=mysz3,bus=xhci.0")
sprawdz("ponowne podłączenie (ten sam numer urządzenia) → znów 3 urządzenia",
        czekaj(r"nowe urządzenie wejścia", od=n_nowe + 1, sekundy=30) and urzadzenia(3, od=1, sekundy=30))
time.sleep(1)
monitor(*["mouse_move 40 20"] * 2)
x3, y3 = x2 + 80, y1 + 40
sprawdz(f"mysz działa dalej po podłączaniu → {x3},{y3}", czekaj(rf"K1-EKRAN: kursor {x3},{y3} ", sekundy=20), str(kursor()))
time.sleep(1)
p = f"{out}/m5-usb.png"
subprocess.run([zrzut_sh, p, mon], check=True, stdout=subprocess.DEVNULL)
w, h, px = png(p)
sprawdz("zrzut: kursor w miejscu z logu", krem(px(x3 + 3, y3 + 9)), str(px(x3 + 3, y3 + 9)))

t = tekst()
sprawdz("brak 'Kernel panic' i błędów usług", "Kernel panic" not in t and "BŁĄD" not in t)
sprawdz("brak odrzuconych i zgubionych zdarzeń", re.findall(r"odrzucone (\d+)\)", t)[-1:] == ["0"] and "pominięto" not in t)
monitor("quit")
sys.exit(0 if all(wyniki) else 1)
PY
kill $QPID 2>/dev/null || true; wait $QPID 2>/dev/null || true
if [ $r -eq 0 ]; then info "M5a: wszystko zaliczone (zrzut: $TESTY/m5-usb.png)"; else echo "[k1] M5a: BŁĄD (log: $LOG)"; fi
exit $r

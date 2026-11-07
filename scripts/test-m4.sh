#!/bin/sh
# Test etapu M4 (powtarzalny): pulpit i aplikacje w działającym K1 (obraz produkcyjny) oraz start
# z obrazu ISO (UEFI i BIOS, grafika z firmware — simpledrm). Sterowanie przez monitor QEMU (mysz,
# klawiatura), dowody: log + zrzuty ekranu sprawdzane automatycznie (build/testy/m4-*.png).
# Wymaga: scripts/zbuduj.sh (z obrazem ISO).
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
ISO=$K1_WYNIK/k1-$K1_WERSJA.iso
mkdir -p "$TESTY"; rm -f "$TESTY"/m4-*
r=0

# uruchom NAZWA [opcje uruchom-qemu.sh] — QEMU w tle; log i gniazdo: $TESTY/m4-NAZWA.{log,sock}
uruchom() {
    n=$1; shift
    rm -f "$TESTY/m4-$n.log" "$TESTY/m4-$n.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/m4-$n.log" --monitor "$TESTY/m4-$n.sock" --czas "$CZAS" "$@" &
    QPID=$!
}
sprawdz() { # NAZWA — scenariusz w Pythonie (niżej) na działającej maszynie
    python3 -I "$K1_BUDOWA/m4.py" "$TESTY/m4-$1.log" "$TESTY/m4-$1.sock" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" \
        "$CZAS" "$1" || r=1
    kill $QPID 2>/dev/null || true; wait $QPID 2>/dev/null || true
}

cat > "$K1_BUDOWA/m4.py" <<'PY'
import re, socket, struct, subprocess, sys, time, zlib
log, mon, out, zrzut_sh, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5]), sys.argv[6]
start = time.time(); wyniki = []

def tekst():
    try: return open(log, encoding="utf-8", errors="replace").read()
    except FileNotFoundError: return ""

def czekaj(wzor, od=0, czas=None):
    koniec = time.time() + (czas or limit)
    while time.time() < koniec and time.time() - start < limit:
        m = list(re.finditer(wzor, tekst()))
        if len(m) > od: return m[-1]
        if "Kernel panic" in tekst(): break
        time.sleep(0.4)
    return None

def monitor(*cmds):
    s = socket.socket(socket.AF_UNIX); s.connect(mon); s.settimeout(1)
    for c in ("",) + cmds:
        s.sendall(c.encode() + b"\n"); time.sleep(0.15)
        try: s.recv(65536)
        except socket.timeout: pass
    s.close()

def png(path):
    raw = open(path, "rb").read(); pos = 8; idat = b""; w = h = 0
    while pos < len(raw):
        n, t = struct.unpack(">I4s", raw[pos:pos + 8]); d = raw[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b"IHDR": w, h = struct.unpack(">II", d[:8])
        elif t == b"IDAT": idat += d
    data = zlib.decompress(idat)
    return w, h, lambda x, y: tuple(data[y * (w * 3 + 1) + 1 + x * 3: y * (w * 3 + 1) + 4 + x * 3])

def zrzut(nazwa):
    p = f"{out}/m4-{nazwa}.png"
    subprocess.run([zrzut_sh, p, mon], check=True, stdout=subprocess.DEVNULL)
    return png(p)

def sprawdz(nazwa, warunek, opis=""):
    wyniki.append(bool(warunek))
    print(f"  [{'OK' if warunek else 'BŁĄD'}]    {nazwa}" + ("" if warunek else f" — {opis}"), flush=True)
    return warunek

jasny = lambda c: min(c) > 200
ciemny = lambda c: max(c) < 70
akcent = lambda c: c[0] > 170 and c[1] < 150 and c[2] < 120 and c[0] > c[1] > c[2]
kursor = [0, 0]

def kursor_teraz():
    m = list(re.finditer(r"K1-EKRAN: kursor (\d+),(\d+) ", tekst()))
    return (int(m[-1].group(1)), int(m[-1].group(2))) if m else None

def przesun(dx, dy):
    cmds = []
    while dx or dy:
        sx, sy = max(-100, min(100, dx)), max(-100, min(100, dy))
        cmds.append(f"mouse_move {sx} {sy}"); dx -= sx; dy -= sy
    if cmds: monitor(*cmds)

def idz(x, y):
    # The harness does not trust its own idea of the cursor: right after boot (fast with KVM) the
    # PS/2 mouse may still be resyncing and lose a move. ekran logs the real position (≤ 2×/s):
    # wait for it and correct the rest of the way, at most 4 times.
    for proba in range(4):
        przesun(x - kursor[0], y - kursor[1])
        kursor[0], kursor[1] = x, y
        koniec = time.time() + 2.5
        while time.time() < koniec and kursor_teraz() != (x, y):
            time.sleep(0.15)
        teraz = kursor_teraz()
        if teraz == (x, y) or teraz is None:
            return
        print(f"  [uwaga] kursor w {teraz[0]},{teraz[1]} zamiast {x},{y} — poprawiam ({proba + 1})", flush=True)
        kursor[0], kursor[1] = teraz

def klik(x, y):
    idz(x, y); time.sleep(0.4)
    monitor("mouse_button 1"); time.sleep(0.3); monitor("mouse_button 0"); time.sleep(0.3)

def pisz(*klawisze):
    monitor(*[f"sendkey {k}" for k in klawisze])

g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)")
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
t = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
if not sprawdz(f"{tryb}: pulpit gotowy (pasek, menu Start)", g and k0 and t): sys.exit(1)
W, H = int(t.group(1)), int(t.group(2))
sx, sy, ax, ay, krok = (int(g.group(i)) for i in range(1, 6))
kursor[:] = [int(k0.group(1)), int(k0.group(2))]
time.sleep(1.5)
w, h, px = zrzut(f"{tryb}-1-pulpit")
sprawdz(f"zrzut: rozmiar {w}x{h} = tryb {W}x{H}", (w, h) == (W, H))
sprawdz("zrzut: górny pasek ciemny, dolny pasek jasny (motyw jasny)", max(px(W // 2, 6)) < 120 and jasny(px(W // 2, ay - 26)),
        f"{px(W // 2, 6)} {px(W // 2, ay - 26)}")
sprawdz("zrzut: kolorowa tapeta (nie czarna, nie jednolita)", px(W // 4, H // 2) != px(W - 20, H // 3) and max(px(W // 3, H // 2)) > 80,
        f"{px(W // 4, H // 2)} {px(W - 20, H // 3)}")
if tryb != "pulpit":
    sprawdz("brak 'Kernel panic', błędów i zablokowanych wywołań", "Kernel panic" not in tekst() and "BŁĄD" not in tekst()
            and "zabronione wywołanie" not in tekst())
    sys.exit(0 if all(wyniki) else 1)

# menu Start → Notatki
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+)\)")
sprawdz("menu Start otwiera się po kliknięciu", m)
if not m: sys.exit(1)
rx, ry, rk, wx, wy = (int(m.group(i)) for i in range(1, 6))
w, h, px = zrzut("2-menu")
sprawdz("zrzut: menu Start widoczne (jasne tło nad paskiem)", jasny(px(rx, ry - 50)), str(px(rx, ry - 50)))
klik(rx, ry + rk)
o = czekaj(r"K1-EKRAN: okno (\d+) \(notatki\) (\d+)x(\d+) w (\d+),(\d+)")
sprawdz("Notatki: nadzorca uruchamia aplikację, ekran pokazuje okno", o and czekaj(r"uruchomiono notatki \(pid \d+, uid 201\)"))
if not o: sys.exit(1)
nid, nw, nh, nx, ny = (int(o.group(i)) for i in range(1, 6))
time.sleep(2)
# "Zażółć K1" (AltGr = polski układ programisty)
pisz("shift-z", "a", "alt_r-z", "alt_r-o", "alt_r-l", "alt_r-c", "spc", "shift-k", "1")
sprawdz("Notatki: polskie znaki wpisane i zapisane automatycznie (9 znaków)", czekaj(r"K1-NOTATKI: zapisano notatkę \(9 znaków\)"))
w, h, px = zrzut("3-notatki")
ciemne = sum(1 for x in range(nx + 24, nx + 140) for y in range(ny + 58, ny + 76) if max(px(x, y)) < 110)
sprawdz("zrzut: tekst w oknie Notatek", ciemne > 40, f"ciemnych pikseli: {ciemne}")
# przeciąganie okna za pasek tytułu
idz(nx + 200, ny + 20); time.sleep(0.3); monitor("mouse_button 1"); time.sleep(0.3)
idz(nx + 320, ny + 80); time.sleep(0.5); monitor("mouse_button 0")
sprawdz("okno przeciągnięte za pasek tytułu", czekaj(rf"K1-EKRAN: okno {nid} przesunięte do {nx + 120},{ny + 60}"))

# Zegar z dolnego paska
klik(ax, ay)
z = czekaj(r"K1-EKRAN: okno (\d+) \(zegar\) (\d+)x(\d+) w (\d+),(\d+)")
sprawdz("Zegar: uruchomiony z dolnego paska", z)
if z:
    zx, zy = int(z.group(4)), int(z.group(5)); time.sleep(2.5)
    w, h, px = zrzut("4-zegar")
    sprawdz("zrzut: tarcza zegara (środek w kolorze akcentu)", akcent(px(zx + 140, zy + 172)), str(px(zx + 140, zy + 172)))

# Ustawienia: ciemny motyw i inna tapeta
klik(ax + 2 * krok, ay)
u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)")
l = czekaj(r"przyciski od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\); tapety od (\d+),(\d+) co (\d+)")
sprawdz("Ustawienia: uruchomione z dolnego paska", u and l)
if u and l:
    uid_, ux, uy = int(u.group(1)), int(u.group(4)), int(u.group(5))
    bx, by, bw, bh, tx, ty, tk = (int(l.group(i)) for i in range(1, 8))
    time.sleep(1.5)
    klik(ux + bx + bw * 3 // 4, uy + by + bh // 2)
    sprawdz("ciemny motyw: ekran przyjmuje ustawienie tylko od Ustawień", czekaj(r"K1-EKRAN: ustawienie 1=1"))
    time.sleep(2.5)
    w, h, px = zrzut("5-ciemny")
    sprawdz("zrzut: okno Ustawień w ciemnym motywie", ciemny(px(ux + 600, uy + 400)), str(px(ux + 600, uy + 400)))
    klik(ux + tx + tk, uy + ty)
    sprawdz("tapeta „Zmierzch” ustawiona", czekaj(r"K1-EKRAN: tapeta 1 "))
    time.sleep(2.5)
    w, h, px = zrzut("6-tapeta")
    c = px(12, H - 12)
    sprawdz("zrzut: nowa tapeta (fiolet/niebieski w rogu)", c[2] > c[0], str(c))
    # Ustawienia → Ekran: inna rozdzielczość, pulpit układa się od nowa (karta bochs ma wiele trybów)
    sek = czekaj(r"sekcje od (\d+),(\d+) co (\d+)")
    klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + int(sek.group(3)))
    tr = czekaj(r"tryby ekranu: ([\dx ]+) \(przyciski od (\d+),(\d+), (\d+) w kolumnie, co (\d+) i (\d+)\)")
    sprawdz("Ustawienia → Ekran: lista rozdzielczości od ekranu", tr and len(tr.group(1).split()) >= 2,
            tr.group(1) if tr else "brak")
    if tr:
        lista = tr.group(1).split(); cel = "1024x768" if "1024x768" in lista and f"{W}x{H}" != "1024x768" else \
            next(t for t in lista if t != f"{W}x{H}")
        i = lista.index(cel); bx0, by0, nk, dx, dy = (int(tr.group(j)) for j in range(2, 7))
        klik(ux + bx0 + (i // nk) * dx, uy + by0 + (i % nk) * dy)
        cw, ch = (int(v) for v in cel.split("x"))
        nowy = czekaj(rf"K1-EKRAN: tryb {cw}x{ch} @ \d+ Hz \(zmiana")
        g2 = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", od=1)
        sprawdz(f"zmiana rozdzielczości {W}x{H} → {cel}: ekran przełącza tryb, pulpit układa się od nowa", nowy and g2)
        if nowy and g2:
            W, H = cw, ch
            sx, sy, ax, ay, krok = (int(g2.group(i)) for i in range(1, 6))
            time.sleep(2.5)
            w, h, px = zrzut("7-rozdzielczosc")
            sprawdz(f"zrzut: obraz {w}x{h} = {cel}, górny pasek na całą szerokość", (w, h) == (W, H)
                    and max(px(W - 8, 6)) < 120, f"{w}x{h} {px(W - 8, 6)}")
    klik(ax + 2 * krok, ay)
    sprawdz("kliknięcie aktywnej aplikacji na pasku minimalizuje okno", czekaj(rf"K1-EKRAN: okno {uid_} zminimalizowane"))

# Zasoby: wykresy z danymi od nadzorcy
klik(ax + 3 * krok, ay)
zs = czekaj(r"K1-EKRAN: okno (\d+) \(zasoby\) (\d+)x(\d+) w (\d+),(\d+)")
dane = czekaj(r"K1-ZASOBY: dane od nadzorcy: procesor (\d+)‰, pamięć (\d+) z (\d+) MiB, (\d+) CPU")
sprawdz("Zasoby: uruchomione z dolnego paska, dane od nadzorcy (procesor, pamięć)", zs and dane and int(dane.group(3)) > 100,
        dane.group(0) if dane else "brak danych")
if zs:
    zx, zy = int(zs.group(4)), int(zs.group(5)); time.sleep(8)
    w, h, px = zrzut("8-zasoby")
    # wykres pamięci: linia w kolorze akcentu (motyw ciemny); po 8 s ma ok. 7 próbek (≈ 50 px
    # szerokości przy prawej krawędzi) — liczymy każdą kolumnę, bo linia jest płaska i cienka
    linia = sum(1 for x in range(zx + 32, zx + 536) for y in range(zy + 200, zy + 276) if akcent(px(x, y)))
    sprawdz("zrzut: wykres pamięci w oknie Zasobów", linia >= 30, f"pikseli linii: {linia}")

tx_ = tekst()
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów",
        "Kernel panic" not in tx_ and "BŁĄD" not in tx_ and "zabronione wywołanie" not in tx_ and "odrzucono" not in tx_)
# Start → Wyłącz: kontrolowane wyłączenie
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+)\)", od=1)
if m: wx, wy = int(m.group(4)), int(m.group(5))
klik(wx, wy)
sprawdz("Start → Wyłącz: system się wyłącza", czekaj(r"K1: wyłączanie zasilania") and czekaj(r"reboot: Power down"))
sys.exit(0 if all(wyniki) else 1)
PY

echo "[k1] test M4: pulpit i aplikacje (obraz produkcyjny)"
uruchom pulpit
sprawdz pulpit
if [ -f "$ISO" ]; then
    echo "[k1] test M4: start z obrazu ISO — UEFI (OVMF), grafika z firmware (simpledrm)"
    uruchom iso-uefi --iso "$ISO" --uefi --vga virtio
    sprawdz iso-uefi
    grep -q "obraz rdzenia zamontowany z /dev/loop" "$TESTY/m4-iso-uefi.log" && grep -q "simpledrm" "$TESTY/m4-iso-uefi.log" \
        && echo "  [OK]    rdzeń wbudowany w ISO (loop), simpledrm" || { echo "  [BŁĄD]  ISO UEFI: brak loop/simpledrm"; r=1; }
    echo "[k1] test M4: start z obrazu ISO — BIOS (SeaBIOS, VESA), grafika z firmware (simpledrm)"
    uruchom iso-bios --iso "$ISO" --vga virtio
    sprawdz iso-bios
else
    echo "  [BŁĄD]  brak obrazu ISO $ISO (scripts/zbuduj-iso.sh)"; r=1
fi
if [ $r -eq 0 ]; then info "M4: wszystko zaliczone (zrzuty: $TESTY/m4-*.png)"; else echo "[k1] M4: BŁĄD (logi: $TESTY/m4-*.log)"; fi
exit $r

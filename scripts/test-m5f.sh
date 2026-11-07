#!/bin/sh
# Test etapu M5f (ADR 0018): instalator — K1 z pendrive'a na pusty dysk SATA i start z dysku.
# 1. pendrive (obraz USB) + pusty dysk 4 GiB: Instalator z menu Start → dysk → „USUŃ” → instalacja;
#    na dysku GPT z „K1-EFI” i „K1-DANE” (sfdisk), program zapisujący w klatce (UID 105);
# 2. start z samego dysku (UEFI, bez pendrive'a): pulpit, nowy magazyn na K1-DANE dysku;
# 3. znów pendrive + dysk: dane z dysku wewnętrznego (partycja pendrive'a pominięta), Instalator
#    rozpoznaje K1 na dysku → aktualizacja (tylko K1-EFI), dane nietknięte (magazyn otwiera się dalej);
# 4. start z dysku po aktualizacji: magazyn z kroku 2 otwarty (dane przetrwały).
# Zrzuty: build/testy/m5f-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-300}
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-usb.img
DYSK=$TESTY/m5f-dysk.img PEN=$TESTY/m5f-pendrive.img
mkdir -p "$TESTY"; rm -f "$TESTY"/m5f-*
[ -f "$USBIMG" ] || blad "brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"
cp "$USBIMG" "$PEN"
truncate -s 4G "$DYSK"
r=0

echo "[k1] test M5f: instalator — pendrive → dysk, start z dysku, aktualizacja z zachowaniem danych"

# $1 = scenario, $2 = log name; the rest: arguments of uruchom-qemu.sh
uruchom() {
    tryb=$1 nazwa=$2; shift 2
    LOG=$TESTY/m5f-$nazwa.log MON=$TESTY/m5f-$nazwa.sock
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" "$@" &
    QPID=$!
    python3 -I - "$tryb" "$LOG" "$MON" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" "$CZAS" <<'PY' || r=1
import re, socket, subprocess, sys, time
tryb, log, mon, out, zrzut_sh, limit = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], float(sys.argv[6])
start = time.time(); wyniki = []

def tekst():
    try: return open(log, encoding="utf-8", errors="replace").read()
    except OSError: return ""
def czekaj(wzor, od=0, czas=120):
    koniec = time.time() + czas
    while time.time() < koniec and time.time() - start < limit:
        m = list(re.finditer(wzor, tekst()))
        if len(m) > od: return m[-1]
        if "Kernel panic" in tekst(): break
        time.sleep(0.4)
    return None
def monitor(*cmds):
    for _ in range(50):
        try:
            s = socket.socket(socket.AF_UNIX); s.connect(mon); break
        except OSError: time.sleep(0.2)
    else: return
    s.settimeout(2)
    try: s.recv(4096)
    except OSError: pass
    for c in cmds:
        s.sendall((c + "\n").encode()); time.sleep(0.05)
    time.sleep(0.2); s.close()
def sprawdz(nazwa, warunek, opis=""):
    wyniki.append(bool(warunek))
    print(f"  [{'OK' if warunek else 'BŁĄD'}]    {nazwa}" + ("" if warunek else f" — {opis}"), flush=True)
    return warunek
def zrzut(nazwa):
    subprocess.run([zrzut_sh, f"{out}/m5f-{nazwa}.png", mon], stdout=subprocess.DEVNULL)
kursor = [0, 0]
def kursor_teraz():
    m = list(re.finditer(r"K1-EKRAN: kursor (\d+),(\d+) ", tekst()))
    return (int(m[-1].group(1)), int(m[-1].group(2))) if m else None
def idz(x, y):
    for _ in range(5):
        k = kursor_teraz() or tuple(kursor)
        dx, dy = x - k[0], y - k[1]; cmds = []
        while dx or dy:
            sx, sy = max(-100, min(100, dx)), max(-100, min(100, dy)); cmds.append(f"mouse_move {sx} {sy}"); dx -= sx; dy -= sy
        if cmds: monitor(*cmds)
        kursor[:] = [x, y]
        koniec = time.time() + 2.5
        while time.time() < koniec and kursor_teraz() != (x, y): time.sleep(0.15)
        if kursor_teraz() == (x, y): return
def klik(x, y):
    idz(x, y); time.sleep(0.4)
    monitor("mouse_button 1"); time.sleep(0.3); monitor("mouse_button 0"); time.sleep(0.5)
def koniec():
    t = tekst()
    sprawdz("brak 'Kernel panic', błędów usług i zabronionych wywołań",
            "Kernel panic" not in t and "BŁĄD" not in t and "zabronione wywołanie" not in t)
    monitor("quit")
    sys.exit(0 if all(wyniki) else 1)

g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=200)
sprawdz("pulpit gotowy", g)
if not g: koniec()

if tryb == "z-dysku":
    sprawdz("start z dysku SATA przez UEFI (bez pendrive'a): K1-DANE dysku otwarta",
            czekaj(r"K1-NADZORCA: partycja danych K1-DANE otwarta dla skarbca", czas=10))
    sprawdz("skarbiec: nowy zaszyfrowany magazyn na K1-DANE dysku",
            czekaj(r"K1-SKARBIEC: utworzono nowy magazyn na pustej partycji K1-DANE", czas=10))
    time.sleep(2); zrzut("2-z-dysku")
    koniec()
if tryb == "po-aktualizacji":
    sprawdz("start z dysku po aktualizacji (pendrive też podłączony): magazyn dysku otwarty — dane przetrwały",
            czekaj(r"K1-SKARBIEC: magazyn otwarty", czas=10) and "utworzono nowy magazyn" not in tekst()
            and czekaj(r"K1-NADZORCA: K1-DANE na dysku wewnętrznym \(sda2\) — partycja na pendrivie pominięta|"
                       r"K1-NADZORCA: partycja danych K1-DANE otwarta dla skarbca \(sda2\)", czas=5))
    koniec()

# Instalator from the Start menu (5th row)
time.sleep(2)
klik(int(g.group(1)), int(g.group(2)))
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+)", czas=20)
sprawdz("menu Start otwarte", m)
if not m: koniec()
klik(int(m.group(1)), int(m.group(2)) + 4 * int(m.group(3)))
o = czekaj(r"K1-EKRAN: okno (\d+) \(instalacja\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
d = czekaj(r"K1-INSTALACJA: dyski: (\d+) \(system do skopiowania: (\w+)\); dysk 1 (\d+),(\d+) co (\d+); przycisk (\d+),(\d+)", czas=30)
sprawdz("Instalator: okno z listą dysków (1 dysk, system do skopiowania z pendrive'a)",
        o and d and d.group(1) == "1" and d.group(2) == "jest", d.group(0) if d else "brak listy")
if not (o and d): koniec()
ox, oy = int(o.group(4)), int(o.group(5))
px, py = ox + int(d.group(6)), oy + int(d.group(7))
time.sleep(1)

if tryb == "instaluj":
    sprawdz("dysk pusty: bez Kons K1", czekaj(r"K1-INSTALACJA: dysk 1: \d+ MiB, rodzaj 1, Kons K1: nie", czas=5))
    zrzut("1-lista")
    klik(ox + int(d.group(3)), oy + int(d.group(4)))  # the disk
    klik(px, py)                                      # Zainstaluj
    time.sleep(1)
    klik(px, py)                                      # without „USUŃ”: nothing happens
    time.sleep(1)
    sprawdz("bez wpisania „USUŃ” instalacja się nie zaczyna", "potwierdzono" not in tekst())
    monitor("sendkey u", "sendkey s", "sendkey u", "sendkey alt_r-n")  # usuń
    time.sleep(1); zrzut("1-potwierdzenie")
    klik(px, py)
    sprawdz("potwierdzone po wpisaniu „usuń”", czekaj(r"K1-INSTALACJA: potwierdzono: instalacja na dysku 1", czas=10))
    sprawdz("nadzorca: program zapisujący w klatce (UID 105), źródło = K1-EFI pendrive'a",
            czekaj(r"K1-NADZORCA: instalator: instalacja na dysku — sda \(z sd\w+\), pid \d+, uid 105", czas=10))
    sprawdz("instalator: kopia, sprawdzenie, tablica partycji", czekaj(r"K1-INSTALATOR: gotowe: K1 zainstalowany", czas=200))
    sprawdz("okno: „Gotowe”", czekaj(r"K1-INSTALACJA: zainstalowano", czas=20))
    time.sleep(1); zrzut("1-gotowe")
    klik(px, py)  # Wyłącz komputer
    sprawdz("„Wyłącz komputer” wyłącza system", czekaj(r"K1: wyłączanie zasilania|reboot: Power down", czas=60))
    t = tekst()
    sprawdz("brak 'Kernel panic', błędów usług i zabronionych wywołań",
            "Kernel panic" not in t and "BŁĄD" not in t and "zabronione wywołanie" not in t)
    sys.exit(0 if all(wyniki) else 1)

if tryb == "aktualizuj":
    sprawdz("pendrive + dysk z K1: skarbiec dostaje K1-DANE dysku wewnętrznego (sda2), nie pendrive'a",
            czekaj(r"K1-NADZORCA: partycja danych K1-DANE otwarta dla skarbca \(sda2\)", czas=5)
            and czekaj(r"K1-SKARBIEC: magazyn otwarty", czas=5))
    sprawdz("Instalator: na dysku jest Kons K1 (dane otwarte)",
            czekaj(r"K1-INSTALACJA: dysk 1: \d+ MiB, rodzaj 1, Kons K1: tak \(dane otwarte\)", czas=5))
    klik(px, py)  # Zaktualizuj system
    time.sleep(1); zrzut("3-aktualizacja")
    klik(px, py)  # Zaktualizuj
    sprawdz("aktualizacja: tylko partycja K1-EFI (sda1)",
            czekaj(r"K1-NADZORCA: instalator: aktualizacja — sda1", czas=10))
    sprawdz("instalator: system zaktualizowany, dane nietknięte", czekaj(r"K1-INSTALATOR: gotowe: system zaktualizowany", czas=200)
            and czekaj(r"K1-INSTALACJA: zaktualizowano", czas=20))
    koniec()
PY
    python3 -I -c 'import socket,sys; s=socket.socket(socket.AF_UNIX); s.settimeout(5); s.connect(sys.argv[1]); s.sendall(b"quit\n"); s.recv(64)' "$MON" 2>/dev/null \
        || kill $QPID 2>/dev/null || true
    wait $QPID 2>/dev/null || true
}

SATA="-drive if=none,id=dysk,file=$DYSK,format=raw -device ide-hd,bus=ide.0,drive=dysk"
uruchom instaluj 1-instalacja --pendrive "$PEN" --qemu "$SATA"
if sfdisk -d "$DYSK" 2>/dev/null | grep -q 'name="K1-EFI"' && sfdisk -d "$DYSK" | grep -q 'name="K1-DANE"' \
    && sfdisk --verify "$DYSK" >/dev/null 2>&1; then
    echo "  [OK]    dysk: GPT z K1-EFI i K1-DANE (sfdisk --verify bez błędów)"
else
    echo "  [BŁĄD]  dysk: brak poprawnej tablicy GPT z K1-EFI i K1-DANE"; r=1
fi
uruchom z-dysku 2-z-dysku --dysk-uefi "$DYSK"
PEN_USB="-device qemu-xhci,id=xhci -drive if=none,id=pendrive,file=$PEN,format=raw -device usb-storage,bus=xhci.0,drive=pendrive"
uruchom aktualizuj 3-aktualizacja --pendrive "$PEN" --qemu "$SATA"
uruchom po-aktualizacji 4-po-aktualizacji --dysk-uefi "$DYSK" --qemu "$PEN_USB"
rm -f "$DYSK" "$PEN"
if [ $r -eq 0 ]; then info "M5f: wszystko zaliczone (zrzuty: $TESTY/m5f-*.png)"; else echo "[k1] M5f: BŁĄD (logi: $TESTY/m5f-*.log)"; fi
exit $r

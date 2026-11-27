#!/bin/sh
# Test 0.13.0 krok 4 (ADR 0059): obraz wydania — nic na porcie szeregowym, błąd startu na ekranie, raport na pendrive.
#   1. „awaria”: jądro testów z linią poleceń wydania (jadro/cmdline-wydanie: console=ttynull) i złym skrótem rdzenia —
#      ekran awarii: panika jądra rysuje opis K1 (bez polskich liter) w kolorach K1 (zrzut); NEGATYWNE: na porcie
#      szeregowym ani jednego wiersza K1 i jądra
#   2. „pulpit”: obraz USB wydania (k1.efi, nie k1-test.efi) jako dysk, start przez UEFI → pulpit (zrzut: górny i dolny
#      pasek); NEGATYWNE: na porcie szeregowym nic od jądra i K1
#   3. „ratunek”: ten sam dysk, grubenv k1_licznik=2 — GRUB uruchamia tryb ratunkowy; klawisze: ↓, Enter („Zapisz raport
#      na pendrive”) → K1RAPORT.TXT na pendrivie: log tego startu jest w pamięci, choć port szeregowy milczy (D4: A)
#   4. „ustawienia”: obraz testowy z opisem ekranu — Ustawienia → Informacje → „Zapisz raport na pendrive”: NEGATYWNE bez
#      pendrive'a („Nie widzę pendrive'a”), potem z pendrive'em → K1RAPORT.TXT („Tryb: zwykły start”, log)
# Dowody: build/testy/wydanie-*.log, zrzuty build/testy/wydanie-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-300}
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-usb.img # the release's: k1.efi without the serial port
DYSK=$TESTY/wydanie-dysk.img STICK=$TESTY/wydanie-pendrive.img
export MTOOLS_SKIP_CHECK=1
[ -f "$USBIMG" ] || blad "brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"
mkdir -p "$TESTY"; rm -f "$TESTY"/wydanie-*
r=0
nowy_pendrive() {
    rm -f "$STICK"; truncate -s 64M "$STICK"
    "$(command -v mkfs.fat || echo /usr/sbin/mkfs.fat)" -F 32 -s 1 -n PENDRIVE "$STICK" >/dev/null
}

# uruchom NAZWA [opcje uruchom-qemu.sh] — QEMU in the background, the scenario of wydanie.py, QEMU closed
uruchom() {
    n=$1; shift
    rm -f "$TESTY/wydanie-$n.log" "$TESTY/wydanie-$n.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/wydanie-$n.log" --monitor "$TESTY/wydanie-$n.sock" --czas "$CZAS" "$@" &
    QPID=$!
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/wydanie.py" "$TESTY/wydanie-$n.log" "$TESTY/wydanie-$n.sock" \
        "$TESTY" "$CZAS" "$n" "$K1_WERSJA" "$STICK" || r=1
    zamknij_qemu "$TESTY/wydanie-$n.sock" $QPID
}

cat > "$K1_BUDOWA/wydanie.py" <<'PY'
import os, re, subprocess, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb, wersja, stick = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), *sys.argv[5:8]
q = Maszyna(log, mon, out, limit, "wydanie")
sprawdz = q.sprawdz
blisko = lambda c, w, t=24: all(abs(a - b) <= t for a, b in zip(c, w))
TLO = (0x26, 0x25, 0x23)  # DRM_PANIC_BACKGROUND_COLOR (jadro/k1.config): the dark theme of K1


def surowy():
    try:
        return open(log, "rb").read()
    except OSError:
        return b""


def cicho(nazwa):
    """NEGATIVE: the serial port got nothing from the kernel or K1 (the firmware and GRUB may write there)."""
    t = surowy()
    sprawdz(f"{nazwa}: NEGATYWNE — na porcie szeregowym nic od jądra i K1 (console=ttynull)",
            not re.search(rb"K1-[A-Z]|K1: |Linux version|Kernel panic|\[ *\d+\.\d+\]", t), t[-300:])


def raport(czas):
    """K1RAPORT.TXT from the stick (mtools on the image QEMU writes), or None after `czas` s."""
    koniec = time.time() + czas
    while time.time() < koniec:
        p = subprocess.run(["mtype", "-i", stick, "::K1RAPORT.TXT"], capture_output=True)
        if p.returncode == 0 and b"Ostatnie wpisy logu" in p.stdout:
            return p.stdout.decode("utf-8", errors="replace")
        time.sleep(2)
    return None


if tryb == "awaria":
    def ekran_awarii(w, h, px):
        tlo = sum(blisko(px(x, y), TLO, 6) for x in range(8, w, w // 16) for y in range(8, h, h // 12))
        tekst = sum(max(px(x, y)) > 200 for x in range(0, w // 2, 2) for y in range(h // 2, h, 2))
        return tlo >= 150 and tekst >= 200
    w, h, px = q.zrzut("awaria", gdy=ekran_awarii, czas=limit * 0.8)
    sprawdz("awaria: ekran awarii — panika jądra rysuje opis K1 na tle K1 (zrzut wydanie-awaria.png)", ekran_awarii(w, h, px),
            f"{w}x{h}, piksel {px(8, 8)}")
    cicho("awaria")
    q.koniec()

if tryb in ("pulpit", "ratunek") and not os.path.exists(stick):
    sys.exit(1)
if tryb == "pulpit":
    # the desktop of the release: the top bar (dark, the whole width) and the bottom one (the Start button's row)
    def pulpit(w, h, px):
        gora = all(max(px(x, 6)) < 120 for x in range(8, w - 8, 64))
        dol = sum(1 for x in range(w // 4, 3 * w // 4, 8) if max(px(x, h - 30)) > 150)
        return w >= 800 and gora and dol >= 8
    w, h, px = q.zrzut("pulpit", gdy=pulpit, czas=limit * 0.8)
    sprawdz("pulpit: obraz wydania (k1.efi) startuje do pulpitu — zrzut wydanie-pulpit.png", pulpit(w, h, px), f"{w}x{h}")
    cicho("pulpit")
    q.koniec()

if tryb == "ratunek":
    akcent = lambda c: c[0] > 170 and c[1] < 150 and c[2] < 120 and c[0] > c[1] > c[2]

    def ekran_ratunkowy(w, h, px):  # dark, the main button ("Uruchom K1 normalnie") a wide bar of the accent colour —
        ciemne = sum(max(px(x, y)) < 70 for x in range(8, w, w // 16) for y in range(8, h, h // 12))  # not the boot mark
        szeroki = any(sum(akcent(px(x, y)) for x in range(w // 4, 3 * w // 4, 2)) >= 100 for y in range(h // 4, h - 40, 4))
        return ciemne >= 120 and szeroki
    w, h, px = q.zrzut("ratunek", gdy=ekran_ratunkowy, czas=limit * 0.7)
    sprawdz("ratunek: k1_licznik=2 → GRUB uruchamia tryb ratunkowy (zrzut wydanie-ratunek.png)", ekran_ratunkowy(w, h, px))
    # The keys only when the rescue screen takes them (CI with KVM: drawn before its keys worked, and keys that came late
    # moved the ring twice — Enter then pressed "Wyłącz komputer"). Which button has the keyboard's terracotta ring is read
    # from a settled picture (the same three times): 0 the main one, 1 "Zapisz raport na pendrive" (slot B is empty),
    # 2 "Wyłącz komputer"; ↓/↑ until it is 1 and stays there, then Enter. Enter on the main button would restart K1.
    def glowny(w, h, px):  # the rows of the main button: the wide terracotta bar
        r = [y for y in range(h // 4, h - 40, 2) if sum(akcent(px(x, y)) for x in range(w // 4, 3 * w // 4, 2)) >= 100]
        return (min(r), max(r)) if r else None

    def obwodka(px, w, od, do):
        return sum(akcent(px(x, y)) for x in range(w // 4, 3 * w // 4, 2) for y in range(od, do, 2)) >= 60

    g = glowny(w, h, px)
    krok = (g[1] - g[0]) + 12 if g else 56  # a button and the gap under it

    def wybrany():
        w_, h_, p_ = q.zrzut("ratunek-wybrany")  # settled: the same picture three times
        return 1 if obwodka(p_, w_, g[1] + 8, g[1] + krok) else 2 if obwodka(p_, w_, g[1] + krok, g[1] + 2 * krok) else 0
    przeszla = False
    for _ in range(12):
        if not g:
            break
        n = wybrany()
        if n == 1 and wybrany() == 1:
            przeszla = True
            break
        q.monitor("sendkey up" if n == 2 else "sendkey down")
    sprawdz("ratunek: obwódka na „Zapisz raport na pendrive” (ekran ratunkowy bierze klawisze)", przeszla)
    if przeszla:
        q.monitor("sendkey ret")
    t = raport(120)
    sprawdz("ratunek: ↓, Enter — K1RAPORT.TXT na pendrivie", t is not None)
    t = t or ""
    sprawdz("ratunek: w raporcie tryb ratunkowy, ta wersja i log tego startu z pamięci (D4: A — port szeregowy milczy)",
            f"Wersja: {wersja}" in t and "Tryb: ratunkowy" in t and "K1-NADZORCA: TRYB RATUNKOWY" in t and
            "K1-EKRAN: pierwsza klatka" in t, t[:400])  # the memory keeps nadzorca's and the services' lines (logi.c)
    cicho("ratunek")
    q.koniec()

if tryb == "ustawienia":
    if not sprawdz("ustawienia: pulpit gotowy (obraz testowy z opisem ekranu)",
                   q.czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.5) and q.czekaj_na_element("Pasek", "Ustawienia", czas=120)):
        q.koniec()
    q.klik_w("Pasek", "Ustawienia", "ikona")
    q.klik_w("Ustawienia", "Informacje", "zakladka", czas=120)
    n = q.ile(r"K1-USTAWIENIA: informacje: raport")
    q.klik_w("Ustawienia", "Zapisz raport na pendrive", "przycisk")
    sprawdz("ustawienia: NEGATYWNE — bez pendrive'a „Nie widzę pendrive'a” (nadzorca odmawia, nic nie zapisuje)",
            q.czekaj(r"K1-USTAWIENIA: informacje: raport niezapisany \(kod -\d+\)", od=n, czas=60) and
            q.czekaj(r"K1-NADZORCA: raport \(\d+ B, Ustawienia\) — brak pendrive'a", czas=10))
    q.zrzut("ustawienia-bez-pendrive")
    q.monitor(f"drive_add 0 if=none,id=pen,file={stick},format=raw", "device_add usb-storage,id=pen,drive=pen,bus=xhci.0")
    zapisany = None
    for _ in range(10):  # usb-storage shows the disk about a second later: until then "no stick" again
        n = q.ile(r"K1-USTAWIENIA: informacje: raport")
        q.klik_w("Ustawienia", "Zapisz raport na pendrive", "przycisk")
        m = q.czekaj(r"K1-USTAWIENIA: informacje: raport (zapisany|niezapisany)", od=n, czas=90)
        if not m or m.group(1) == "zapisany":
            zapisany = m
            break
        q.cisza(r"K1-USTAWIENIA: informacje: raport", 3)
    sprawdz("ustawienia: „Zapisz raport na pendrive” — raport zapisany (program zapisujący w klatce)",
            zapisany and q.ile(r"K1-INSTALATOR: raport: zapisany") == 1)
    q.zrzut("ustawienia-zapisany")
    t = raport(30) or ""
    sprawdz("ustawienia: K1RAPORT.TXT — zwykły start, ta wersja, log tego startu",
            "Tryb: zwykły start" in t and f"Wersja: {wersja}" in t and "K1-USTAWIENIA: informacje: zapisz raport" in t, t[:400])
    q.czysty_log()
    q.koniec()
sys.exit(1)
PY

echo "[k1] test wydania: ekran awarii (linia poleceń wydania, zły skrót rdzenia)"
ZERA=0000000000000000000000000000000000000000000000000000000000000000
LINIA="$(sed 's|k1\.rdzen=wbudowany|k1.rdzen=/dev/vda|' "$K1_REPO/jadro/cmdline-wydanie") k1.rdzen_skrot=$(echo $ZERA | tr 0 a)"
# through UEFI with the firmware's picture (simpledrm): the screen the kernel's panic draws on before ekran sets a mode
uruchom awaria --uefi --vga virtio --obraz "$K1_WYNIK/rdzen.squashfs" --cmdline "$LINIA"

echo "[k1] test wydania: obraz USB wydania — pulpit, tryb ratunkowy z raportem"
cp --sparse=always "$USBIMG" "$DYSK"
nowy_pendrive
USB="-device qemu-xhci,id=xhci -drive if=none,id=pen,file=$STICK,format=raw -device usb-storage,bus=xhci.0,drive=pen"
uruchom pulpit --dysk-uefi "$DYSK" --qemu "$USB"
# two failed starts, as GRUB counts them (k1_licznik=2 in grubenv — the layout block at 1 KiB of K1-EFI says where)
python3 -I - "$DYSK" <<'PY'
import sys
f = open(sys.argv[1], "r+b")
f.seek(1048576 + 1024 + 16)
env = 1048576 + int.from_bytes(f.read(8), "little")
f.seek(env)
stary = f.read(1024)
zmienne = [l for l in stary.split(b"\n") if l.startswith(b"k1_") and not l.startswith((b"k1_licznik=", b"k1_tryb="))]
blok = b"# GRUB Environment Block\n# WARNING: Do not edit this file by tools other than grub-editenv!!!\n" + \
       b"".join(l + b"\n" for l in zmienne) + b"k1_licznik=2\nk1_tryb=\n"
f.seek(env); f.write(blok + b"#" * (1024 - len(blok))); f.close()
PY
uruchom ratunek --dysk-uefi "$DYSK" --qemu "$USB"

echo "[k1] test wydania: Ustawienia → Informacje → „Zapisz raport na pendrive” (obraz testowy z opisem ekranu)"
nowy_pendrive
ZNAK=$TESTY/wydanie-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
uruchom ustawienia --obraz "$K1_WYNIK/rdzen-test.squashfs" --qemu "-device qemu-xhci,id=xhci
 -drive if=none,id=znak,file=$ZNAK,format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-opis,addr=0x1e"
rm -f "$DYSK" "$STICK" "$ZNAK"
if [ $r -eq 0 ]; then info "wydanie: wszystko zaliczone (zrzuty: $TESTY/wydanie-*.png)"; else echo "[k1] wydanie: BŁĄD (logi: $TESTY/wydanie-*.log)"; fi
exit $r

#!/bin/sh
# Test etapu M5f (ADR 0018): instalator — K1 z pendrive'a na pusty dysk SATA i start z dysku.
# 1. pendrive (obraz USB) + pusty dysk 4 GiB: Instalator z menu Start → dysk → „USUŃ” → instalacja;
#    na dysku GPT z „K1-EFI” i „K1-DANE” (sfdisk), program zapisujący w klatce (UID 105);
# 2. start z samego dysku (UEFI, bez pendrive'a): pulpit, nowy magazyn na K1-DANE dysku;
# 3. znów pendrive + dysk: dane z dysku wewnętrznego (partycja pendrive'a pominięta), Instalator
#    rozpoznaje K1 na dysku → aktualizacja (tylko K1-EFI), dane nietknięte (magazyn otwiera się dalej);
# 4. start z dysku po aktualizacji: nowe miejsce (B) wystartowało na próbę i K1 je potwierdził (0.7.0, A/B),
#    magazyn z kroku 2 otwarty (dane przetrwały);
# 5. wycofanie (A/B): „nowe” miejsce A jest uszkodzone — GRUB wraca do B, K1 zgłasza nieudaną aktualizację.
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
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$tryb" "$LOG" "$MON" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" "$CZAS" <<'PY' || r=1
import os, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
tryb = sys.argv[1]
q = Maszyna(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], "m5f")
czekaj, sprawdz, tekst, monitor, klik = q.czekaj, q.sprawdz, q.tekst, q.monitor, q.klik
def zrzut(nazwa): q.zrzut(nazwa)
def koniec():
    q.czysty_log()
    q.koniec()

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
    sprawdz("A/B: GRUB uruchomił nowe miejsce B na próbę, K1 je potwierdził po udanym starcie",
            czekaj(r"K1-NADZORCA: A/B: nowa wersja [0-9.]+ wystartowała — potwierdzona \(sda1, miejsce b\)", czas=60))
    koniec()
if tryb == "wycofanie":
    sprawdz("A/B: uszkodzone nowe miejsce A — GRUB wrócił do B, K1 zgłasza nieudaną aktualizację",
            czekaj(r"K1-NADZORCA: A/B: aktualizacja \(miejsce a\) nie wystartowała — działa poprzednia wersja \(sda1, miejsce b\)",
                   czas=60))
    koniec()

# Instalator from the Start menu (5th row)
time.sleep(2)
klik(int(g.group(1)), int(g.group(2)))
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+)", czas=20)
sprawdz("menu Start otwarte", m)
if not m: koniec()
klik(int(m.group(1)), int(m.group(2)) + 4 * int(m.group(3)))
o = czekaj(r"K1-EKRAN: okno (\d+) \(instalacja\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
d = czekaj(r"K1-INSTALACJA: dyski: (\d+) \(system do skopiowania: ([\w' ]+)\); dysk 1 (\d+),(\d+) co (\d+); przycisk (\d+),(\d+)", czas=30)
sprawdz("Instalator: okno z listą dysków (1 dysk" + (")" if tryb == "z-iso" else ", system do skopiowania z pendrive'a)"),
        o and d and d.group(1) == "1" and (tryb == "z-iso" or d.group(2) == "jest"), d.group(0) if d else "brak listy")
if not (o and d): koniec()
ox, oy = int(o.group(4)), int(o.group(5))
px, py = ox + int(d.group(6)), oy + int(d.group(7))
time.sleep(1)

if tryb == "z-iso":
    sprawdz("start z obrazu .iso: Instalator mówi, że nie ma skąd kopiować (0.6.1)",
            d.group(2) == "brak", d.group(0))
    time.sleep(1); zrzut("0-z-iso")
    klik(px, py)  # where the button would be: nothing may start
    time.sleep(2)
    sprawdz("bez kopii systemu nie ma przycisku „Zainstaluj” (nic się nie zaczyna)",
            "potwierdzono" not in tekst() and "instalator: odmowa" not in tekst())
    koniec()

if tryb == "uszkodzony":  # M6a: one byte of the system on the stick changed
    klik(px, py); time.sleep(1)
    monitor("sendkey u", "sendkey s", "sendkey u", "sendkey n"); time.sleep(1)
    klik(px, py)
    sprawdz("uszkodzony system na pendrivie: instalator odmawia przed zapisem (skrót niezgodny z manifestem)",
            czekaj(r"K1-INSTALATOR: źródło [0-9.]+: skrót nie zgadza się z manifestem", czas=120)
            and czekaj(r"K1-INSTALACJA: kod -8", czas=20))
    zrzut("0-uszkodzony")
    koniec()

if tryb == "starsza":  # M6a: the disk says 99.0.0 — the stick is older
    sprawdz("na dysku nowsza wersja (99.0.0) niż na pendrivie",
            czekaj(r"K1-INSTALACJA: dysk 1: \d+ MiB, rodzaj 1, Kons K1: tak \(dane otwarte\) \(wersja 6488064\)", czas=5))
    klik(px, py); time.sleep(1); zrzut("5-starsza")
    klik(px, py); time.sleep(2)
    sprawdz("powrót do starszej wersji zablokowany (przycisk nieaktywny, nic się nie zaczyna)", "potwierdzono" not in tekst())
    koniec()

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
    q.czysty_log()
    q.koniec(wylacz=False)

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
    sprawdz("instalator (A/B): nowy system w wolnym miejscu B, działający i dane nietknięte",
            czekaj(r"K1-INSTALATOR: aktualizacja A/B: system [0-9.]+ \(\d+ MiB\) do miejsca b", czas=60)
            and czekaj(r"K1-INSTALATOR: gotowe: nowy system w miejscu b", czas=200)
            and czekaj(r"K1-INSTALACJA: zaktualizowano \(nowe miejsce A/B\)", czas=20))
    time.sleep(1); zrzut("3-gotowe")
    koniec()
PY
    python3 -I "$K1_REPO/testy/qemu/k1qemu.py" zamknij "$MON" \
        || kill $QPID 2>/dev/null || true
    wait $QPID 2>/dev/null || true
}

SATA="-drive if=none,id=dysk,file=$DYSK,format=raw -device ide-hd,bus=ide.0,drive=dysk"
uruchom z-iso 0-z-iso --iso "$K1_WYNIK/k1-$K1_WERSJA.iso" --uefi --vga virtio --qemu "$SATA"
# M6a: a stick whose system was changed (one byte of slot A that the checksum covers, in the free space of its
# inner FAT after k1.efi — the stick itself still starts) — refused before any write.
# The layout block (k1_uklad.h) at 1 KiB of K1-EFI (which starts at 1 MiB) says where the slots are.
uklad() { # $1 = image, $2 = field offset in the block (32: slot A, 40: slot B, 16: grubenv) → bytes from the image start
    python3 -I -c 'import sys; f=open(sys.argv[1],"rb"); f.seek(1048576+1024+int(sys.argv[2])); print(1048576+int.from_bytes(f.read(8),"little"))' "$1" "$2"
}
cp "$PEN" "$TESTY/m5f-uszkodzony.img"
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); a=int(sys.argv[2]); f.seek(a+127*1048576+16); n=int.from_bytes(f.read(8),"little"); f.seek(a+n-100); b=f.read(1); f.seek(-1,1); f.write(bytes([b[0]^0xff])); f.close()' \
    "$TESTY/m5f-uszkodzony.img" "$(uklad "$TESTY/m5f-uszkodzony.img" 32)"
uruchom uszkodzony 0-uszkodzony --pendrive "$TESTY/m5f-uszkodzony.img" --qemu "$SATA"
if sfdisk -d "$DYSK" >/dev/null 2>&1; then echo "  [BŁĄD]  po odmowie na dysku jest tablica partycji"; r=1
else echo "  [OK]    po odmowie dysk nietknięty (bez tablicy partycji)"; fi
rm -f "$TESTY/m5f-uszkodzony.img"
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
env_dysku() { python3 -I -c 'import sys; f=open(sys.argv[1],"rb"); f.seek(int(sys.argv[2])); print(" ".join(l.decode() for l in f.read(1024).split(b"\n") if l.startswith(b"k1_")))' "$DYSK" "$(uklad "$DYSK" 16)"; }
if env_dysku | grep -q 'k1_slot=b k1_start=b k1_nowy= k1_proba=$'; then echo "  [OK]    grubenv dysku: potwierdzone miejsce B, nic do próby"
else echo "  [BŁĄD]  grubenv dysku po potwierdzeniu: $(env_dysku)"; r=1; fi
# A/B rollback: "a new system in A" that cannot even be opened (its inner FAT zeroed), tried once
python3 -I - "$DYSK" "$(uklad "$DYSK" 16)" "$(uklad "$DYSK" 32)" <<'PY'
import sys
f = open(sys.argv[1], "r+b"); env, a = int(sys.argv[2]), int(sys.argv[3])
f.seek(a); f.write(bytes(65536))
blok = b"# GRUB Environment Block\n# WARNING: Do not edit this file by tools other than grub-editenv!!!\n" \
       b"k1_slot=b\nk1_start=b\nk1_nowy=a\nk1_proba=\n"
f.seek(env); f.write(blok + b"#" * (1024 - len(blok))); f.close()
PY
uruchom wycofanie 5-wycofanie --dysk-uefi "$DYSK"
if env_dysku | grep -q 'k1_slot=b k1_start=b k1_nowy= k1_proba=$'; then echo "  [OK]    grubenv dysku po wycofaniu: działa B, próba wyczyszczona"
else echo "  [BŁĄD]  grubenv dysku po wycofaniu: $(env_dysku)"; r=1; fi
# M6a: pretend the disk runs 99.0.0 (the manifest of its confirmed slot B, 127 MiB into the slot)
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); o=int(sys.argv[2])+127*1048576; f.seek(o+8); f.write((99<<16).to_bytes(4,"little")); f.seek(o+24); f.write(b"99.0.0".ljust(24,b"\0")); f.close()' "$DYSK" "$(uklad "$DYSK" 40)"
uruchom starsza 6-starsza --pendrive "$PEN" --qemu "$SATA"
rm -f "$DYSK" "$PEN"
if [ $r -eq 0 ]; then info "M5f: wszystko zaliczone (zrzuty: $TESTY/m5f-*.png)"; else echo "[k1] M5f: BŁĄD (logi: $TESTY/m5f-*.log)"; fi
exit $r

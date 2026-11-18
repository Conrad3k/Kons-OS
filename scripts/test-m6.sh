#!/bin/sh
# Test etapu M6 (0.7.0, ADR 0022): Secure Boot z kluczem K1. Oprogramowanie UEFI z obsługą Secure Boot
# (OVMF secboot) i trwałymi zmiennymi; klucze Microsoftu wpisane jak w laptopie (shim jest podpisany przez MS).
# 1. Secure Boot włączony, klucz K1 w MOK: shim → GRUB → k1.efi → pulpit; Instalator: „włączony, klucz wpisany”;
# 2. Secure Boot włączony, klucza K1 nie ma: GRUB odmawia, na ekranie wyjaśnienie, K1 nie startuje;
# 3. Secure Boot włączony, klucz jest, ale k1.efi zmieniony (jeden bajt): odmowa jak wyżej;
# 4. klucz wydania (sekret K1_KLUCZ_PODPISU): pełna droga właściciela — Instalator → Secure Boot → „Przygotuj
#    klucz K1” → restart → MokManager (Enroll MOK → Continue → Yes → hasło „kons” → Reboot) → Secure Boot
#    włączony → K1 startuje; wersja deweloperska: klucza nie da się wpisać (jest publiczny).
# 5. (0.8.5 krok 4, zgłoszenie z X230) K1 uruchomiony z dysku (bez pendrive'a — nie ma systemu do skopiowania):
#    Instalator i tak pokazuje link „Secure Boot…” (do 0.8.3 go nie rysował) i przez niego da się przygotować klucz.
# Zrzuty: build/testy/m6-*.png. Wymaga: scripts/zbuduj.sh, python3-virt-firmware (virt-fw-vars).
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-300}
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-usb.img
PEN=$TESTY/m6-pendrive.img
mkdir -p "$TESTY"; rm -f "$TESTY"/m6-*
[ -f "$USBIMG" ] || blad "brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"
command -v virt-fw-vars >/dev/null || blad "brak virt-fw-vars (scripts/przygotuj-srodowisko.sh: python3-virt-firmware)"
MS=/usr/share/OVMF/OVMF_VARS_4M.ms.fd
openssl x509 -inform der -in "$K1_WYNIK/k1-sb.cer" -out "$TESTY/m6-k1.pem"
grep -q 'K1_SB_PRAWDZIWY 1' "$K1_BUDOWA/gen/sb/k1_sb_cert.h" && PRAWDZIWY=1 || PRAWDZIWY=0
r=0

echo "[k1] test M6: Secure Boot — klucz K1 w MOK, odmowa bez klucza i dla zmienionego systemu, wpisanie klucza"

# $1 = scenario, $2 = log name, $3 = UEFI variables file, $4 = stick image (or, with $5 = dysk, the SATA disk)
uruchom() {
    tryb=$1 nazwa=$2 nosnik=--pendrive; LOG=$TESTY/m6-$nazwa.log MON=$TESTY/m6-$nazwa.sock
    [ "${5:-}" = dysk ] && nosnik=--dysk-uefi
    "$K1_REPO/scripts/uruchom-qemu.sh" $nosnik "$4" --zmienne-uefi "$3" --log "$LOG" --monitor "$MON" --czas "$CZAS" &
    QPID=$!
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$tryb" "$LOG" "$MON" "$TESTY" "$CZAS" <<'PY' || r=1
import os, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
tryb = sys.argv[1]
q = Maszyna(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], "m6")
czekaj, sprawdz, tekst, klik, cisza = q.czekaj, q.sprawdz, q.tekst, q.klik, q.cisza
def koniec(czysty=True):
    if czysty: q.czysty_log()
    q.koniec()

def instalator_sb():
    """Opens the Instalator from the Start menu; returns (window x, y, SB_STAN line)."""
    g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+)", czas=200)
    # the desktop can be ready before the input service: moves sent before it grabs the mouse arrive late and the
    # click lands elsewhere (CI run 193: Start clicked at 422 instead of 491)
    if not g or not czekaj(r"K1-WEJSCIE: gotowe", czas=60): return None
    klik(int(g.group(1)), int(g.group(2)))
    m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+)", czas=20)
    if not m: return None
    klik(int(m.group(1)), int(m.group(2)) + 4 * int(m.group(3)))
    o = czekaj(r"K1-EKRAN: okno (\d+) \(instalacja\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
    s = czekaj(r"K1-INSTALACJA: secure boot: (.*); link (\d+),(\d+)", czas=30)
    if not (o and s): return None
    return int(o.group(4)), int(o.group(5)), s

if tryb in ("sb-bez-klucza", "sb-zmieniony"):
    m = czekaj(r"Kons K1 [0-9.]+ nie moze wystartowac", czas=200)
    q.zrzut(tryb)
    sprawdz("Secure Boot: " + ("klucza K1 nie ma" if tryb == "sb-bez-klucza" else "k1.efi zmieniony") +
            " — GRUB odmawia, na ekranie wyjaśnienie, K1 nie startuje", m and "K1-INITRAMFS" not in tekst())
    q.koniec()

if tryb == "sb-z-kluczem":
    w = instalator_sb()
    sprawdz("Secure Boot włączony, klucz K1 w MOK: shim → GRUB → podpisany k1.efi → pulpit",
            w and "Secure boot enabled" in tekst())
    sprawdz("Instalator: „Secure Boot: włączony, klucz K1: wpisany”",
            w and w[2].group(1).startswith("włączony, klucz K1: wpisany"), w[2].group(0) if w else "brak")
    if w:
        klik(w[0] + int(w[2].group(2)), w[1] + int(w[2].group(3))); q.zrzut("1-wlaczony")
    koniec()

if tryb == "mok-1":  # release key: K1 prepares the key for MokManager
    w = instalator_sb()
    sprawdz("Secure Boot wyłączony, klucz K1 nie wpisany", w and w[2].group(1).startswith("wyłączony, klucz K1: brak"),
            w[2].group(0) if w else "brak")
    if not w: koniec()
    klik(w[0] + int(w[2].group(2)), w[1] + int(w[2].group(3)))
    e = czekaj(r"K1-INSTALACJA: secure boot: ekran; przycisk (\d+),(\d+), wróć (\d+),(\d+)", czas=10)
    q.zrzut("2-przed")
    klik(w[0] + int(e.group(1)), w[1] + int(e.group(2)))
    sprawdz("„Przygotuj klucz K1”: nadzorca zapisuje MokNew + MokAuth", czekaj(r"K1-NADZORCA: Secure Boot: klucz K1 czeka", czas=20))
    q.zrzut("2-czeka")
    klik(w[0] + int(e.group(1)), w[1] + int(e.group(2)))
    sprawdz("„Uruchom ponownie”", czekaj(r"reboot: Restarting", czas=60))
    q.czysty_log(); q.koniec(wylacz=False)

if tryb == "mok-2":  # MokManager: the owner's steps
    niebieski = lambda w_, h_, px: w_ > 0 and px(w_ // 2, h_ // 2)[2] > 120 and px(w_ // 2, h_ // 2)[0] < 60
    sprawdz("po restarcie shim pokazuje MokManagera („Press any key to perform MOK management”)",
            niebieski(*q.zrzut("mok", gdy=niebieski, czas=150)))
    # The only fixed pauses left in the QEMU tests: the firmware's MokManager logs nothing and some of
    # these keys (the password) change nothing on screen, so there is no state to wait for.
    for k in ["ret", "down", "ret", "down", "ret", "down", "ret", "k", "o", "n", "s"]:
        q.monitor("sendkey " + k); time.sleep(1.2)
    q.zrzut("3-haslo")
    q.monitor("sendkey ret"); q.zrzut("3-wpisany")
    q.monitor("sendkey ret")  # Reboot: the key is in the UEFI variables once K1 starts again
    czekaj(r"K1-INITRAMFS", czas=150)
    q.koniec(wylacz=False)

if tryb == "mok-3":
    w = instalator_sb()
    sprawdz("po MokManagerze i włączeniu Secure Boot: K1 startuje, „włączony, klucz K1: wpisany”",
            w and "Secure boot enabled" in tekst() and w[2].group(1).startswith("włączony, klucz K1: wpisany"),
            w[2].group(0) if w else "brak")
    koniec()

if tryb == "z-dysku":  # 0.8.5 step 4: started from the disk — no system to copy, the link is there all the same
    w = instalator_sb()
    sprawdz("z dysku: Instalator — nie ma systemu do skopiowania (start nie z pendrive'a)",
            w and czekaj(r"K1-INSTALACJA: dyski: \d+ \(system do skopiowania: brak"), w[2].group(0) if w else "brak")
    if not w: koniec()
    lx, ly = w[0] + int(w[2].group(2)), w[1] + int(w[2].group(3))
    akcent = lambda px, x, y: any(px(x + dx, y + dy)[0] > px(x + dx, y + dy)[2] + 60 for dx in range(-20, 60, 2)
                                  for dy in range(-8, 4))
    _, _, px = q.zrzut("5-z-dysku", gdy=lambda w_, h_, px: akcent(px, lx, ly))
    sprawdz("z dysku: link „Secure Boot…” widać (kolor akcentu w jego miejscu)", akcent(px, lx, ly))
    klik(lx, ly)
    e = czekaj(r"K1-INSTALACJA: secure boot: ekran; przycisk (\d+),(\d+), wróć (\d+),(\d+)", czas=10)
    sprawdz("z dysku: link otwiera ekran Secure Boot", e)
    if e and "(wersja deweloperska)" not in w[2].group(1):
        klik(w[0] + int(e.group(1)), w[1] + int(e.group(2)))
        sprawdz("z dysku: „Przygotuj klucz K1” — nadzorca zapisuje MokNew + MokAuth",
                czekaj(r"K1-NADZORCA: Secure Boot: klucz K1 czeka", czas=20))
    q.zrzut("5-z-dysku-sb", gdy=lambda w_, h_, px_: True)
    koniec()

if tryb == "deweloperska":
    w = instalator_sb()
    sprawdz("wersja deweloperska: Instalator mówi, że jej klucza się nie wpisuje",
            w and "(wersja deweloperska)" in w[2].group(1), w[2].group(0) if w else "brak")
    if not w: koniec()
    klik(w[0] + int(w[2].group(2)), w[1] + int(w[2].group(3)))
    e = czekaj(r"K1-INSTALACJA: secure boot: ekran; przycisk (\d+),(\d+), wróć (\d+),(\d+)", czas=10)
    q.zrzut("4-deweloperska")
    if e:
        klik(w[0] + int(e.group(1)) - 60, w[1] + int(e.group(2)))  # where „Przygotuj klucz K1” would be
    sprawdz("bez przycisku „Przygotuj klucz K1” — nic nie jest zapisywane",
            cisza(r"proszę o przygotowanie|MokNew|klucz K1 czeka", 3) and "MokNew" not in tekst())
    koniec()
PY
    zamknij_qemu "$MON" $QPID
}

cp "$USBIMG" "$PEN"
virt-fw-vars --input "$MS" --output "$TESTY/m6-zmienne-mok.fd" --add-mok 4b310000-0000-4000-8000-0000000000aa "$TESTY/m6-k1.pem" \
    --secure-boot >/dev/null 2>&1 || blad "virt-fw-vars (MOK)"
uruchom sb-z-kluczem 1-z-kluczem "$TESTY/m6-zmienne-mok.fd" "$PEN"
virt-fw-vars --input "$MS" --output "$TESTY/m6-zmienne-bez.fd" --secure-boot >/dev/null 2>&1 || blad "virt-fw-vars"
uruchom sb-bez-klucza 2-bez-klucza "$TESTY/m6-zmienne-bez.fd" "$PEN"
# one byte of the kernel inside k1.efi (slot A: the layout block at 1 KiB of K1-EFI says where; k1.efi starts
# in the first MiB of the slot's inner FAT, its kernel code well before 2 MiB) — the signature no longer matches
cp "$PEN" "$TESTY/m6-zmieniony.img"
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); f.seek(1048576+1024+32); a=1048576+int.from_bytes(f.read(8),"little"); f.seek(a+2*1048576); b=f.read(1); f.seek(-1,1); f.write(bytes([b[0]^0xff])); f.close()' "$TESTY/m6-zmieniony.img"
virt-fw-vars --input "$MS" --output "$TESTY/m6-zmienne-mok2.fd" --add-mok 4b310000-0000-4000-8000-0000000000aa "$TESTY/m6-k1.pem" \
    --secure-boot >/dev/null 2>&1
uruchom sb-zmieniony 3-zmieniony "$TESTY/m6-zmienne-mok2.fd" "$TESTY/m6-zmieniony.img"
rm -f "$TESTY/m6-zmieniony.img"
virt-fw-vars --input "$MS" --output "$TESTY/m6-zmienne.fd" --set-false SecureBootEnable >/dev/null 2>&1
if [ "$PRAWDZIWY" = 1 ]; then
    uruchom mok-1 4-mok-1 "$TESTY/m6-zmienne.fd" "$PEN"
    uruchom mok-2 5-mok-2 "$TESTY/m6-zmienne.fd" "$PEN"
    if virt-fw-vars --input "$TESTY/m6-zmienne.fd" --print 2>/dev/null | grep -q '^MokList '; then
        echo "  [OK]    MokManager wpisał klucz K1 do MokList"
    else echo "  [BŁĄD]  po MokManagerze brak MokList w zmiennych UEFI"; r=1; fi
    virt-fw-vars --input "$TESTY/m6-zmienne.fd" --output "$TESTY/m6-zmienne-sb.fd" --secure-boot >/dev/null 2>&1  # F1 → Secure Boot → Enabled
    uruchom mok-3 6-mok-3 "$TESTY/m6-zmienne-sb.fd" "$PEN"
else
    echo "  [--]    wpisanie klucza przez MokManagera: pominięte (wersja deweloperska — bez sekretu K1_KLUCZ_PODPISU)"
    uruchom deweloperska 4-deweloperska "$TESTY/m6-zmienne.fd" "$PEN"
fi
# 0.8.5 step 4: the same image as an installed disk (SATA, 2 GiB — a disk the Instalator lists), no stick
cp "$USBIMG" "$TESTY/m6-dysk.img" && truncate -s 2G "$TESTY/m6-dysk.img"
virt-fw-vars --input "$MS" --output "$TESTY/m6-zmienne-dysk.fd" --set-false SecureBootEnable >/dev/null 2>&1
uruchom z-dysku 7-z-dysku "$TESTY/m6-zmienne-dysk.fd" "$TESTY/m6-dysk.img" dysk
rm -f "$PEN" "$TESTY/m6-dysk.img" "$TESTY"/m6-zmienne*.fd
if [ $r -eq 0 ]; then info "M6: wszystko zaliczone (zrzuty: $TESTY/m6-*.png)"; else echo "[k1] M6: BŁĄD (logi: $TESTY/m6-*.log)"; fi
exit $r

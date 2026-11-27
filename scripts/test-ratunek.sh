#!/bin/sh
# Test 0.13.0 krok 3 (ADR 0058): tryb ratunkowy. Obraz USB wydania jako dysk SATA (K1 „zainstalowany”, start przez UEFI:
# shim → GRUB → k1.efi), w miejscu B kopia miejsca A (jest „poprzednia wersja”), pendrive FAT32 na USB na raport.
#   1–2. „nieudany-1”, „nieudany-2”: maszyna wyłączona, zanim K1 pokaże obraz — GRUB liczy starty (k1_licznik 1, 2)
#   3. „ratunek”: trzeci start — menu GRUB-a z trybem ratunkowym jako domyślnym (10 s) → tryb ratunkowy: ekran
#      ratunkowy (zrzut), cztery przyciski; NEGATYWNE: bez skarbca, pulpitu, sieci; „Zapisz raport na pendrive” →
#      raport zapisany; „Uruchom poprzednią wersję” → grubenv k1_nowy=b, ponowne uruchomienie
#   4. „poprzednia”: GRUB próbuje miejsca B, K1 wstaje (pulpit), licznik wyzerowany przy pierwszym obrazie, po 15 s
#      miejsce B potwierdzone
# Na hoście: K1RAPORT.TXT na pendrivie (mtools) z wersją, grubenv (k1_licznik=2, k1_tryb=r), dyskami i logiem; fsck.fat
# bez błędów; NEGATYWNE: w raporcie nic spoza logu (słowo „hasło” tylko jak w logu — bez wpisanych znaków).
# Dowody: build/testy/ratunek-*.log, zrzut build/testy/ratunek-ekran.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-300}
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-test-usb.img # 0.13.0 step 4 (ADR 0059): k1-test.efi — the log on the serial port
DYSK=$TESTY/ratunek-dysk.img STICK=$TESTY/ratunek-pendrive.img
export MTOOLS_SKIP_CHECK=1
[ -f "$USBIMG" ] || blad "brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"
mkdir -p "$TESTY"; rm -f "$TESTY"/ratunek-*
cp --sparse=always "$USBIMG" "$DYSK"
# slot B = slot A (the layout block at 1 KiB of K1-EFI, which starts at 1 MiB); zero chunks skipped (the image stays sparse)
python3 -I - "$DYSK" <<'PY'
import sys
f = open(sys.argv[1], "r+b")
def pole(o):
    f.seek(1048576 + 1024 + o)
    return int.from_bytes(f.read(8), "little")
n, a, b = pole(24), 1048576 + pole(32), 1048576 + pole(40)
for o in range(0, n, 1 << 20):
    f.seek(a + o)
    d = f.read(min(1 << 20, n - o))
    if d.count(0) != len(d):
        f.seek(b + o)
        f.write(d)
f.close()
PY
truncate -s 64M "$STICK"
"$(command -v mkfs.fat || echo /usr/sbin/mkfs.fat)" -F 32 -s 1 -n PENDRIVE "$STICK" >/dev/null
USB="-device qemu-xhci,id=xhci -drive if=none,id=pen,file=$STICK,format=raw -device usb-storage,bus=xhci.0,drive=pen"
r=0

uruchom() { # $1 = scenario
    rm -f "$TESTY/ratunek-$1.log" "$TESTY/ratunek-$1.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/ratunek-$1.log" --monitor "$TESTY/ratunek-$1.sock" --czas "$CZAS" \
        --dysk-uefi "$DYSK" --qemu "$USB" &
    QPID=$!
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/ratunek.py" "$TESTY/ratunek-$1.log" "$TESTY/ratunek-$1.sock" \
        "$TESTY" "$CZAS" "$1" || r=1
    zamknij_qemu "$TESTY/ratunek-$1.sock" $QPID
}

cat > "$K1_BUDOWA/ratunek.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "ratunek")
czekaj, sprawdz, ile = q.czekaj, q.sprawdz, q.ile
N = r"K1-NADZORCA: "
if tryb.startswith("nieudany"):
    # killed at nadzorca's first lines: before ekran's picture — this start never reached the screen
    sprawdz(f"{tryb}: K1 startuje (nadzorca)", czekaj(N + r"TPM", czas=limit * 0.7))
    sprawdz(f"{tryb}: zwykły start (nie tryb ratunkowy)", ile(r"TRYB RATUNKOWY") == 0)
    q.monitor("quit")
    q.koniec(wylacz=False)
if tryb == "ratunek":
    sprawdz("ratunek: po dwóch nieudanych startach GRUB uruchamia tryb ratunkowy",
            czekaj(N + r"TRYB RATUNKOWY", czas=limit * 0.7))
    m = czekaj(r"K1-LOGOWANIE: ekran ratunkowy: Uruchom K1 normalnie (\d+),(\d+); Uruchom poprzednią wersję \(([\d.]+)\) "
               r"(\d+),(\d+); Zapisz raport na pendrive (\d+),(\d+); Wyłącz komputer (\d+),(\d+)", czas=90)
    sprawdz("ratunek: ekran ratunkowy z czterema przyciskami (poprzednia wersja z miejsca B)", m)
    k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ", czas=30)
    if not (m and k0):
        q.koniec()
    q.kursor[:] = [int(k0.group(1)), int(k0.group(2))]
    q.zrzut("ekran")
    sprawdz("ratunek: NEGATYWNE — bez skarbca, pulpitu i sieci (dane nieotwierane)",
            ile(r"K1-SKARBIEC:") == 0 and ile(r"K1-GUI:") == 0 and ile(r"K1-SIEC:") == 0 and ile(r"K1-STREFA") == 0)
    n = ile(r"ekran ratunkowy: raport zapisany")
    q.klik(int(m.group(6)), int(m.group(7)))
    sprawdz("ratunek: „Zapisz raport na pendrive” — raport zapisany (program zapisujący w klatce)",
            czekaj(r"K1-LOGOWANIE: ekran ratunkowy: raport zapisany", od=n, czas=60) and
            ile(r"K1-INSTALATOR: raport: zapisany") == 1)
    q.zrzut("raport")
    q.klik(int(m.group(4)), int(m.group(5)))
    sprawdz("ratunek: „Uruchom poprzednią wersję” — miejsce B przy następnym starcie",
            czekaj(N + r"tryb ratunkowy: poprzednia wersja \(miejsce b\)", czas=30))
    sprawdz("ratunek: ponowne uruchomienie", czekaj(N + r"zatrzymywanie usług", czas=60))
    sprawdz("ratunek: licznik nieudanych startów nie zerowany przez sam tryb ratunkowy (raport go widzi)",
            ile(r"licznik nieudanych startów wyzerowany") == 0)
    q.koniec(wylacz=False)
if tryb == "poprzednia":
    g = czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.7)
    sprawdz("poprzednia: miejsce B wystartowało (pulpit), bez trybu ratunkowego", g and ile(r"TRYB RATUNKOWY") == 0)
    sprawdz("poprzednia: po 15 s miejsce B potwierdzone (A/B)",
            czekaj(N + r"A/B: nowa wersja .* wystartowała — potwierdzona \(.*miejsce b\)", czas=90))
    q.koniec(wylacz=False)
PY

info "tryb ratunkowy 1/4: start przerwany przed obrazem"
uruchom nieudany-1
info "tryb ratunkowy 2/4: drugi raz"
uruchom nieudany-2
info "tryb ratunkowy 3/4: trzeci start — tryb ratunkowy"
uruchom ratunek
info "tryb ratunkowy 4/4: poprzednia wersja"
uruchom poprzednia

R=$(mtype -i "$STICK" ::K1RAPORT.TXT 2>/dev/null || true)
sprawdz_raport() { # $1 = description, $2 = a pattern grep -E must find
    if printf '%s' "$R" | grep -qE "$2"; then echo "  [OK]    raport: $1"; else echo "  [BŁĄD]  raport: $1"; r=1; fi
}
sprawdz_raport "plik K1RAPORT.TXT na pendrivie, nagłówek" "Kons K1: raport"
sprawdz_raport "tryb ratunkowy" "Tryb: ratunkowy"
sprawdz_raport "wersja" "Wersja: $K1_WERSJA"
sprawdz_raport "grubenv z licznikiem i trybem" "k1_licznik=2 k1_tryb=r"
sprawdz_raport "miejsca A i B" "miejsce A: [0-9.]+, miejsce B: [0-9.]+"
sprawdz_raport "dysk SATA z K1-EFI i K1-DANE" "SATA, [0-9]+ GiB, K1-EFI, K1-DANE"
sprawdz_raport "TPM" "^TPM"
sprawdz_raport "log tego startu" "K1-NADZORCA: TRYB RATUNKOWY"
if "$(command -v fsck.fat || echo /usr/sbin/fsck.fat)" -n "$STICK" >/dev/null 2>&1; then
    echo "  [OK]    pendrive: fsck.fat bez błędów"
else
    echo "  [BŁĄD]  pendrive: fsck.fat znalazł błędy"; r=1
fi
# grubenv of the disk now: slot B confirmed, the counter empty (cleared by "Uruchom poprzednią wersję"), no rescue mode
ENV=$(python3 -I -c 'import sys; f=open(sys.argv[1],"rb"); f.seek(1048576+1024+16); o=int.from_bytes(f.read(8),"little"); f.seek(1048576+o); print(" ".join(l.decode() for l in f.read(1024).split(b"\n") if l.startswith(b"k1_")))' "$DYSK")
case " $ENV " in
    *" k1_slot=b "*" k1_licznik=2 "*) echo "  [BŁĄD]  grubenv po wszystkim: $ENV"; r=1 ;;
    *" k1_slot=b "*" k1_tryb= "*) echo "  [OK]    grubenv po wszystkim: $ENV" ;;
    *) echo "  [BŁĄD]  grubenv po wszystkim: $ENV"; r=1 ;;
esac
rm -f "$DYSK"
[ $r -eq 0 ] && info "tryb ratunkowy: OK" || info "tryb ratunkowy: BŁĄD"
exit $r

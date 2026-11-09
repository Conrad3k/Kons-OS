#!/bin/sh
# Test etapu M2 (powtarzalny): ekran i wejście w działającym K1 (obraz produkcyjny).
# 1. start → pierwsza klatka (tapeta, pulpit), 2. mysz (monitor QEMU: mouse_move),
# 3. klawiatura (sendkey: strzałki przesuwają kursor, gdy żadne okno nie ma fokusu) i przycisk myszy
#    (zmiana koloru kursora).
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
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$LOG" "$MON" "$TESTY" "$CZAS" <<'PY' || r=$?
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
q = Maszyna(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], "m2")
czekaj, monitor, zrzut, sprawdz = q.czekaj, q.monitor, q.zrzut, q.sprawdz

krem = lambda c: c[0] > 200 and c[1] > 195 and c[2] > 180
akcent = lambda c: c[0] > 170 and c[1] < 150 and c[2] < 120 and c[0] > c[1] > c[2]
WZOR = r"K1-EKRAN: kursor (\d+),(\d+) przycisk (\S+)"

tryb = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
klatka = czekaj(r"K1-EKRAN: pierwsza klatka")
k0 = czekaj(WZOR)
sprawdz("ekran: tryb i pierwsza klatka w logu", bool(tryb and klatka and k0))
if not (tryb and klatka and k0): sys.exit(1)
W, H = int(tryb.group(1)), int(tryb.group(2)); x0, y0 = int(k0.group(1)), int(k0.group(2))
# the top bar is gui's: its first frame may come a moment after ekran's (0.7.5: it reads the desktop
# arrangement from skarbiec first) — wait for gui, then for the screen showing both
sprawdz("pulpit gotowy (gui)", czekaj(r"K1-GUI: pulpit gotowy") is not None)
w, h, px = zrzut("1-start", gdy=lambda w, h, px: krem(px(x0 + 3, y0 + 9)) and max(px(W // 2, 4)) < 120)
sprawdz(f"zrzut 1: rozmiar {w}x{h} = tryb {W}x{H}", (w, h) == (W, H))
sprawdz("zrzut 1: tapeta (kolorowa, nie jednolita)", px(W // 4, H // 2) != px(W - 20, H // 3) and max(px(W // 3, H // 2)) > 80,
        str(px(W // 4, H // 2)))
sprawdz("zrzut 1: ciemny górny pasek", max(px(W // 2, 4)) < 120, str(px(W // 2, 4)))
sprawdz("zrzut 1: kursor w miejscu z logu", krem(px(x0 + 3, y0 + 9)), str(px(x0 + 3, y0 + 9)))
px1 = px
# the mouse must be open in wejscie before it is moved (a fast boot may find it a moment later —
# nadzorca then restarts wejscie with the full set of devices)
sprawdz("wejście: mysz otwarta", czekaj(r"K1-WEJSCIE: urządzenie \d+: mysz") is not None)

monitor(*["mouse_move -60 -30"] * 4)
x1, y1 = x0 - 240, y0 - 120
m = czekaj(rf"K1-EKRAN: kursor {x1},{y1} ")
sprawdz(f"mysz: kursor przesunięty o (-240,-120) → {x1},{y1}", m is not None)
w, h, px = zrzut("2-mysz", gdy=lambda w, h, px: krem(px(x1 + 3, y1 + 9)))
sprawdz("zrzut 2: kursor w nowym miejscu", krem(px(x1 + 3, y1 + 9)), str(px(x1 + 3, y1 + 9)))
px2 = px

monitor(*["sendkey right"] * 3 + ["mouse_button 1"])
x2 = x1 + 60
m = czekaj(rf"K1-EKRAN: kursor {x2},{y1} przycisk wciśnięty")
sprawdz(f"klawiatura: 3 x strzałka w prawo → {x2},{y1}; przycisk myszy wciśnięty", m is not None)
w, h, px = zrzut("3-klawiatura-przycisk", gdy=lambda w, h, px: akcent(px(x2 + 3, y1 + 9)))
sprawdz("zrzut 3: kursor w kolorze akcentu (przycisk wciśnięty)", akcent(px(x2 + 3, y1 + 9)), str(px(x2 + 3, y1 + 9)))
# Where the cursor was at the start, screenshots 2 and 3 show the same wallpaper again (no trace).
sprawdz("zrzut 2 i 3: w starym miejscu kursora znów tapeta (brak śladu)",
        px2(x0 + 3, y0 + 9) == px(x0 + 3, y0 + 9) != px1(x0 + 3, y0 + 9), f"{px1(x0 + 3, y0 + 9)} {px2(x0 + 3, y0 + 9)}")
monitor("mouse_button 0")

tekst = q.tekst()
sprawdz("brak 'Kernel panic' i błędów usług", "Kernel panic" not in tekst and "BŁĄD" not in tekst)
sprawdz("brak odrzuconych i zgubionych zdarzeń przy poprawnym wejściu",
        re.findall(r"odrzucone (\d+)\)", tekst)[-1:] == ["0"] and "pominięto" not in tekst)
q.koniec()
PY
zamknij_qemu "$MON" $QPID
if [ $r -eq 0 ]; then info "M2: wszystko zaliczone (zrzuty: $TESTY/m2-*.png)"; else echo "[k1] M2: BŁĄD (log: $LOG)"; fi
exit $r

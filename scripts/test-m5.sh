#!/bin/sh
# Test etapu M5 (powtarzalny), krok M5a: sprzęt — wejście przez USB w działającym K1 (obraz produkcyjny).
# Maszyna BEZ kontrolera PS/2 (i8042=off), tylko klawiatura i mysz USB na kontrolerze xHCI:
# 1. start → wejscie znajduje klawiaturę i mysz USB, 2. mysz USB przesuwa kursor (monitor QEMU),
# 3. klawiatura USB (strzałki przesuwają kursor, gdy żadne okno nie ma fokusu),
# 4. podłączenie drugiej myszy w trakcie pracy (device_add), odłączenie (device_del) i ponowne
#    podłączenie — system zauważa zmiany, nic się nie zawiesza,
# 5. (0.5.8) pendrive z partycją K1-DANE podłączony po starcie → skarbiec i pulpit od nowa,
# 6. (0.5.3) bateria laptopa (tablica ACPI z testy/acpi/bateria.asl: 73 %, ładowanie, zasilacz
#    podłączony) — wskaźnik na górnym pasku (log powłoki + zrzut).
# Dyski (SATA, NVMe, pendrive) i kontrolery EHCI/OHCI sprawdza test w systemie (scripts/test-m1.sh).
# Zrzut: build/testy/m5-usb.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
LOG=$TESTY/m5-usb.log MON=$TESTY/m5-usb.sock
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$LOG" "$MON" "$TESTY"/m5-*.png

echo "[k1] test M5a: klawiatura i mysz USB (bez PS/2), podłączanie w trakcie pracy, bateria"
bateria_aml 2 1 "$TESTY/bateria-ladowanie.aml"
# 0.5.8: an empty stick with a K1-DANE partition, plugged in after boot (late detection)
rm -f "$TESTY/m5-pozny.img"; truncate -s 64M "$TESTY/m5-pozny.img"
printf 'label: gpt\nstart=2048, size=126976, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="K1-DANE"\n' \
    | sfdisk -q "$TESTY/m5-pozny.img" >/dev/null || blad "sfdisk (m5-pozny.img)"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" --maszyna ",i8042=off" \
    --qemu "-device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0
            -acpitable file=$TESTY/bateria-ladowanie.aml" &
QPID=$!
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$LOG" "$MON" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" "$CZAS" <<'PY' || r=$?
import os, re, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
q = Maszyna(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], "m5")
tekst, czekaj, ile, monitor, sprawdz, kursor, idz, klik = q.tekst, q.czekaj, q.ile, q.monitor, q.sprawdz, q.kursor_teraz, q.idz, q.klik

krem = lambda c: c[0] > 200 and c[1] > 195 and c[2] > 180
GOTOWE = r"K1-WEJSCIE: gotowe \((\d+) urządzeń\)"
KURSOR = r"K1-EKRAN: kursor (\d+),(\d+) przycisk"

def urzadzenia(n, od=0, czas=60):
    """Waits until wejscie (re)started with n devices, at least `od` times before."""
    t_end = time.time() + czas
    while time.time() < t_end:
        m = re.findall(GOTOWE, tekst())
        if m and int(m[-1]) == n and m.count(str(n)) > od: return True
        time.sleep(0.5)
    return False


tryb = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
sprawdz("ekran: pierwsza klatka", tryb and czekaj(r"K1-EKRAN: pierwsza klatka") and czekaj(KURSOR))
if not tryb: sys.exit(1)
sprawdz("brak kontrolera PS/2 (maszyna tylko z USB)", "i8042: PNP: No PS/2 controller found" in tekst())
sprawdz("wejscie: klawiatura i mysz USB (2 urządzenia)", urzadzenia(2, czas=90),
        str(re.findall(GOTOWE, tekst())))
time.sleep(1)
x0, y0 = kursor()

monitor(*["mouse_move -60 -30"] * 4)
x1, y1 = x0 - 240, y0 - 120
sprawdz(f"mysz USB: kursor przesunięty o (-240,-120) → {x1},{y1}", czekaj(rf"K1-EKRAN: kursor {x1},{y1} ", czas=20),
        str(kursor()))
monitor(*["sendkey right"] * 3)
x2 = x1 + 60
sprawdz(f"klawiatura USB: 3 x strzałka w prawo → {x2},{y1}", czekaj(rf"K1-EKRAN: kursor {x2},{y1} ", czas=20),
        str(kursor()))

# hotplug: a second mouse appears, disappears, appears again (same event number)
n_nowe = ile(r"nowe urządzenie wejścia")
monitor("device_add usb-mouse,id=mysz2,bus=xhci.0")
sprawdz("podłączenie myszy w trakcie pracy → wejscie z 3 urządzeniami",
        czekaj(r"nowe urządzenie wejścia", od=n_nowe, czas=30) and urzadzenia(3, czas=30))
monitor("device_del mysz2")
sprawdz("odłączenie myszy: nadzorca i wejscie to zauważają",
        czekaj(r"urządzenie wejścia odłączone \(event\d+\)", czas=30) and czekaj(r"K1-WEJSCIE: urządzenie odłączone", czas=30))
monitor("device_add usb-mouse,id=mysz3,bus=xhci.0")
sprawdz("ponowne podłączenie (ten sam numer urządzenia) → znów 3 urządzenia",
        czekaj(r"nowe urządzenie wejścia", od=n_nowe + 1, czas=30) and urzadzenia(3, od=1, czas=30))
time.sleep(1)
monitor(*["mouse_move 40 20"] * 2)
x3, y3 = x2 + 80, y1 + 40
sprawdz(f"mysz działa dalej po podłączaniu → {x3},{y3}", czekaj(rf"K1-EKRAN: kursor {x3},{y3} ", czas=20), str(kursor()))
time.sleep(1)
w, h, px = q.zrzut("usb")
sprawdz("zrzut: kursor w miejscu z logu", krem(px(x3 + 3, y3 + 9)), str(px(x3 + 3, y3 + 9)))
sprawdz("bateria: powłoka dostała stan od nadzorcy (73 %, ładowanie, zasilacz podłączony)",
        czekaj(r"K1-GUI: bateria 73%, ładowanie, zasilacz podłączony", czas=10))
glosnik = czekaj(r"K1-GUI: pulpit gotowy: .* dźwięk (\d+),16", czas=5)
if glosnik:  # battery icon, percent and bolt: left of the sound icon on the top bar
    gx = int(glosnik.group(1))
    jasne = sum(1 for x in range(max(0, gx - 110), gx - 16) for y in range(6, 26) if krem(px(x, y)))
    sprawdz(f"zrzut: wskaźnik baterii na górnym pasku ({jasne} jasnych pikseli)", jasne > 60, str(jasne))
else:
    sprawdz("zrzut: wskaźnik baterii na górnym pasku", False, "brak położenia dźwięku w logu")

# 0.5.8: a K1-DANE that shows up late (slow stick, X230 port hand-over): skarbiec started without
# one — nadzorca notices the stick, restarts skarbiec with it and ekran with the desktop.
sprawdz("start bez partycji K1-DANE: dane tylko w pamięci", "K1-SKARBIEC: brak partycji K1-DANE" in tekst())
n_pulpit = ile(r"K1-GUI: pulpit gotowy")
monitor(f"drive_add 0 if=none,id=pozny,file={q.out}/m5-pozny.img,format=raw", "device_add usb-storage,id=pozny,drive=pozny,bus=xhci.0")
sprawdz("pendrive z K1-DANE podłączony po starcie → nadzorca uruchamia skarbiec ponownie",
        czekaj(r"K1-NADZORCA: partycja K1-DANE pojawiła się po starcie", czas=30))
sprawdz("skarbiec: nowy magazyn na tej partycji, pulpit od nowa",
        czekaj(r"K1-SKARBIEC: utworzono nowy magazyn na pustej partycji K1-DANE", czas=60)
        and czekaj(r"K1-GUI: pulpit gotowy", od=n_pulpit, czas=60))
time.sleep(2)

# 0.5.5: Ustawienia → Bateria — the charge chart since boot (samples every 30 s)
q.kursor[:] = list(kursor() or (0, 0))
g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=5)
if g:
    ax, ay, krok = int(g.group(3)), int(g.group(4)), int(g.group(5))
    klik(ax + 2 * krok, ay)
    u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
    sek = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    if u and sek:
        ux, uy = int(u.group(4)), int(u.group(5))
        time.sleep(1)
        klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + 2 * int(sek.group(3)))
        WZB = r"K1-USTAWIENIA: bateria: jest, 73%, próbek (\d+) co 30 s; wykres (\d+),(\d+) (\d+)x(\d+)"
        b = czekaj(WZB, czas=20)
        if b and int(b.group(1)) < 2:  # a sample every 30 s: wait for the second one, open the section again
            time.sleep(35)
            klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + 2 * int(sek.group(3)))
            b = czekaj(WZB, od=1, czas=20)
        sprawdz("Ustawienia → Bateria: stan i historia od nadzorcy (73 %, próbki co 30 s)", b and int(b.group(1)) >= 2,
                b.group(0) if b else "brak")
        if b:
            wx, wy, ww, wh = (int(b.group(i)) for i in range(2, 6))
            time.sleep(1.5)
            w, h, px = q.zrzut("bateria")
            akcent = lambda c: c[0] > 150 and c[1] < 140 and c[2] < 110 and c[0] > c[1] > c[2]
            linia = sum(1 for x in range(ux + wx + 8, ux + wx + ww - 8) for y in range(uy + wy, uy + wy + wh) if akcent(px(x, y)))
            sprawdz(f"zrzut: wykres naładowania na całą szerokość ({linia} pikseli linii)", linia >= (ww - 16) * 2 * 0.8,
                    str(linia))
    else:
        sprawdz("Ustawienia otwarte z dolnego paska", False, f"okno: {bool(u)}, układ: {bool(sek)}")
else:
    sprawdz("położenia pulpitu w logu", False)

t = tekst()
sprawdz("brak 'Kernel panic' i błędów usług", "Kernel panic" not in t and "BŁĄD" not in t)
sprawdz("brak odrzuconych i zgubionych zdarzeń", re.findall(r"odrzucone (\d+)\)", t)[-1:] == ["0"] and "pominięto" not in t)
q.koniec()
PY
kill $QPID 2>/dev/null || true; wait $QPID 2>/dev/null || true

# M5d: the USB image (k1-<version>-usb.img) booted by UEFI firmware from a USB stick, twice: the first
# boot creates the encrypted vault on the empty K1-DANE partition, the second one opens it.
echo "[k1] test M5d: obraz na pendrive — start przez UEFI, magazyn na partycji K1-DANE, ponowny start"
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-usb.img
if [ -f "$USBIMG" ]; then
    cp "$USBIMG" "$TESTY/m5-pendrive.img"
    for n in 1 2; do
        PLOG=$TESTY/m5-pendrive$n.log PMON=$TESTY/m5-pendrive$n.sock
        rm -f "$PLOG" "$PMON"
        "$K1_REPO/scripts/uruchom-qemu.sh" --pendrive "$TESTY/m5-pendrive.img" --log "$PLOG" --monitor "$PMON" --czas "$CZAS" \
            --qemu "-acpitable file=$TESTY/bateria-ladowanie.aml" &
        PPID_=$!
        # 0.6.0: the battery history of the first boot is kept in skarbiec and read by the second one
        if [ $n -eq 1 ]; then OCZ="K1-SKARBIEC: utworzono nowy magazyn na pustej partycji K1-DANE" BAT="historia baterii zapisana (poprzednich uruchomień: 0)"
        else OCZ="K1-SKARBIEC: magazyn otwarty" BAT="K1-NADZORCA: historia baterii: 1 poprzednich uruchomień"; fi
        t=0
        while [ $t -lt "$CZAS" ] && ! { grep -qF "$OCZ" "$PLOG" 2>/dev/null && grep -qF "K1-GUI: pulpit gotowy" "$PLOG" 2>/dev/null \
            && grep -qF "$BAT" "$PLOG" 2>/dev/null; }; do
            kill -0 $PPID_ 2>/dev/null || break; sleep 1; t=$((t + 1))
        done
        python3 -I "$K1_REPO/testy/qemu/k1qemu.py" zamknij "$PMON" \
            || kill $PPID_ 2>/dev/null || true
        wait $PPID_ 2>/dev/null || true
        if grep -qF "$OCZ" "$PLOG" && grep -qF "K1-GUI: pulpit gotowy" "$PLOG" && grep -qF "partycja danych K1-DANE otwarta" "$PLOG" \
            && grep -qF "$BAT" "$PLOG" \
            && ! grep -q "Kernel panic" "$PLOG" && ! grep -qF "BŁĄD" "$PLOG"; then
            echo "  [OK]    start $n z pendrive'a: pulpit, $( [ $n -eq 1 ] && echo 'nowy zaszyfrowany magazyn, historia baterii zapisana' || echo 'magazyn z poprzedniego startu otwarty, historia baterii z poprzedniego startu') (${t} s)"
        else
            echo "  [BŁĄD]  start $n z pendrive'a (log: $PLOG)"; r=1
        fi
    done
else
    echo "  [BŁĄD]  brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"; r=1
fi
if [ $r -eq 0 ]; then info "M5: wszystko zaliczone (zrzuty: $TESTY/m5-*.png)"; else echo "[k1] M5: BŁĄD (logi: $TESTY/m5-*.log)"; fi
exit $r

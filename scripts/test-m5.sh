#!/bin/sh
# Test etapu M5 (powtarzalny), krok M5a: sprzęt — wejście przez USB w działającym K1 (obraz produkcyjny).
# Maszyna BEZ kontrolera PS/2 (i8042=off), tylko klawiatura i mysz USB na kontrolerze xHCI:
# 1. start → wejscie znajduje klawiaturę i mysz USB, 2. mysz USB przesuwa kursor (monitor QEMU),
# 3. klawiatura USB (strzałki przesuwają kursor, gdy żadne okno nie ma fokusu),
# 4. podłączenie drugiej myszy w trakcie pracy (device_add), odłączenie (device_del) i ponowne
#    podłączenie — system zauważa zmiany, nic się nie zawiesza,
# 5. (0.5.8) pendrive z partycją K1-DANE podłączony po starcie → skarbiec i pulpit od nowa,
#    (0.8.0) obraz na pendrive większym od obrazu: pusta K1-DANE rośnie przy pierwszym starcie do końca pendrive'a,
# 6. (0.5.3) bateria laptopa (tablica ACPI z testy/acpi/bateria.asl: 73 %, ładowanie, zasilacz
#    podłączony) — wskaźnik na górnym pasku (log powłoki + zrzut),
# 7. (0.8.6) Start → Wyłącz na końcu: nadzorca zapisuje historię baterii, zanim init zatrzyma skarbiec,
# 8. (0.11.8 krok 3) okienko baterii: kliknięcie baterii na górnym pasku → procent, stan, czas do pełna i pobór mocy
#    (liczby wyliczone z tablicy ACPI), drugie kliknięcie i menu Start je zamykają, przycisk „Ustawienia baterii”
#    otwiera Ustawienia na sekcji Bateria. Zrzut: build/testy/m5-okienko-baterii.png.
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
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$LOG" "$MON" "$TESTY" "$CZAS" <<'PY' || r=$?
import os, re, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
q = Maszyna(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], "m5")
tekst, czekaj, ile, monitor, sprawdz, kursor, idz, klik = q.tekst, q.czekaj, q.ile, q.monitor, q.sprawdz, q.kursor_teraz, q.idz, q.klik

krem = lambda c: c[0] > 200 and c[1] > 195 and c[2] > 180
GOTOWE = r"K1-WEJSCIE: gotowe \((\d+) urządzeń\)"
KURSOR = r"K1-EKRAN: kursor (\d+),(\d+) przycisk"

def urzadzenia(n, od=0, czas=60):
    """Waits until wejscie (re)started with n devices, at least `od` times before."""
    def jest():
        m = re.findall(GOTOWE, tekst())
        return bool(m) and int(m[-1]) == n and m.count(str(n)) > od
    return q.czekaj_na(jest, czas)


tryb = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
sprawdz("ekran: pierwsza klatka", tryb and czekaj(r"K1-EKRAN: pierwsza klatka") and czekaj(KURSOR))
if not tryb: sys.exit(1)
sprawdz("brak kontrolera PS/2 (maszyna tylko z USB)", "i8042: PNP: No PS/2 controller found" in tekst())
sprawdz("wejscie: klawiatura i mysz USB (2 urządzenia)", urzadzenia(2, czas=90),
        str(re.findall(GOTOWE, tekst())))
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
monitor(*["mouse_move 40 20"] * 2)
x3, y3 = x2 + 80, y1 + 40
sprawdz(f"mysz działa dalej po podłączaniu → {x3},{y3}", czekaj(rf"K1-EKRAN: kursor {x3},{y3} ", czas=20), str(kursor()))
w, h, px = q.zrzut("usb", gdy=lambda w, h, px: krem(px(x3 + 3, y3 + 9)))
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

# 0.11.8 step 3 (D4: A): the battery's popover. The expected numbers come from the ACPI table itself (testy/acpi/bateria.asl):
# _BIF's last full capacity, _BST's rate and remaining capacity — time to full = (full - remaining) / rate.
asl = open(os.path.join(os.environ["K1_TESTY_PY"], "..", "acpi", "bateria.asl"), encoding="utf-8").read()
pelna = int(re.search(r"_BIF.*?Package \(0x0D\) \{ \w+, \d+, (\d+),", asl, re.S).group(1))
moc, zostalo = (int(v) for v in re.search(r"_BST.*?\{ @STAN@, (\d+), (\d+),", asl, re.S).groups())
procent, do_pelna = round(100 * zostalo / pelna), round((pelna - zostalo) * 60 / moc)
waty = f"{moc // 1000},{(moc % 1000 + 50) // 100}"
OKIENKO = r"K1-GUI: okienko paska: Bateria w (\d+),(\d+) \((\d+)x(\d+)\)"
bat = czekaj(r"K1-GUI: bateria na pasku: środek (\d+),16", czas=10)
okienko = None
if bat:
    bx = int(bat.group(1))
    klik(bx, 16)
    okienko = czekaj(OKIENKO, czas=20)
    o = czekaj(r"K1-GUI: okienko baterii: (\d+)%, (\S+), (\S+) W, do pełna (\d+) min", czas=20)
    sprawdz(f"okienko baterii: {procent} %, ładowanie, {waty} W, do pełna {do_pelna} min (z tablicy ACPI)",
            o and int(o.group(1)) == procent and o.group(2) == "ładowanie" and o.group(3) == waty
            and int(o.group(4)) == do_pelna, o.group(0) if o else "brak")
    sprawdz("nadzorca czyta baterię częściej, póki okienko jest otwarte",
            czekaj(r"K1-NADZORCA: okienko baterii otwarte: odczyt baterii co 5 s", czas=10))
    if okienko:
        ox, oy = int(okienko.group(1)), int(okienko.group(2))
        jasne = lambda c: min(c) > 225  # the popover's background in the light theme
        w, h, px = q.zrzut("okienko-baterii", gdy=lambda w, h, px: jasne(px(ox + 6, oy + 100)))
        sprawdz("zrzut: okienko baterii pod ikoną baterii", jasne(px(ox + 6, oy + 100)) and
                ox <= bx <= ox + int(okienko.group(3)), str(px(ox + 6, oy + 100)))
    n_z = ile(r"okienko baterii zamknięte")
    klik(bx, 16)
    sprawdz("drugie kliknięcie baterii zamyka okienko (nadzorca wraca do odczytu co 15 s)",
            czekaj(r"K1-NADZORCA: okienko baterii zamknięte: odczyt baterii co 15 s", od=n_z, czas=10))
else:
    sprawdz("bateria na górnym pasku: położenie w logu", False)

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
        klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + q.wiersz_sekcji(2) * int(sek.group(3)))
        WZB = r"K1-USTAWIENIA: bateria: jest, 73%, próbek (\d+) co 30 s; wykres (\d+),(\d+) (\d+)x(\d+)"
        b = czekaj(WZB, czas=20)
        sprawdz("Ustawienia → Bateria: stan od nadzorcy (73 %, próbki co 30 s)", b, "brak")
        if b and bat:  # 0.11.8 step 3: the popover's button "Ustawienia baterii" (the window shows Wygląd before it)
            klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + q.wiersz_sekcji(0) * int(sek.group(3)))
            n_o, n_b = ile(OKIENKO), ile(WZB)
            klik(int(bat.group(1)), 16)
            okienko = czekaj(OKIENKO, od=n_o, czas=20)
            if okienko:
                klik(int(okienko.group(1)) + int(okienko.group(3)) // 2, int(okienko.group(2)) + 174)
            sprawdz("„Ustawienia baterii” w okienku → Ustawienia na sekcji Bateria",
                    okienko and czekaj(r"K1-NADZORCA: otwieram Ustawienia → Bateria", czas=20) and czekaj(WZB, od=n_b, czas=20)
                    and czekaj(r"K1-NADZORCA: okienko baterii zamknięte", czas=10))
        if b:
            wx, wy, ww, wh = (int(b.group(i)) for i in range(2, 6))
            akcent = lambda c: c[0] > 150 and c[1] < 140 and c[2] < 110 and c[0] > c[1] > c[2]
            linia_w = lambda px: sum(1 for x in range(ux + wx + 8, ux + wx + ww - 8) for y in range(uy + wy, uy + wy + wh)
                                     if akcent(px(x, y)))
            # a sample every 30 s: the chart spans the width once there are two (the window redraws by itself)
            w, h, px = q.zrzut("bateria", gdy=lambda w, h, px: linia_w(px) >= (ww - 16) * 2 * 0.8, czas=90)
            linia = linia_w(px)
            sprawdz(f"zrzut: wykres naładowania na całą szerokość ({linia} pikseli linii)", linia >= (ww - 16) * 2 * 0.8,
                    str(linia))
            n_b = ile(WZB)
            klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + q.wiersz_sekcji(2) * int(sek.group(3)))
            b = czekaj(WZB, od=n_b, czas=20)
            sprawdz("Ustawienia → Bateria: historia od nadzorcy (co najmniej 2 próbki)", b and int(b.group(1)) >= 2,
                    b.group(0) if b else "brak")
    else:
        sprawdz("Ustawienia otwarte z dolnego paska", False, f"okno: {bool(u)}, układ: {bool(sek)}")
else:
    sprawdz("położenia pulpitu w logu", False)

t = tekst()
sprawdz("brak 'Kernel panic' i błędów usług", "Kernel panic" not in t and "BŁĄD" not in t)
sprawdz("brak odrzuconych i zgubionych zdarzeń", re.findall(r"odrzucone (\d+)\)", t)[-1:] == ["0"] and "pominięto" not in t)
# 0.8.6: Start → Wyłącz on a laptop — init stops nadzorca first, so the battery history is saved while skarbiec still
# runs (until 0.8.5 both were stopped at the same moment and that save was lost), then the power goes off
MENU = r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);"
m = None
if g and bat:  # 0.11.8 step 3: the Start menu closes the battery's popover (the menu stays open for Wyłącz below)
    n_o, n_z, n_m = ile(OKIENKO), ile(r"okienko baterii zamknięte"), ile(MENU)
    klik(int(bat.group(1)), 16)
    otw = czekaj(OKIENKO, od=n_o, czas=20)
    if otw:
        klik(int(g.group(1)), int(g.group(2)))
        m = czekaj(MENU, od=n_m, czas=30)
    sprawdz("menu Start zamyka okienko baterii",
            otw and m and czekaj(r"K1-NADZORCA: okienko baterii zamknięte", od=n_z, czas=20))
if g:
    if not m:
        n_m = ile(MENU)
        klik(int(g.group(1)), int(g.group(2)))
        m = czekaj(MENU, od=n_m, czas=30)
    if m:
        klik(int(m.group(4)), int(m.group(5)))
    sprawdz("Start → Wyłącz: historia baterii zapisana przed wyłączeniem, potem system się wyłącza",
            m and czekaj(r"K1-NADZORCA: historia baterii zapisana przed wyłączeniem", czas=30)
            and czekaj(r"K1: wyłączanie zasilania", czas=30) and czekaj(r"reboot: Power down", czas=30))
q.koniec(wylacz=False)
PY
zamknij_qemu "$MON" $QPID

# M5d: the USB image (k1-<version>-usb.img) booted by UEFI firmware from a USB stick, twice: the first
# boot creates the encrypted vault on the empty K1-DANE partition, the second one opens it.
echo "[k1] test M5d: obraz na pendrive — start przez UEFI, magazyn na partycji K1-DANE, ponowny start"
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-test-usb.img # 0.13.0 step 4 (ADR 0059): k1-test.efi — the log on the serial port
if [ -f "$USBIMG" ]; then
    cp --sparse=always "$USBIMG" "$TESTY/m5-pendrive.img"
    truncate -s +512M "$TESTY/m5-pendrive.img" # 0.8.0 step 5: a stick larger than the image
    for n in 1 2; do
        PLOG=$TESTY/m5-pendrive$n.log PMON=$TESTY/m5-pendrive$n.sock
        rm -f "$PLOG" "$PMON"
        "$K1_REPO/scripts/uruchom-qemu.sh" --pendrive "$TESTY/m5-pendrive.img" --log "$PLOG" --monitor "$PMON" --czas "$CZAS" \
            --qemu "-acpitable file=$TESTY/bateria-ladowanie.aml" &
        PPID_=$!
        # 0.6.0: the battery history of the first boot is kept in skarbiec and read by the second one
        if [ $n -eq 1 ]; then OCZ="K1-SKARBIEC: utworzono nowy magazyn na pustej partycji K1-DANE" BAT="historia baterii zapisana (poprzednich uruchomień: 0)"
        else OCZ="K1-SKARBIEC: magazyn otwarty" BAT="K1-NADZORCA: historia baterii: 1 poprzednich uruchomień"; fi
        # 0.8.0 step 5: the first start grows the empty K1-DANE (64 MiB) by the 512 MiB the stick has more; never again
        ROS="K1-NADZORCA: K1-DANE powiększona do końca pendrive'a: 64 → 576 MiB"
        t=0
        jest() { log_k1 "$PLOG" | grep -qF "$1"; } # a kernel message may sit in the middle of a K1 line
        OBR="K1-NADZORCA: obraz strefy przegladarka: miejsce a na sd" # 0.10.9 step 4: the zone image from slot A of the stick
        while [ $t -lt "$CZAS" ] && ! { jest "$OCZ" && jest "K1-GUI: pulpit gotowy" && jest "$BAT" && jest "$OBR"; }; do
            kill -0 $PPID_ 2>/dev/null || break; sleep 1; t=$((t + 1))
        done
        zamknij_qemu "$PMON" $PPID_
        if jest "$OCZ" && jest "K1-GUI: pulpit gotowy" && jest "partycja danych K1-DANE otwarta" && jest "$BAT" && jest "$OBR" \
            && { [ $n -eq 1 ] && jest "$ROS" || { [ $n -eq 2 ] && ! jest "powiększona"; }; } \
            && ! jest "Kernel panic" && ! jest "BŁĄD"; then
            echo "  [OK]    start $n z pendrive'a: pulpit, $( [ $n -eq 1 ] && echo 'K1-DANE powiększona do końca pendrive'"'"'a (64 → 576 MiB), nowy zaszyfrowany magazyn, historia baterii zapisana' || echo 'magazyn z poprzedniego startu otwarty (K1-DANE już nie rośnie), historia baterii z poprzedniego startu'), obraz przeglądarki z miejsca A sprawdzony (${t} s)"
        else
            echo "  [BŁĄD]  start $n z pendrive'a (log: $PLOG)"; r=1
        fi
    done
    # the table K1 wrote, read by another program: valid, K1-DANE up to the last MiB of the stick
    if sfdisk --verify "$TESTY/m5-pendrive.img" >/dev/null 2>&1 &&
        sfdisk -d "$TESTY/m5-pendrive.img" | grep -q 'start=     3278848, size=     1179648, .*name="K1-DANE"'; then
        echo "  [OK]    tablica partycji po powiększeniu poprawna (sfdisk): K1-DANE do ostatniego MiB pendrive'a"
    else
        echo "  [BŁĄD]  tablica partycji po powiększeniu:"; sfdisk -d "$TESTY/m5-pendrive.img" 2>&1 | sed 's/^/            | /'; r=1
    fi
else
    echo "  [BŁĄD]  brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"; r=1
fi
if [ $r -eq 0 ]; then info "M5: wszystko zaliczone (zrzuty: $TESTY/m5-*.png)"; else echo "[k1] M5: BŁĄD (logi: $TESTY/m5-*.log)"; fi
exit $r

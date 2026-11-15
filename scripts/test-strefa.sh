#!/bin/sh
# Test strefy próbnej (0.10.0 krok 1, ADR 0042; powtarzalny): obraz testowy z małym dyskiem o numerze seryjnym
# „k1-test-strefy” (znak dla programu testowego: /proc/cmdline go nie widzi) — program testowy prosi
# nadzorcę o strefę próbną (mikro-maszyna z jądrem strefy i programem gosc), a ten skrypt patrzy z zewnątrz:
#   1. jądro K1 ma KVM (w emulacji QEMU udaje wirtualizację AMD — ADR 0042 T10), strefa startuje, okno w ramce
#   2. ramka (obwódka 3 px w kolorze strefy) i znaczek w rogu rysuje ekran K1; w środku wzór gościa (zrzut)
#   3. kliknięcie i klawisz docierają do gościa (licznik w logu gościa, zmiana wzoru); bez fokusu klawisz nie dociera
#   4. kursor na znaczku: plakietka z nazwą strefy (zrzut); przeciągnięcie znaczka przesuwa okno
#   5. okno K1 (Zegar) na wierzchu zasłania strefę, ramka strefy zostaje tam, gdzie jej nie zasłania; ciemny motyw
#   6. „zamknij” w plakietce: gość dostaje „zamknij” (vsock), kończy się, maszyna znika, okno też
#   7. NEGATYWNE: złośliwy gość (deskryptor poza pamięcią, pętla deskryptorów, odczyt ponad limit, zapis do obrazu
#      tylko do odczytu) — kończy się tylko ta strefa, z powodem w logu; K1 działa dalej
#   8. NEGATYWNE: procesor bez wirtualizacji — nadzorca mówi „brak wirtualizacji”, nic nie startuje, nadzorca żyje
#   9. obraz produkcyjny nie ma strefy próbnej (tylko jądro strefy)
# Dowody: build/testy/strefa.log, strefa-bez-wirt.log, zrzuty build/testy/strefa-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1200}
mkdir -p "$TESTY"; rm -f "$TESTY"/strefa*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
ZNAK=$TESTY/strefa-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
# CI with KVM: K1 needs nested virtualization from the runner (S7); without it the zone test runs emulated
EMULACJA=
if [ -w /dev/kvm ]; then
    grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested || EMULACJA=--emulacja
fi

cat > "$K1_BUDOWA/strefa.py" <<'PY'
import os, re, subprocess, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, znak, emulacja = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6:]
ZNAK = f"-drive if=none,id=znak,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-strefy,addr=0x1e"
log, mon = f"{out}/strefa.log", f"{out}/strefa.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--pamiec", "1024", "--qemu", ZNAK] + emulacja)
q = Maszyna(log, mon, out, limit, "strefa")
czekaj, sprawdz = q.czekaj, q.sprawdz
PIASEK = (0xA8, 0x83, 0x4A)   # K1_STREFY: the test zone's frame
TLO_GOSCIA = (0x20, 0x30, 0x4A)
ZIELONY, POMARANCZOWY, BIALY = (0x2E, 0x8B, 0x57), (0xD2, 0x69, 0x1E), (0xFF, 0xFF, 0xFF)


def blisko(c, w, tol=24):
    return all(abs(a - b) <= tol for a, b in zip(c, w))


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=limit * 0.4)
# The kernel's KVM line comes long before the desktop. It is read from the raw bytes of the log: q.tekst() drops a
# kernel message printed in the middle of a K1 line (WTRACENIE), and with KVM in CI this one landed there once (run 77).
sprawdz("jądro K1 z KVM: wirtualizacja w maszynie testowej (w emulacji: AMD z zagnieżdżonym stronicowaniem)",
        re.search(rb"kvm_amd: Nested Paging enabled|kvm_intel|kvm: ", open(log, "rb").read()))
ax, ay, krok = (int(g.group(i)) for i in (3, 4, 5)) if g else (0, 0, 0)
o = czekaj(r"K1-EKRAN: okno (\d+) \(strefa\) (\d+)x(\d+) w (\d+),(\d+)", czas=180)
nr, w_, h_, x, y = (int(o.group(i)) for i in range(1, 6)) if o else (0, 0, 0, 0, 0)
sprawdz("nadzorca uruchamia strefę próbną (VMM w klatce), ekran pokazuje jej okno",
        czekaj(r"K1-NADZORCA: strefa 1 \(Strefa próbna\): pid \d+, uid 300, pamięć 128 MiB", czas=60) and o)
sprawdz("gość startuje: jądro strefy, gosc, kanał sterujący (vsock) „gotowa”, pierwszy obraz",
        czekaj(r"K1-STREFA: gość gotowy \(kanał sterujący\)", czas=240) and
        czekaj(r"K1-STREFA: pierwszy obraz gościa", czas=120))
start = czekaj(r"K1-STREFA: strefa 1 \(Strefa próbna\): maszyna działa")
gotowy = czekaj(r"K1-GOSC: gotowa: urządzenia wejścia 2, kanał sterujący jest", czas=30)
sprawdz("gość: dwa urządzenia wejścia (klawiatura, wskaźnik) i kanał sterujący", gotowy)


def ramka_i_wzor(w, h, px):
    return (o and blisko(px(x - 2, y + h_ // 2), PIASEK) and blisko(px(x + w_ + 1, y + h_ // 2), PIASEK) and
            blisko(px(x + w_ // 2, y + h_ // 6), TLO_GOSCIA) and blisko(px(x + w_ // 2, y + h_ // 2), ZIELONY) and
            blisko(px(x + w_ - 6, y + 6), (0xFF, 0xD7, 0x00)))


w, h, px = q.zrzut("1-okno", gdy=ramka_i_wzor)
sprawdz("okno strefy: obwódka w kolorze strefy rysowana przez ekran, w środku wzór gościa (zrzut strefa-1-okno)",
        ramka_i_wzor(w, h, px), str((px(x - 2, y + h_ // 2), px(x + w_ // 2, y + h_ // 2))) if o else "brak okna")
zx, zy = x - 8 + 15, y - 8 + 15  # the mark (strefy.c: 30 px, 8 px out of the corner)
sprawdz("znaczek strefy w lewym górnym rogu, nad treścią (kolor obwódki przy jasnym motywie — strefa jednorazowa)",
        o and not blisko(px(zx, zy + 12), TLO_GOSCIA))

kx, ky = x + w_ // 3, y + h_ // 4
q.klik(kx, ky)
sprawdz("kliknięcie w okno strefy dociera do gościa (wskaźnik virtio-input)",
        czekaj(r"K1-GOSC: wejście: klawisze 0, kliknięcia 1", czas=60))
w, h, px = q.zrzut("2-klikniecie", gdy=lambda w, h, px: blisko(px(kx - 7, ky - 7), BIALY))  # beside the cursor's tip
sprawdz("gość rysuje biały kwadrat tam, gdzie kliknięto (zrzut strefa-2-klikniecie)", blisko(px(kx - 7, ky - 7), BIALY))
q.pisz("a")
sprawdz("klawisz dociera do gościa przy fokusie (licznik, nigdy kod klawisza w logu)",
        czekaj(r"K1-GOSC: wejście: klawisze 1, kliknięcia 1", czas=60))
w, h, px = q.zrzut("3-klawisz", gdy=lambda w, h, px: blisko(px(x + w_ // 2, y + h_ // 2), POMARANCZOWY))
sprawdz("gość zmienia pas po klawiszu (zrzut strefa-3-klawisz)", blisko(px(x + w_ // 2, y + h_ // 2), POMARANCZOWY))

PLAK = r"K1-EKRAN: strefa: plakietka okna {} \(minimalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)".format(nr)
n = q.ile(PLAK)
q.idz(zx, zy)
p = czekaj(PLAK, od=n, czas=30)
w, h, px = q.zrzut("4-plakietka", gdy=lambda w, h, px: p and not blisko(px(x + 120, zy), TLO_GOSCIA))
sprawdz("kursor na znaczku: plakietka z nazwą strefy nad oknem, rysowana przez K1 (zrzut strefa-4-plakietka)",
        p and not blisko(px(x + 120, zy), TLO_GOSCIA))
PRZES = r"K1-EKRAN: okno {} przesunięte do (\d+),(\d+)".format(nr)
q.przeciagnij(zx, zy, zx + 60, zy + 40)
m = czekaj(PRZES, czas=30)
sprawdz("przeciągnięcie znaczka przesuwa okno strefy", m and int(m.group(1)) == x + 60 and int(m.group(2)) == y + 40,
        m.group(0) if m else "")
if m:
    x, y = int(m.group(1)), int(m.group(2))
    zx, zy = x - 8 + 15, y - 8 + 15

# the bottom bar: the zone's window among the running ones; minimise from the badge, back from the bar
pk = czekaj(r"K1-GUI: pasek zmieniony: Start \d+,\d+, aplikacje od (\d+),(\d+) co (\d+), dźwięk \d+,\d+ \(pasek: ([^)]*)\)",
            czas=30)
sprawdz("dolny pasek: okno strefy wśród działających (ikona strefy)", pk and pk.group(4).endswith("Strefa próbna"),
        pk.group(4) if pk else "")
n = q.ile(PLAK)
q.idz(zx + 40, zy + 60)
q.idz(zx, zy)
p = czekaj(PLAK, od=n, czas=30)
if p:
    q.klik(int(p.group(1)), int(p.group(2)))
sprawdz("„minimalizuj” w plakietce chowa okno strefy", p and czekaj(r"K1-EKRAN: okno {} zminimalizowane".format(nr), czas=30))
if pk:
    elementy = pk.group(4).split(", ")
    przypiete = next((i for i, e in enumerate(elementy) if e.startswith("| ")), len(elementy))
    q.klik(int(pk.group(1)) + (len(elementy) - 1) * int(pk.group(3)) + (9 if przypiete < len(elementy) else 0),
           int(pk.group(2)))
sprawdz("kliknięcie strefy na pasku przywraca jej okno", czekaj(r"K1-EKRAN: okno {} przywrócone".format(nr), czas=30))

# a K1 window on top: Zegar (the first application of the bottom bar — where it is now: the zone's item moved the
# centred bar)
pk = czekaj(r"K1-GUI: (?:pasek zmieniony|pulpit gotowy): Start \d+,\d+, aplikacje od (\d+),(\d+) co (\d+)", czas=10)
if pk:
    ax, ay, krok = (int(pk.group(i)) for i in (1, 2, 3))
q.klik(ax, ay)
zg = czekaj(r"K1-EKRAN: okno (\d+) \(zegar\) (\d+)x(\d+) w (\d+),(\d+)", czas=120)
if zg:
    gw, gh, gx, gy = (int(zg.group(i)) for i in (2, 3, 4, 5))
    px_x, px_y = max(x, gx) + 20, max(y, gy) + 60  # inside both rectangles


def zegar_na_wierzchu(w, h, px):
    return zg and not blisko(px(px_x, px_y), TLO_GOSCIA) and not blisko(px(px_x, px_y), ZIELONY) and \
        not blisko(px(px_x, px_y), POMARANCZOWY)


w, h, px = q.zrzut("5-zegar-na-wierzchu", gdy=zegar_na_wierzchu)
sprawdz("okno K1 (Zegar) na wierzchu zasłania treść strefy (zrzut strefa-5-zegar-na-wierzchu)", zegar_na_wierzchu(w, h, px))
n = q.ile(r"K1-GOSC: wejście: klawisze")
q.pisz("b")
sprawdz("NEGATYWNY: klawisz przy fokusie Zegara nie trafia do strefy", q.cisza(r"K1-GOSC: wejście: klawisze", 15))

# the dark theme: Ustawienia → Wygląd → Ciemny (as in the other tests)
q.klik(ax + 2 * krok, ay)
u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=60)
sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+); przyciski od (\d+),(\d+) \(szer\. (\d+)", czas=30)
if u and sk:
    ux, uy = int(u.group(4)), int(u.group(5))
    q.klik(ux + int(sk.group(1)), uy + int(sk.group(2)))
    q.klik(ux + int(sk.group(4)) + int(sk.group(6)) * 3 // 4, uy + int(sk.group(5)) + 10)
sprawdz("ciemny motyw", czekaj(r"K1-EKRAN: ustawienie 1=1", czas=60))
# the zone's window to the front: a click on its content (focus), the frame then in the full colour
n = q.ile(r"K1-GOSC: wejście: klawisze 1, kliknięcia 2")
q.klik(x + w_ - 40, y + h_ - 40)
czekaj(r"K1-GOSC: wejście: klawisze 1, kliknięcia 2", czas=60)
w, h, px = q.zrzut("6-ciemny", gdy=lambda w, h, px: blisko(px(x - 2, y + h_ // 2), PIASEK))
sprawdz("ciemny motyw: obwódka strefy w jej kolorze, znaczek nad treścią (zrzut strefa-6-ciemny)",
        blisko(px(x - 2, y + h_ // 2), PIASEK) and not blisko(px(zx, zy + 12), TLO_GOSCIA))

# closing through the badge
n = q.ile(PLAK)
q.idz(x + 60, y + 60)
q.idz(zx, zy)
p = czekaj(PLAK, od=n, czas=30)
if p:
    q.klik(int(p.group(3)), int(p.group(4)))
sprawdz("„zamknij” w plakietce: VMM prosi gościa (vsock), gość się zamyka, maszyna kończy się, okno znika",
        p and czekaj(r"K1-STREFA: zamykam strefę na prośbę ekranu", czas=30) and
        czekaj(r"K1-STREFA: gość się zamyka", czas=60) and
        czekaj(r"K1-STREFA: strefa 1: koniec — gość zakończył pracę \(reset\)", czas=60) and
        czekaj(r"K1-EKRAN: okno {} zamknięte".format(nr), czas=30) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 0\)", czas=30))

POWODY = {1: "deskryptor poza pamięcią gościa", 2: r"pętla deskryptorów \(łańcuch dłuższy niż kolejka\)",
          3: "virtio-blk: odczyt dłuższy niż limit", 4: "virtio-blk: zapis do obrazu tylko do odczytu"}
OPISY = {1: "deskryptor poza pamięcią", 2: "pętla deskryptorów", 3: "odczyt ponad limit", 4: "zapis do obrazu tylko do odczytu"}
for t in range(1, 5):
    sprawdz(f"NEGATYWNY: złośliwy gość ({OPISY[t]}) — kończy się tylko ta strefa, powód w logu",
            czekaj(rf"K1-TEST: strefy: strefa próbna uruchomiona \(tryb {t}\)", czas=120) and
            czekaj(rf"K1-STREFA: strefa 1: koniec — {POWODY[t]}", czas=180) and
            czekaj(rf"K1-TEST: strefy: strefa skończyła się \(tryb {t}\)", czas=60))
sprawdz("K1 działa dalej po złośliwych gościach: nadzorca, ekran i pulpit bez awarii",
        czekaj(r"K1-TEST: strefy: koniec scenariusza", czas=60) and
        not re.search(r"K1-NADZORCA: (ekran|gui|nadzorca).*zakończon", q.tekst()))
t_ = q.tekst()
sprawdz("brak zabronionych wywołań i paniki; strefa nie otwiera plików ani gniazd (klatka: jej log bez SIGSYS)",
        "zabronione wywołanie" not in t_ and "Kernel panic" not in t_)
if start and gotowy:
    print(f"  [info]  czas startu strefy (od maszyny do „gotowa”): w logu {start.group(0)[:60]}", flush=True)
q.monitor("quit")
qemu.wait()

# ---- a processor without virtualization ----
log2, mon2 = f"{out}/strefa-bez-wirt.log", f"{out}/strefa-bez-wirt.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log2, "--monitor", mon2, "--czas", str(int(limit)),
                         "--pamiec", "1024", "--qemu", ZNAK, "--bez-wirtualizacji"] + emulacja)
q2 = Maszyna(log2, mon2, out, limit, "strefa-bez-wirt")
q2.wyniki = q.wyniki
q2.sprawdz("NEGATYWNY: bez wirtualizacji nadzorca mówi „brak wirtualizacji”, strefa nie startuje, nadzorca żyje",
           q2.czekaj(r"K1-NADZORCA: strefa 1: brak wirtualizacji", czas=limit * 0.5) and
           q2.czekaj(r"K1-TEST: strefy: brak wirtualizacji — strefa nie ruszyła", czas=60))
ok = q2.czekaj(r"K1-GUI: strefa 1 nie ruszyła \(powód 1\): okienko", czas=30)
q2.zrzut("7-bez-wirtualizacji")
q2.sprawdz("pulpit mówi, co zrobić: okienko „Strefa próbna nie może ruszyć” z radą o BIOS-ie (zrzut "
           "strefa-bez-wirt-7-bez-wirtualizacji)", ok)
q2.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test strefy próbnej (0.10.0): KVM, VMM w klatce, okno w zaufanej ramce, wejście, złośliwy gość, brak wirtualizacji"
r=0
if unsquashfs -l "$K1_WYNIK/rdzen.squashfs" 2>/dev/null | grep -q 'strefy/probna'; then
    echo "  [BŁĄD]  obraz produkcyjny ma strefę próbną"; r=1
else
    echo "  [OK]    obraz produkcyjny bez strefy próbnej (jest tylko jądro strefy)"
fi
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/strefa.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$ZNAK" $EMULACJA || r=1
if [ $r -eq 0 ]; then info "strefa: wszystko zaliczone"; else echo "[k1] strefa: BŁĄD (log: $TESTY/strefa.log)"; fi
exit $r

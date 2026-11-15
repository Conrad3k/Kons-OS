#!/bin/sh
# Test przeglądarki w strefie (0.10.0 krok 2, ADR 0043; powtarzalny): obraz produkcyjny, K1 z kartą sieciową w sieci QEMU
# (-netdev user: router 10.0.2.2 = ten komputer, adres od DHCP), na „routerze” serwer strony testowej w Pythonie:
#   1. Start → Przeglądarka: nadzorca uruchamia strefę „Ogólna” (VMM w klatce), w oknie w ramce koloru sesji Firefox
#      (zrzut przegladarka-1-okno: pasek kart i adresu)
#   2. adres strony testowej wpisany z klawiatury (virtio-input → kompozytor → Firefox) → serwer ją wysyła przez
#      łącznik w VMM (SOCKS5 od przekaźnika w strefie) i gniazdo od nadzorcy; strona w oknie (zrzut: duże pola koloru)
#   3. tekst wpisany w pole formularza dociera do serwera (pisanie w polach działa)
#   4. NEGATYWNE: adres w sieci domowej, który nie jest routerem (10.0.2.3) — nadzorca odmawia (D3), przeglądarka
#      pokazuje błąd, strefa i K1 żyją
#   5. w logu K1 żadnego adresu strony ani wpisanego tekstu (niezmiennik 8) — tylko liczniki
#   6. zamknięcie okna: maszyna kończy się (sesja jednorazowa: nic nie zostaje), liczniki połączeń w logu
# Nazwy stron (DNS) — tylko testy na hoście (test_lacznik, fuzz_lacznik): DNS sieci QEMU prowadzi do resolvera
# komputera, którego kontener testów nie ma (plan 0.10.0, S17).
# Bez KVM (emulacja QEMU, kontener Claude) — scenariusz SKRÓCONY do kroku 1 i tak opisany w wyniku (plan 0.10.0, S20,
# TESTY.md): Firefox w gościu emulowanego SVM bywa zatrzymany przez błędy emulacji (podwójny błąd przy asynchronicznych
# błędach stron, ADR 0043). Pełny scenariusz — CI z zagnieżdżonym KVM (etykieta „testy” na PR-ze).
# Dowody: build/testy/przegladarka.log, zrzuty build/testy/przegladarka-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-2400}
mkdir -p "$TESTY"; rm -f "$TESTY"/przegladarka*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
# KVM with nested virtualization (CI): the full scenario; otherwise (the container: no KVM at all) — emulated, shortened
EMULACJA=--emulacja
if [ -w /dev/kvm ] && grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested; then
    EMULACJA=
fi

cat > "$K1_BUDOWA/przegladarka.py" <<'PY'
import base64, http.server, os, re, socket, subprocess, sys, threading, zlib
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, emulacja = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5:]
TEKST = "kontestk1"
odebrane = []
STRONA = (b"<!doctype html><html><head><meta charset=utf-8><title>K1</title></head>"
          b"<body style='margin:0;background:#ff8800'><div style='height:200px;background:#1e64c8'></div>"
          b"<form action=/wyslij><input name=t autofocus style='font-size:30px;width:400px'></form></body></html>")


class Serwer(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        odebrane.append(self.path)
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(STRONA)))
        self.end_headers()
        self.wfile.write(STRONA)

    def log_message(self, *a):
        pass


serwer = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Serwer)
port = serwer.server_address[1]
threading.Thread(target=serwer.serve_forever, daemon=True).start()
# QEMU's user network: a connection to the router 10.0.2.2 reaches this computer's 127.0.0.1 (our server's port)
SIEC = "-netdev user,id=n0 -device e1000e,netdev=n0"
log, mon = f"{out}/przegladarka.log", f"{out}/przegladarka.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--pamiec", "3072", "--qemu", SIEC] + emulacja)
q = Maszyna(log, mon, out, limit, "przegladarka")
czekaj, sprawdz = q.czekaj, q.sprawdz
SZALWIA = (0x5F, 0x8A, 0x6A)  # K1_STREFY: Ogólna
POMARANCZ, NIEBIESKI = (0xFF, 0x88, 0x00), (0x1E, 0x64, 0xC8)


def blisko(c, w, tol=24):
    return all(abs(a - b) <= tol for a, b in zip(c, w))


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+)", czas=limit * 0.3)
sprawdz("K1 dostaje adres od routera (DHCP z serwerem DNS — opcja 6)",
        czekaj(r"K1-NADZORCA: sieć: połączono \(", czas=240))
q.klik(int(g.group(1)), int(g.group(2)))
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);", czas=60)
mx, my, co, sy = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(5))
wiersze = (sy - my + co // 2 - 2 - 36) // co  # the rows of the menu; the browser is the last one (k1_uslugi.h)
q.klik(mx, my + (wiersze - 1) * co)
sprawdz("menu Start → Przeglądarka: gui prosi nadzorcę o strefę „Ogólna”", czekaj(r"K1-GUI: uruchamiam: Przeglądarka"))
o = czekaj(r"K1-EKRAN: okno (\d+) \(strefa\) (\d+)x(\d+) w (\d+),(\d+)", czas=180)
nr, w_, h_, x, y = (int(o.group(i)) for i in range(1, 6)) if o else (0, 0, 0, 0, 0)
sprawdz("VMM strefy 2 w klatce (uid 301, 1536 MiB), maszyna działa",
        czekaj(r"K1-NADZORCA: strefa 2 \(Przeglądarka\): pid \d+, uid 301, pamięć 1536 MiB", czas=60) and
        czekaj(r"K1-STREFA: strefa 2 \(Przeglądarka\): maszyna działa", czas=240))
sprawdz("gość: kanał sterujący „gotowa” (gosc uruchomił przekaźnik, kompozytor i przeglądarkę)",
        czekaj(r"K1-STREFA: gość gotowy \(kanał sterujący\)", czas=300))
if emulacja:
    print("  [SKRÓT] emulacja bez KVM: dalsze kroki (Firefox, strona, formularz, odmowa D3, logi) — tylko z KVM (CI)",
          flush=True)
    q.monitor("quit")
    qemu.wait()
    sys.exit(0 if all(q.wyniki) else 1)


def firefox(w, h, px):  # the frame in the session's colour, Firefox's address field (not K1's "Uruchamiam…": 250,249,246)
    return (o and blisko(px(x - 2, y + h_ // 2), SZALWIA) and blisko(px(x + w_ // 2, y + 64), (242, 242, 242), 4)
            and blisko(px(x + w_ // 3, y + 20), (250, 250, 251), 4))


w, h, px = q.zrzut("1-okno", gdy=firefox, czas=900)
sprawdz("okno przeglądarki w ramce w kolorze sesji, pasek kart i adresu (zrzut przegladarka-1-okno)",
        firefox(w, h, px))


def wpisz(tekst):
    nazwy = {".": "dot", ":": "shift-semicolon", "/": "slash"}
    q.pisz(*[nazwy.get(c, c) for c in tekst])


def strona(w, h, px):
    return blisko(px(x + w_ // 2, y + h_ - 40), POMARANCZ) and blisko(px(x + w_ // 2, y + 160), NIEBIESKI, 40)


obrazki = []


def adres(nazwa):  # CI's diagnosis: does the typed text reach Firefox's address bar (dark pixels of text in it)?
    w, h, px = q.zrzut(nazwa)
    ciemne = sum(1 for xx in range(x + 80, x + w_ - 80, 2) for yy in range(y + 52, y + 78, 2) if max(px(xx, yy)) < 110)
    # the top of the window (the test page only: no address of the user's), half size, 16 greys, zlib + base64
    sz, wy = w_ // 2, 100
    b = bytes(min(15, sum(px(x + 2 * i, y + 2 * j)) // 48) for j in range(wy) for i in range(sz))
    obrazki.append(f"{nazwa} {sz}x{wy} " + base64.b64encode(zlib.compress(b, 9)).decode())
    return f"{nazwa}: ciemne piksele w pasku adresu {ciemne}, środek okna {px(x + w_ // 2, y + h_ // 2)}"


opisy = [adres("2a-przed")]
q.klik(x + w_ // 2, y + 64)  # the address bar
opisy.append(adres("2b-klik"))
wpisz(f"10.0.2.2:{port}/")
opisy.append(adres("2c-wpisane"))
q.pisz("ret")
w, h, px = q.zrzut("2-strona", gdy=strona, czas=120)
if not strona(w, h, px):  # the diagnosis: the same through the keyboard only (Ctrl+L), not the click
    q.pisz("ctrl-l")
    opisy.append(adres("2d-ctrl-l"))
    wpisz(f"10.0.2.2:{port}/")
    opisy.append(adres("2e-wpisane-po-ctrl-l"))
    q.pisz("ret")
    w, h, px = q.zrzut("2-strona", gdy=strona, czas=60)
    opisy.append(f"po Ctrl+L strona {'jest' if strona(w, h, px) else 'nie ma'}, serwer dostał {len(odebrane)} żądań")


def pokaz():  # what the zone and nadzorca said (counts, reasons, the watchman — never an address)
    for l_ in [l for l in q.tekst().splitlines() if re.search(r"K1-STREFA|strefa 2", l)][-25:]:
        print("            | " + l_[:240], flush=True)
    print(f"            | serwer dostał: {len(odebrane)} żądań", flush=True)
    for o_ in opisy:
        print("            | " + o_, flush=True)
    for o_ in obrazki:
        print("            | obrazek " + o_, flush=True)


if not strona(w, h, px):
    pokaz()
sprawdz("strona testowa z serwera na routerze otwiera się w strefie (SOCKS5 → łącznik → gniazdo od nadzorcy; zrzut "
        "przegladarka-2-strona)", strona(w, h, px) and "/" in odebrane)
q.klik(x + 200, y + h_ // 2)  # the form's field (autofocus too)
wpisz(TEKST)
q.pisz("ret")
dotarlo = q.czekaj_na(lambda: any(TEKST in p for p in odebrane), czas=120)
sprawdz("tekst wpisany w pole formularza dociera do serwera", dotarlo)

q.klik(x + w_ // 2, y + 64)
n0 = q.ile(r"K1-NADZORCA: strefa 2: pierwsza odmowa połączenia")
wpisz(f"10.0.2.3:{port}/")
q.pisz("ret")
sprawdz("NEGATYWNY: adres w sieci domowej (nie router) — nadzorca odmawia (D3), strefa żyje",
        czekaj(r"K1-NADZORCA: strefa 2: pierwsza odmowa połączenia \(powód 1\)", od=n0, czas=120) and
        "strefa 2: koniec" not in q.tekst())
q.zrzut("3-odmowa")
t_ = q.tekst()
sprawdz("w logu K1 żadnego adresu strony ani wpisanego tekstu — tylko liczniki (niezmiennik 8)",
        TEKST not in t_ and f"10.0.2.2:{port}" not in t_ and "10.0.2.3" not in t_ and "wyslij" not in t_)

# closing: the frame's badge (hover the mark, then its close button) — as test-strefa does
PLAK = r"K1-EKRAN: strefa: plakietka okna {} \(minimalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)".format(nr)
n = q.ile(PLAK)
q.idz(x + 60, y + 60)
q.idz(x - 8 + 15, y - 8 + 15)  # the mark (src/ekran/strefy.c: 30 px, 8 px out of the corner)
p = czekaj(PLAK, od=n, czas=30)
if p:
    q.idz(int(p.group(1)), int(p.group(2)))  # along the badge first: a long move overshoots and the badge closes
    q.klik(int(p.group(3)), int(p.group(4)))
koniec_ = czekaj(r"K1-STREFA: strefa 2: koniec", czas=120)
if not koniec_:
    pokaz()
sprawdz("zamknięcie okna: gość dostaje „zamknij”, przeglądarka kończy się, maszyna znika (sesja jednorazowa)", koniec_)
sprawdz("liczniki połączeń w logu: VMM i nadzorca (bez adresów)",
        czekaj(r"K1-STREFA: połączenia przeglądarki: \d+, nazwy \d+, odmowy \d+", czas=30) and
        czekaj(r"K1-NADZORCA: strefa 2: połączenia \d+, odmowy [1-9]", czas=30))
t_ = q.tekst()
sprawdz("brak zabronionych wywołań i paniki w K1", "zabronione wywołanie" not in t_ and "Kernel panic" not in t_)
q.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test przeglądarki (0.10.0 krok 2): strefa „Ogólna”, Firefox, strona przez łącznik, formularz, odmowa D3"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/przegladarka.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" $EMULACJA || r=1
if [ $r -eq 0 ]; then info "przeglądarka: wszystko zaliczone"; else echo "[k1] przeglądarka: BŁĄD (log: $TESTY/przegladarka.log)"; fi
exit $r

#!/bin/sh
# Test przeglądarki w strefie (0.10.0 krok 2, ADR 0043; powtarzalny): obraz produkcyjny, K1 z kartą sieciową w sieci QEMU
# (-netdev user: router 10.0.2.2 = ten komputer, adres od DHCP), na „routerze” serwer strony testowej w Pythonie:
#   1. Start → Przeglądarka: nadzorca uruchamia strefę „Ogólna” (VMM w klatce), w oknie w ramce koloru sesji Firefox
#      (zrzut przegladarka-1-okno: pasek kart i adresu)
#   2. adres strony testowej wpisany z klawiatury (virtio-input → kompozytor → Firefox), kliknięcie strzałki „→” na
#      końcu paska adresu (ponowienie: adres jeszcze raz, strzałka i podpowiedź „otwórz stronę”) → serwer ją wysyła
#      przez łącznik w VMM (SOCKS5 od przekaźnika w strefie) i gniazdo od nadzorcy; strona w oknie (zrzut: duże pola
#      koloru)
#   3. tekst wpisany w pole formularza dociera do serwera (pisanie w polach działa; strona wysyła pole sama, gdy jest
#      w nim cały tekst)
#   4. NEGATYWNE: strona testowa sama prosi o obrazek z adresu w sieci domowej, który nie jest routerem (10.0.2.3) —
#      jak złośliwa strona, która szuka urządzeń w domu; nadzorca odmawia (D3), strefa i K1 żyją
#   5. w logu K1 żadnego adresu strony ani wpisanego tekstu (niezmiennik 8) — tylko liczniki
#   6. PAMIĘĆ (0.10.0 krok 3, S26): strona testowa zajmuje 1400 MiB w strefie, która ma 1536 MiB (jak ciężkie strony,
#      po których na IdeaPadzie z 4 GB okno przeglądarki znikało) — strona dochodzi do końca, przeglądarka i strefa żyją
#   7. zamknięcie okna: maszyna kończy się (sesja jednorazowa: nic nie zostaje), liczniki połączeń w logu
# BEZ ENTERA (decyzja właściciela 2026-11-15): w CI z KVM Enter w pasku adresu i w polu formularza bywał bez skutku
# (przebiegi 95–103 PR-a #119; klawisz dochodził do Firefoksa — „Enter pressed” w jego dzienniku), a na sprzęcie
# (IdeaPad 110) działa zawsze. Cały dzień szukania nie dał przyczyny po stronie K1 — dlatego test nie naciska Entera
# nigdzie: stronę otwiera kliknięcie strzałki w pasku adresu, formularz wysyła się sam, D3 sprawdza obrazek na stronie.
# Nie wracaj do Entera w tym teście bez nowej decyzji właściciela (plan 0.10.0, krok 2, ustalenie 7; skill kons-dev).
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
import base64, http.server, os, re, socket, subprocess, sys, threading, time, zlib
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, emulacja = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5:]
TEKST = "kontestk1"
odebrane = []
# the page asks for a picture from 10.0.2.3 (the home network, not the router) by itself: D3's refusal without typing
STRONA = ("<!doctype html><html><head><meta charset=utf-8><title>K1</title></head>"
          "<body style='margin:0;background:#ff8800'><div style='height:200px;background:#1e64c8'></div>"
          "<form action=/wyslij><input name=t autofocus style='font-size:30px;width:400px' "
          "oninput=\"if (this.value.indexOf('" + TEKST + "') >= 0) this.form.submit()\"></form>"
          "<img src='http://10.0.2.3:{port}/dom.png' width=1 height=1></body></html>")
# 0.10.0 step 3 (S26): a page that takes PAMIEC_MIB of the zone's 1536 MiB in pieces of 32 MiB (a quarter of each 4 KiB
# random, the rest zeros — about as compressible as a browser's real memory) and says when it holds all of it
PAMIEC_MIB = 1400
STRONA_PAMIEC = ("<!doctype html><html><head><meta charset=utf-8><title>K1</title></head>"
                 "<body style='margin:0;background:#1e64c8'><script>var b = [], n = 0;"
                 "function krok() { if (n >= " + str(PAMIEC_MIB) + ") { document.body.style.background = '#00a050';"
                 " fetch('/pamiec-koniec'); return; } var a = new Uint8Array(32 << 20);"
                 " for (var i = 0; i < a.length; i += 4096) crypto.getRandomValues(a.subarray(i, i + 1024));"
                 " b.push(a); n += 32; if (n % 256 == 0) fetch('/pamiec-postep?n=' + n); setTimeout(krok, 0); }"
                 "setTimeout(krok, 100);</script></body></html>")


class Serwer(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        odebrane.append(self.path)
        b = (STRONA_PAMIEC if self.path == "/pamiec" else STRONA.format(port=self.server.server_address[1])).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

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


def bez_przyciskow(px):  # S28 (step 3): the top right corner of the tab bar is empty — Firefox draws no minimise,
    # maximise and close of its own (strefy/przegladarka/gtk-settings.ini); its close button was right there
    tlo = px(x + w_ // 3, y + 20)
    return all(blisko(px(x + w_ - 30 + i, y + 10 + j), tlo, 12) for i in range(0, 26, 2) for j in range(0, 24, 2))


w, h, px = q.zrzut("1-okno", gdy=firefox, czas=900)
sprawdz("okno przeglądarki w ramce w kolorze sesji, pasek kart i adresu (zrzut przegladarka-1-okno)",
        firefox(w, h, px))
sprawdz("Firefox bez własnych przycisków okna (minimalizuj, powiększ, zamknij) — tylko pasek K1 w kolorze sesji (S28)",
        bez_przyciskow(px), str(px(x + w_ - 16, y + 20)))
sprawdz("pasek tytułu K1 nad oknem przeglądarki: obwódka w kolorze sesji, jasny pasek sesji jednorazowej (krok 3, D1: A)",
        blisko(px(x + w_ // 2, y - 42), SZALWIA) and blisko(px(x + w_ - 60, y - 20), (232, 238, 234), 16),
        str((px(x + w_ // 2, y - 42), px(x + w_ - 60, y - 20))))
for l_ in re.findall(r"K1-STREFA: gość (?:gotowy|: okno przeglądarki)[^\n]*", q.tekst()):  # S30: where the start goes
    print("            | start: " + l_[:200], flush=True)


def zamknij_strefe():  # "zamknij" on K1's title bar above the zone (step 3, D1: sketch A) — as test-strefa does
    pasek = r"K1-EKRAN: strefa: pasek okna {} \(minimalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)".format(nr)
    n = q.ile(pasek)
    q.idz(x + 60, y + 60)
    q.idz(x + w_ // 2, y - 20)  # the bar (K1_STREFA_TYTUL = 40 px above the picture)
    p = czekaj(pasek, od=n, czas=30)
    if p:
        q.klik(int(p.group(3)), int(p.group(4)))
    return czekaj(r"K1-STREFA: strefa 2: koniec", czas=120)


def wpisz(tekst):
    nazwy = {".": "dot", ":": "shift-semicolon", "/": "slash"}
    q.pisz(*[nazwy.get(c, c) for c in tekst])
    time.sleep(1)


def otworz_strone(czas, sciezka="/", gotowe=None):  # no Enter (the note at the top): typed, then a click opens it.
    # First the go arrow at the right end of the address bar; run 109 (PR #124) had the address typed but no list of
    # suggestions under the bar, so a click on "otwórz stronę" alone hit the bookmarks bar. Next tries: the address
    # typed again, then the arrow again and the suggestion. Each try waits for the test page; the last screen returns.
    proby = [("strzałka", []), ("strzałka po ponownym wpisaniu", [(x + 330, y + 107)])]
    for i, (opis, potem) in enumerate(proby):
        if i:
            q.klik(x + w_ // 2, y + 64)  # the address bar again: the text is selected and typed over
            q.pisz("ctrl-a")
            wpisz(f"10.0.2.2:{port}{sciezka}")
        time.sleep(2)
        q.klik(x + w_ - 204, y + 63)  # the go arrow (1024-wide window: 820,63 — the picture in run 109's log)
        for kx, ky in potem:
            time.sleep(2)
            q.klik(kx, ky)  # the suggestion "otwórz stronę", if the list is there
        gotowe = gotowe or strona
        w, h, px = q.zrzut("2-strona" if sciezka == "/" else "5-pamiec", gdy=gotowe, czas=czas)
        if gotowe(w, h, px):
            return w, h, px
        print(f"            | próba {i + 1} ({opis}): strona się nie otworzyła", flush=True)
    return w, h, px


def strona(w, h, px):
    return blisko(px(x + w_ // 2, y + h_ - 40), POMARANCZ) and blisko(px(x + w_ // 2, y + 160), NIEBIESKI, 40)


def pokaz(w, h, px):  # on failure: what the zone and nadzorca said (counts only) and the top of the window — the test
    # page's address only — half size, 16 greys, zlib + base64 (the screenshots are in the artifact, not in the log)
    for l_ in [l for l in q.tekst().splitlines() if re.search(r"K1-STREFA|strefa 2", l)][-12:]:
        print("            | " + l_[:300], flush=True)
    print(f"            | serwer dostał: {len(odebrane)} żądań", flush=True)
    b = bytes(min(15, sum(px(x + 2 * i, y + 2 * j)) // 48) for j in range(150) for i in range(w_ // 2))
    print(f"            | obrazek okno {w_ // 2}x150 " + base64.b64encode(zlib.compress(b, 9)).decode(), flush=True)


def pamiec():  # S26: the page that takes 1400 MiB; True when it got all of it and the zone lives on
    n0 = q.ile(r"K1-STREFA: gość: strona zakończona — brak pamięci")
    q.pisz("ctrl-l")
    wpisz(f"10.0.2.2:{port}/pamiec")
    otworz_strone(60, "/pamiec", lambda w, h, px: "/pamiec" in odebrane)
    t0 = time.time()
    q.czekaj_na(lambda: "/pamiec-koniec" in odebrane or "strefa 2: koniec" in q.tekst() or
                q.ile(r"K1-STREFA: gość: strona zakończona — brak pamięci") > n0, czas=600)
    zyje = "strefa 2: koniec" not in q.tekst()
    print(f"            | pamięć: {[p for p in odebrane if p.startswith('/pamiec-postep')][-1:]} po "
          f"{time.time() - t0:.0f} s, strony zakończone z braku pamięci: "
          f"{q.ile(r'K1-STREFA: gość: strona zakończona — brak pamięci') - n0}, strefa żyje: {zyje}", flush=True)
    if not zyje:
        pokaz(*q.zrzut("5-pamiec"))
    return "/pamiec-koniec" in odebrane and zyje


if os.environ.get("K1_PRZEGLADARKA_WASKO"):  # looking for a fault (CI label testy-przegladarka): only the page
    q.pisz("ctrl-l")  # the address bar by its shortcut: Ctrl reaches the zone
    wpisz(f"10.0.2.2:{port}/")
    w, h, px = otworz_strone(60)
    jest = strona(w, h, px) and "/" in odebrane
    if not jest:
        pokaz(w, h, px)
    sprawdz("[WĄSKO] strona testowa otwiera się (Ctrl+L, adres, strzałka)", jest)
    sprawdz(f"[WĄSKO] strona zajmuje {PAMIEC_MIB} MiB w strefie 1536 MiB: dochodzi do końca, strefa żyje", pamiec())
    zamknij_strefe()
    q.czekaj(r"K1-NADZORCA: strefa 2: połączenia", czas=20)
    for l_ in [l for l in q.tekst().splitlines() if re.search(r"połączenia|wejście do gościa", l)][-3:]:
        print("            | " + l_[:300], flush=True)
    q.monitor("quit")
    qemu.wait()
    sys.exit(0 if all(q.wyniki) else 1)

n0 = q.ile(r"K1-NADZORCA: strefa 2: pierwsza odmowa połączenia")
q.klik(x + w_ // 2, y + 64)  # the address bar
wpisz(f"10.0.2.2:{port}/")
w, h, px = otworz_strone(150)
if not strona(w, h, px):
    pokaz(w, h, px)
sprawdz("strona testowa z serwera na routerze otwiera się w strefie (SOCKS5 → łącznik → gniazdo od nadzorcy; zrzut "
        "przegladarka-2-strona)", strona(w, h, px) and "/" in odebrane)
q.klik(x + 200, y + h_ // 2)  # the form's field (autofocus too); the page sends it by itself, without Enter
wpisz(TEKST)
dotarlo = q.czekaj_na(lambda: any(TEKST in p for p in odebrane), czas=120)
sprawdz("tekst wpisany w pole formularza dociera do serwera", dotarlo)

sprawdz("NEGATYWNY: strona prosi o obrazek z sieci domowej (nie router) — nadzorca odmawia (D3), strefa żyje",
        czekaj(r"K1-NADZORCA: strefa 2: pierwsza odmowa połączenia \(powód 1\)", od=n0, czas=120) and
        "strefa 2: koniec" not in q.tekst())
q.zrzut("3-odmowa")
t_ = q.tekst()
sprawdz("w logu K1 żadnego adresu strony ani wpisanego tekstu — tylko liczniki (niezmiennik 8)",
        TEKST not in t_ and f"10.0.2.2:{port}" not in t_ and "10.0.2.3" not in t_ and "wyslij" not in t_)

sprawdz(f"PAMIĘĆ: strona zajmuje {PAMIEC_MIB} MiB w strefie 1536 MiB — dochodzi do końca, przeglądarka i strefa żyją "
        "(S26; zrzut przegladarka-5-pamiec)", pamiec())
koniec_ = zamknij_strefe()
if not koniec_:
    pokaz(*q.zrzut("4-koniec"))
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

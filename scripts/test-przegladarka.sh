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
#   5a. PRZEWIJANIE (0.10.0 krok 4): długa strona testowa przewijana kółkiem — w dół i z powrotem; strona sama mówi
#      serwerowi, jak daleko ją przewinięto (zrzut przegladarka-6-dluga)
#   5b. KARTY, MINIMALIZACJA, CZEKANIE (0.10.1 krok 1, D1 właściciela): druga karta, 20 przełączeń kart z kursorem nad
#      kartą (jej podgląd); 10 × minimalizuj na pasku K1 i przywróć z paska zadań; 1 min bez ruchu — przeglądarka żyje;
#      przed poprawką kompozytora strefy (0.10.1) drugi podgląd karty kończył przeglądarkę (reguła 28)
#   6. PAMIĘĆ (0.10.0 krok 3, S26; od przebiegu 142 na samym końcu, we własnej strefie): strona testowa zajmuje 1400 MiB w strefie, która ma 1536 MiB (jak ciężkie strony,
#      po których na IdeaPadzie z 4 GB okno przeglądarki znikało) — strona dochodzi do końca, przeglądarka i strefa żyją
#   7. zamknięcie okna: maszyna kończy się, liczniki połączeń w logu
#   8. SESJE (0.10.0 krok 5): K1 z dyskiem danych (K1-DANE) — „Ogólna” pamięta: strona testowa ustawia ciasteczko,
#      po zamknięciu i ponownym otwarciu serwer je dostaje; po ponownym starcie K1 (ten sam K1-DANE) — dalej; „Prywatna”
#      (prawy przycisk na Przeglądarce w menu Start) go nie ma; Ustawienia → Przeglądarka: „Wyczyść dane” dla Ogólnej
#      (dwa kliknięcia) i „Zapomina” — następne otwarcie bez ciasteczka i z jasnym paskiem, kolejne znów bez
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
CZAS=${K1_TEST_CZAS:-2100} # the whole test, both starts of K1 together (35 min — the owner, 2026-11-16)
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
START = time.time()  # the test's limit counts from here, over both starts of K1
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


# 0.10.0 step 4: a long page (6600 px) that tells the server how far it was scrolled, once the scrolling stops
STRONA_DLUGA = ("<!doctype html><html><head><meta charset=utf-8><title>K1</title></head><body style='margin:0'>"
                "<div style='height:600px;background:#1e64c8'></div>"
                "<div style='height:6000px;background:linear-gradient(#ff8800,#00a050)'></div><script>var t;"
                " onscroll = function () { clearTimeout(t); t = setTimeout(function () {"
                " fetch('/przewinieto?y=' + Math.round(scrollY)); }, 400); };"
                " onload = function () { fetch('/dluga-gotowa'); };</script></body></html>")


class Serwer(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        odebrane.append(self.path)
        b = (STRONA_PAMIEC if self.path == "/pamiec" else STRONA_DLUGA if self.path == "/dluga" else
             STRONA.format(port=self.server.server_address[1])).encode()
        jest = "k1test=tak" in (self.headers.get("Cookie") or "")
        if self.path == "/ciastko":  # 0.10.0 step 5: did the session remember the cookie it got before?
            odebrane.append("/ciastko-jest" if jest else "/ciastko-brak")
        self.send_response(200)
        if self.path == "/ciastko" and not jest:
            self.send_header("Set-Cookie", "k1test=tak; Max-Age=864000; Path=/")
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
# 0.10.0 step 5: K1-DANE (skarbiec makes a vault on it) — a session remembers only with the data on a disk
DANE = f"-drive if=none,id=dane,file={out}/przegladarka-dane.img,format=raw -device ide-hd,drive=dane,bus=ide.0"
subprocess.run(["truncate", "-s", "1G", f"{out}/przegladarka-dane.img"], check=True)
subprocess.run(["sfdisk", "-q", f"{out}/przegladarka-dane.img"], input=b'label: gpt\nstart=2048, name="K1-DANE"\n',
               check=True, stdout=subprocess.DEVNULL)
log, mon = f"{out}/przegladarka.log", f"{out}/przegladarka.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--pamiec", "3072", "--qemu", SIEC + " " + DANE] + emulacja)
q = Maszyna(log, mon, out, limit, "przegladarka")
czekaj, sprawdz = q.czekaj, q.sprawdz
SZALWIA, WRZOS = (0x5F, 0x8A, 0x6A), (0x8B, 0x6A, 0xA8)  # K1_STREFY: Ogólna, Prywatna
KOLOR = SZALWIA  # the frame of the zone shown now
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
o = czekaj(r"K1-EKRAN: okno (\d+) \(strefa\) (\d+)x(\d+) w (\d+),(\d+)", czas=120)
nr, w_, h_, x, y = (int(o.group(i)) for i in range(1, 6)) if o else (0, 0, 0, 0, 0)
sprawdz("VMM strefy 2 w klatce (uid 301, 1536 MiB na start, od 0.10.1 krok 2 górna granica: pamięć K1 minus rezerwa), "
        "maszyna działa",
        czekaj(r"K1-NADZORCA: strefa 2 \(Przeglądarka\): pid \d+, uid 301, pamięć 1536 MiB na start, granica (\d+) MiB", czas=60) and
        czekaj(r"K1-STREFA: strefa 2 \(Przeglądarka\): maszyna działa", czas=120))
sprawdz("gość: kanał sterujący „gotowa” (gosc uruchomił przekaźnik, kompozytor i przeglądarkę)",
        czekaj(r"K1-STREFA: gość gotowy \(kanał sterujący\)", czas=120))
if emulacja:
    print("  [SKRÓT] emulacja bez KVM: dalsze kroki (Firefox, strona, formularz, odmowa D3, logi) — tylko z KVM (CI)",
          flush=True)
    q.monitor("quit")
    qemu.wait()
    sys.exit(0 if all(q.wyniki) else 1)


# The frame in the session's colour, Firefox's address field and tab bar (not K1's "Uruchamiam…": 250,249,246). After
# Firefox quit by itself it starts focused (CI run 140, przegladarka-7-sesja-2): the field white, the tab bar darker.
def firefox(w, h, px):
    pole, karty = px(x + w_ // 2, y + 64), px(x + w_ // 3, y + 20)
    return (o and blisko(px(x - 2, y + h_ // 2), KOLOR)
            and (blisko(pole, (242, 242, 242), 4) or blisko(pole, (255, 255, 255), 4))
            and (blisko(karty, (250, 250, 251), 4) or blisko(karty, (235, 235, 237), 4)))


def bez_przyciskow(px):  # S28 (step 3): the top right corner of the tab bar is empty — Firefox draws no minimise,
    # maximise and close of its own (strefy/przegladarka/gtk-settings.ini); its close button was right there
    tlo = px(x + w_ // 3, y + 20)
    return all(blisko(px(x + w_ - 30 + i, y + 10 + j), tlo, 12) for i in range(0, 26, 2) for j in range(0, 24, 2))


w, h, px = q.zrzut("1-okno", gdy=firefox, czas=300)  # seen: 33-156 s
sprawdz("okno przeglądarki w ramce w kolorze sesji, pasek kart i adresu (zrzut przegladarka-1-okno)",
        firefox(w, h, px))
sprawdz("Firefox bez własnych przycisków okna (minimalizuj, powiększ, zamknij) — tylko pasek K1 w kolorze sesji (S28)",
        bez_przyciskow(px), str(px(x + w_ - 16, y + 20)))
sprawdz("pasek tytułu K1 nad oknem przeglądarki w kolorze sesji, która pamięta (krok 3, D1: A; krok 5: Ogólna pamięta)",
        blisko(px(x + w_ // 2, y - 42), SZALWIA) and blisko(px(x + w_ - 150, y - 20), SZALWIA),
        str((px(x + w_ // 2, y - 42), px(x + w_ - 150, y - 20))))
sprawdz("nadzorca: Ogólna pamięta — VMM dostaje dysk strefy w skarbcu, gość montuje go jako katalog domowy",
        czekaj(r"K1-NADZORCA: strefa 2 \(Przeglądarka\): pid \d+, .*pamięta \(dysk w skarbcu\)") and
        czekaj(r"K1-STREFA: dysk strefy: pamięta", czas=30))
for l_ in re.findall(r"K1-STREFA: gość(?: gotowy|: okno przeglądarki)[^\n]*", q.tekst()):  # S30: where the start goes
    print("            | start: " + l_[:200], flush=True)


STREFA = 2  # the zone shown now (Ogólna; Prywatna: 4)


def zamknij_strefe():  # "zamknij" on K1's title bar above the zone (step 3, D1: sketch A) — as test-strefa does
    pasek = r"K1-EKRAN: strefa: pasek okna {} \(minimalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)".format(nr)
    n = q.ile(pasek)
    q.idz(x + 60, y + 60)
    q.idz(x + w_ // 2, y - 20)  # the bar (K1_STREFA_TYTUL = 40 px above the picture)
    p = czekaj(pasek, od=n, czas=30)
    n = q.ile(rf"K1-STREFA: strefa {STREFA}: koniec")
    if p:
        q.klik(int(p.group(3)), int(p.group(4)))
    return czekaj(rf"K1-STREFA: strefa {STREFA}: koniec", od=n, czas=90)  # VMM: 40 s + saving the disk


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
        w, h, px = q.zrzut({"/": "2-strona", "/pamiec": "5-pamiec"}.get(sciezka, "6-" + sciezka.strip("/")), gdy=gotowe,
                           czas=czas)
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


def zyje():  # the zone still runs (no end, no program of the guest ended by itself)
    t_ = q.tekst()
    return f"strefa {STREFA}: koniec" not in t_ and "K1-STREFA: gość: program strefy zakończył się sam" not in t_


def kto_skonczyl():  # 0.10.1 step 1 (S15): who of the guest's programs ended, numbers only (the VMM's line)
    k = re.findall(r"K1-STREFA: gość: (?:przeglądarka|kompozytor) zakończony[^\n]*", q.tekst())
    return k[-1][:200] if k else "nikt"


KARTA1, KARTA2 = (150, 22), (380, 22)  # the tabs in a 1024-wide window (Firefox's tab bar: 225 px a tab)


def karty():  # 0.10.1 step 1 (D1 f, S15): the owner's test — two tabs, switching every 1–2 s, the pointer stays over
    # the tab (Firefox shows the tab's preview after about a second). Before the fix of 0.10.1 the second preview
    # ended the browser: our compositor refused a subsurface made again on the same surface (rule 28).
    q.pisz("ctrl-t")  # a new, empty tab
    time.sleep(3)
    for i in range(20):
        kx, ky = KARTA1 if i % 2 == 0 else KARTA2
        q.klik(x + kx, y + ky)
        time.sleep(1.5)  # over the tab: its preview comes
        if not zyje():
            print(f"            | karty: strefa skończyła się po {i + 1} przełączeniach — {kto_skonczyl()}", flush=True)
            return False
    q.zrzut("9-karty")
    return zyje()


def pasek_zadan():  # where the zone's item is on the bottom bar (gui logs the bar); None: not found
    pk = re.findall(r"K1-GUI: pasek zmieniony: Start \d+,\d+, aplikacje od (\d+),(\d+) co (\d+), dźwięk \d+,\d+ "
                    r"\(pasek: ([^)]*)\)", q.tekst())
    if not pk:
        return None
    ax, ay, co, el = int(pk[-1][0]), int(pk[-1][1]), int(pk[-1][2]), pk[-1][3].split(", ")
    i = max((j for j, e in enumerate(el) if "Przeglądarka" in e), default=None)
    przypiete = next((j for j, e in enumerate(el) if e.startswith("| ")), len(el))
    return None if i is None else (ax + i * co + (9 if przypiete <= i else 0), ay)


def minimalizacja():  # 0.10.1 step 1 (D1 d, S12): minimise from K1's title bar, back from the bottom bar, 10 times —
    # the pointer leaves the picture over its top edge (the tab bar) on the way to the bar's button
    pasek = r"K1-EKRAN: strefa: pasek okna {} \(minimalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)".format(nr)
    for i in range(10):
        n, nz, np_ = q.ile(pasek), q.ile(rf"K1-EKRAN: okno {nr} zminimalizowane"), q.ile(rf"K1-EKRAN: okno {nr} przywrócone")
        q.idz(x + w_ // 2, y + 60)
        q.idz(x + w_ // 2, y - 20)  # onto the bar: ekran logs its buttons
        p = czekaj(pasek, od=n, czas=30)
        if not p:
            return False
        q.klik(int(p.group(1)), int(p.group(2)))
        if not czekaj(rf"K1-EKRAN: okno {nr} zminimalizowane", od=nz, czas=30):
            return False
        time.sleep(1)
        e = pasek_zadan()
        if e is None:
            return False
        q.klik(*e)
        if not czekaj(rf"K1-EKRAN: okno {nr} przywrócone", od=np_, czas=30):
            return False
        q.idz(x + KARTA1[0], y + KARTA1[1] + 4)  # back over the tabs, as a hand would
        time.sleep(1.5)  # long enough for the tab's preview (about a second)
        if not zyje():
            print(f"            | minimalizacja: strefa skończyła się po {i + 1} razach — {kto_skonczyl()}", flush=True)
            return False
    return zyje()


def czekanie(s):  # 0.10.1 step 1 (D1 e): two tabs and nothing done — the zone lives; the counters every 5 s (S13)
    n = q.ile(r"K1-STREFA: liczniki: gość")
    q.idz(x + w_ // 2, y + h_ // 2)
    q.cisza(rf"strefa {STREFA}: koniec|program strefy zakończył się sam", s)
    for l_ in re.findall(r"K1-STREFA: liczniki: gość[^\n]*", q.tekst())[-2:]:
        print("            | " + l_[:240], flush=True)
    return zyje() and q.ile(r"K1-STREFA: liczniki: gość — dostępna \d+ MiB, zram \d+ MiB, zabite \d+") > n


def przewiniecia():  # 0.10.0 step 4: how far the long page said it was scrolled, in order
    return [int(p.split("=", 1)[1]) for p in odebrane if p.startswith("/przewinieto?y=")]


def przewijanie():  # the long page: 3 notches down, then 5 up — back at the top
    q.pisz("ctrl-l")
    wpisz(f"10.0.2.2:{port}/dluga")
    otworz_strone(60, "/dluga", lambda w, h, px: "/dluga-gotowa" in odebrane)
    q.kolko(x + w_ // 2, y + h_ // 2 + 40, w_dol=3)
    w_dol = q.czekaj_na(lambda: any(v > 0 for v in przewiniecia()), czas=60)
    q.zrzut("6-dluga-przewinieta")
    n = len(przewiniecia())
    q.kolko(x + w_ // 2, y + h_ // 2 + 40, w_dol=-5)
    w_gore = q.czekaj_na(lambda: len(przewiniecia()) > n and przewiniecia()[-1] == 0, czas=60)
    print(f"            | przewinięcia strony: {przewiniecia()}", flush=True)
    return w_dol and w_gore


def pamiec():  # S26: the page that takes 1400 MiB; True when it got all of it and the zone lives on
    n0 = q.ile(r"K1-STREFA: gość: strona zakończona — brak pamięci")
    q.pisz("ctrl-l")
    wpisz(f"10.0.2.2:{port}/pamiec")
    otworz_strone(60, "/pamiec", lambda w, h, px: "/pamiec" in odebrane)
    t0 = time.time()
    q.czekaj_na(lambda: "/pamiec-koniec" in odebrane or "strefa 2: koniec" in q.tekst() or
                q.ile(r"K1-STREFA: gość: strona zakończona — brak pamięci") > n0, czas=240)
    zyje = "strefa 2: koniec" not in q.tekst()
    print(f"            | pamięć: {[p for p in odebrane if p.startswith('/pamiec-postep')][-1:]} po "
          f"{time.time() - t0:.0f} s, strony zakończone z braku pamięci: "
          f"{q.ile(r'K1-STREFA: gość: strona zakończona — brak pamięci') - n0}, strefa żyje: {zyje}", flush=True)
    if not zyje:
        pokaz(*q.zrzut("5-pamiec"))
    return "/pamiec-koniec" in odebrane and zyje


# "jest" when ANY request for the page carried the cookie, "brak" when none did. Firefox may send the page twice right
# after it starts — CI run 138: ['brak', 'jest'], the cookie was there (it was after the restart of K1 too); so
# "remembers" = the server got it, "forgets" = it never did (the strict side for privacy).
def ciastko():  # 0.10.0 step 5: the page /ciastko — did the request carry the cookie set before?
    n = len(odebrane)
    q.klik(x + w_ // 2, y + 64)  # the address bar
    q.pisz("ctrl-a")
    wpisz(f"10.0.2.2:{port}/ciastko")
    otworz_strone(120, "/ciastko", lambda w, h, px: any(p.startswith("/ciastko-") for p in odebrane[n:]))
    q.czekaj_na(lambda: "/ciastko-jest" in odebrane[n:], czas=10)  # a second request, if one comes
    wynik = [p[9:] for p in odebrane[n:] if p.startswith("/ciastko-")]
    print(f"            | /ciastko (strefa {STREFA}): {wynik}", flush=True)
    return "jest" if "jest" in wynik else ("brak" if wynik else "nic")


def okno_strefy(n0):  # the zone's window after a start: its place, Firefox drawn in it
    global o, nr, w_, h_, x, y
    o = czekaj(r"K1-EKRAN: okno (\d+) \(strefa\) (\d+)x(\d+) w (\d+),(\d+)", od=n0, czas=120)
    if not o:
        return False
    nr, w_, h_, x, y = (int(o.group(i)) for i in range(1, 6))
    w, h, px = q.zrzut(f"7-sesja-{STREFA}", gdy=firefox, czas=300)
    return firefox(w, h, px)


def otworz_przegladarke(strefa, prawym=False):  # Start → Przeglądarka (Ogólna), or the right button → a session
    global STREFA, KOLOR
    STREFA, KOLOR = strefa, (WRZOS if strefa == 4 else SZALWIA)
    n0, nm = q.ile(r"K1-EKRAN: okno (\d+) \(strefa\)"), q.ile(r"menu Start otwarte")
    s = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+)")
    q.klik(int(s.group(1)), int(s.group(2)))
    m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);", od=nm, czas=60)
    if not m:
        return False
    mx_, my_, co_, sy_ = int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(5))
    wiersz = (mx_, my_ + ((sy_ - my_ + co_ // 2 - 2 - 36) // co_ - 1) * co_)  # the browser: the last row
    if not prawym:
        q.klik(*wiersz)
    else:
        nk = q.ile(r"K1-GUI: menu kontekstowe \(menu Start Przeglądarka\)")
        q.prawy_klik(*wiersz)
        k = czekaj(r"K1-GUI: menu kontekstowe \(menu Start Przeglądarka\): (.*)", od=nk, czas=30)
        p = k and re.search(rf"sesja {strefa} (\d+),(\d+)", k.group(1))
        print(f"            | menu pod prawym przyciskiem: {k.group(1)[:200] if k else 'brak'}", flush=True)
        if not p:
            return False
        q.klik(int(p.group(1)), int(p.group(2)))
    return okno_strefy(n0)


def uruchom_ponownie_k1():  # the same K1-DANE, a new QEMU: K1 starts again (the zone was closed before)
    global q, qemu, czekaj
    wyniki = q.wyniki
    q.monitor("quit")
    qemu.wait()
    # one limit for the whole test: the second machine gets what is left of it, never a fresh one (CI run 125)
    reszta = int(limit - (time.time() - START))
    print(f"            | drugie uruchomienie K1: zostało {reszta} s z limitu testu", flush=True)
    if reszta < 300:
        return False
    log2, mon2 = f"{out}/przegladarka-2.log", f"{out}/przegladarka-2.sock"
    qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen.squashfs",
                             "--cmdline", cmdline, "--log", log2, "--monitor", mon2, "--czas", str(reszta),
                             "--pamiec", "3072", "--qemu", SIEC + " " + DANE] + emulacja)
    q = Maszyna(log2, mon2, out, reszta, "przegladarka-2")
    q.wyniki = wyniki
    czekaj = q.czekaj
    return czekaj(r"K1-GUI: pulpit gotowy", czas=reszta * 0.3) and czekaj(r"K1-NADZORCA: sieć: połączono \(", czas=240)


def ustawienia_ogolna():  # Ustawienia → Przeglądarka: "Wyczyść dane" of Ogólna (two clicks), then "Zapomina"
    g2 = czekaj(r"K1-GUI: pulpit gotowy: Start \d+,\d+, aplikacje od (\d+),(\d+) co (\d+)")
    n0 = q.ile(r"K1-EKRAN: okno (\d+) \(ustawienia\)")
    q.klik(int(g2.group(1)) + 2 * int(g2.group(3)), int(g2.group(2)))  # the bottom bar: 0 Zegar, 1 Notatki, 2 Ustawienia
    u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", od=n0, czas=60)
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    if not (u and sk):
        return False
    ux, uy = int(u.group(4)), int(u.group(5))
    np_ = q.ile(r"K1-USTAWIENIA: przeglądarka: sesje")
    q.klik(ux + int(sk.group(1)), uy + int(sk.group(2)) + 9 * int(sk.group(3)))  # section 9: Przeglądarka
    l_ = czekaj(r"K1-USTAWIENIA: przeglądarka: sesje \d+ \(dane na dysku 1, wirtualizacja 1\): (.*)", od=np_, czas=30)
    p = l_ and re.search(r"2 pamięta, przełącznik (\d+),(\d+), Zmień nazwę \d+,\d+, Wyczyść (\d+),(\d+)", l_.group(1))
    print(f"            | Ustawienia → Przeglądarka: {l_.group(1)[:220] if l_ else 'brak'}", flush=True)
    if not p:
        return False
    wx, wy = ux + int(p.group(3)), uy + int(p.group(4))
    q.klik(wx, wy)
    q.klik(wx, wy)  # "Na pewno? Wyczyść"
    ok = czekaj(r"K1-NADZORCA: strefa 2: dane sesji usunięte \(\d+ kawałków\)", czas=60)
    q.klik(ux + int(p.group(1)) + 150, uy + int(p.group(2)) + 16)  # "Zapomina": the right half of the switch
    return ok and czekaj(r"K1-NADZORCA: strefa 2: zapomina \(od następnego otwarcia\)", czas=30)


def sesje():  # 0.10.0 step 5: the scenario of the sessions (the first page of /ciastko was in the first window)
    sprawdz("SESJE: Ogólna pamięta — po zamknięciu i ponownym otwarciu serwer dostaje ciasteczko",
            otworz_przegladarke(2) and ciastko() == "jest")
    zamknij_strefe()
    sprawdz("SESJE: po ponownym starcie K1 (ten sam K1-DANE) Ogólna dalej ma ciasteczko",
            uruchom_ponownie_k1() and otworz_przegladarke(2) and ciastko() == "jest")
    zamknij_strefe()
    sprawdz("SESJE: Prywatna (prawy przycisk na Przeglądarce w menu Start) nie ma ciasteczka Ogólnej, jasny pasek",
            otworz_przegladarke(4, prawym=True) and ciastko() == "brak")
    w, h, px = q.zrzut("8-prywatna")
    sprawdz("SESJE: Prywatna zapomina — jasny pasek w obwódce koloru sesji (wrzos)",
            blisko(px(x + w_ // 2, y - 42), WRZOS) and not blisko(px(x + w_ - 150, y - 20), WRZOS),
            str((px(x + w_ // 2, y - 42), px(x + w_ - 150, y - 20))))
    zamknij_strefe()
    sprawdz("SESJE: Ustawienia → Przeglądarka — „Wyczyść dane” Ogólnej (dwa kliknięcia), potem „Zapomina”",
            ustawienia_ogolna())
    sprawdz("SESJE: po „Wyczyść dane” Ogólna nie ma ciasteczka (strona ustawia je jeszcze raz)",
            otworz_przegladarke(2) and ciastko() == "brak" and
            czekaj(r"K1-NADZORCA: strefa 2 \(Przeglądarka\): pid \d+, .*zapomina po zamknięciu"))
    zamknij_strefe()
    sprawdz("SESJE: Ogólna przełączona na „Zapomina” — następne otwarcie znów bez ciasteczka",
            otworz_przegladarke(2) and ciastko() == "brak")
    zamknij_strefe()
    t_ = q.tekst()
    sprawdz("SESJE: w logu K1 żadnej nazwy sesji ani zawartości dysku — tylko numery stref i liczby kawałków",
            "k1test" not in t_ and not re.search(r"menu kontekstowe \(menu Start Przeglądarka\): .*(Ogólna|Bank|Prywatna)", t_))


if os.environ.get("K1_PRZEGLADARKA_WASKO"):  # looking for a fault (CI label testy-przegladarka): only the page
    q.pisz("ctrl-l")  # the address bar by its shortcut: Ctrl reaches the zone
    wpisz(f"10.0.2.2:{port}/")
    w, h, px = otworz_strone(60)
    jest = strona(w, h, px) and "/" in odebrane
    if not jest:
        pokaz(w, h, px)
    sprawdz("[WĄSKO] strona testowa otwiera się (Ctrl+L, adres, strzałka)", jest)
    sprawdz(f"[WĄSKO] strona zajmuje {PAMIEC_MIB} MiB: dochodzi do końca, strefa żyje", pamiec())
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

sprawdz("PRZEWIJANIE: kółko nad długą stroną przewija ją w dół, potem z powrotem na górę (0.10.0 krok 4; zrzut "
        "przegladarka-6-dluga-przewinieta)", przewijanie())
sprawdz("SESJE: strona ustawia ciasteczko (serwer: pierwsze wejście bez niego)", ciastko() == "brak")
# 0.10.1 step 1: the owner's cases after scrolling and the cookie — the new tab breaks scrolling (plan 0.10.1
# step 5; CI run 150: scrolled down, not back up after the tab switches)
sprawdz("KARTY: dwie karty, przełączanie co 1,5 s z kursorem nad kartą (jej podgląd), 20 razy — przeglądarka żyje "
        "(0.10.1 krok 1, D1 f; zrzut przegladarka-9-karty)", karty())
sprawdz("MINIMALIZACJA: 10 × minimalizuj na pasku K1 i przywróć z paska zadań — przeglądarka żyje (0.10.1 krok 1, D1 d)",
        minimalizacja())
sprawdz("CZEKANIE: dwie karty, 1 min bez ruchu — przeglądarka żyje, liczniki strefy co 5 s w logu (0.10.1 krok 1, D1 e)",
        czekanie(60))
koniec_ = zamknij_strefe()
if not koniec_:
    pokaz(*q.zrzut("4-koniec"))
sprawdz("zamknięcie okna: gość dostaje „zamknij”, przeglądarka kończy się, maszyna znika", koniec_)
sprawdz("liczniki połączeń w logu: VMM i nadzorca (bez adresów)",
        czekaj(r"K1-STREFA: połączenia przeglądarki: \d+, nazwy \d+, odmowy \d+", czas=30) and
        czekaj(r"K1-NADZORCA: strefa 2: połączenia \d+, odmowy [1-9]", czas=30))
sesje()
# PAMIĘĆ last, in a fresh zone of its own (CI run 142): the page pushes the browser to the edge of the zone's memory and
# the browser may end soon after it (on X230 and IdeaPad too: 0.10.1 Balon) — before, every check after it in the same
# browser failed with it. Ogólna forgets by now (sesje), so nothing of this lands on the session's disk.
sprawdz(f"PAMIĘĆ: strona zajmuje {PAMIEC_MIB} MiB (więcej niż 1536 MiB na start minus jądro; od 0.10.1 krok 2 strefa ma "
        "górną granicę z balonem) — dochodzi do końca, przeglądarka i strefa żyją "
        "(S26; zrzut przegladarka-5-pamiec)", otworz_przegladarke(2) and pamiec())
zamknij_strefe()
t_ = q.tekst()
sprawdz("brak zabronionych wywołań i paniki w K1", "zabronione wywołanie" not in t_ and "Kernel panic" not in t_)
q.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test przeglądarki (0.10.0 krok 2): strefa „Ogólna”, Firefox, strona przez łącznik, formularz, odmowa D3"
r=0
# The whole test within its limit (+2 min to close): on a slow CI day it ends as a failed test — informational
# (testy.sh) — instead of running past the limit of the whole tests step (CI run 146 of PR #134: 44 min, all killed).
K1_TESTY_PY="$K1_REPO/testy/qemu" timeout --kill-after=10 $((CZAS + 120)) \
    python3 -I "$K1_BUDOWA/przegladarka.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" $EMULACJA || r=1
if [ $r -eq 0 ]; then info "przeglądarka: wszystko zaliczone"; else echo "[k1] przeglądarka: BŁĄD (log: $TESTY/przegladarka.log)"; fi
exit $r

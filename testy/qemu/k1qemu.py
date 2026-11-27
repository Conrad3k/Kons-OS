"""K1 — wspólne funkcje testów w QEMU (0.6.0, ADR 0019).

Do tej pory każdy skrypt testu (test-m2/m4/m5/m5f) miał własną kopię tych samych funkcji: czytanie
logu z portu szeregowego, czekanie na wiersz, monitor QEMU, zrzut ekranu, ruch myszy po pozycji z
logu ekranu, kliknięcie, wynik [OK]/[BŁĄD]. Tutaj są raz. Skrypty uruchamiają Pythona z -I (bez
katalogu skryptu w ścieżce), więc dołączają ten katalog jawnie:

    sys.path.insert(0, os.environ["K1_TESTY_PY"]); from k1qemu import Maszyna

0.7.3: bez stałych pauz. Każdy krok czeka na dowód, że się stał: monitor QEMU — na odpowiedź na
polecenie, kliknięcie — na wiersz ekranu „przycisk wciśnięty/puszczony”, zrzut — na stan ekranu
(warunek `gdy` albo ten sam obraz kilka razy z rzędu). Wolna maszyna czeka dłużej, nie zawodzi.
"""
import importlib.util
import json
import os
import re
import socket
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # also when run as a script with -I (zamknij)
import k1wzorce  # noqa: E402

KURSOR = re.compile(r"K1-EKRAN: kursor (\d+),(\d+) ")
PRZYCISK = r"K1-EKRAN: kursor \d+,\d+ przycisk "
# A kernel message printed in the middle of one of our lines on the shared serial console
# ("K1: wyłączanie[   12.3] reboot: …\nzasilania", the CI failure of 0.6.1): cut out, the line joined again.
# A kernel message printed in the middle of a line of K1 (the serial console has one stream) — removed from the bytes,
# before decoding: it may split a character of UTF-8 in two (0.9.1: "sie\xc4[ 12.1] wlan0: …\n\x87 w zasięgu").
WTRACENIE = re.compile(rb"(?<=[^\n])\[\s*\d+\.\d+\][^\n]*\n")
# The machine stops or restarts: nothing more will come, waiting longer only wastes time.
KONIEC_MASZYNY = r"Kernel panic|reboot: Power down|reboot: Restarting"
ZACHETA = b"(qemu) "

_spec = importlib.util.spec_from_file_location(
    "ppm_na_png", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "scripts", "ppm-na-png.py"))
_ppm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ppm)


def piksele(w, h, data, poczatek=0, krok=None):
    """px(x, y) -> (r, g, b) for RGB8 rows of `krok` bytes starting at `poczatek`."""
    krok = krok or w * 3
    return lambda x, y: tuple(data[poczatek + y * krok + x * 3:poczatek + y * krok + x * 3 + 3])


def png(sciezka):
    """Nasze zrzuty (RGB8, filtr 0 w każdym wierszu): (szer, wys, piksel(x, y) -> (r, g, b))."""
    raw = open(sciezka, "rb").read()
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(raw):
        n, t = struct.unpack(">I4s", raw[pos:pos + 8])
        d = raw[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b"IHDR":
            w, h = struct.unpack(">II", d[:8])
        elif t == b"IDAT":
            idat += d
    return w, h, piksele(w, h, zlib.decompress(idat), 1, w * 3 + 1)


def _polacz(gniazdo, czas=10):
    """The HMP monitor socket (it may not exist yet right after QEMU starts), after its first prompt."""
    koniec = time.time() + czas
    while True:
        s = socket.socket(socket.AF_UNIX)
        try:
            s.connect(gniazdo)
            s.settimeout(30)
            _do_zachety(s)
            return s
        except OSError:
            s.close()
            if time.time() >= koniec:
                return None
            time.sleep(0.2)


def _do_zachety(s):
    """Reads until the monitor prompt: the previous command has been carried out."""
    dane = b""
    while not dane.endswith(ZACHETA):
        kawalek = s.recv(65536)
        if not kawalek:  # "quit": QEMU closes the connection
            break
        dane += kawalek
    return dane


def ekran(gniazdo, ppm):
    """One screendump through the monitor: (w, h, RGB bytes) or None."""
    s = _polacz(gniazdo)
    if s is None:
        return None
    try:
        os.unlink(ppm)
    except OSError:
        pass
    try:
        s.sendall(f"screendump {ppm}\n".encode())
        _do_zachety(s)
    except OSError:
        return None
    finally:
        s.close()
    try:
        raw = open(ppm, "rb").read()
        os.unlink(ppm)
    except OSError:
        return None
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", raw)
    if not m or len(raw) != m.end() + int(m.group(1)) * int(m.group(2)) * 3:
        return None
    return int(m.group(1)), int(m.group(2)), raw[m.end():]


def zapisz_png(sciezka, w, h, rgb):
    _ppm.ppm_na_png(b"P6\n%d %d\n255\n" % (w, h) + rgb, sciezka)


class Maszyna:
    """Jedna uruchomiona maszyna: log, gniazdo monitora, katalog zrzutów, limit czasu."""

    def __init__(self, log, mon, out, limit, przedrostek):
        self.log, self.mon, self.out = log, mon, out
        self.limit, self.przedrostek = float(limit), przedrostek
        self.start = time.time()
        self.po_limicie = False
        self.wyniki = []
        self.kursor = [0, 0]

    # --- log ---
    def tekst(self):
        """The log up to its last complete line: QEMU may be writing a line just now, and a pattern must never match
        half of it (CI with KVM: "menu kontekstowe …: Zegar 548,617" without the rest of the menu's rows)."""
        try:
            t = WTRACENIE.sub(b"", open(self.log, "rb").read()).decode("utf-8", errors="replace")
            t = t.replace("\r\n", "\n").replace("\r", "\n")  # what reading as text did (universal newlines)
        except OSError:
            return ""
        return t[: t.rfind("\n") + 1]

    def czekaj(self, wzor, od=0, czas=None):
        """Ostatnie dopasowanie, gdy jest ich więcej niż `od`; None po czasie albo gdy maszyna się
        zatrzymała (panika jądra, wyłączenie, restart — po rozpoczęciu czekania)."""
        koniec = time.time() + (czas or self.limit)
        stop = len(re.findall(KONIEC_MASZYNY, self.tekst()))
        while True:
            t = self.tekst()
            m = list(re.finditer(wzor, t))
            if len(m) > od:
                return m[-1]
            if len(re.findall(KONIEC_MASZYNY, t)) > stop or "Kernel panic" in t:
                return None
            if time.time() - self.start >= self.limit:
                if not self.po_limicie:  # QEMU is stopped at the same moment (uruchom-qemu.sh --czas): say so once
                    print(f"  [uwaga] minął limit czasu maszyny ({self.limit:.0f} s) — dalsze sprawdzenia nie mają na co czekać",
                          flush=True)
                    self.po_limicie = True
                return None
            if time.time() >= koniec:
                return None
            time.sleep(0.2)

    def czekaj_na(self, warunek, czas):
        """True as soon as `warunek()` holds; False after `czas` s."""
        koniec = time.time() + czas
        while not warunek():
            if time.time() >= koniec:
                return False
            time.sleep(0.15)
        return True

    def ile(self, wzor):
        return len(re.findall(wzor, self.tekst()))

    def cisza(self, wzor, czas):
        """True when `wzor` does NOT show up again within `czas` s — for checks that something does not happen
        (a slow machine can only make such a check pass too early, never fail it)."""
        return self.czekaj(wzor, od=self.ile(wzor), czas=czas) is None

    # --- wynik ---
    def sprawdz(self, nazwa, warunek, opis=""):
        self.wyniki.append(bool(warunek))
        print(f"  [{'OK' if warunek else 'BŁĄD'}]    {nazwa}" + ("" if warunek else f" — {opis}"), flush=True)
        if not warunek:  # what the system did last (cursor lines included): the CI console shows it
            for w in [w for w in self.tekst().splitlines() if w.startswith("K1")][-12:]:
                print(f"            | {w}", flush=True)
        return warunek

    def czysty_log(self):
        t = self.tekst()
        return self.sprawdz("brak 'Kernel panic', błędów usług i zabronionych wywołań",
                            "Kernel panic" not in t and "BŁĄD" not in t and "zabronione wywołanie" not in t)

    def koniec(self, wylacz=True):
        if wylacz:
            self.monitor("quit")
        sys.exit(0 if self.wyniki and all(self.wyniki) else 1)

    # --- monitor, zrzuty ---
    def monitor(self, *polecenia):
        """HMP commands in order, each one sent after the previous one was carried out (its prompt)."""
        s = _polacz(self.mon)
        if s is None:
            return
        try:
            for c in polecenia:
                s.sendall(c.encode() + b"\n")
                _do_zachety(s)
        except OSError:
            pass
        finally:
            s.close()

    def monitor_tekst(self, polecenie):
        """One HMP command and what it printed (e.g. "info mice"); "" when the monitor is gone."""
        s = _polacz(self.mon)
        if s is None:
            return ""
        try:
            s.sendall(polecenie.encode() + b"\n")
            return _do_zachety(s).decode(errors="replace")
        except OSError:
            return ""
        finally:
            s.close()

    def zrzut(self, nazwa, gdy=None, czas=None):
        """Zrzut ekranu (PNG w katalogu testów), gdy ekran pokazuje to, czego test oczekuje: `gdy(w, h, px)`
        jest prawdą — albo (bez warunku) ten sam obraz trzy razy z rzędu, czyli ekran skończył rysować.
        Po czasie (domyślnie 30 s z warunkiem, 8 s bez) — ostatni obraz; sprawdzenie i tak zrobi skrypt."""
        ppm = f"{self.out}/.{self.przedrostek}-{os.getpid()}.ppm"
        koniec = time.time() + (czas or (30 if gdy else 8))
        ost, takie_same = None, 0
        while True:
            e = ekran(self.mon, ppm)
            if e:
                takie_same = takie_same + 1 if ost and e == ost else 0
                ost = e
                if (gdy(e[0], e[1], piksele(*e)) if gdy else takie_same >= 2):
                    break
            if time.time() >= koniec:
                break
            time.sleep(0.2)
        if not ost:
            return 0, 0, lambda x, y: (0, 0, 0)
        zapisz_png(f"{self.out}/{self.przedrostek}-{nazwa}.png", *ost)
        return ost[0], ost[1], piksele(*ost)

    # --- mysz i klawiatura ---
    def kursor_teraz(self):
        m = list(KURSOR.finditer(self.tekst()))
        return (int(m[-1].group(1)), int(m[-1].group(2))) if m else None

    def przesun(self, dx, dy):
        polecenia = []
        while dx or dy:
            sx, sy = max(-100, min(100, dx)), max(-100, min(100, dy))
            polecenia.append(f"mouse_move {sx} {sy}")
            dx -= sx
            dy -= sy
        if polecenia:
            self.monitor(*polecenia)

    def idz(self, x, y):
        """Do (x, y) według pozycji z logu ekranu (mysz bywa zgubiona tuż po starcie): do 5 poprawek."""
        for _ in range(5):
            k = self.kursor_teraz() or tuple(self.kursor)
            self.przesun(x - k[0], y - k[1])
            self.kursor[:] = [x, y]
            if self.czekaj_na(lambda: self.kursor_teraz() == (x, y), czas=5):
                return True
        return False

    def przycisk(self, wcisniety):
        """Presses or releases the left button; waits until ekran has it (its rate-limited cursor line)."""
        wzor = PRZYCISK + ("wciśnięty" if wcisniety else "puszczony")
        n = self.ile(wzor)
        self.monitor(f"mouse_button {1 if wcisniety else 0}")
        return self.czekaj(wzor, od=n, czas=10) is not None

    def klik(self, x, y):
        """Click at (x, y); False when the pointer never got there (the click went elsewhere)."""
        trafil = self.idz(x, y)
        if not trafil:
            print(f"  [uwaga] kursor nie dotarł do {x},{y}", flush=True)
        stop = self.ile(KONIEC_MASZYNY)
        if not (self.przycisk(True) and self.przycisk(False)) and self.ile(KONIEC_MASZYNY) == stop:
            print(f"  [uwaga] ekran nie potwierdził kliknięcia w {x},{y}", flush=True)
        return trafil

    def dwuklik(self, x, y):
        """Double click at (x, y): both clicks in one batch of monitor commands (ekran confirms a button at
        most twice a second, so two klik() calls would be too far apart for a double click)."""
        trafil = self.idz(x, y)
        n = self.ile(PRZYCISK + "puszczony")
        self.monitor("mouse_button 1", "mouse_button 0", "mouse_button 1", "mouse_button 0")
        self.czekaj(PRZYCISK + "puszczony", od=n, czas=10)
        return trafil

    def prawy_klik(self, x, y):
        """Right click at (x, y) (QEMU: button mask 2). Ekran does not log the right button: the caller waits
        for what the click opens."""
        trafil = self.idz(x, y)
        self.monitor("mouse_button 2", "mouse_button 0")
        return trafil

    def kolko(self, x, y, w_dol=1):
        """The wheel over (x, y) (0.10.0 step 4): `w_dol` notches towards the user (further down; < 0: up), one monitor
        command each (QEMU: mouse_move 0 0 dz — dz > 0 is a turn away from the user, Linux REL_WHEEL +1). Waits until
        ekran counted them (its cursor line: "przewijanie N"). False when the pointer never got there."""
        trafil = self.idz(x, y)
        n = self.przewiniecia()
        self.monitor(*[f"mouse_move 0 0 {-1 if w_dol > 0 else 1}"] * abs(w_dol))
        self.czekaj_na(lambda: self.przewiniecia() >= n + abs(w_dol), czas=10)
        return trafil

    def przewiniecia(self):
        """How many scrollings ekran has counted (its rate-limited cursor line)."""
        m = re.findall(r"K1-EKRAN: kursor .*przewijanie (\d+),", self.tekst())
        return int(m[-1]) if m else 0

    def przeciagnij(self, x1, y1, x2, y2, krokow=4):
        """Dragging (0.8.2 step 6): press at (x1, y1), move in a few steps to (x2, y2) (each one confirmed by ekran's
        cursor line), release. False when the pointer never got to the start."""
        trafil = self.idz(x1, y1)
        self.przycisk(True)
        for i in range(1, krokow + 1):
            self.idz(x1 + (x2 - x1) * i // krokow, y1 + (y2 - y1) * i // krokow)
        self.przycisk(False)
        return trafil

    def klik_z(self, klawisz, x, y):
        """A click with a key held (\"ctrl\", \"shift\"): QEMU's sendkey with a hold time presses the key now and
        lets it go 1.5 s later — the click in between, in one batch of monitor commands."""
        trafil = self.idz(x, y)
        n = self.ile(PRZYCISK + "puszczony")
        self.monitor(f"sendkey {klawisz} 1500", "mouse_button 1", "mouse_button 0")
        self.czekaj(PRZYCISK + "puszczony", od=n, czas=10)
        return trafil

    def pisz(self, *klawisze):
        self.monitor(*[f"sendkey {k}" for k in klawisze])

    def klawisze(self, *zdarzenia):
        """Keys going down and up one by one, in this order (0.10.5 step 6: Alt held while Tab is pressed twice — HMP's
        sendkey lets every key of a combination go at once): ("alt", 1), ("tab", 1), ("tab", 0), … with QEMU's key names
        (qcode). Through QMP: the socket next to the monitor's (scripts/uruchom-qemu.sh: <monitor>.qmp). True when QEMU
        took every one."""
        s = socket.socket(socket.AF_UNIX)
        try:
            s.connect(self.mon + ".qmp")
            s.settimeout(30)
            f = s.makefile("rwb")
            f.readline()  # the greeting
            polecenia = [{"execute": "qmp_capabilities"}] + [
                {"execute": "input-send-event", "arguments": {"events": [
                    {"type": "key", "data": {"down": bool(w), "key": {"type": "qcode", "data": k}}}]}}
                for k, w in zdarzenia]
            for p in polecenia:
                f.write(json.dumps(p).encode() + b"\n")
                f.flush()
                while True:  # events ("event": …) may come first; the answer has "return" or "error"
                    odp = json.loads(f.readline() or b"{}")
                    if "return" in odp or "error" in odp or not odp:
                        break
                if "return" not in odp:
                    return False
            return True
        except (OSError, ValueError):
            return False
        finally:
            s.close()

    # ---- 0.10.2 step 2 (ADR 0045): the screen's description — the tests click by names, not by log positions ----
    # The test program (test image, a disk with the serial number "k1-test-opis") logs what is on the screen whenever it
    # changes: "K1-TEST: ekran <n> <part>/<parts>: <record> | …", record "<kind> <window> <x>,<y>,<w>,<h> <state> <name>".

    OPIS = re.compile(r"K1-TEST: ekran (\d+) (\d+)/(\d+): (.*)")
    REKORD = re.compile(r"([OPBWKIZFM]) (\d+) (-?\d+),(-?\d+),(\d+),(\d+) (\d+) (.+)")
    RODZAJE = {"O": "okno", "P": "panel", "B": "przycisk", "W": "wiersz", "K": "kafelek", "I": "ikona", "Z": "zakladka",
               "F": "pole", "M": "zmienne"}

    def ekran(self):
        """The newest whole description: a list of windows and panels bottom to top, each a dict (rodzaj, okno, x, y, w,
        h, stan, nazwa, elementy: the same dicts) — or [] before the first one."""
        czesci = {}
        for m in self.OPIS.finditer(self.tekst()):
            czesci.setdefault(int(m.group(1)), {})[int(m.group(2))] = (int(m.group(3)), m.group(4))
        for nr in sorted(czesci, reverse=True):
            c = czesci[nr]
            n = next(iter(c.values()))[0]
            if len(c) == n and all(i in c for i in range(1, n + 1)):
                return self._opis(" | ".join(c[i][1] for i in range(1, n + 1)))
        return []

    def _opis(self, tekst):
        okna = []
        for r in tekst.split(" | "):
            m = self.REKORD.fullmatch(r.strip())
            if not m:
                continue
            e = {"rodzaj": self.RODZAJE[m.group(1)], "okno": int(m.group(2)), "x": int(m.group(3)), "y": int(m.group(4)),
                 "w": int(m.group(5)), "h": int(m.group(6)), "stan": int(m.group(7)), "nazwa": m.group(8)}
            if e["rodzaj"] in ("okno", "panel"):
                e["elementy"] = []
                okna.append(e)
            elif okna:
                okna[-1]["elementy"].append(e)
        return okna

    def element(self, okno, nazwa=None, rodzaj=None):
        """The topmost element `nazwa` (kind `rodzaj`, any when None) of the window or panel named `okno` — or that window
        itself when `nazwa` is None; None when the newest description has none."""
        for o in reversed(self.ekran()):
            if o["nazwa"] != okno:
                continue
            if nazwa is None:
                return o
            for e in o["elementy"]:
                if e["nazwa"] == nazwa and (rodzaj is None or e["rodzaj"] == rodzaj):
                    return e
        return None

    def czekaj_na_element(self, okno, nazwa=None, rodzaj=None, czas=30):
        """The element as soon as the description has it; None after `czas` s."""
        wynik = []
        self.czekaj_na(lambda: wynik.append(self.element(okno, nazwa, rodzaj)) or wynik[-1] is not None, czas)
        return wynik[-1] if wynik else None

    @staticmethod
    def srodek(e):
        return e["x"] + e["w"] // 2, e["y"] + e["h"] // 2

    # 0.11.8 step 1: panels whose list scrolls — klik_w scrolls them to the element first
    PRZEWIJANE = ("Menu Start",)

    def przewin_do(self, okno, nazwa, rodzaj=None, zabkow=6):
        """0.11.8 step 1: element `nazwa` of a list that scrolls (the Start menu shows only part of it): while the panel is
        there but the element is not in the description, the wheel over the panel one notch at a time — up first (the
        Start menu opens at the end of its list, D2: A), then down. The element or None."""
        for w_dol in (-1, 1):
            for _ in range(zabkow):
                e = self.element(okno, nazwa, rodzaj)
                o = self.element(okno)
                if e is not None or o is None:
                    return e
                przed = [x["nazwa"] for x in o["elementy"]]
                self.kolko(*self.srodek(o), w_dol=w_dol)
                if not self.czekaj_na(lambda: self.element(okno, nazwa, rodzaj) is not None or
                                      [x["nazwa"] for x in (self.element(okno) or {"elementy": []})["elementy"]] != przed,
                                      czas=5):
                    break  # nothing moved: the end of the list this way
        return self.element(okno, nazwa, rodzaj)

    def klik_w(self, okno, nazwa, rodzaj=None, czas=30):
        """A click in the middle of element `nazwa` of window or panel `okno` (waits for it; a list that scrolls is
        scrolled to it); the element or None."""
        e = None
        if okno in self.PRZEWIJANE and self.czekaj_na_element(okno, czas=czas) is not None:
            e = self.czekaj_na_element(okno, nazwa, rodzaj, czas=3) or self.przewin_do(okno, nazwa, rodzaj)
        e = e or self.czekaj_na_element(okno, nazwa, rodzaj, czas)
        if e is None:
            print(f"  [uwaga] brak elementu „{nazwa}” w „{okno}” w opisie ekranu", flush=True)
            return None
        self.klik(*self.srodek(e))
        return e

    # ---- 0.10.2 step 3: reference screenshots (k1wzorce.py) ----

    def wiersz_sekcji(self, nr):
        """0.10.7 step 2: the row of Ustawienia's section `nr` (S_* in src/aplikacje/ustawienia/ustawienia.h) on its list,
        as the newest Ustawienia logged it ("sekcje na liście: 0 1 3 …") — without a battery Bateria is not there."""
        m = re.findall(r"K1-USTAWIENIA: sekcje na liście: ([\d ]+)", self.tekst())
        lista = [int(v) for v in m[-1].split()] if m else []
        return lista.index(nr) if nr in lista else nr

    def tryb_ekranu(self):
        """(W, H) of the screen's newest mode from ekran's log, or (0, 0)."""
        m = re.findall(r"K1-EKRAN: tryb (\d+)x(\d+)", self.tekst())
        return (int(m[-1][0]), int(m[-1][1])) if m else (0, 0)

    def _wycinek(self, okno, elementy, prostokat):
        """The rectangle (screen) and the changing places in it (its coordinates) — from the newest description."""
        o = self.element(okno) if okno else None
        if prostokat:
            r = prostokat
        elif o and elementy:
            el = [e for e in o["elementy"] if e["rodzaj"] != "zmienne"]
            if not el:
                return None, []
            x0, y0 = min(e["x"] for e in el), min(e["y"] for e in el)
            r = (x0, y0, max(e["x"] + e["w"] for e in el) - x0, max(e["y"] + e["h"] for e in el) - y0)
        elif o:
            r = (o["x"], o["y"], o["w"], o["h"])
        else:
            return None, []
        maski = [(e["x"] - r[0], e["y"] - r[1], e["w"], e["h"]) for e in (o["elementy"] if o else [])
                 if e["rodzaj"] == "zmienne"]
        return r, maski

    def _poza(self, r):
        """The pointer out of rectangle r (the cursor is about 16 × 24 px from its tip): where it is, when it is out
        already; otherwise the nearest point beside r (short moves: the pointer's position in ekran's log lags)."""
        W, H = self.tryb_ekranu()
        poza = lambda x, y: not (r[0] - 24 <= x < r[0] + r[2] + 4 and r[1] - 32 <= y < r[1] + r[3] + 4)
        k = self.kursor_teraz() or tuple(self.kursor)
        if poza(*k):
            return
        kandydaci = [(k[0], r[1] - 40), (k[0], r[1] + r[3] + 12), (r[0] - 32, k[1]), (r[0] + r[2] + 12, k[1])]
        kandydaci = [(x, y) for x, y in kandydaci if 4 <= x < W - 24 and 4 <= y < H - 32 and poza(x, y)]
        if kandydaci:
            self.idz(*min(kandydaci, key=lambda p: abs(p[0] - k[0]) + abs(p[1] - k[1])))

    def wzorzec(self, nazwa, okno=None, elementy=False, prostokat=None, pomin=(), czas=30):
        """Window or panel `okno` (whole; with `elementy`: the box of its elements; or `prostokat` on the screen) equal
        to testy/wzorce/<nazwa>.png pixel by pixel, the changing places and `pomin` (rectangles in the cut-out) left out.
        Waits up to `czas` s for the window to look so; then the check fails with before / after / difference pictures
        in <out>/wzorce/. Without a reference picture: fails too, with the "after" (D3 — the owner approves it)."""
        if okno and not prostokat:
            self.czekaj_na_element(okno, czas=czas)
        r, _ = self._wycinek(okno, elementy, prostokat)
        if r is None:
            return self.sprawdz(f"wzorzec {nazwa}: „{okno}” na ekranie", False, "brak w opisie ekranu")
        self._poza(r)
        katalog = os.path.join(self.out, "wzorce")
        for przyrostek in ("-przed", "-po", "-roznica"):  # of an earlier run (tests run in parallel: only its own)
            try:
                os.unlink(os.path.join(katalog, nazwa + przyrostek + ".png"))
            except OSError:
                pass
        sciezka = os.path.join(k1wzorce.KATALOG, nazwa + ".png")
        wzor = k1wzorce.wczytaj(sciezka)
        ppm = f"{self.out}/.{self.przedrostek}-wzorzec-{os.getpid()}.ppm"
        koniec = time.time() + czas
        ost, takie_same, wynik = None, 0, None
        while True:
            e = ekran(self.mon, ppm)
            r, maski = self._wycinek(okno, elementy, prostokat)
            if e and r and r[0] >= 0 and r[1] >= 0 and r[0] + r[2] <= e[0] and r[1] + r[3] <= e[1]:
                teraz = k1wzorce.zamaskuj(r[2], r[3], k1wzorce.wytnij(e[0], e[1], e[2], *r), maski + list(pomin))
                takie_same = takie_same + 1 if ost == (r[2], r[3], teraz) else 0
                ost = (r[2], r[3], teraz)
                if wzor and wzor[:2] == ost[:2]:
                    wynik = k1wzorce.rozne(r[2], r[3], wzor[2], teraz)
                    if wynik[0] == 0:
                        return self.sprawdz(f"wzorzec {nazwa}: zgodny co do piksela ({r[2]}x{r[3]})", True)
                elif not wzor and takie_same >= 2:
                    break
            if time.time() >= koniec:
                break
            time.sleep(0.2)
        if ost:
            k1wzorce.zapisz(os.path.join(katalog, nazwa + "-po.png"), *ost)
        if not wzor:
            return self.sprawdz(f"wzorzec {nazwa}: brak obrazu wzorcowego", False,
                                f"nowy: wzorce/{nazwa}-po.png — po akceptacji właściciela scripts/przyjmij-wzorce.sh")
        k1wzorce.zapisz(os.path.join(katalog, nazwa + "-przed.png"), *wzor)
        if not ost or wzor[:2] != ost[:2]:
            return self.sprawdz(f"wzorzec {nazwa}: zgodny co do piksela", False,
                                f"inny rozmiar: wzorzec {wzor[0]}x{wzor[1]}, teraz " + (f"{ost[0]}x{ost[1]}" if ost else "brak"))
        k1wzorce.zapisz(os.path.join(katalog, nazwa + "-roznica.png"), ost[0], ost[1],
                        k1wzorce.obraz_roznicy(ost[0], ost[1], wzor[2], ost[2]))
        return self.sprawdz(f"wzorzec {nazwa}: zgodny co do piksela", False,
                            f"{wynik[0]} różnych pikseli w {wynik[1]} — wzorce/{nazwa}-przed/-po/-roznica.png")


def zamknij(gniazdo):
    """Quits a QEMU by its monitor socket (shell scripts: python3 -I testy/qemu/k1qemu.py zamknij GNIAZDO)."""
    s = _polacz(gniazdo, czas=0)  # one attempt: a machine that already stopped needs no waiting
    if s is None:
        return False
    try:
        s.sendall(b"quit\n")
        _do_zachety(s)
        return True
    except OSError:
        return False
    finally:
        s.close()


if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "zamknij":
    sys.exit(0 if zamknij(sys.argv[2]) else 1)
if __name__ == "__main__" and len(sys.argv) == 4 and sys.argv[1] == "zrzut":  # scripts/zrzut-ekranu.sh
    e = ekran(sys.argv[3], sys.argv[2] + ".ppm")
    if not e:
        sys.exit(1)
    zapisz_png(sys.argv[2], *e)
    print(f"{e[0]}x{e[1]}")
if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "log":  # log_k1 in scripts/wspolne.sh
    with open(sys.argv[2], "rb") as f:
        sys.stdout.buffer.write(WTRACENIE.sub(b"", f.read()))

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
import os
import re
import socket
import struct
import sys
import time
import zlib

KURSOR = re.compile(r"K1-EKRAN: kursor (\d+),(\d+) ")
PRZYCISK = r"K1-EKRAN: kursor \d+,\d+ przycisk "
# A kernel message printed in the middle of one of our lines on the shared serial console
# ("K1: wyłączanie[   12.3] reboot: …\nzasilania", the CI failure of 0.6.1): cut out, the line joined again.
WTRACENIE = re.compile(r"(?<=[^\n])\[\s*\d+\.\d+\][^\n]*\n")
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
        self.wyniki = []
        self.kursor = [0, 0]

    # --- log ---
    def tekst(self):
        try:
            return WTRACENIE.sub("", open(self.log, encoding="utf-8", errors="replace").read())
        except OSError:
            return ""

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
            if time.time() >= koniec or time.time() - self.start >= self.limit:
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

    def pisz(self, *klawisze):
        self.monitor(*[f"sendkey {k}" for k in klawisze])


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

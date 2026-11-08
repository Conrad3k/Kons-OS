"""K1 — wspólne funkcje testów w QEMU (0.6.0, ADR 0019).

Do tej pory każdy skrypt testu (test-m2/m4/m5/m5f) miał własną kopię tych samych funkcji: czytanie
logu z portu szeregowego, czekanie na wiersz, monitor QEMU, zrzut ekranu, ruch myszy po pozycji z
logu ekranu, kliknięcie, wynik [OK]/[BŁĄD]. Tutaj są raz. Skrypty uruchamiają Pythona z -I (bez
katalogu skryptu w ścieżce), więc dołączają ten katalog jawnie:

    sys.path.insert(0, os.environ["K1_TESTY_PY"]); from k1qemu import Maszyna
"""
import re
import socket
import struct
import subprocess
import sys
import time
import zlib

KURSOR = re.compile(r"K1-EKRAN: kursor (\d+),(\d+) ")
# A kernel message printed in the middle of one of our lines on the shared serial console
# ("K1: wyłączanie[   12.3] reboot: …\nzasilania", the CI failure of 0.6.1): cut out, the line joined again.
WTRACENIE = re.compile(r"(?<=[^\n])\[\s*\d+\.\d+\][^\n]*\n")


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
    data = zlib.decompress(idat)
    krok = w * 3 + 1
    return w, h, lambda x, y: tuple(data[y * krok + 1 + x * 3:y * krok + 4 + x * 3])


class Maszyna:
    """Jedna uruchomiona maszyna: log, gniazdo monitora, katalog zrzutów, limit czasu."""

    def __init__(self, log, mon, out, zrzut_sh, limit, przedrostek):
        self.log, self.mon, self.out, self.zrzut_sh = log, mon, out, zrzut_sh
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
        """Ostatnie dopasowanie, gdy jest ich więcej niż `od`; None po czasie albo po panice jądra."""
        koniec = time.time() + (czas or self.limit)
        while time.time() < koniec and time.time() - self.start < self.limit:
            m = list(re.finditer(wzor, self.tekst()))
            if len(m) > od:
                return m[-1]
            if "Kernel panic" in self.tekst():
                break
            time.sleep(0.4)
        return None

    def ile(self, wzor):
        return len(re.findall(wzor, self.tekst()))

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
        s = None
        for _ in range(50):
            try:
                s = socket.socket(socket.AF_UNIX)
                s.connect(self.mon)
                break
            except OSError:
                s = None
                time.sleep(0.2)
        if s is None:
            return
        s.settimeout(1)
        for c in ("",) + polecenia:
            s.sendall(c.encode() + b"\n")
            time.sleep(0.05)
            try:
                s.recv(65536)
            except OSError:
                pass
        s.close()

    def zrzut(self, nazwa):
        p = f"{self.out}/{self.przedrostek}-{nazwa}.png"
        subprocess.run([self.zrzut_sh, p, self.mon], stdout=subprocess.DEVNULL, check=False)
        return png(p)

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
            koniec = time.time() + 2.5
            while time.time() < koniec and self.kursor_teraz() != (x, y):
                time.sleep(0.15)
            if self.kursor_teraz() == (x, y):
                return True
        return False

    def klik(self, x, y):
        """Click at (x, y); False when the pointer never got there (the click went elsewhere)."""
        trafil = self.idz(x, y)
        if not trafil:
            print(f"  [uwaga] kursor nie dotarł do {x},{y}", flush=True)
        time.sleep(0.4)
        self.monitor("mouse_button 1")
        time.sleep(0.3)
        self.monitor("mouse_button 0")
        time.sleep(0.4)
        return trafil

    def pisz(self, *klawisze):
        self.monitor(*[f"sendkey {k}" for k in klawisze])


def zamknij(gniazdo):
    """Quits a QEMU by its monitor socket (shell scripts: python3 -I testy/qemu/k1qemu.py zamknij GNIAZDO)."""
    try:
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(5)
        s.connect(gniazdo)
        s.sendall(b"quit\n")
        s.recv(64)
        return True
    except OSError:
        return False


if __name__ == "__main__" and len(sys.argv) == 3 and sys.argv[1] == "zamknij":
    sys.exit(0 if zamknij(sys.argv[2]) else 1)


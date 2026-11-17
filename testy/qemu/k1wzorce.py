"""K1 — zrzuty wzorcowe (0.10.2 krok 3): wygląd okien K1 porównywany co do piksela z zatwierdzonym obrazem.

K1 rysuje wszystko sam, procesorem, bez obcych bibliotek — ten sam kod daje ten sam piksel w każdym uruchomieniu (S9).
Test wycina z zrzutu ekranu okno albo panel według opisu ekranu (ADR 0045) i porównuje z `testy/wzorce/<nazwa>.png`.
Miejsca zmienne (opis: rodzaj „zmienne” — zegar, liczby, wykresy, wersja) i miejsca podane przez test nie liczą się:
we wzorcu są zamalowane kolorem MASKA. Kursor przed zrzutem stoi poza wycinkiem.

Różnica = błąd testu i trzy obrazy w build/testy/wzorce/: <nazwa>-przed.png (wzorzec), -po.png (teraz), -roznica.png
(różne piksele na czerwono na wyblakłym obrazie). Zmianę wyglądu zatwierdza właściciel (decyzja D3): dopiero po jego
„OK” `scripts/przyjmij-wzorce.sh` przenosi obrazy „po” do testy/wzorce/. Brak wzorca = też błąd (z obrazem „po”).

Tylko biblioteka standardowa (PNG: zlib, filtr 0 w każdym wierszu — tak zapisuje go ten moduł).
"""
import os
import struct
import zlib

MASKA = (255, 0, 255)  # a pixel the comparison leaves out (no colour of K1's palette)
KATALOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "wzorce")


def wczytaj(sciezka):
    """(w, h, RGB bytes) of a PNG RGB8 written by zapisz() (filter 0 in every row); None when there is no such file."""
    try:
        raw = open(sciezka, "rb").read()
    except OSError:
        return None
    pos, idat, w, h = 8, b"", 0, 0
    while pos < len(raw):
        n, t = struct.unpack(">I4s", raw[pos:pos + 8])
        d = raw[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b"IHDR":
            w, h = struct.unpack(">II", d[:8])
        elif t == b"IDAT":
            idat += d
    dane = zlib.decompress(idat)
    return w, h, b"".join(dane[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h))


def zapisz(sciezka, w, h, rgb):
    def kawalek(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    wiersze = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    os.makedirs(os.path.dirname(sciezka) or ".", exist_ok=True)
    with open(sciezka, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + kawalek(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + kawalek(b"IDAT", zlib.compress(wiersze, 9)) + kawalek(b"IEND", b""))


def wytnij(sw, sh, rgb, x, y, w, h):
    """Rectangle (x, y, w, h) of a screen sw × sh (it must lie inside)."""
    return b"".join(rgb[((y + r) * sw + x) * 3:((y + r) * sw + x + w) * 3] for r in range(h))


def zamaskuj(w, h, rgb, maski):
    """The rectangles `maski` (inside the cut-out, its coordinates) painted with MASKA."""
    b = bytearray(rgb)
    kolor = bytes(MASKA)
    for mx, my, mw, mh in maski:
        x0, y0, x1, y1 = max(mx, 0), max(my, 0), min(mx + mw, w), min(my + mh, h)
        for r in range(y0, y1):
            if x1 > x0:
                b[(r * w + x0) * 3:(r * w + x1) * 3] = kolor * (x1 - x0)
    return bytes(b)


def rozne(w, h, wzor, teraz):
    """Pixels that differ, MASKA in either image left out: (number, (x0, y0, x1, y1) or None)."""
    n, ramka = 0, None
    maska = bytes(MASKA)
    for r in range(h):
        a, b = wzor[r * w * 3:(r + 1) * w * 3], teraz[r * w * 3:(r + 1) * w * 3]
        if a == b:
            continue
        for x in range(w):
            pa, pb = a[x * 3:x * 3 + 3], b[x * 3:x * 3 + 3]
            if pa != pb and pa != maska and pb != maska:
                n += 1
                ramka = (x, r, x, r) if ramka is None else (min(ramka[0], x), min(ramka[1], r), max(ramka[2], x),
                                                             max(ramka[3], r))
    return n, ramka


def obraz_roznicy(w, h, wzor, teraz):
    """The present picture faded to grey, the differing pixels red (MASKA stays)."""
    b = bytearray(w * h * 3)
    maska = bytes(MASKA)
    for i in range(0, w * h * 3, 3):
        pa, pb = wzor[i:i + 3], teraz[i:i + 3]
        if pa == maska or pb == maska:
            b[i:i + 3] = maska
        elif pa != pb:
            b[i:i + 3] = b"\xff\x00\x00"
        else:
            g = (pb[0] + pb[1] + pb[2]) // 3 // 3 + 160
            b[i:i + 3] = bytes((g, g, g))
    return bytes(b)

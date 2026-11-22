# K1 — the host stand's scenario for the owner's report after 0.11.0 (2026-11-22): "Zapisz obraz jako…" in the menu of a
# picture (Wikipedia, Google's images) closed the zone. GTK's file chooser reads its settings with GSettings, and the zone
# image had the schemas without the compiled file (gschemas.compiled — a package script makes it; we unpack without
# them): GLib ended Firefox at once. Here: right clicks on pictures (in a link, at the edges, after a paste from K1) —
# the menu stays, Firefox lives; then "Zapisz obraz jako…" (Z, Enter) and Enter in the window: the picture is in the session's
# folder of downloads (the agent's), Firefox lives. Run inside stanowisko.py (its functions).
import base64, zlib


def png(w, h):  # a plain picture, enough for Firefox's "image" menu
    wiersz = b"\x00" + b"\xc0\x60\x30" * w
    def blok(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + blok(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            blok(b"IDAT", zlib.compress(wiersz * h)) + blok(b"IEND", b""))


with open(f"{ROOT}/home/k1/Z K1/obraz.png", "wb") as f:
    f.write(png(300, 200))
with open(f"{ROOT}/home/k1/Z K1/menu.html", "w") as f:
    f.write("<!doctype html><meta charset=utf-8><title>obraz</title><body style='margin:0'>"
            "<a href='https://example.org/strona' style='position:fixed;left:100px;top:50px'><img src='obraz.png' width=300 height=200></a>"
            "<a href='https://example.org/dol' style='position:fixed;left:100px;top:480px'><img src='obraz.png' width=300 height=120></a>"
            "<img src='obraz.png' style='position:fixed;left:800px;top:250px' width=200 height=150>"
            "</body>")


def kolejka(linia):
    fd = os.open(f"{ROOT}/run/k1/otworz", os.O_WRONLY | os.O_NONBLOCK)
    os.write(fd, linia.encode() + b"\n")
    os.close(fd)


def prawy(x, y):
    idz(x, y)
    time.sleep(0.2)
    ev(EV_KEY, 0x111, 1)  # BTN_RIGHT
    syn()
    time.sleep(0.1)
    ev(EV_KEY, 0x111, 0)
    syn()


for n in ("obraz.png", "menu.html"):
    os.chown(f"{ROOT}/home/k1/Z K1/{n}", 1000, 1000)
kolejka("file:///home/k1/Z%20K1/menu.html")
time.sleep(6)


def proba(opis, x, y, ruch=None, przed=None):
    if przed:
        przed()
    prawy(x, y)
    time.sleep(1.5)
    for (dx, dy) in ruch or ():
        idz(x + dx, y + dy)
        time.sleep(0.3)
    time.sleep(2)
    zyje = ff.poll() is None and komp.poll() is None
    sprawdz(f"prawy przycisk ({opis}): Firefox żyje", zyje)
    if zyje:
        klawisz(1)  # KEY_ESC
        time.sleep(1)
    return zyje


def wklej_k1():
    fd = os.open(f"{ROOT}/run/k1/otworz", os.O_WRONLY | os.O_NONBLOCK)
    os.write(fd, b"wklej 3 5\nz K1!")
    os.close(fd)
    time.sleep(1)


# the page's top is about 85 px below the window's
proby = [("obraz w linku", 250, 200, None, None),
         ("ruch po menu", 250, 200, [(20, 20), (40, 60), (40, 120), (40, 180)], None),
         ("obraz przy dolnej krawędzi — menu odwrócone", 250, 660, [(20, -20), (40, -80)], None),
         ("obraz przy prawej krawędzi", 980, 400, [(-40, 20)], None),
         ("po wklejeniu tekstu z K1", 250, 200, [(30, 30)], wklej_k1)]
for pr in proby:
    if not proba(*pr):
        break
if ff.poll() is None:
    prawy(900, 400)
    time.sleep(1.5)
    klawisz(KLAWISZE["z"])  # "Zapisz obraz jako…" lit (more items start with Z), Enter takes it
    klawisz(28)
    time.sleep(6)
    log_f.flush()
    okno_zapisu = 'set_title("Zapisz obraz' in open(f"{praca}/firefox.log", errors="replace").read()
    sprawdz("„Zapisz obraz jako…”: okno wyboru pliku otwarte, Firefox żyje",
            okno_zapisu and ff.poll() is None and komp.poll() is None)
    klawisz(28)  # Enter: save where it proposes
    time.sleep(4)
    sprawdz("…Enter zapisuje obraz w folderze pobranych sesji (widzi go agent K1), Firefox żyje",
            ff.poll() is None and any(n.endswith(".png") for n in os.listdir(f"{ROOT}/home/k1/Pobrane")))
zrzut("menu")
log_f.flush()
log_k.flush()
print("  kompozytor.log (koniec):", flush=True)
for l in open(f"{praca}/kompozytor.log", errors="replace").read().splitlines()[-25:]:
    print("   ", l[:220], flush=True)
if ff.poll() is not None:
    print("  firefox.log (koniec):", flush=True)
    for l in open(f"{praca}/firefox.log", errors="replace").read().splitlines()[-60:]:
        print("   ", l[:220], flush=True)

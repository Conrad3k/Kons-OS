# K1 — the host stand's scenario for 0.11.4 (the owner's report after 0.11.3): "Zapisz obraz jako…" on a laptop — GTK's
# file chooser was taller than the session's window, the compositor centred it and its top row (with "Zapisz") was above
# the display; and K1 did not always ask about a saved file — the agent watched only the folder of downloads, while the
# chooser may save anywhere in the browser's home. Run with a laptop's session window:
#   K1_EKRAN=1366x640 scripts/stanowisko.sh testy/stanowisko/zapis.py
# Here: the real agent (build/bin/agent, without the VMM: it says in its log what it would tell K1) next to Firefox; the
# chooser fits the display (the compositor's log, a picture of it: zapis-okno.ppm); Enter saves into the downloads, a path
# typed in the chooser into the home — the agent sees both. Run inside stanowisko.py (its functions).
import zlib


def png(w, h):  # a plain picture, enough for Firefox's "image" menu
    wiersz = b"\x00" + b"\x30\x60\xc0" * w
    def blok(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + blok(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            blok(b"IDAT", zlib.compress(wiersz * h)) + blok(b"IEND", b""))


with open(f"{ROOT}/tmp/obraz.png", "wb") as f:
    f.write(png(400, 300))
with open(f"{ROOT}/tmp/obraz.html", "w") as f:
    f.write("<!doctype html><meta charset=utf-8><title>obraz</title><body style='margin:0'>"
            "<img src='obraz.png' style='position:fixed;left:100px;top:100px' width=400 height=300></body>")
AGENT = os.environ["K1_AGENT"]
sh("mount", "--bind", AGENT, f"{ROOT}/usr/lib/k1/agent")
log_a = open(f"{praca}/agent.log", "w")
agent = subprocess.Popen(["/usr/sbin/chroot", "--userspec=1000:1000", ROOT, "/usr/lib/k1/agent", "/home/k1/Pobrane",
                          "/home/k1/Z K1", "/run/k1/otworz"], stdout=log_a, stderr=log_a)


def agent_mowi(co):
    log_a.flush()
    return open(f"{praca}/agent.log", errors="replace").read().count(co)


def prawy(x, y):
    idz(x, y)
    time.sleep(0.2)
    ev(EV_KEY, 0x111, 1)  # BTN_RIGHT
    syn()
    time.sleep(0.1)
    ev(EV_KEY, 0x111, 0)
    syn()


def okno_zapisu():  # right click on the picture, "Zapisz obraz jako…" (Z, Enter), the chooser open
    prawy(300, 250)
    time.sleep(1.5)
    klawisz(KLAWISZE["z"])
    klawisz(28)
    time.sleep(6)


time.sleep(2)
sprawdz("agent gotowy", agent_mowi("gotowy") == 1 and agent.poll() is None)
klawisz(KLAWISZE["l"], ctrl=True)
time.sleep(0.5)
pisz("file:///tmp/obraz.html\n")
time.sleep(4)
okno_zapisu()
zrzut("zapis-okno")  # the compositor draws on a change: the picture may come later, as zrzut.ppm
log_k.flush()
dopasowane = re.findall(r"okno dialogowe (\d+)×(\d+) większe niż ekran (\d+)×(\d+) — prośba o (\d+)×(\d+)",
                        open(f"{praca}/kompozytor.log", errors="replace").read())
print(f"  kompozytor: {dopasowane or 'okno dialogowe mieści się na ekranie'}", flush=True)
sprawdz("„Zapisz obraz jako…”: okno wyboru pliku otwarte, Firefox żyje", ff.poll() is None and komp.poll() is None)
klawisz(28)  # Enter: save where it proposes — the downloads
time.sleep(4)
sprawdz("Enter: obraz w folderze pobranych, agent go zgłasza",
        any(n.endswith(".png") for n in os.listdir(f"{ROOT}/home/k1/Pobrane")) and agent_mowi("plik do zgłoszenia") == 1)
okno_zapisu()
klawisz(KLAWISZE["a"], ctrl=True)  # the name field: a path into the home instead
pisz("/home/k1/dom.png\n")
time.sleep(4)
sprawdz("ścieżka w oknie: obraz w katalogu domowym, agent go zgłasza (do 0.11.3: nic)",
        os.path.exists(f"{ROOT}/home/k1/dom.png") and agent_mowi("plik do zgłoszenia") == 2)
sprawdz("Firefox i kompozytor żyją", ff.poll() is None and komp.poll() is None)
for d, ds, fs in os.walk(f"{ROOT}/home/k1"):  # where the pictures went
    if "/profil" not in d:
        print("  ", d[len(ROOT):], fs, flush=True)
print("   /tmp", os.listdir(f"{ROOT}/tmp"), flush=True)
agent.terminate()
log_a.flush()
print("  agent.log:", flush=True)
for l in open(f"{praca}/agent.log", errors="replace").read().splitlines()[-10:]:
    print("   ", l[:200], flush=True)

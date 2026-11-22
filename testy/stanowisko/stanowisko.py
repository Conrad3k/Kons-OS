# K1 — the host stand (0.10.1 step 5; the way step 1 found rule 28): the zone's compositor built on the host with ASan
# and Firefox from the zone image (build/strefa-przegladarka) in a chroot, without a machine. The compositor's input is
# a FIFO it opens as /dev/input/event0 (struct input_event, ABS 0…32767 — as the VMM's virtio-input), its display is
# in memory (testy/stanowisko/ekran.c). Firefox reports what it gets with WAYLAND_DEBUG (firefox.log); the test page
# puts how far it is scrolled into its title ("y=N"), which the log shows in xdg_toplevel.set_title.
# Run by scripts/stanowisko.sh, inside its own mount namespace (unshare -m): python3 -I stanowisko.py IMAGE WORK SCEN
import os, re, struct, subprocess, sys, time

obraz, praca, scenariusz = sys.argv[1], sys.argv[2], sys.argv[3]
KOMPOZYTOR = os.environ["K1_KOMPOZYTOR"]
W, H = 1024, 700  # testy/stanowisko/ekran.c
ROOT = "/mnt/k1-stanowisko"  # short: the compositor's socket path must fit in sun_path


def sh(*a):
    subprocess.run(a, check=True)


# the zone's root: the image read-only with tmpfs where gosc puts them (gosc/przegladarka.c)
os.makedirs(ROOT, exist_ok=True)
sh("mount", "--bind", "-o", "ro", obraz, ROOT)
for d in ("tmp", "run", "home", "dev"):
    sh("mount", "-t", "tmpfs", "-o", "mode=1777" if d == "tmp" else "mode=0755", "tmpfs", f"{ROOT}/{d}")
for n in ("null", "zero", "urandom", "random"):
    open(f"{ROOT}/dev/{n}", "w").close()
    sh("mount", "--bind", f"/dev/{n}", f"{ROOT}/dev/{n}")
os.makedirs(f"{ROOT}/dev/shm")
sh("mount", "-t", "tmpfs", "-o", "mode=1777", "tmpfs", f"{ROOT}/dev/shm")
sh("mount", "-t", "proc", "proc", f"{ROOT}/proc")
os.makedirs(f"{ROOT}/run/k1")
with open(f"{ROOT}/run/machine-id", "w") as f:  # as gosc (id_maszyny): GLib looks for D-Bus with it
    f.write("%032x\n" % int.from_bytes(os.urandom(16), "big"))
os.makedirs(f"{ROOT}/home/k1/profil")
os.makedirs(f"{ROOT}/home/k1/Pobrane")  # 0.11.0 step 2: the folder of downloads (gosc/przegladarka.c)
os.makedirs(f"{ROOT}/home/k1/Z K1")  # 0.11.0 step 3: files from Pliki (the agent writes there)
sh("chown", "-R", "1000:1000", f"{ROOT}/home/k1")
os.makedirs("/dev/input", exist_ok=True)
sh("mount", "-t", "tmpfs", "tmpfs", "/dev/input")
os.mkfifo("/dev/input/event0")
fifo = os.open("/dev/input/event0", os.O_RDWR)  # a writer for good: no end of file for the compositor

STRONA = ("<!doctype html><html><head><meta charset=utf-8><title>y=0</title></head><body style='margin:0'><script>"
          "for (let i = 0; i < 60; i++) { const d = document.createElement('div'); d.style.height = '200px';"
          " d.style.background = 'hsl(' + (i * 37 % 360) + ',70%,50%)'; d.textContent = 'blok ' + i;"
          " document.body.appendChild(d); }"
          "addEventListener('scroll', () => { document.title = 'y=' + Math.round(scrollY); });</script></body></html>")
with open(f"{ROOT}/tmp/d.html", "w") as f:
    f.write(STRONA)

log_k = open(f"{praca}/kompozytor.log", "w")
komp = subprocess.Popen([KOMPOZYTOR, f"{ROOT}/run/k1/wayland-0", f"{ROOT}/usr/share/k1/klawiatura.xkb"],
                        stdout=log_k, stderr=log_k,
                        env=dict(os.environ, K1_ZRZUT=f"{praca}/zrzut", ASAN_OPTIONS="detect_leaks=0"))
while not os.path.exists(f"{ROOT}/run/k1/wayland-0"):
    time.sleep(0.1)
env_p = {"PATH": "/usr/bin", "HOME": "/home/k1", "XDG_RUNTIME_DIR": "/run/k1", "WAYLAND_DISPLAY": "wayland-0",
         "MOZ_ENABLE_WAYLAND": "1", "LANG": "C.UTF-8", "MOZ_CRASHREPORTER_DISABLE": "1",
         "MOZ_DISABLE_WAYLAND_PROXY": "1", "GSETTINGS_BACKEND": "memory", "WAYLAND_DEBUG": "client"}
log_f = open(f"{praca}/firefox.log", "w")
ff = subprocess.Popen(["/usr/sbin/chroot", "--userspec=1000:1000", ROOT, "/usr/bin/firefox-esr", "--no-remote",
                       "--profile", "/home/k1/profil", "file:///tmp/d.html"], stdout=log_f, stderr=log_f, env=env_p)

EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3
BTN_LEFT, BTN_TOOL_FINGER, REL_WHEEL, REL_WHEEL_HI_RES = 0x110, 0x145, 8, 11
SRODEK = (500, 400)


def ev(t, c, v):
    os.write(fifo, struct.pack("llHHi", 0, 0, t, c, v))


def syn():
    ev(EV_SYN, 0, 0)


def idz(x, y):
    ev(EV_ABS, 0, x * 32767 // (W - 1))
    ev(EV_ABS, 1, y * 32767 // (H - 1))
    syn()


def kolko(n):  # n notches, > 0 down (REL_WHEEL < 0), as the VMM: the 1/120 parts and the notch in one frame
    for _ in range(abs(n)):
        v = -1 if n > 0 else 1
        ev(EV_REL, REL_WHEEL_HI_RES, v * 120)
        ev(EV_REL, REL_WHEEL, v)
        syn()
        time.sleep(0.05)


def dokladne(dy, kroki=8):  # a precise wheel: parts of a notch with no notch, dy > 0 down
    for _ in range(kroki):
        ev(EV_REL, REL_WHEEL_HI_RES, -dy)
        syn()
        time.sleep(0.03)


def palce(dy, kroki=10):  # two fingers on a touchpad, as the VMM (0.10.1 step 5): BTN_TOOL_FINGER around the parts
    for _ in range(kroki):
        ev(EV_KEY, BTN_TOOL_FINGER, 1)
        ev(EV_REL, REL_WHEEL_HI_RES, -dy)
        ev(EV_KEY, BTN_TOOL_FINGER, 0)
        syn()
        time.sleep(0.016)


def klik(x, y):
    idz(x, y)
    ev(EV_KEY, BTN_LEFT, 1)
    syn()
    time.sleep(0.05)
    ev(EV_KEY, BTN_LEFT, 0)
    syn()


KLAWISZE = dict(zip("1234567890", range(2, 12)))
KLAWISZE.update(zip("qwertyuiop", range(16, 26)))
KLAWISZE.update(zip("asdfghjkl", range(30, 39)))
KLAWISZE.update(zip("zxcvbnm", range(44, 51)))
KLAWISZE.update({".": 52, "/": 53, ";": 39, "\n": 28})


def klawisz(k, shift=False, ctrl=False):
    for m, jest in ((29, ctrl), (42, shift)):
        if jest:
            ev(EV_KEY, m, 1)
            syn()
    ev(EV_KEY, k, 1)
    syn()
    ev(EV_KEY, k, 0)
    syn()
    for m, jest in ((42, shift), (29, ctrl)):
        if jest:
            ev(EV_KEY, m, 0)
            syn()
    time.sleep(0.03)


def pisz(tekst):
    for c in tekst:
        klawisz(39, shift=True) if c == ":" else klawisz(KLAWISZE[c])


def zrzut(nazwa):
    open(f"{praca}/zrzut.prosba", "w").close()
    idz(*SRODEK)  # something to draw
    time.sleep(0.5)
    if os.path.exists(f"{praca}/zrzut.ppm"):
        os.replace(f"{praca}/zrzut.ppm", f"{praca}/{nazwa}.ppm")


def przewinieto():  # how far the page shown now is scrolled (its title); None before the first title
    log_f.flush()
    t = re.findall(r'set_title\("y=(\d+)', open(f"{praca}/firefox.log", errors="replace").read())
    return int(t[-1]) if t else None


wyniki = []


def sprawdz(opis, ok):
    wyniki.append(bool(ok))
    print(f"  [{'OK' if ok else 'BŁĄD'}] {opis} (y={przewinieto()})", flush=True)


try:
    time.sleep(25)  # Firefox's first window
    idz(*SRODEK)
    exec(open(scenariusz).read())
finally:
    ff.terminate()
    try:
        ff.wait(10)
    except subprocess.TimeoutExpired:
        ff.kill()
    komp.terminate()
    komp.wait()
bledy_asan = [l for l in open(f"{praca}/kompozytor.log", errors="replace") if "ERROR: AddressSanitizer" in l or
              "runtime error" in l]
sprawdz("kompozytor bez błędów ASan/UBSan", not bledy_asan)
sys.exit(0 if all(wyniki) else 1)

# K1 — the host stand's scenario for 0.13.2 step 2 (ADR 0063 pkt 7): Firefox from the zone image plays H.264 with our
# FFmpeg (libavcodec.so.61 in the image) — before it, a .mp4 with H.264 did not play at all. A short film made on this
# computer (ffmpeg from the host's packages — only here, not in the image) lies next to a page that says in its title what
# the browser can play (canPlayType, MediaSource.isTypeSupported) and how many frames it decoded. The frames are decoded
# by the processor (VA-API is off in k1.js; films by the card — step 4).
#   scripts/stanowisko.sh testy/stanowisko/film.py
# Run inside stanowisko.py (its functions).
import shutil

FFMPEG = shutil.which("ffmpeg")
sprawdz("ffmpeg na komputerze (film próbny)", FFMPEG is not None)
subprocess.run([FFMPEG, "-loglevel", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=30", "-t", "6",
                "-c:v", "libx264", "-profile:v", "high", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                f"{ROOT}/tmp/film.mp4"], check=True)
with open(f"{ROOT}/tmp/film.html", "w") as f:
    f.write("<!doctype html><meta charset=utf-8><title>film=czeka</title><body style='margin:0'>"
            "<video id=v src='film.mp4' muted loop width=640 height=360></video><script>"
            "const v = document.getElementById('v');"
            "const umie = v.canPlayType('video/mp4; codecs=\"avc1.64001E\"');"
            "const mse = window.MediaSource && MediaSource.isTypeSupported('video/mp4; codecs=\"avc1.64001E\"');"
            "v.onerror = () => { document.title = 'film=blad umie=' + umie + ' mse=' + mse; };"
            "v.play().catch(() => {});"
            "setInterval(() => { const q = v.getVideoPlaybackQuality();"
            " document.title = 'film=' + (q.totalVideoFrames > 0 ? 'gra' : 'stoi') + ' umie=' + umie + ' mse=' + mse +"
            " ' klatki=' + q.totalVideoFrames; }, 500);</script></body>")


def tytul_filmu():
    log_f.flush()
    t = re.findall(r'set_title\("(film=[^"]*)', open(f"{praca}/firefox.log", errors="replace").read())
    return t[-1] if t else ""


klawisz(KLAWISZE["l"], ctrl=True)
time.sleep(0.5)
pisz("file:///tmp/film.html\n")
for _ in range(40):  # up to 20 s: the RDD process starts and loads libavcodec
    time.sleep(0.5)
    m = re.search(r"klatki=(\d+)", tytul_filmu())
    if m and int(m.group(1)) >= 60:
        break
t = tytul_filmu()
print(f"  tytuł strony: {t}", flush=True)
sprawdz("Firefox umie H.264 (canPlayType = probably)", "umie=probably" in t)
sprawdz("MediaSource przyjmuje H.264 (strony jak YouTube)", "mse=true" in t)
m = re.search(r"klatki=(\d+)", t)
sprawdz("film H.264 gra (co najmniej 60 klatek rozpakowanych)", m is not None and int(m.group(1)) >= 60)
zrzut("film")

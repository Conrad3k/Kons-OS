# K1 — the host stand's scenario for 0.11.0 step 5 (ADR 0052, T8, D5: A): the clipboard between Firefox and K1, as the
# agent sees it. Copy: Ctrl+A, Ctrl+C in a field of a page — the compositor reads the browser's selection and writes
# "kopia <n>\n" and the text on its FIFO to the agent (/run/k1/schowek). Paste: K1's text ("wklej <nr> <n>\n" and the bytes
# on /run/k1/otworz, as the agent writes it before the VMM lets Ctrl+V go) — the compositor answers "wklejone <nr>", and
# Ctrl+V in another field pastes exactly that text (the page's title shows it). Negative: a line "wklej" with a length
# past the limit and text that is not UTF-8 — refused, the field keeps K1's text. Run inside stanowisko.py.
STRONA_S = ("<!doctype html><meta charset=utf-8><title>czeka</title><body style='margin:0'>"
            "<input id=z value='Ze strony: zażółć' style='position:fixed;left:20px;top:20px;width:400px;height:40px'>"
            "<textarea id=d style='position:fixed;left:20px;top:120px;width:400px;height:120px'></textarea>"
            "<script>document.getElementById('d').addEventListener('input', e => {"
            " document.title = 'wklejone=' + e.target.value; });</script></body>")
with open(f"{ROOT}/home/k1/Z K1/schowek.html", "w") as f:
    f.write(STRONA_S)
os.chown(f"{ROOT}/home/k1/Z K1/schowek.html", 1000, 1000)
od_komp = os.open(f"{ROOT}/run/k1/schowek", os.O_RDONLY | os.O_NONBLOCK)


def kolejka(dane):
    fd = os.open(f"{ROOT}/run/k1/otworz", os.O_WRONLY | os.O_NONBLOCK)
    os.write(fd, dane)
    os.close(fd)


def od_kompozytora(czas=5):
    b, koniec = b"", time.time() + czas
    while time.time() < koniec:
        try:
            b += os.read(od_komp, 1 << 20)
        except BlockingIOError:
            time.sleep(0.2)
    return b


def tytuly():
    log_f.flush()
    return re.findall(r'set_title\("([^"]*)', open(f"{praca}/firefox.log", errors="replace").read())


kolejka(b"file:///home/k1/Z%20K1/schowek.html\n")
time.sleep(6)
sprawdz("strona z polami otwarta", any(t.startswith("czeka") for t in tytuly()))
# the page's top is about 85 px below the window's (Firefox's bars): the field "z" at 20..60 of the page
klik(200, 125)
time.sleep(0.5)
klawisz(KLAWISZE["a"], ctrl=True)
klawisz(46, ctrl=True)  # KEY_C
b = od_kompozytora()
tekst = "Ze strony: zażółć".encode()
sprawdz("kopia w Firefoksie: kompozytor daje agentowi „kopia N” i dokładnie ten tekst",
        b"kopia %d\n" % len(tekst) + tekst in b)
print("  od kompozytora:", repr(b[:80]), flush=True)
time.sleep(1.2)
tekst_k1 = "Z K1: ąę\nwiersz 2".encode()
kolejka(b"wklej 7 %d\n" % len(tekst_k1) + tekst_k1)
b = od_kompozytora(3)
sprawdz("tekst z K1: kompozytor robi go zaznaczeniem i odpowiada „wklejone 7”", b"wklejone 7\n" in b)
klik(200, 260)  # the field "d"
time.sleep(0.5)
klawisz(47, ctrl=True)  # KEY_V
time.sleep(3)
print("  tytuły:", tytuly()[-3:], flush=True)
sprawdz("Ctrl+V w Firefoksie wkleja tekst z K1 (nowy wiersz w tytule strony to spacja)",
        any(t.startswith("wklejone=Z K1: ąę wiersz 2") for t in tytuly()))
log_k.flush()
przed = open(f"{praca}/kompozytor.log", errors="replace").read().count("schowek: prośba odrzucona")
kolejka(b"wklej 8 99999999\n")
time.sleep(1)
kolejka(b"wklej 9 3\n\xff\xfe\x00")
time.sleep(2)
log_k.flush()
k_log = open(f"{praca}/kompozytor.log", errors="replace").read()
sprawdz("NEGATYWNY: długość ponad 1 MiB — odrzucona; tekst nie UTF-8 — nie trafia do okna",
        k_log.count("schowek: prośba odrzucona") == przed + 1 and "schowek: tekst z K1 nie trafił do okna" in k_log and
        b"wklejone 9" not in od_kompozytora(2))
sprawdz("Firefox i kompozytor żyją", ff.poll() is None and komp.poll() is None)

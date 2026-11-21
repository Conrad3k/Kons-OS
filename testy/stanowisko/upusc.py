# K1 — the host stand's scenario for 0.11.0 step 4 (ADR 0052, P22): files dropped on the session's window in K1. The agent
# puts the copies in "Z K1" and writes "upusc X Y <addresses>" on the compositor's FIFO; the compositor plays the drop
# there (wl_data_device: an offer of text/uri-list, enter, motion, drop). Firefox must take them as files: a page with a
# drop target (its title: the names it got) and an <input type=file> (a drop on it fills it — its change event; the page
# leaves that drop to Firefox).
# Then the negative ones: an address outside "Z K1", a point outside the display — refused, nothing dropped.
# Run inside stanowisko.py (its functions).
STRONA_U = ("<!doctype html><meta charset=utf-8><title>czeka</title><body style='margin:0;height:100vh'>"
            "<input type=file id=f style='position:fixed;left:0;top:0;width:300px;height:120px'>"
            "<script>"
            "addEventListener('dragover', e => { if (e.target.id != 'f') e.preventDefault(); });"
            "addEventListener('drop', e => { if (e.target.id == 'f') return; e.preventDefault();"
            " document.title = 'upusc=' + [...e.dataTransfer.files].map(f => f.name + ':' + f.size).join(','); });"
            "document.getElementById('f').addEventListener('change', e => {"
            " document.title = 'pole=' + [...e.target.files].map(f => f.name).join(','); });"
            "</script></body>")
Z_K1 = f"{ROOT}/home/k1/Z K1"
for nazwa, tresc in (("upusc.html", STRONA_U), ("raport z K1.txt", "z K1\n" * 100), ("zdjecie.jpg", "x" * 2048)):
    with open(f"{Z_K1}/{nazwa}", "w") as f:
        f.write(tresc)
    os.chown(f"{Z_K1}/{nazwa}", 1000, 1000)


def kolejka(linia):
    fd = os.open(f"{ROOT}/run/k1/otworz", os.O_WRONLY | os.O_NONBLOCK)
    os.write(fd, linia.encode() + b"\n")
    os.close(fd)


def tytuly():
    log_f.flush()
    return re.findall(r'set_title\("([^"]*)', open(f"{praca}/firefox.log", errors="replace").read())


def log_kompozytora():
    log_k.flush()
    return open(f"{praca}/kompozytor.log", errors="replace").read()


kolejka("file:///home/k1/Z%20K1/upusc.html")  # the page, through "Otwórz w…" (step 3)
time.sleep(6)
sprawdz("strona z celem upuszczenia otwarta", any(t.startswith("czeka") for t in tytuly()))
time.sleep(1.5)  # the compositor takes one line a second
przed = len(tytuly())
kolejka("upusc 600 450 file:///home/k1/Z%20K1/raport%20z%20K1.txt file:///home/k1/Z%20K1/zdjecie.jpg")
time.sleep(4)
zrzut("upusc")
nowe = tytuly()[przed:]
print("  tytuły:", nowe, flush=True)
sprawdz("upuszczenie na stronę: Firefox dostaje oba pliki z „Z K1” (nazwy i rozmiary)",
        any(t.startswith("upusc=raport z K1.txt:500,zdjecie.jpg:2048") for t in nowe))
sprawdz("kompozytor: przeglądarka odebrała adresy i przyjęła pliki",
        "upuszczenie: przeglądarka odebrała adresy" in log_kompozytora() and
        "upuszczenie: przeglądarka przyjęła pliki" in log_kompozytora())
time.sleep(1.5)
przed = len(tytuly())
kolejka("upusc 150 150 file:///home/k1/Z%20K1/zdjecie.jpg")  # onto the field "Prześlij plik" (under the toolbar)
time.sleep(4)
nowe = tytuly()[przed:]
print("  tytuły:", nowe, flush=True)
sprawdz("upuszczenie na pole wyboru pliku: pole ma plik z „Z K1”", any(t.startswith("pole=zdjecie.jpg") for t in nowe))
time.sleep(1.5)
przed, odrzucone = len(tytuly()), log_kompozytora().count("upuszczenie: prośba odrzucona")
kolejka("upusc 600 450 file:///etc/passwd")
time.sleep(1.5)
kolejka("upusc 6000 450 file:///home/k1/Z%20K1/zdjecie.jpg")
time.sleep(1.5)
kolejka("upusc 600 450")
time.sleep(3)
sprawdz("NEGATYWNY: adres spoza „Z K1”, punkt poza ekranem, bez adresów — odrzucone, nic nie upuszczone",
        not any(t.startswith("upusc=") or t.startswith("pole=") for t in tytuly()[przed:]) and
        log_kompozytora().count("upuszczenie: prośba odrzucona") == odrzucone + 3)
sprawdz("Firefox i kompozytor żyją", ff.poll() is None and komp.poll() is None)

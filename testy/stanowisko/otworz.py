# K1 — the host stand's scenario for 0.11.0 step 3 (ADR 0052, P16): "Otwórz w…" without D-Bus. A file is in the session's
# folder "Z K1" (as the agent writes it); the agent's line on the compositor's FIFO (/run/k1/otworz) makes the compositor
# type Ctrl+L, the address and Alt+Enter — Firefox must open the file in a new tab (its title in firefox.log). Then the
# negative ones: an address outside "Z K1" and one with characters the compositor does not type — refused, no new tab.
# Run inside stanowisko.py (its functions).
with open(f"{ROOT}/home/k1/Z K1/strona z K1.html", "w") as f:  # a page: its title shows in firefox.log (a text file's not)
    f.write("<!doctype html><title>plik-z-K1-otwarty</title><p>z K1</p>")
os.chown(f"{ROOT}/home/k1/Z K1/strona z K1.html", 1000, 1000)


def kolejka(linia):
    fd = os.open(f"{ROOT}/run/k1/otworz", os.O_WRONLY | os.O_NONBLOCK)
    os.write(fd, linia.encode() + b"\n")
    os.close(fd)


def tytuly():
    log_f.flush()
    return re.findall(r'set_title\("([^"]*)', open(f"{praca}/firefox.log", errors="replace").read())


przed = len(tytuly())
kolejka("file:///home/k1/Z%20K1/strona%20z%20K1.html")
time.sleep(6)
zrzut("otworz")
nowe = tytuly()[przed:]
print("  tytuły:", nowe, flush=True)
sprawdz("„Otwórz w…”: Firefox otwiera plik z „Z K1” w nowej karcie (tytuł strony z pliku)",
        any("plik-z-K1-otwarty" in t for t in nowe))
przed = len(tytuly())
log_k.flush()
kolejka("file:///etc/passwd")
kolejka("file:///home/k1/Z%20K1/a b")
time.sleep(4)
log_k.flush()
sprawdz("NEGATYWNY: adres spoza „Z K1” i ze spacją — kompozytor odrzuca, żadnej nowej karty",
        not any("passwd" in t or "a b" in t for t in tytuly()[przed:]) and
        open(f"{praca}/kompozytor.log", errors="replace").read().count("otwórz w: prośba odrzucona") == 2)
sprawdz("Firefox i kompozytor żyją", ff.poll() is None and komp.poll() is None)

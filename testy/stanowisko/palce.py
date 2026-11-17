# K1 — the host stand's scenario for 0.10.1 step 5 (D6): two fingers on the first tab, a new tab, then the fingers,
# a wheel and a precise wheel there, and back on the first tab. Before the fix the fingers' gesture never ended
# (no axis_stop): after the new tab nothing scrolled with the fingers or the precise wheel, also after moving the
# pointer, a click and going back to the first tab. Run inside stanowisko.py (its functions).
palce(40)
time.sleep(2)
sprawdz("karta 1: dwa palce przewijają w dół", (przewinieto() or 0) > 0)
palce(-40)
time.sleep(2)
sprawdz("karta 1: i z powrotem na górę", przewinieto() == 0)
klawisz(KLAWISZE["t"], ctrl=True)
time.sleep(3)
pisz("file:///tmp/d.html\n")
time.sleep(5)
idz(*SRODEK)
time.sleep(0.5)
palce(40)
time.sleep(2)
y1 = przewinieto() or 0
sprawdz("nowa karta: dwa palce przewijają (przed poprawką: nic)", y1 > 0)
idz(SRODEK[0] + 40, SRODEK[1] + 20)
palce(40)
time.sleep(2)
y2 = przewinieto() or 0
sprawdz("nowa karta: po ruchu wskaźnika dalej", y2 > y1)
kolko(3)
time.sleep(2)
y3 = przewinieto() or 0
sprawdz("nowa karta: kółko", y3 > y2)
klik(600, 450)
time.sleep(1)
palce(40)
time.sleep(2)
y4 = przewinieto() or 0
sprawdz("nowa karta: palce po kliknięciu w stronę", y4 > y3)
dokladne(30)
time.sleep(2)
y5 = przewinieto() or 0
sprawdz("nowa karta: dokładne kółko (części ząbka) — przed poprawką brane za palce", y5 > y4)
zrzut("nowa-karta")
klik(150, 22)  # the first tab
time.sleep(2)
idz(*SRODEK)
time.sleep(0.5)
palce(40)
time.sleep(2)
sprawdz("karta 1 po powrocie: dwa palce przewijają", (przewinieto() or 0) > 0)

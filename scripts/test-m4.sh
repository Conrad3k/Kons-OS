#!/bin/sh
# Test etapu M4 (powtarzalny): pulpit i aplikacje w działającym K1 (obraz produkcyjny) oraz start
# z obrazu ISO (UEFI i BIOS, grafika z firmware — simpledrm). Sterowanie przez monitor QEMU (mysz,
# klawiatura), dowody: log + zrzuty ekranu sprawdzane automatycznie (build/testy/m4-*.png).
# Wymaga: scripts/zbuduj.sh (z obrazem ISO).
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
ISO=$K1_WYNIK/k1-$K1_WERSJA.iso
mkdir -p "$TESTY"; rm -f "$TESTY"/m4-*
r=0

# uruchom NAZWA [opcje uruchom-qemu.sh] — QEMU w tle; log i gniazdo: $TESTY/m4-NAZWA.{log,sock}
uruchom() {
    n=$1; shift
    rm -f "$TESTY/m4-$n.log" "$TESTY/m4-$n.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/m4-$n.log" --monitor "$TESTY/m4-$n.sock" --czas "$CZAS" "$@" &
    QPID=$!
}
sprawdz() { # NAZWA — scenariusz w Pythonie (niżej) na działającej maszynie
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/m4.py" "$TESTY/m4-$1.log" "$TESTY/m4-$1.sock" "$TESTY" "$CZAS" "$1" || r=1
    zamknij_qemu "$TESTY/m4-$1.sock" $QPID
}

cat > "$K1_BUDOWA/m4.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna, ekran, piksele
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "m4")
tekst, czekaj, monitor, zrzut, sprawdz = q.tekst, q.czekaj, q.monitor, q.zrzut, q.sprawdz
kursor_teraz, idz, klik, pisz, kursor, wyniki, przycisk = q.kursor_teraz, q.idz, q.klik, q.pisz, q.kursor, q.wyniki, q.przycisk

jasny = lambda c: min(c) > 200
ciemny = lambda c: max(c) < 70
akcent = lambda c: c[0] > 170 and c[1] < 150 and c[2] < 120 and c[0] > c[1] > c[2]

if tryb == "haslo":
    # M5e: the vault has a password now — the password screen instead of the desktop
    e = czekaj(r"K1-LOGOWANIE: ekran hasła gotowy", czas=90)
    sprawdz("magazyn z hasłem: zamiast pulpitu ekran hasła (wariant B „Zegar”)", e and "K1-GUI: pulpit gotowy" not in tekst())
    zrzut("haslo-1-ekran")
    pisz("z", "l", "e", "1", "ret")
    sprawdz("złe hasło odrzucone (skarbiec i ekran hasła)",
            czekaj(r"K1-SKARBIEC: złe hasło", czas=90) and czekaj(r"K1-LOGOWANIE: złe hasło", czas=30))
    zrzut("haslo-2-zle")
    pisz("t", "a", "j", "n", "e", "1", "ret")
    sprawdz("dobre hasło: magazyn odblokowany", czekaj(r"K1-SKARBIEC: magazyn odblokowany", czas=120))
    sprawdz("po odblokowaniu pulpit z zapisanymi ustawieniami (tapeta Zmierzch, ciemny motyw)",
            czekaj(r"K1-EKRAN: ustawienia wczytane ze skarbca \(tapeta 1, motyw 1", czas=90) and
            czekaj(r"K1-GUI: pulpit gotowy", czas=90))
    zrzut("haslo-3-pulpit")
    # 0.6.3: Start → Wyloguj: the apps close, the vault locks again, the password screen comes back. The saved
    # resolution comes back a moment after the first desktop (TCG: seconds later): the Start button moves — wait for
    # the mode, then for a desktop laid out after it.
    u = czekaj(r"K1-EKRAN: ustawienia wczytane ze skarbca \(.*rozdzielczość (\d+)x(\d+)\)", czas=10)
    if u:
        czekaj(rf"K1-EKRAN: tryb {u.group(1)}x{u.group(2)} ", czas=30)
    q.czekaj_na(lambda: tekst().rfind("K1-GUI: pulpit gotowy") > tekst().rfind("K1-EKRAN: tryb "), czas=60)
    g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+)", czas=10)
    kt = kursor_teraz()
    if kt:
        kursor[:] = list(kt)
    if g:
        klik(int(g.group(1)), int(g.group(2)))
        m = czekaj(r"menu Start otwarte \(.*; Wyloguj (\d+),(\d+)\)", czas=20)
        if m:
            klik(int(m.group(1)), int(m.group(2)))
    sprawdz("Start → Wyloguj: magazyn zamknięty, z powrotem ekran hasła",
            czekaj(r"K1-NADZORCA: wylogowanie", czas=30) and
            czekaj(r"K1-LOGOWANIE: ekran hasła gotowy", od=1, czas=90))
    zrzut("haslo-4-wylogowano")
    pisz("t", "a", "j", "n", "e", "1", "ret")
    sprawdz("po wylogowaniu hasło znów otwiera pulpit",
            czekaj(r"K1-SKARBIEC: magazyn odblokowany", od=1, czas=120) and
            czekaj(r"K1-GUI: pulpit gotowy", od=1, czas=90))
    t_ = tekst()
    sprawdz("hasło nie trafia do logu", "tajne1" not in t_ and "zle1" not in t_)
    sprawdz("brak 'Kernel panic', błędów i zablokowanych wywołań", "Kernel panic" not in t_ and "BŁĄD" not in t_
            and "zabronione wywołanie" not in t_)
    sys.exit(0 if all(wyniki) else 1)

g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)")
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
t = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
if not sprawdz(f"{tryb}: pulpit gotowy (pasek, menu Start)", g and k0 and t): sys.exit(1)
# 0.7.2: "Wyloguj" only in K1 installed on a disk (K1-DANE on an internal disk), not from the .iso
zainst = tryb in ("pulpit", "ponownie")
s_ = czekaj(r"K1-GUI: (K1 zainstalowany na tym komputerze|sesja z pendrive'a)", czas=10)
sprawdz(f"{tryb}: Wyloguj w menu Start {'jest (K1-DANE na dysku)' if zainst else 'ukryte (K1 nie jest zainstalowany)'}",
        s_ and s_.group(1).startswith("K1 zainst") == zainst, s_.group(0) if s_ else "brak")
if tryb == "pulpit" and not sprawdz("wejście: mysz otwarta", czekaj(r"K1-WEJSCIE: urządzenie \d+: mysz")): sys.exit(1)
if tryb == "ponownie":
    # M5d: the same data disk after power off — the settings and the note come back from skarbiec
    sprawdz("skarbiec: magazyn z dysku otwarty (obiekty z poprzedniego uruchomienia)",
            czekaj(r"K1-SKARBIEC: magazyn otwarty: [1-9]\d* obiektów", czas=30))
    u = czekaj(r"K1-EKRAN: ustawienia wczytane ze skarbca \(tapeta 1, motyw 1, mysz \d, rozdzielczość (\d+)x(\d+)\)", czas=30)
    sprawdz("ustawienia wróciły: tapeta „Zmierzch”, ciemny motyw", u, u.group(0) if u else "brak")
    t_start = re.search(r"K1-EKRAN: tryb (\d+)x(\d+)", tekst())  # the first mode, before the restore
    if u and t_start and (u.group(1), u.group(2)) != (t_start.group(1), t_start.group(2)):
        sprawdz(f"rozdzielczość {u.group(1)}x{u.group(2)} wróciła",
                czekaj(rf"K1-EKRAN: tryb {u.group(1)}x{u.group(2)} @ \d+ Hz \(zmiana", czas=30))
        g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", od=1, czas=30) or g
    kursor[:] = list(kursor_teraz())
    ax, ay, krok = int(g.group(3)), int(g.group(4)), int(g.group(5))
    # 0.7.5: the desktop arrangement from skarbiec — the bar's order and Kalkulator in column 3
    ik = re.findall(r"K1-GUI: ikony na pulpicie \(siatka \d+x\d+, co (\d+),\d+\): (.*)", tekst())
    kal = re.search(r"Kalkulator (\d+),", ik[-1][1]) if ik else None
    zeg = re.search(r"Zegar (\d+),", ik[-1][1]) if ik else None
    sprawdz("rozmieszczenie pulpitu wróciło ze skarbca (pasek i Kalkulator w kolumnie 3)",
            czekaj(r"K1-GUI: rozmieszczenie pulpitu wczytane \(na pasku: 6, na pulpicie: 7\)", czas=10) and
            "(pasek: Zegar, Notatki, Ustawienia, Zasoby, Kalkulator, Pliki)" in tekst() and kal and zeg and
            int(kal.group(1)) == int(zeg.group(1)) + 2 * int(ik[-1][0]), ik[-1][1] if ik else "brak")
    klik(ax + krok, ay)  # Notatki
    sprawdz("notatki wróciły: 2, na górze ostatnio zmieniona „Druga” (5 B)",
            czekaj(r"K1-NOTATKI: notatki wczytane: 2 \(pierwsza: 5 B\)", czas=40))
    o = czekaj(r"K1-EKRAN: okno (\d+) \(notatki\) (\d+)x(\d+) w (\d+),(\d+)", czas=20)
    u_n = czekaj(r"K1-NOTATKI: układ: nowa (\d+),(\d+); lista od (\d+),(\d+) co (\d+); usuń (\d+),(\d+), potwierdź (\d+)")
    zrzut("ponownie-notatki")
    if o and u_n:
        nx, ny = int(o.group(4)), int(o.group(5))
        lx, ly, lk = int(u_n.group(3)), int(u_n.group(4)), int(u_n.group(5))
        klik(nx + lx, ny + ly + lk)  # the second row: „Zażółć K1”
        sprawdz("Notatki: druga na liście to „Zażółć K1” (13 B)", czekaj(r"K1-NOTATKI: otwarto notatkę \(13 B\)"))
        klik(nx + int(u_n.group(6)), ny + int(u_n.group(7)))   # Usuń notatkę
        klik(nx + int(u_n.group(8)), ny + int(u_n.group(7)))   # Usuń (potwierdzenie)
        sprawdz("Notatki: usunięcie z potwierdzeniem (została 1)", czekaj(r"K1-NOTATKI: usunięto notatkę \(zostało: 1\)"))
    # M5e: Ustawienia → Hasło: set the password "tajne1" (the next boot asks for it)
    klik(ax + 2 * krok, ay)
    uo = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    if uo and sk:
        ux, uy = int(uo.group(4)), int(uo.group(5))
        klik(ux + int(sk.group(1)), uy + int(sk.group(2)) + 5 * int(sk.group(3)))
        hp = czekaj(r"K1-USTAWIENIA: hasło: przełącznik (\d+),(\d+);", czas=20)
        if hp:
            klik(ux + int(hp.group(1)), uy + int(hp.group(2)))
            pisz("t", "a", "j", "n", "e", "1", "tab", "t", "a", "j", "n", "e", "1", "ret")
    sprawdz("Ustawienia → Hasło: hasło ustawione (skarbiec)", czekaj(r"K1-SKARBIEC: hasło ustawione", czas=90))
    zrzut("ponownie-haslo")
    sprawdz("brak 'Kernel panic', błędów i zablokowanych wywołań", "Kernel panic" not in tekst() and "BŁĄD" not in tekst()
            and "zabronione wywołanie" not in tekst())
    sys.exit(0 if all(wyniki) else 1)
W, H = int(t.group(1)), int(t.group(2))
sx, sy, ax, ay, krok = (int(g.group(i)) for i in range(1, 6))
kursor[:] = [int(k0.group(1)), int(k0.group(2))]
w, h, px = zrzut(f"{tryb}-1-pulpit", gdy=lambda w, h, px: (w, h) == (W, H) and max(px(W // 2, 6)) < 120
                 and jasny(px(W // 2, ay - 26)))
sprawdz(f"zrzut: rozmiar {w}x{h} = tryb {W}x{H}", (w, h) == (W, H))
sprawdz("zrzut: górny pasek ciemny, dolny pasek jasny (motyw jasny)", max(px(W // 2, 6)) < 120 and jasny(px(W // 2, ay - 26)),
        f"{px(W // 2, 6)} {px(W // 2, ay - 26)}")
sprawdz("zrzut: kolorowa tapeta (nie czarna, nie jednolita)", px(W // 4, H // 2) != px(W - 20, H // 3) and max(px(W // 3, H // 2)) > 80,
        f"{px(W // 4, H // 2)} {px(W - 20, H // 3)}")
if tryb != "pulpit":
    sprawdz("brak 'Kernel panic', błędów i zablokowanych wywołań", "Kernel panic" not in tekst() and "BŁĄD" not in tekst()
            and "zabronione wywołanie" not in tekst())
    sys.exit(0 if all(wyniki) else 1)

# 0.7.5: the desktop (owner's sketch A "Kafelki"): icons, double click, the right-click menus, pinning,
# dragging on the bar and on the desktop. At the end the bar is as at the start (the steps below use it).
IKONY = r"K1-GUI: ikony na pulpicie \(siatka (\d+)x(\d+), co (\d+),(\d+)\): (.*)"
ik = czekaj(IKONY)
ikony = {n: (int(x), int(y)) for n, x, y in re.findall(r"(\w+) (\d+),(\d+)", ik.group(5))} if ik else {}
sprawdz("pulpit: Kosz i ikony aplikacji przy pierwszym starcie (bez Instalatora)",
        set(ikony) == {"Kosz", "Zegar", "Notatki", "Ustawienia", "Zasoby", "Kalkulator", "Pliki"}, ik.group(0) if ik else "brak")
if not ikony: sys.exit(1)
kafel = lambda px, n: jasny(px(ikony[n][0] - 20, ikony[n][1]))  # a light tile left of the line icon
w, h, px = zrzut("1b-ikony", gdy=lambda w, h, px: kafel(px, "Zegar") and kafel(px, "Kalkulator"))
sprawdz("zrzut: jasne kafelki ikon na tapecie (motyw jasny)", kafel(px, "Zegar") and kafel(px, "Kalkulator"),
        str(px(ikony["Zegar"][0] - 20, ikony["Zegar"][1])))
q.dwuklik(*ikony["Zegar"])
zo = czekaj(r"K1-EKRAN: okno (\d+) \(zegar\)", czas=30)
sprawdz("pulpit: podwójne kliknięcie ikony otwiera Zegar", czekaj(r"K1-GUI: pulpit: otwieram Zegar") and zo)


PASEK = r"K1-GUI: (?:pulpit gotowy|pasek zmieniony): Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)"


def pasek():
    """The bar's latest positions (it is centred: Start moves when applications are pinned or unpinned)."""
    m = list(re.finditer(PASEK, tekst()))
    return tuple(int(m[-1].group(i)) for i in range(1, 6))


def ostatni(wzor):
    """The newest log line matching `wzor` (for the failure description), or "brak"."""
    m = list(re.finditer(wzor, tekst()))
    return m[-1].group(0) if m else "brak"


menu_opis = [""]  # what the last menu_k saw: the failure line of the check says it (CI keeps no artefacts then)


def kadr():
    """The screen now (not saved): px(x, y), or None."""
    e = ekran(mon, f"{out}/.m4-kadr-{os.getpid()}.ppm")
    return piksele(*e) if e else None


def menu_k(prawy, wzor, wiersz, nazwa_zrzutu=None):
    """Right click at `prawy`, wait for the menu `wzor` in gui's log AND on the screen (its row light and the pixels
    around it changed since before the click: with KVM a click can come before ekran shows the panel), click its
    row `wiersz` (its centre from the log)."""
    wz = r"K1-GUI: menu kontekstowe \(" + wzor + r"\): (.*)"
    n = q.ile(wz)
    przed = kadr()
    q.prawy_klik(*prawy)
    mk = czekaj(wz, od=n, czas=20)
    pozycje = {t.strip(): (int(x), int(y)) for t, x, y in re.findall(r"([^;:]+?) (\d+),(\d+)", mk.group(1))} if mk else {}
    widac = False
    if wiersz in pozycje:
        x, y = pozycje[wiersz]
        inne = lambda px: sum(1 for dx in range(-90, 91, 10) if przed is None or px(x + dx, y) != przed(x + dx, y))
        widac = q.czekaj_na(lambda: (lambda px: px is not None and jasny(px(x + 80, y)) and inne(px) >= 12)(kadr()), czas=20)
        if nazwa_zrzutu:
            zrzut(nazwa_zrzutu, gdy=lambda w, h, px: jasny(px(x + 80, y)))
        klik(x, y)
    menu_opis[0] = f"menu: {mk.group(0) if mk else 'brak w logu'}; na ekranie: {'tak' if widac else 'nie'}"
    return mk is not None and wiersz in pozycje


if zo:
    sprawdz("pasek: prawy przycisk → „Zamknij okno” zamyka Zegar (aplikacja zamyka się sama)",
            menu_k((ax, ay), "pasek Zegar", "Zamknij okno", "1c-menu-paska") and
            czekaj(r"K1-ZEGAR: okno zamknięte z dolnego paska") and czekaj(rf"K1-EKRAN: okno {zo.group(1)} zamknięte"), menu_opis[0])
sprawdz("pulpit: prawy przycisk → „Ułóż ikony”", menu_k((W // 2, H // 3), "pulpit", "Ułóż ikony") and
        czekaj(r"K1-GUI: ułożono ikony"), menu_opis[0])
sprawdz("ikona: „Odepnij od paska” — Notatki znikają z paska",
        menu_k(ikony["Notatki"], "ikona Notatki", "Odepnij od paska", "1d-menu-ikony") and
        czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Ustawienia, Zasoby, Kalkulator, Pliki\)"),
        menu_opis[0] + " | " + ostatni(r"K1-GUI: pasek zmieniony: .*"))
sprawdz("ikona: „Usuń z pulpitu” — Zasoby", menu_k(ikony["Zasoby"], "ikona Zasoby", "Usuń z pulpitu") and
        czekaj(r"K1-GUI: usunięto z pulpitu: Zasoby"), menu_opis[0])
sx, sy, ax, ay, krok = pasek()
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+);")
if m:
    rx, ry, rk = (int(m.group(i)) for i in range(1, 4))
    sprawdz("menu Start: prawy przycisk → „Dodaj do pulpitu” — Zasoby wracają",
            menu_k((rx, ry + 3 * rk), "menu Start Zasoby", "Dodaj do pulpitu") and czekaj(r"K1-GUI: dodano do pulpitu: Zasoby"),
            menu_opis[0])
    sprawdz("menu Start: prawy przycisk → „Przypnij do paska” — Notatki na końcu paska",
            menu_k((rx, ry + rk), "menu Start Notatki", "Przypnij do paska") and
            czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Ustawienia, Zasoby, Kalkulator, Pliki, Notatki\)"),
            menu_opis[0] + " | " + ostatni(r"K1-GUI: pasek zmieniony: .*"))
klik(W // 2, H // 3)  # a click on the empty desktop closes the Start menu
sx, sy, ax, ay, krok = pasek()
# the bar: Notatki dragged from the end back to the second place
idz(ax + 5 * krok, ay); przycisk(True); idz(ax + krok, ay); przycisk(False)
sprawdz("pasek: przeciągnięcie zmienia kolejność (Notatki z powrotem na drugim miejscu)",
        czekaj(r"K1-GUI: pasek: Notatki na miejscu 2") and
        czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Notatki, Ustawienia, Zasoby, Kalkulator, Pliki\)"))
sx, sy, ax, ay, krok = pasek()
# the desktop: Kalkulator dragged to column 3, Zegar's row — row 2 under Kosz (the next start must find it there)
n_ik = q.ile(IKONY)
ik = czekaj(IKONY, od=n_ik - 1)
ikony = {n: (int(x), int(y)) for n, x, y in re.findall(r"(\w+) (\d+),(\d+)", ik.group(5))} if ik else ikony
kk, kw = int(ik.group(3)), int(ik.group(4))
cel = (ikony["Zegar"][0] + 2 * kk, ikony["Zegar"][1])
n_zap = q.ile(r"K1-GUI: rozmieszczenie pulpitu zapisane")
idz(*ikony["Kalkulator"]); przycisk(True); idz(cel[0], cel[1] - 30); idz(*cel); przycisk(False)
sprawdz("pulpit: przeciągnięcie ikony do innej komórki (Kalkulator → kolumna 3, wiersz 2)",
        czekaj(r"K1-GUI: ikona Kalkulator przeniesiona do kolumny 3, wiersza 2") and
        czekaj(r"K1-GUI: rozmieszczenie pulpitu zapisane", od=n_zap))
w, h, px = zrzut("1e-pulpit-po-zmianach", gdy=lambda w, h, px: jasny(px(cel[0] - 20, cel[1])))
sprawdz("zrzut: kafelek Kalkulatora w nowej komórce", jasny(px(cel[0] - 20, cel[1])), str(px(cel[0] - 20, cel[1])))

# menu Start → Notatki
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);", od=1)
sprawdz("menu Start otwiera się po kliknięciu", m)
if not m: sys.exit(1)
rx, ry, rk, wx, wy = (int(m.group(i)) for i in range(1, 6))
w, h, px = zrzut("2-menu")
sprawdz("zrzut: menu Start widoczne (jasne tło nad paskiem)", jasny(px(rx, ry - 50)), str(px(rx, ry - 50)))
klik(rx, ry + rk)
o = czekaj(r"K1-EKRAN: okno (\d+) \(notatki\) (\d+)x(\d+) w (\d+),(\d+)")
sprawdz("Notatki: nadzorca uruchamia aplikację, ekran pokazuje okno", o and czekaj(r"uruchomiono notatki \(pid \d+, uid 201\)"))
if not o: sys.exit(1)
nid, nw, nh, nx, ny = (int(o.group(i)) for i in range(1, 6))
# "Zażółć K1" (AltGr = polski układ programisty)
pisz("shift-z", "a", "alt_r-z", "alt_r-o", "alt_r-l", "alt_r-c", "spc", "shift-k", "1")
sprawdz("Notatki: polskie znaki wpisane i zapisane automatycznie (9 znaków)", czekaj(r"K1-NOTATKI: zapisano notatkę \(9 znaków\)"))
ciemne_w = lambda px: sum(1 for x in range(nx + 256, nx + 372) for y in range(ny + 58, ny + 76) if max(px(x, y)) < 110)
w, h, px = zrzut("3-notatki", gdy=lambda w, h, px: ciemne_w(px) > 40)
ciemne = ciemne_w(px)
sprawdz("zrzut: tekst w oknie Notatek", ciemne > 40, f"ciemnych pikseli: {ciemne}")
# 0.5.8: a second note („Druga”) — the list keeps both, the last changed on top
u_n = czekaj(r"K1-NOTATKI: układ: nowa (\d+),(\d+); lista od (\d+),(\d+) co (\d+)")
if u_n:
    klik(nx + int(u_n.group(1)), ny + int(u_n.group(2)))
    sprawdz("Notatki: „Nowa notatka” dodaje drugą", czekaj(r"K1-NOTATKI: nowa notatka \(notatek: 2\)"))
    pisz("shift-d", "r", "u", "g", "a")
    sprawdz("Notatki: druga notatka zapisana (5 znaków)", czekaj(r"K1-NOTATKI: zapisano notatkę \(5 znaków\)"))
    zrzut("3-notatki-dwie")
# przeciąganie okna za pasek tytułu
idz(nx + 200, ny + 20); przycisk(True)
idz(nx + 320, ny + 80); przycisk(False)
sprawdz("okno przeciągnięte za pasek tytułu", czekaj(rf"K1-EKRAN: okno {nid} przesunięte do {nx + 120},{ny + 60}"))

# Zegar z dolnego paska
klik(ax, ay)
z = czekaj(r"K1-EKRAN: okno (\d+) \(zegar\) (\d+)x(\d+) w (\d+),(\d+)")
sprawdz("Zegar: uruchomiony z dolnego paska", z)
if z:
    zx, zy = int(z.group(4)), int(z.group(5))
    w, h, px = zrzut("4-zegar", gdy=lambda w, h, px: akcent(px(zx + 140, zy + 172)))
    sprawdz("zrzut: tarcza zegara (środek w kolorze akcentu)", akcent(px(zx + 140, zy + 172)), str(px(zx + 140, zy + 172)))

# Ustawienia: ciemny motyw i inna tapeta
klik(ax + 2 * krok, ay)
u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)")
l = czekaj(r"przyciski od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\); tapety od (\d+),(\d+) co (\d+)")
sprawdz("Ustawienia: uruchomione z dolnego paska", u and l)
if u and l:
    uid_, ux, uy = int(u.group(1)), int(u.group(4)), int(u.group(5))
    bx, by, bw, bh, tx, ty, tk = (int(l.group(i)) for i in range(1, 8))
    klik(ux + bx + bw * 3 // 4, uy + by + bh // 2)
    sprawdz("ciemny motyw: ekran przyjmuje ustawienie tylko od Ustawień", czekaj(r"K1-EKRAN: ustawienie 1=1"))
    w, h, px = zrzut("5-ciemny", gdy=lambda w, h, px: ciemny(px(ux + 600, uy + 400)))
    sprawdz("zrzut: okno Ustawień w ciemnym motywie", ciemny(px(ux + 600, uy + 400)), str(px(ux + 600, uy + 400)))
    zx_, zy_ = ikony["Zegar"]  # 0.7.5: the tiles follow the theme too (the owner's wish)
    w, h, px = zrzut("5b-ciemne-ikony", gdy=lambda w, h, px: ciemny(px(zx_ - 20, zy_)))
    sprawdz("zrzut: kafelki ikon w ciemnym motywie", ciemny(px(zx_ - 20, zy_)), str(px(zx_ - 20, zy_)))
    klik(ux + tx + tk, uy + ty)
    sprawdz("tapeta „Zmierzch” ustawiona", czekaj(r"K1-EKRAN: tapeta 1 "))
    w, h, px = zrzut("6-tapeta", gdy=lambda w, h, px: px(12, H - 12)[2] > px(12, H - 12)[0])
    c = px(12, H - 12)
    sprawdz("zrzut: nowa tapeta (fiolet/niebieski w rogu)", c[2] > c[0], str(c))
    # Ustawienia → Ekran: inna rozdzielczość, pulpit układa się od nowa (karta bochs ma wiele trybów)
    sek = czekaj(r"sekcje od (\d+),(\d+) co (\d+)")
    klik(ux + int(sek.group(1)), uy + int(sek.group(2)) + int(sek.group(3)))
    tr = czekaj(r"tryby ekranu: ([\dx ]+) \(przyciski od (\d+),(\d+), (\d+) w kolumnie, co (\d+) i (\d+)\)")
    sprawdz("Ustawienia → Ekran: lista rozdzielczości od ekranu", tr and len(tr.group(1).split()) >= 2,
            tr.group(1) if tr else "brak")
    if tr:
        lista = tr.group(1).split(); cel = "1024x768" if "1024x768" in lista and f"{W}x{H}" != "1024x768" else \
            next(t for t in lista if t != f"{W}x{H}")
        i = lista.index(cel); bx0, by0, nk, dx, dy = (int(tr.group(j)) for j in range(2, 7))
        klik(ux + bx0 + (i // nk) * dx, uy + by0 + (i % nk) * dy)
        cw, ch = (int(v) for v in cel.split("x"))
        nowy = czekaj(rf"K1-EKRAN: tryb {cw}x{ch} @ \d+ Hz \(zmiana")
        g2 = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", od=1)
        sprawdz(f"zmiana rozdzielczości {W}x{H} → {cel}: ekran przełącza tryb, pulpit układa się od nowa", nowy and g2)
        if nowy and g2:
            W, H = cw, ch
            sx, sy, ax, ay, krok = (int(g2.group(i)) for i in range(1, 6))
            w, h, px = zrzut("7-rozdzielczosc", gdy=lambda w, h, px: (w, h) == (W, H) and max(px(W - 8, 6)) < 120)
            sprawdz(f"zrzut: obraz {w}x{h} = {cel}, górny pasek na całą szerokość", (w, h) == (W, H)
                    and max(px(W - 8, 6)) < 120, f"{w}x{h} {px(W - 8, 6)}")
    klik(ax + 2 * krok, ay)
    sprawdz("kliknięcie aktywnej aplikacji na pasku minimalizuje okno", czekaj(rf"K1-EKRAN: okno {uid_} zminimalizowane"))

# Zasoby: wykresy z danymi od nadzorcy
klik(ax + 3 * krok, ay)
zs = czekaj(r"K1-EKRAN: okno (\d+) \(zasoby\) (\d+)x(\d+) w (\d+),(\d+)")
dane = czekaj(r"K1-ZASOBY: dane od nadzorcy: procesor (\d+)‰, pamięć (\d+) z (\d+) MiB, (\d+) CPU")
sprawdz("Zasoby: uruchomione z dolnego paska, dane od nadzorcy (procesor, pamięć)", zs and dane and int(dane.group(3)) > 100,
        dane.group(0) if dane else "brak danych")
sprawdz("Zasoby: wiersz baterii (QEMU bez baterii: zasilanie z sieci)",
        czekaj(r"K1-ZASOBY: bateria: Brak baterii — zasilanie z sieci", czas=10))
if zs:
    zx, zy = int(zs.group(4)), int(zs.group(5))
    # wykres pamięci: linia w kolorze akcentu (motyw ciemny), próbka co sekundę; ok. 7 próbek to ≈ 50 px
    # szerokości przy prawej krawędzi — liczymy każdą kolumnę, bo linia jest płaska i cienka
    linia_w = lambda px: sum(1 for x in range(zx + 32, zx + 536) for y in range(zy + 200, zy + 276) if akcent(px(x, y)))
    w, h, px = zrzut("8-zasoby", gdy=lambda w, h, px: linia_w(px) >= 30, czas=60)
    linia = linia_w(px)
    sprawdz("zrzut: wykres pamięci w oknie Zasobów", linia >= 30, f"pikseli linii: {linia}")

# 0.7.1: Kalkulator z dolnego paska (piąta ikona): klawiatura, mysz, dzielenie przez zero
klik(ax + 4 * krok, ay)
ko = czekaj(r"K1-EKRAN: okno (\d+) \(kalkulator\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
ku = czekaj(r"K1-KALKULATOR: układ: 7 (\d+),(\d+) co (\d+) w poziomie, (\d+) w pionie", czas=30)
sprawdz("Kalkulator: uruchomiony z dolnego paska", ko and ku)
if ko and ku:
    kx, ky = int(ko.group(4)) + int(ku.group(1)), int(ko.group(5)) + int(ku.group(2))
    dx, dy = int(ku.group(3)), int(ku.group(4))
    pisz("1", "2", "kp_add", "7", "kp_multiply", "3", "ret")  # od lewej do prawej: (12 + 7) × 3
    sprawdz("Kalkulator z klawiatury: 12 + 7 × 3 = 57", czekaj(r"K1-KALKULATOR: klawisz = → 57\n", czas=20))
    klik(kx, ky - dy)                            # C
    klik(kx, ky + 2 * dy)                        # 1
    klik(kx + 3 * dx, ky + 2 * dy)               # +
    klik(kx + dx, ky + 3 * dy)                   # 0 (zero: middle of the last row)
    klik(kx + 2 * dx, ky + 3 * dy)               # ,
    klik(kx + dx, ky + 2 * dy)                   # 2
    klik(kx + 3 * dx, ky + 3 * dy)               # =
    sprawdz("Kalkulator myszą: C, 1 + 0,2 = 1,2", czekaj(r"K1-KALKULATOR: klawisz = → 1,2\n", czas=20))
    zrzut("9-kalkulator")
    pisz("5", "kp_divide", "0", "ret")
    sprawdz("Kalkulator: dzielenie przez zero → komunikat, nie awaria",
            czekaj(r"K1-KALKULATOR: klawisz = → Nie można dzielić przez zero", czas=20))

tx_ = tekst()
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów",
        "Kernel panic" not in tx_ and "BŁĄD" not in tx_ and "zabronione wywołanie" not in tx_ and "odrzucono" not in tx_)
# Start → Wyłącz: kontrolowane wyłączenie
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);", od=2)
if m: wx, wy = int(m.group(4)), int(m.group(5))
klik(wx, wy)
sprawdz("Start → Wyłącz: system się wyłącza", czekaj(r"K1: wyłączanie zasilania") and czekaj(r"reboot: Power down"))
sys.exit(0 if all(wyniki) else 1)
PY

echo "[k1] test M4: pulpit i aplikacje (obraz produkcyjny)"
# M5d: a data disk (GPT, empty partition „K1-DANE”) — the desktop run changes settings and writes a
# note, powers off; the second run with the same disk must get them back.
DANE=$TESTY/m4-dane.img
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
DYSK_DANYCH="-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0"
CZAS_ZW=$CZAS; CZAS=$((CZAS * 2))  # 0.7.5: the longest scenario (the desktop steps) — twice the time
uruchom pulpit --qemu "$DYSK_DANYCH"
sprawdz pulpit
CZAS=$CZAS_ZW
python3 -I - "$DANE" <<'PY' || r=1
import sys
d = open(sys.argv[1], "rb").read()
jawne = [t for t in ("Zażółć".encode(), b"K1U1", b"K1U2", b"K1U3", b"K1U4") if t in d]
ok = b"K1SKARB1" in d and not jawne
print(f"  [{'OK' if ok else 'BŁĄD'}]    dysk danych: magazyn K1 zapisany, notatka i ustawienia zaszyfrowane (brak jawnego tekstu)"
      + ("" if ok else f" — jawne: {jawne}, nagłówek: {b'K1SKARB1' in d}"))
sys.exit(0 if ok else 1)
PY
echo "[k1] test M5d: ponowne uruchomienie z tym samym dyskiem danych"
uruchom ponownie --qemu "$DYSK_DANYCH"
sprawdz ponownie
echo "[k1] test M5e: start z hasłem — ekran hasła, złe i dobre hasło, pulpit z ustawieniami"
uruchom haslo --qemu "$DYSK_DANYCH"
sprawdz haslo
if [ -f "$ISO" ]; then
    echo "[k1] test M4: start z obrazu ISO — UEFI (OVMF), grafika z firmware (simpledrm)"
    uruchom iso-uefi --iso "$ISO" --uefi --vga virtio
    sprawdz iso-uefi
    grep -q "obraz rdzenia zamontowany z /dev/loop" "$TESTY/m4-iso-uefi.log" && grep -q "simpledrm" "$TESTY/m4-iso-uefi.log" \
        && echo "  [OK]    rdzeń wbudowany w ISO (loop), simpledrm" || { echo "  [BŁĄD]  ISO UEFI: brak loop/simpledrm"; r=1; }
    echo "[k1] test M4: start z obrazu ISO — BIOS (SeaBIOS, VESA), grafika z firmware (simpledrm)"
    uruchom iso-bios --iso "$ISO" --vga virtio
    sprawdz iso-bios
else
    echo "  [BŁĄD]  brak obrazu ISO $ISO (scripts/zbuduj-iso.sh)"; r=1
fi
if [ $r -eq 0 ]; then info "M4: wszystko zaliczone (zrzuty: $TESTY/m4-*.png)"; else echo "[k1] M4: BŁĄD (logi: $TESTY/m4-*.log)"; fi
exit $r

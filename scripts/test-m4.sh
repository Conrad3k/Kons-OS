#!/bin/sh
# Test etapu M4 (powtarzalny): pulpit i aplikacje w działającym K1 (obraz produkcyjny) — od 0.10.0 na początku także
# kursor, mysz i klawiatura (dawny test-m2.sh) — oraz start z obrazu ISO przez BIOS (grafika z firmware — simpledrm;
# start z ISO przez UEFI: test-m5f.sh, „0-z-iso”). Sterowanie przez monitor QEMU (mysz,
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
blisko = lambda c, w, tol=28: all(abs(a - b) <= tol for a, b in zip(c, w))
SESJE_KOLORY = {2: (0x5F, 0x8A, 0x6A), 3: (0x4A, 0x6F, 0xA5), 4: (0x8B, 0x6A, 0xA8)}  # K1_STREFY: Ogólna, Bank, Prywatna

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
    sprawdz("NEGATYWNE: Wyloguj nie pokazuje ekranu „Wyłączanie” (0.10.7 krok 6)",
            czekaj(r"K1-NADZORCA: wylogowanie", czas=30) and "ekran wyłączania" not in tekst())
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
    sprawdz("rozmieszczenie pulpitu wróciło ze skarbca (pasek z Przeglądarką, Kalkulator w kolumnie 3)",
            czekaj(r"K1-GUI: rozmieszczenie pulpitu wczytane \(na pasku: 7, na pulpicie: 8\)", czas=10) and
            "(pasek: Zegar, Notatki, Ustawienia, Zasoby, Kalkulator, Pliki, Przeglądarka)" in tekst() and kal and zeg and
            int(kal.group(1)) == int(zeg.group(1)) + 2 * int(ik[-1][0]) and "Przeglądarka " in ik[-1][1],
            ik[-1][1] if ik else "brak")
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
        klik(ux + int(sk.group(1)), uy + int(sk.group(2)) + q.wiersz_sekcji(5) * int(sk.group(3)))  # Hasło
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
if tryb == "pulpit":  # 0.10.2 step 2: positions by name from the screen's description (ADR 0045)
    sprawdz("opis ekranu dla testów: program testowy podaje pasek z „Start” (kanał tylko w obrazie testowym)",
            q.czekaj_na_element("Pasek", "Start", czas=60), str(q.ekran())[:300])


def na_pasku(nazwa):
    """The centre of `nazwa` on the bottom bar (the description: the bar is centred and changes)."""
    e = q.czekaj_na_element("Pasek", nazwa, czas=20)
    return q.srodek(e) if e else (0, 0)


def otworz_start():
    """Start clicked, the Start menu in the description."""
    q.klik(*na_pasku("Start"))
    return q.czekaj_na_element("Menu Start", "Wyłącz", czas=20)
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

# M2 (until 0.10.0 its own start, scripts/test-m2.sh): the cursor, the mouse and the keyboard before any window is open.
# Each step: the screen's log + a screenshot checked by colours (m4-kursor-*.png).
krem = lambda c: c[0] > 200 and c[1] > 195 and c[2] > 180
x0, y0 = kursor
sprawdz("zrzut: kursor w miejscu z logu", krem(px(x0 + 3, y0 + 9)), str(px(x0 + 3, y0 + 9)))
px1 = px
monitor(*["mouse_move -60 -30"] * 4)
x1, y1 = x0 - 240, y0 - 120
sprawdz(f"mysz: kursor przesunięty o (-240,-120) → {x1},{y1}", czekaj(rf"K1-EKRAN: kursor {x1},{y1} ") is not None)
w, h, px2 = zrzut("kursor-2-mysz", gdy=lambda w, h, px: krem(px(x1 + 3, y1 + 9)))
sprawdz("zrzut: kursor w nowym miejscu", krem(px2(x1 + 3, y1 + 9)), str(px2(x1 + 3, y1 + 9)))
# arrows move the cursor while no window has the focus; the mouse button changes its colour
monitor(*["sendkey right"] * 3 + ["mouse_button 1"])
x2 = x1 + 60
sprawdz(f"klawiatura: 3 x strzałka w prawo → {x2},{y1}; przycisk myszy wciśnięty",
        czekaj(rf"K1-EKRAN: kursor {x2},{y1} przycisk wciśnięty") is not None)
w, h, px3 = zrzut("kursor-3-klawiatura-przycisk", gdy=lambda w, h, px: akcent(px(x2 + 3, y1 + 9)))
sprawdz("zrzut: kursor w kolorze akcentu (przycisk wciśnięty)", akcent(px3(x2 + 3, y1 + 9)), str(px3(x2 + 3, y1 + 9)))
sprawdz("zrzuty: w starym miejscu kursora znów tapeta (brak śladu)",
        px2(x0 + 3, y0 + 9) == px3(x0 + 3, y0 + 9) != px1(x0 + 3, y0 + 9), f"{px1(x0 + 3, y0 + 9)} {px2(x0 + 3, y0 + 9)}")
monitor("mouse_button 0")
czekaj(rf"K1-EKRAN: kursor {x2},{y1} przycisk puszczony")
kursor[:] = [x2, y1]
sprawdz("brak odrzuconych i zgubionych zdarzeń przy poprawnym wejściu",
        re.findall(r"zdarzenia: .*odrzucone (\d+)\)", tekst())[-1:] == ["0"] and "pominięto" not in tekst())

# 0.7.5: the desktop (owner's sketch A "Kafelki"): icons, double click, the right-click menus, pinning,
# dragging on the bar and on the desktop. At the end the bar is as at the start (the steps below use it).
IKONY = r"K1-GUI: ikony na pulpicie \(siatka (\d+)x(\d+), co (\d+),(\d+)\): (.*)"
ik = czekaj(IKONY)


def ostatni_pasek():
    m = re.findall(r"K1-GUI: (?:pulpit gotowy|pasek zmieniony): .*", tekst())
    return m[-1] if m else "brak"


def ikony_pulpitu():
    """{name: centre of its tile's icon} from the description (the log's point: 8 px under the cell's top + the tile)."""
    p_ = q.element("Pulpit")
    return {e["nazwa"]: (e["x"] + e["w"] // 2, e["y"] + 8 + 26) for e in p_["elementy"]} if p_ else {}  # 26: KAFEL / 2
q.czekaj_na_element("Pulpit", "Pliki", czas=30)
ikony = ikony_pulpitu()
sprawdz("pulpit: Kosz, ikony aplikacji (bez Instalatora) i Przeglądarka przy pierwszym starcie — z opisu ekranu",
        set(ikony) == {"Kosz", "Zegar", "Notatki", "Ustawienia", "Zasoby", "Kalkulator", "Pliki", "Przeglądarka"},
        str(sorted(ikony)))
sprawdz("pasek przy pierwszym starcie: aplikacje i na końcu Przeglądarka (D12)",
        "(pasek: Zegar, Notatki, Ustawienia, Zasoby, Kalkulator, Pliki, Przeglądarka)" in tekst(), ostatni_pasek())
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
    """The bar's positions (it is centred: Start moves when applications are pinned or unpinned) — 0.10.2 step 2: from
    the screen's description, once it shows the bar as gui's newest log line has it: Start, the first application, the
    step."""
    m = list(re.finditer(PASEK + r", dźwięk \d+,\d+ \(pasek: ([^)]*)\)", tekst()))
    chce = m[-1].group(6).replace("| ", "").split(", ") if m else None
    ikony_ = lambda: [e for e in (q.element("Pasek") or {"elementy": []})["elementy"] if e["rodzaj"] == "ikona"]
    q.czekaj_na(lambda: [e["nazwa"] for e in ikony_()][1:] == chce, czas=20)
    el_ = ikony_()
    if len(el_) < 3:
        return tuple(int(m[-1].group(i)) for i in range(1, 6))  # the log's, when the description has no bar
    return q.srodek(el_[0]) + q.srodek(el_[1]) + (el_[2]["x"] - el_[1]["x"],)


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
    mk = czekaj(wz, od=n, czas=20)  # which menu opened: gui's log; where its rows are: the screen's description
    e_ = q.czekaj_na_element("Menu", wiersz, "wiersz", czas=20) if mk else None
    pozycje = {wiersz: q.srodek(e_)} if e_ else {}
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
        czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Ustawienia, Zasoby, Kalkulator, Pliki, Przeglądarka\)"),
        menu_opis[0] + " | " + ostatni(r"K1-GUI: pasek zmieniony: .*"))
sprawdz("ikona: „Usuń z pulpitu” — Zasoby", menu_k(ikony["Zasoby"], "ikona Zasoby", "Usuń z pulpitu") and
        czekaj(r"K1-GUI: usunięto z pulpitu: Zasoby"), menu_opis[0])
sx, sy, ax, ay, krok = pasek()
m = otworz_start()
if m:
    sprawdz("menu Start: prawy przycisk → „Dodaj do pulpitu” — Zasoby wracają",
            menu_k(q.srodek(q.element("Menu Start", "Zasoby")), "menu Start Zasoby", "Dodaj do pulpitu") and
            czekaj(r"K1-GUI: dodano do pulpitu: Zasoby"), menu_opis[0])
    q.czekaj_na(lambda: q.element("Menu") is None, czas=10)  # the right-click menu closed: the Start menu again
    sprawdz("menu Start: prawy przycisk → „Przypnij do paska” — Notatki na końcu paska",
            menu_k(q.srodek(q.czekaj_na_element("Menu Start", "Notatki")), "menu Start Notatki", "Przypnij do paska") and
            czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Ustawienia, Zasoby, Kalkulator, Pliki, Przeglądarka, "
                   r"Notatki\)"),
            menu_opis[0] + " | " + ostatni(r"K1-GUI: pasek zmieniony: .*"))
klik(W * 3 // 4, H // 3)  # a click on the empty desktop (right of the Start menu, not on its rows) closes it
sx, sy, ax, ay, krok = pasek()
# 0.10.1 step 4 (the owner's D4: sketch B): Start → Przeglądarka asks "Którą sesję otworzyć?"; "Anuluj" starts nothing
m = otworz_start()
if m:
    q.klik_w("Menu Start", "Przeglądarka", "wiersz")  # 0.10.2 step 2: the row by its name
    ok_ = czekaj(r"K1-GUI: okienko \(sesje\): (sesja.*)", czas=20)
    an_ = q.czekaj_na_element("Okienko", "Anuluj", "przycisk", czas=20)
    kaf = {int(e["nazwa"].split()[1]): (e["x"] + e["w"] // 2, e["y"] + e["h"] // 2)
           for e in (q.element("Okienko") or {"elementy": []})["elementy"] if e["rodzaj"] == "kafelek"}
    an = q.srodek(an_) if an_ else None
    kafelki = lambda px: all(jasny(px(x, y - 2)) and blisko(px(x, y - 24), SESJE_KOLORY[n]) for n, (x, y) in kaf.items())
    sprawdz("menu Start → Przeglądarka: okienko „Którą sesję otworzyć?” — Ogólna, Bank, Prywatna",
            sorted(kaf) == [2, 3, 4] and an, ok_.group(0) if ok_ else "brak")
    if sorted(kaf) == [2, 3, 4]:
        w, h, px = zrzut("1f-okienko-sesji", gdy=lambda w, h, px: kafelki(px))
        sprawdz("zrzut: kafelki sesji (jasne) z kropką w kolorze sesji",
                kafelki(px), str([(px(x, y - 2), px(x, y - 24)) for x, y in kaf.values()]))
    if an:
        klik(*an)
        sprawdz("okienko sesji: „Anuluj” zamyka je, żadna strefa nie rusza",
                czekaj(r"K1-GUI: okienko \(sesje\): Anuluj") and "uruchamiam: Przeglądarka" not in tekst())
# 0.10.5 step 6 (the owner's D12: sketch A): Przeglądarka pins and lies on the desktop like an application — one icon
# for all its sessions. Unpinned and pinned again (it goes to the end: Notatki is sixth again for the drag below),
# removed from the desktop and added again; a click on the bar's icon, a double click on the desktop's and "Otwórz" in
# the icon's menu ask "Którą sesję otworzyć?" like the Start menu.
def okienko_sesji(co):
    """The window of the sessions opened by `co` (a click): it is there, and "Anuluj" closes it — nothing starts."""
    n_ = q.ile(r"K1-GUI: okienko \(sesje\): sesja")
    co()
    ok_ = czekaj(r"K1-GUI: okienko \(sesje\): sesja", od=n_, czas=20)
    an_ = q.czekaj_na_element("Okienko", "Anuluj", "przycisk", czas=20) if ok_ else None
    if an_:
        n_ = q.ile(r"K1-GUI: okienko \(sesje\): Anuluj")
        klik(*q.srodek(an_))
        czekaj(r"K1-GUI: okienko \(sesje\): Anuluj", od=n_, czas=20)
        q.czekaj_na(lambda: q.element("Okienko") is None, czas=10)
    return an_ is not None and "uruchamiam: Przeglądarka" not in tekst()


m = otworz_start()
if m:
    sprawdz("menu Start: Przeglądarka → „Odepnij od paska” (jak aplikacja)",
            menu_k(q.srodek(q.czekaj_na_element("Menu Start", "Przeglądarka")), "menu Start Przeglądarka",
                   "Odepnij od paska") and
            czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Ustawienia, Zasoby, Kalkulator, Pliki, Notatki\)"),
            menu_opis[0] + " | " + ostatni(r"K1-GUI: pasek zmieniony: .*"))
    q.czekaj_na(lambda: q.element("Menu") is None, czas=10)
    sprawdz("menu Start: Przeglądarka → „Przypnij do paska” — wraca na koniec paska",
            menu_k(q.srodek(q.czekaj_na_element("Menu Start", "Przeglądarka")), "menu Start Przeglądarka",
                   "Przypnij do paska") and
            czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Ustawienia, Zasoby, Kalkulator, Pliki, Notatki, "
                   r"Przeglądarka\)"), menu_opis[0] + " | " + ostatni(r"K1-GUI: pasek zmieniony: .*"))
    q.czekaj_na(lambda: q.element("Menu") is None, czas=10)
    sprawdz("menu Start: Przeglądarka → „Usuń z pulpitu”",
            menu_k(q.srodek(q.czekaj_na_element("Menu Start", "Przeglądarka")), "menu Start Przeglądarka",
                   "Usuń z pulpitu") and czekaj(r"K1-GUI: usunięto z pulpitu: Przeglądarka") and
            q.czekaj_na(lambda: "Przeglądarka" not in ikony_pulpitu(), czas=20), menu_opis[0])
    q.czekaj_na(lambda: q.element("Menu") is None, czas=10)
    sprawdz("menu Start: Przeglądarka → „Dodaj do pulpitu” — ikona wraca",
            menu_k(q.srodek(q.czekaj_na_element("Menu Start", "Przeglądarka")), "menu Start Przeglądarka",
                   "Dodaj do pulpitu") and czekaj(r"K1-GUI: dodano do pulpitu: Przeglądarka") and
            q.czekaj_na(lambda: "Przeglądarka" in ikony_pulpitu(), czas=20), menu_opis[0])
klik(W * 3 // 4, H // 3)  # the Start menu closes
q.czekaj_na(lambda: q.element("Menu Start") is None, czas=10)
pp = na_pasku("Przeglądarka")
pi = ikony_pulpitu().get("Przeglądarka")
if pi:
    kafel_p = lambda px: jasny(px(pi[0] - 20, pi[1])) and jasny(px(pp[0] + 17, pp[1]))
    w, h, px = zrzut("1g-przegladarka", gdy=lambda w, h, px: kafel_p(px))
    sprawdz("zrzut: Przeglądarka na pasku i na pulpicie (jasny kafelek)", kafel_p(px), str(px(pi[0] - 20, pi[1])))
sprawdz("pasek: kliknięcie przypiętej Przeglądarki — okienko „Którą sesję otworzyć?”", pp != (0, 0) and
        okienko_sesji(lambda: klik(*pp)))
sprawdz("pulpit: podwójne kliknięcie ikony Przeglądarki — okienko „Którą sesję otworzyć?”", pi and
        okienko_sesji(lambda: q.dwuklik(*pi)))
sprawdz("ikona Przeglądarki: menu „Otwórz” / „Odepnij od paska” / „Usuń z pulpitu”, „Otwórz” pyta o sesję", pi and
        okienko_sesji(lambda: menu_k(pi, "ikona Przeglądarka", "Otwórz")) and
        re.search(r"menu kontekstowe \(ikona Przeglądarka\): Otwórz \d+,\d+; Odepnij od paska \d+,\d+; "
                  r"Usuń z pulpitu \d+,\d+", tekst()), menu_opis[0])
sx, sy, ax, ay, krok = pasek()
# the bar: Notatki dragged from the end back to the second place
idz(ax + 5 * krok, ay); przycisk(True); idz(ax + krok, ay); przycisk(False)
sprawdz("pasek: przeciągnięcie zmienia kolejność (Notatki z powrotem na drugim miejscu)",
        czekaj(r"K1-GUI: pasek: Notatki na miejscu 2") and
        czekaj(r"K1-GUI: pasek zmieniony: .*\(pasek: Zegar, Notatki, Ustawienia, Zasoby, Kalkulator, Pliki, "
               r"Przeglądarka\)"))
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
m = otworz_start()
sprawdz("menu Start otwiera się po kliknięciu (w opisie ekranu: panel „Menu Start” z „Wyłącz”)", m)
if not m: sys.exit(1)
rx, ry = q.srodek(q.element("Menu Start", "Zegar"))
w, h, px = zrzut("2-menu")
sprawdz("zrzut: menu Start widoczne (jasne tło nad paskiem)", jasny(px(rx, ry - 50)), str(px(rx, ry - 50)))
q.klik_w("Menu Start", "Notatki", "wiersz")
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


def ust_przegladarka(ux, uy, ciemny_):
    """Ustawienia → Przeglądarka (0.10.1 step 4, D5: sketch A): a card per session, none runs into the next one — the
    row of pixels between two cards is the window's background all along, inside a card under its buttons the card's
    field (S17: before, a session's description was drawn over the next one); then back to Wygląd."""
    n_ = q.ile(r"K1-USTAWIENIA: przeglądarka: sesje")
    q.klik_w("Ustawienia", "Przeglądarka", "zakladka")  # 0.10.2 step 2: the section by its name
    l_ = czekaj(r"K1-USTAWIENIA: przeglądarka: sesje (\d+) \(.*?\): (.*)", od=n_, czas=30)
    karty = [tuple(int(v) for v in k) for k in re.findall(r"karta (\d+),(\d+) (\d+)x(\d+)", l_.group(2))] if l_ else []
    tlo, pole = ((0x26, 0x25, 0x23), (0x30, 0x2F, 0x2C)) if ciemny_ else ((0xFA, 0xF9, 0xF6), (0xFF, 0xFF, 0xFF))
    def dobre(px):
        for i, (x, y, w_, h_) in enumerate(karty):
            if not blisko(px(ux + x + 12, uy + y + h_ - 4), pole, 3):
                return False
            if i + 1 < len(karty) and not all(blisko(px(ux + xx, uy + y + h_ + 4), tlo, 3) for xx in range(x, x + w_, 4)):
                return False
        return len(karty) == 3 and all(karty[i + 1][1] >= karty[i][1] + karty[i][3] + 4 for i in range(2))
    w, h, px = zrzut(f"5c-przegladarka-{'ciemny' if ciemny_ else 'jasny'}", gdy=lambda w, h, px: dobre(px))
    sprawdz(f"Ustawienia → Przeglądarka ({'ciemny' if ciemny_ else 'jasny'} motyw): trzy karty sesji, opis nie wchodzi "
            "na następną (zrzut)", dobre(px), l_.group(0)[:300] if l_ else "brak linii")
    # 0.10.9 step 2 (ADR 0049): QEMU has no Intel card — the line under the list says the computer does not decode films
    # with its graphics card (state 1 = K1_FILMY_KARTA_BRAK; nadzorca knows it at once, without a decoder)
    sprawdz("Ustawienia → Przeglądarka: bez karty Intela linia „nie rozpakowuje filmów kartą graficzną”",
            l_ is not None and "karta a filmy: stan 1," in l_.group(0), l_.group(0)[:200] if l_ else "brak linii")
    # 0.10.9 step 5 (ADR 0049 pkt 7): …and the second line, no fast graphics (state 1 = K1_POSR_KARTA_BRAK)
    sprawdz("Ustawienia → Przeglądarka: bez karty Intela linia „nie obsługuje szybkiej grafiki”",
            l_ is not None and "szybka grafika: stan 1," in l_.group(0), l_.group(0)[:240] if l_ else "brak linii")
    q.klik_w("Ustawienia", "Wygląd", "zakladka")


# 0.10.5 step 6 (the owner's D11: sketch A): Alt+Tab — ekran gives Tab with Alt held to the shell, which shows the window
# switcher (the most recently used first) and the window chosen when Alt goes up. Three windows now: Ustawienia (the
# focus), Zegar, Notatki.
def fokus(nazwa):
    """`nazwa`'s window has the focus in the screen's description (waits for it)."""
    return q.czekaj_na(lambda: ((q.element(nazwa) or {}).get("stan", 0) & 4) != 0, czas=30)


def przelacznik(tabow, nazwa_zrzutu=None, shift=False):
    """Alt held, Tab `tabow` times (with Shift: back): the switcher's tiles {name: element}, the one chosen (a screenshot
    `nazwa_zrzutu` with its accent outline); Alt stays held — the caller lets it go (or presses Esc first)."""
    n_ = q.ile(r"K1-GUI: przełącznik okien: ")
    zd = [("alt", 1)] + ([("shift", 1)] if shift else []) + [z for _ in range(tabow) for z in (("tab", 1), ("tab", 0))]
    q.klawisze(*(zd + ([("shift", 0)] if shift else [])))
    czekaj(r"K1-GUI: przełącznik okien: ", od=n_ + tabow - 1, czas=20)
    p_ = q.czekaj_na_element("Przełącznik okien", czas=20)
    kaf = {e["nazwa"]: e for e in (p_ or {"elementy": []})["elementy"] if e["rodzaj"] == "kafelek"}
    wyb = q.czekaj_na(lambda: [e["nazwa"] for e in (q.element("Przełącznik okien") or {"elementy": []})["elementy"]
                               if e["stan"] & 1], czas=10)
    wybrane = [e["nazwa"] for e in (q.element("Przełącznik okien") or {"elementy": []})["elementy"] if e["stan"] & 1]
    if nazwa_zrzutu and wybrane:
        e = kaf[wybrane[0]]
        obrys = lambda px: akcent(px(e["x"] + 1, e["y"] + e["h"] // 2))
        w, h, px = zrzut(nazwa_zrzutu, gdy=lambda w, h, px: obrys(px))
        sprawdz(f"zrzut: przełącznik okien, wybrany kafelek z obwódką w kolorze akcentu ({nazwa_zrzutu})", obrys(px),
                str(px(e["x"] + 1, e["y"] + e["h"] // 2)))
    return kaf, (wybrane[0] if wyb and wybrane else None)


def pusc_alt():
    q.klawisze(("alt", 0))


if u and l:
    sprawdz("Ustawienia mają fokus (trzy okna: Ustawienia, Zegar, Notatki)", fokus("Ustawienia"))
    kaf, wyb = przelacznik(2, "4b-alt-tab")
    sprawdz("Alt+Tab, Tab: przełącznik okien z kafelkami od ostatnio używanego, wybrane trzecie (Notatki)",
            list(kaf) == ["Ustawienia", "Zegar", "Notatki"] and wyb == "Notatki",
            f"{list(kaf)}, wybrane {wyb} | {ostatni(r'K1-GUI: przełącznik okien: .*')}")
    n_ = q.ile(r"K1-GUI: przełącznik okien: wybrane Zegar")  # `od`: earlier matches of the same pattern
    q.klawisze(("shift", 1), ("tab", 1), ("tab", 0), ("shift", 0))
    sprawdz("Shift+Tab: wybór wstecz (Zegar)", czekaj(r"K1-GUI: przełącznik okien: wybrane Zegar", od=n_, czas=20))
    q.klawisze(("esc", 1), ("esc", 0))
    pusc_alt()
    sprawdz("Esc: przełącznik znika, fokus bez zmian (Ustawienia)",
            czekaj(r"K1-GUI: przełącznik okien: Anuluj", czas=20) and
            q.czekaj_na(lambda: q.element("Przełącznik okien") is None, czas=20) and fokus("Ustawienia") and
            "pokazuję" not in ostatni(r"K1-GUI: przełącznik okien: .*"))
    n_ = q.ile(r"K1-GUI: przełącznik okien: pokazuję Zegar")
    pisz("alt-tab")  # a quick Alt+Tab: the window used just before
    sprawdz("szybkie Alt+Tab: okno używane przed chwilą (Zegar) na wierzchu z fokusem",
            czekaj(r"K1-GUI: przełącznik okien: pokazuję Zegar", od=n_, czas=20) and fokus("Zegar"))
    zo_ = re.findall(r"K1-EKRAN: okno (\d+) \(zegar\)", tekst())
    q.klik_w("Pasek", "Zegar", "ikona")  # the active application's icon: minimised
    sprawdz("Zegar zminimalizowany z paska", zo_ and czekaj(rf"K1-EKRAN: okno {zo_[-1]} zminimalizowane", czas=20))
    n_ = q.ile(r"K1-GUI: przełącznik okien: pokazuję Zegar")
    kaf, wyb = przelacznik(1)
    pusc_alt()
    sprawdz("Alt+Tab do zminimalizowanego okna: wraca (Zegar przywrócony, z fokusem)",
            wyb == "Zegar" and czekaj(r"K1-GUI: przełącznik okien: pokazuję Zegar", od=n_, czas=20) and
            zo_ and czekaj(rf"K1-EKRAN: okno {zo_[-1]} przywrócone", czas=20) and fokus("Zegar"), str(list(kaf)))
    n_ = q.ile(r"K1-GUI: przełącznik okien: pokazuję Ustawienia")
    pisz("alt-tab")
    sprawdz("szybkie Alt+Tab z powrotem: Ustawienia z fokusem (ich okno dalej w tym samym miejscu)",
            czekaj(r"K1-GUI: przełącznik okien: pokazuję Ustawienia", od=n_, czas=20) and fokus("Ustawienia"))
    sprawdz("ekran oddaje Tab z Alt powłoce (log ekranu), nic nie odrzucone",
            "K1-EKRAN: Alt+Tab: przełączanie okien w powłoce" in tekst() and "odrzucono" not in tekst())
if u and l:
    uid_, ux, uy = int(u.group(1)), int(u.group(4)), int(u.group(5))
    bx, by, bw, bh, tx, ty, tk = (int(l.group(i)) for i in range(1, 8))
    ust_przegladarka(ux, uy, False)
    q.klik_w("Ustawienia", "Ciemny", "zakladka")  # the theme's segment by its label
    sprawdz("ciemny motyw: ekran przyjmuje ustawienie tylko od Ustawień", czekaj(r"K1-EKRAN: ustawienie 1=1"))
    w, h, px = zrzut("5-ciemny", gdy=lambda w, h, px: ciemny(px(ux + 600, uy + 400)))
    sprawdz("zrzut: okno Ustawień w ciemnym motywie", ciemny(px(ux + 600, uy + 400)), str(px(ux + 600, uy + 400)))
    zx_, zy_ = ikony["Zegar"]  # 0.7.5: the tiles follow the theme too (the owner's wish)
    w, h, px = zrzut("5b-ciemne-ikony", gdy=lambda w, h, px: ciemny(px(zx_ - 20, zy_)))
    sprawdz("zrzut: kafelki ikon w ciemnym motywie", ciemny(px(zx_ - 20, zy_)), str(px(zx_ - 20, zy_)))
    kaf, wyb = przelacznik(1, "5c-alt-tab-ciemny")  # 0.10.5 step 6: the switcher in the dark theme, then Esc
    q.klawisze(("esc", 1), ("esc", 0))
    pusc_alt()
    sprawdz("ciemny motyw: przełącznik okien (Esc — fokus zostaje w Ustawieniach)",
            wyb == "Zegar" and czekaj(r"K1-GUI: przełącznik okien: Anuluj", od=1, czas=20) and fokus("Ustawienia"),
            f"{list(kaf)}, wybrane {wyb}")
    q.klik_w("Ustawienia", "Zmierzch", "kafelek")  # 0.10.7 step 2 (T3): the wallpaper's card by its name
    sprawdz("tapeta „Zmierzch” ustawiona", czekaj(r"K1-EKRAN: tapeta 1 "))
    w, h, px = zrzut("6-tapeta", gdy=lambda w, h, px: px(12, H - 12)[2] > px(12, H - 12)[0])
    c = px(12, H - 12)
    sprawdz("zrzut: nowa tapeta (fiolet/niebieski w rogu)", c[2] > c[0], str(c))
    ust_przegladarka(ux, uy, True)
    # Ustawienia → Ekran: inna rozdzielczość, pulpit układa się od nowa (karta bochs ma wiele trybów)
    q.klik_w("Ustawienia", "Ekran", "zakladka")
    tr = czekaj(r"tryby ekranu: ([\dx ]+) \(przyciski w opisie ekranu")
    sprawdz("Ustawienia → Ekran: lista rozdzielczości od ekranu", tr and len(tr.group(1).split()) >= 2,
            tr.group(1) if tr else "brak")
    if tr:
        lista = tr.group(1).split(); cel = "1024x768" if "1024x768" in lista and f"{W}x{H}" != "1024x768" else \
            next(t for t in lista if t != f"{W}x{H}")
        cw, ch = (int(v) for v in cel.split("x"))
        q.klik_w("Ustawienia", f"{cw} × {ch}", "przycisk")  # 0.10.7 step 2 (T3): the mode by its name
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
        czekaj(r"K1-ZASOBY: bateria: Brak baterii, zasilanie z sieci", czas=10))
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
otworz_start()
q.klik_w("Menu Start", "Wyłącz", "przycisk")
sprawdz("Start → Wyłącz: system się wyłącza", czekaj(r"K1: wyłączanie zasilania") and czekaj(r"reboot: Power down"))
t_ = tekst()  # 0.10.7 step 6: the screen "Wyłączanie" asked for at once — before init
sprawdz("Start → Wyłącz: prośba o ekran „Wyłączanie” przed wyłączeniem zasilania",
        0 <= t_.find("K1-NADZORCA: ekran wyłączania: prośba wysłana do ekranu") < t_.find("K1: wyłączanie zasilania"))
# 0.10.9 (the owner's report of 2026-11-19: no dots on the X230): without a zone nadzorca used to hand over to init at
# once and ekran was stopped before it drew the screen; now ekran shows it, then nadzorca waits one round of the dots
sprawdz("Start → Wyłącz bez strefy: ekran pokazał „Wyłączanie”, dopiero potem reszta K1 się wyłącza",
        0 <= t_.find("K1-EKRAN: ekran wyłączania: wyłączanie") < t_.find("K1-NADZORCA: ekran wyłączania pokazany — wyłączam resztę")
        < t_.find("K1: wyłączanie zasilania"))
sys.exit(0 if all(wyniki) else 1)
PY

echo "[k1] test M4: pulpit i aplikacje (obraz testowy z opisem ekranu; ponowny start, hasło, ISO — obraz produkcyjny)"
# M5d: a data disk (GPT, empty partition „K1-DANE”) — the desktop run changes settings and writes a
# note, powers off; the second run with the same disk must get them back.
DANE=$TESTY/m4-dane.img
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
DYSK_DANYCH="-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0"
CZAS_ZW=$CZAS; CZAS=$((CZAS * 2))  # 0.7.5: the longest scenario (the desktop steps) — twice the time
# 0.10.2 step 2 (ADR 0045): the desktop run on the test image — its test program (disk "k1-test-opis") logs the screen's
# description and the script clicks by names (testy/qemu/k1qemu.py: q.ekran(), q.klik_w()); the other runs: production
ZNAK=$TESTY/m4-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
ZNAK_OPISU="-drive if=none,id=znak,file=$ZNAK,format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-opis,addr=0x1e"
uruchom pulpit --obraz "$K1_WYNIK/rdzen-test.squashfs" --qemu "$DYSK_DANYCH $ZNAK_OPISU"
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
    # the start from the ISO through UEFI (OVMF) is checked by test-m5f ("0-z-iso"): the same image, one start
    echo "[k1] test M4: start z obrazu ISO — BIOS (SeaBIOS, VESA), grafika z firmware (simpledrm)"
    uruchom iso-bios --iso "$ISO" --vga virtio
    sprawdz iso-bios
else
    echo "  [BŁĄD]  brak obrazu ISO $ISO (scripts/zbuduj-iso.sh)"; r=1
fi
if [ $r -eq 0 ]; then info "M4: wszystko zaliczone (zrzuty: $TESTY/m4-*.png)"; else echo "[k1] M4: BŁĄD (logi: $TESTY/m4-*.log)"; fi
exit $r

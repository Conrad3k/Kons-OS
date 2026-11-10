#!/bin/sh
# Test 0.8.0 Pliki (ADR 0029, powtarzalny): aplikacja Pliki i Kosz w działającym K1 (obraz produkcyjny) z dyskiem
# danych. Pierwsze uruchomienie: nowy folder i zmiana nazwy z klawiatury, zajęta nazwa, wejście do folderu i „w górę”,
# Kosz (usuń, przywróć, usuń na zawsze z pytaniem), folder na pulpicie (ikona, podwójne kliknięcie, „Usuń” z menu,
# „Opróżnij Kosz”); drugie — to samo po wyłączeniu.
# Krok 2 (ADR 0030), osobny dysk z notatkami w formacie 0.5.8–0.7.6 (testy/host/stare_notatki.c): Notatki przenoszą je
# do plików, okno wyboru pliku (wybór, „Anuluj”), „Otwórz w…” i podwójne kliknięcie w Plikach, Ustawienia →
# Aplikacje; po ponownym starcie pliki i domyślna aplikacja zostają. Dowody: log + zrzuty build/testy/pliki-*.png.
# Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$TESTY"/pliki-*
r=0

uruchom() {
    n=$1; shift
    rm -f "$TESTY/pliki-$n.log" "$TESTY/pliki-$n.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/pliki-$n.log" --monitor "$TESTY/pliki-$n.sock" --czas "$CZAS" "$@" &
    QPID=$!
}
sprawdz() {
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/pliki.py" "$TESTY/pliki-$1.log" "$TESTY/pliki-$1.sock" "$TESTY" \
        "$CZAS" "$1" || r=1
    zamknij_qemu "$TESTY/pliki-$1.sock" $QPID
}

cat > "$K1_BUDOWA/pliki.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "pliki")
tekst, czekaj, zrzut, sprawdz, klik, pisz, kursor = q.tekst, q.czekaj, q.zrzut, q.sprawdz, q.klik, q.pisz, q.kursor
IKONY = r"K1-GUI: ikony na pulpicie \(siatka \d+x\d+, co \d+,\d+\): (.*)"
WIDOK = r"K1-PLIKI: widok: ([^,]+), (\d+) elementów"


def ikony():
    """The desktop icons from gui's latest line: name (or "plik N") → centre."""
    m = list(re.finditer(IKONY, tekst()))
    wynik = {}
    for czesc in (m[-1].group(1).split("; ") if m else []):
        nazwa, xy = czesc.rsplit(" ", 1)
        wynik[nazwa] = tuple(int(v) for v in xy.split(","))
    return wynik


def po(wzor, akcja, czas=30):
    """Does `akcja`, then waits for a NEW line `wzor`."""
    n = q.ile(wzor)
    akcja()
    return czekaj(wzor, od=n, czas=czas)


def widok(akcja, miejsce, ile, czas=30):
    """`akcja`, then Pliki shows `miejsce` with `ile` entries (the newest such line)."""
    n = q.ile(WIDOK)
    akcja()
    for _ in range(6):
        m = czekaj(WIDOK, od=n, czas=czas)
        if not m:
            return False
        if m.group(1) == miejsce and int(m.group(2)) == ile:
            return True
        n = q.ile(WIDOK)
    return False


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)")
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
if not sprawdz(f"{tryb}: pulpit gotowy", g and k0): sys.exit(1)
kursor[:] = [int(k0.group(1)), int(k0.group(2))]
czekaj(IKONY)
ik = ikony()
sprawdz("pulpit: Kosz i Pliki na pulpicie", "Kosz" in ik and "Pliki" in ik, str(ik))

# Pliki from the desktop (double click)
o = po(r"K1-EKRAN: okno (\d+) \(pliki\) (\d+)x(\d+) w (\d+),(\d+)", lambda: q.dwuklik(*ik["Pliki"]))
u = czekaj(r"K1-PLIKI: układ: miejsca od (\d+),(\d+) co (\d+), Kosz (\d+),(\d+); wiersze od (\d+),(\d+) co (\d+); "
           r"w górę (\d+),(\d+)")
w_ = czekaj(r"K1-PLIKI: pliki wczytane: (\d+)", czas=30)
if not sprawdz(f"{tryb}: Pliki otwarte (okno, pliki ze skarbca)", o and u and w_): sys.exit(1)
ox, oy = int(o.group(4)), int(o.group(5))
mx, my, mk, kx, ky, wx, wy, wk, gx, gy = (int(u.group(i)) for i in range(1, 11))
miejsce = lambda i: (ox + mx, oy + my + i * mk)  # 0 Pulpit, 1 Dokumenty, 2 Obrazy …
kosz = (ox + kx, oy + ky)
wiersz = lambda i: (ox + wx, oy + wy + i * wk)


def menu(cel, punkt, wiersz):
    """Right click at `punkt`, waits for gui's menu `cel` and for it ON THE SCREEN (a light row: with KVM the click
    could come before ekran shows the panel); the centre of row `wiersz`, or None."""
    wz = r"K1-GUI: menu kontekstowe \(" + cel + r"\): (.*)"
    n = q.ile(wz)
    q.prawy_klik(*punkt)
    m = czekaj(wz, od=n, czas=20)
    poz = {t.strip(): (int(x), int(y)) for t, x, y in re.findall(r"([^;:]+?) (\d+),(\d+)", m.group(1))} if m else {}
    if wiersz not in poz:
        return None
    x, y = poz[wiersz]
    zrzut(f"menu-{wiersz.split()[0].lower()}", gdy=lambda w, h, px: min(px(x + 80, y)) > 200)
    return poz[wiersz]


def przyciski():
    m = list(re.finditer(r"K1-PLIKI: przyciski: (.*)", tekst()))
    return {t.strip(): (ox + int(x), oy + int(y)) for t, x, y in re.findall(r"([^;]+?) (\d+),(\d+)", m[-1].group(1))} if m else {}


if tryb == "ponownie":
    sprawdz("po ponownym starcie: Dokumenty z folderem „Projekt” (1 element)", widok(lambda: klik(*miejsce(1)), "Dokumenty", 1))
    w, h, px = zrzut("ponownie-dokumenty")
    sprawdz("po ponownym starcie: Kosz pusty", widok(lambda: klik(*kosz), "Kosz", 0))
    t_ = tekst()
    sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
            "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
    sys.exit(0 if all(q.wyniki) else 1)

sprawdz("Dokumenty: pusty folder na start", widok(lambda: klik(*miejsce(1)), "Dokumenty", 0))
p = przyciski()
sprawdz("przyciski folderu: Nowy folder, Zmień nazwę, Usuń", {"Nowy folder", "Zmień nazwę", "Usuń"} <= set(p), str(p))
# a new folder: its name is selected, typing replaces it (as in Windows)
e = po(r"K1-PLIKI: zmiana nazwy: pole otwarte", lambda: klik(*p["Nowy folder"]))
sprawdz("Nowy folder: powstaje i od razu ma pole nazwy", e)
n_ok = q.ile(r"K1-PLIKI: skarbiec: zrobione \(komunikat 88\)")
pisz("shift-p", "r", "o", "j", "e", "k", "t", "ret")
sprawdz("zmiana nazwy na „Projekt” (klawiatura) zapisana w skarbcu", czekaj(r"K1-PLIKI: skarbiec: zrobione \(komunikat 88\)", od=n_ok))
w, h, px = zrzut("1-projekt")
# a second folder named like the first one (other letter case): refused, as in Windows
po(r"K1-PLIKI: zmiana nazwy: pole otwarte", lambda: klik(*p["Nowy folder"]))
n_z = q.ile(r"K1-PLIKI: skarbiec odmówił \(komunikat 88, wynik -21\)")
pisz("p", "r", "o", "j", "e", "k", "t", "ret")
sprawdz("NEGATYWNY: zajęta nazwa (bez względu na wielkość liter) odrzucona przez skarbiec",
        czekaj(r"K1-PLIKI: skarbiec odmówił \(komunikat 88, wynik -21\)", od=n_z))
zrzut("2-zajeta-nazwa")
# folders first, by name: 0 „Nowy folder”, 1 „Projekt”
sprawdz("podwójne kliknięcie otwiera „Projekt” (pusty)", widok(lambda: q.dwuklik(*wiersz(1)), "folder", 0))
sprawdz("„w górę” wraca do Dokumentów (2 foldery)", widok(lambda: klik(ox + gx, oy + gy), "Dokumenty", 2))
# Kosz: delete, restore, delete for good
klik(*wiersz(0))
sprawdz("Usuń: „Nowy folder” trafia do Kosza", widok(lambda: klik(*p["Usuń"]), "Dokumenty", 1))
sprawdz("Kosz: jeden element", widok(lambda: klik(*kosz), "Kosz", 1))
pk = przyciski()
sprawdz("przyciski Kosza: Przywróć, Usuń na zawsze, Opróżnij Kosz", {"Przywróć", "Usuń na zawsze", "Opróżnij Kosz"} <= set(pk),
        str(pk))
klik(*wiersz(0))
zrzut("3-kosz")
sprawdz("Przywróć: Kosz pusty", widok(lambda: klik(*pk["Przywróć"]), "Kosz", 0))
sprawdz("przywrócony folder znów w Dokumentach", widok(lambda: klik(*miejsce(1)), "Dokumenty", 2))
klik(*wiersz(0))
widok(lambda: klik(*p["Usuń"]), "Dokumenty", 1)
widok(lambda: klik(*kosz), "Kosz", 1)
klik(*wiersz(0))
po(r"K1-PLIKI: przyciski: Tak, usuń", lambda: klik(*pk["Usuń na zawsze"]))
pt = przyciski()
zrzut("4-pytanie")
sprawdz("Usuń na zawsze: pytanie, „Tak, usuń” — Kosz pusty", "Tak, usuń" in pt and widok(lambda: klik(*pt["Tak, usuń"]), "Kosz", 0))
# a folder on the desktop: the Pulpit folder in Pliki = icons on the desktop
widok(lambda: klik(*miejsce(0)), "Pulpit", 0)
n_ik = q.ile(r"K1-GUI: pliki na pulpicie: 1, ")
po(r"K1-PLIKI: zmiana nazwy: pole otwarte", lambda: klik(*p["Nowy folder"]))
pisz("ret")
sprawdz("folder w Pulpicie: ikona na pulpicie", czekaj(r"K1-GUI: pliki na pulpicie: 1, ", od=n_ik))
sx, sy, ax, ay, krok = (int(g.group(i)) for i in range(1, 6))
pliki_na_pasku = (ax + 5 * krok, ay)  # Pliki: the sixth pinned application


def schowaj_pliki():
    """The Pliki window covers the desktop's icons: a click on its bar button minimises it."""
    return po(r"K1-EKRAN: okno \d+ zminimalizowane", lambda: klik(*pliki_na_pasku))


sprawdz("pasek: kliknięcie aktywnych Plików minimalizuje okno", schowaj_pliki())
ikp = ikony()
plik = next((n for n in ikp if n.startswith("plik ")), None)
sprawdz("pulpit: ikona pliku w logu (bez nazwy — numer)", plik, str(ikp))
w, h, px = zrzut("5-pulpit-z-folderem", gdy=lambda w, h, px: plik and min(px(ikp[plik][0] - 20, ikp[plik][1])) > 180)
if plik:
    sprawdz("podwójne kliknięcie folderu na pulpicie: Pliki pokazują jego wnętrze",
            po(r"K1-PLIKI: pokazane miejsce: folder, 0 elementów", lambda: q.dwuklik(*ikp[plik])))
    schowaj_pliki()
    poz = menu(r"ikona pliku", ikp[plik], "Usuń")
    n_k = q.ile(r"K1-GUI: pliki na pulpicie: dodane 0, usunięte 1")
    if poz:
        klik(*poz)
    sprawdz("menu pliku na pulpicie → „Usuń”: ikona znika, plik w Koszu",
            czekaj(r"K1-GUI: pliki na pulpicie: dodane 0, usunięte 1", od=n_k) and czekaj(r"K1-GUI: pliki na pulpicie: 0, w Koszu: 1"))
    w, h, px = zrzut("6-kosz-pelny")
    ik = ikony()
    poz = menu(r"ikona Kosz", ik["Kosz"], "Opróżnij Kosz")
    if poz:
        klik(*poz)
    sprawdz("menu Kosza → „Opróżnij Kosz”: pusty", czekaj(r"K1-GUI: pulpit: opróżniam Kosz") and
            czekaj(r"K1-GUI: pliki na pulpicie: 0, w Koszu: 0"))
t_ = tekst()
sprawdz("nazwy plików nie trafiają do logu", "Projekt" not in t_)
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
        "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test Pliki (0.8.0): foldery, zmiana nazwy, Kosz, plik na pulpicie"
DANE=$TESTY/pliki-dane.img
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
DYSK_DANYCH="-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0"
CZAS_ZW=$CZAS; CZAS=$((CZAS * 2))  # the longer scenario
uruchom pierwszy --qemu "$DYSK_DANYCH"
sprawdz pierwszy
CZAS=$CZAS_ZW
python3 -I - "$DANE" <<'PY' || r=1
import sys
d = open(sys.argv[1], "rb").read()
ok = b"K1SKARB1" in d and b"Projekt" not in d
print(f"  [{'OK' if ok else 'BŁĄD'}]    dysk danych: nazwy folderów zaszyfrowane (brak jawnego „Projekt”)")
sys.exit(0 if ok else 1)
PY
echo "[k1] test Pliki: ponowne uruchomienie z tym samym dyskiem danych"
uruchom ponownie --qemu "$DYSK_DANYCH"
sprawdz ponownie

cat > "$K1_BUDOWA/pliki.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "pliki")
tekst, czekaj, zrzut, sprawdz, klik, pisz, kursor = q.tekst, q.czekaj, q.zrzut, q.sprawdz, q.klik, q.pisz, q.kursor
IKONY = r"K1-GUI: ikony na pulpicie \(siatka \d+x\d+, co \d+,\d+\): (.*)"
WIDOK = r"K1-PLIKI: widok: ([^,]+), (\d+) elementów"
OKNO = r"K1-EKRAN: okno (\d+) \({}\) (\d+)x(\d+) w (\d+),(\d+)"


def po(wzor, akcja, czas=30):
    n = q.ile(wzor)
    akcja()
    return czekaj(wzor, od=n, czas=czas)


def widok(akcja, miejsce, ile, czas=30):
    n = q.ile(WIDOK)
    akcja()
    for _ in range(6):
        m = czekaj(WIDOK, od=n, czas=czas)
        if not m:
            return False
        if m.group(1) == miejsce and int(m.group(2)) == ile:
            return True
        n = q.ile(WIDOK)
    return False


def okno(nazwa, akcja):
    """`akcja` opens a window (or brings an open one up — ekran logs only new windows): its top left corner."""
    wz = OKNO.format(nazwa)
    if q.ile(wz) == 0:
        m = po(wz, akcja)
    else:
        akcja()
        q.cisza(r"K1-EKRAN: okno \d+ zminimalizowane", 1)  # brought up, not minimised
        m = list(re.finditer(wz, tekst()))[-1]
    return (int(m.group(4)), int(m.group(5))) if m else None


def przyciski(o):
    """The buttons of Pliki from its newest line: label → centre on the screen."""
    m = list(re.finditer(r"K1-PLIKI: przyciski: (.*)", tekst()))
    return {t.strip(): (o[0] + int(x), o[1] + int(y)) for t, x, y in re.findall(r"([^;]+?) (\d+),(\d+)", m[-1].group(1))} if m else {}


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)")
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
if not sprawdz(f"{tryb}: pulpit gotowy", g and k0): sys.exit(1)
kursor[:] = [int(k0.group(1)), int(k0.group(2))]
sx, sy, ax, ay, krok = (int(g.group(i)) for i in range(1, 6))
pasek = lambda i: (ax + i * krok, ay)  # 0 Zegar, 1 Notatki, 2 Ustawienia, 3 Zasoby, 4 Kalkulator, 5 Pliki

# Notatki: the notes of 0.7.6 become files (first run) or come back as files (second run)
n0 = okno("notatki", lambda: klik(*pasek(1)))
un = czekaj(r"K1-NOTATKI: układ: nowa .*; otwórz (\d+),(\d+)")
wcz = czekaj(r"K1-NOTATKI: notatki wczytane: (\d+) \(pierwsza: (\d+) B\)", czas=60)
if tryb == "notatki":
    sprawdz("stare notatki (format 0.7.6): 3 do przeniesienia do plików", czekaj(r"K1-NOTATKI: stare notatki: 3 do przeniesienia do plików"))
    sprawdz("stare notatki przeniesione do plików bez błędów", czekaj(r"K1-NOTATKI: stare notatki przeniesione do plików \(błędów: 0\)"))
else:
    sprawdz("po ponownym starcie: nic do przeniesienia (stare obiekty usunięte)", "stare notatki" not in tekst())
sprawdz("Notatki: 3 notatki z plików, najnowsza na górze (9 B)", n0 and un and wcz and wcz.group(1) == "3" and wcz.group(2) == "9",
        wcz.group(0) if wcz else "brak")
zrzut(f"{tryb}-1-notatki", gdy=lambda w, h, px: True)
if not (n0 and un): sys.exit(1)

if tryb == "notatki":
    # the file picker of K1: Notatki asks, K1 shows the window, the user picks one file
    ow = okno("wybor", lambda: klik(n0[0] + int(un.group(1)), n0[1] + int(un.group(2))))
    sprawdz("Otwórz…: skarbiec prosi nadzorcę o okno wyboru pliku", czekaj(r"K1-SKARBIEC: pliki: aplikacja 2 prosi o plik") and
            czekaj(r"K1-NADZORCA: skarbiec: aplikacja 8 uruchomiona \(okno wyboru pliku\)"))
    uw = czekaj(r"K1-WYBOR: układ: miejsca od (\d+),(\d+) co (\d+); wiersze od (\d+),(\d+) co (\d+); Otwórz (\d+),(\d+); Anuluj (\d+),(\d+)")
    sprawdz("okno wyboru: prośba od Notatek o plik tekstowy (od skarbca, nie od aplikacji)",
            ow and uw and czekaj(r"K1-WYBOR: prośba: aplikacja 2, typ 2") and czekaj(r"K1-WYBOR: widok: Dokumenty, 1 elementów"))
    if not (ow and uw): sys.exit(1)
    wiersz = lambda i: (ow[0] + int(uw.group(4)), ow[1] + int(uw.group(5)) + i * int(uw.group(6)))
    otworz, anuluj = (ow[0] + int(uw.group(7)), ow[1] + int(uw.group(8))), (ow[0] + int(uw.group(9)), ow[1] + int(uw.group(10)))
    zrzut("notatki-2-wybor", gdy=lambda w, h, px: True)
    sprawdz("okno wyboru: folder Notatki (3 pliki do wyboru)",
            po(r"K1-WYBOR: widok: folder, 3 elementów \(plików do wyboru: 3\)", lambda: q.dwuklik(*wiersz(0))))
    klik(*wiersz(2))  # by name: „Lista zakupy”, „Najnowsza”, „Zażółć K1”
    zrzut("notatki-3-wybor-plik", gdy=lambda w, h, px: True)
    n_o = q.ile(r"K1-NOTATKI: otwarto notatkę \(13 B\)")
    klik(*otworz)
    sprawdz("Otwórz: plik trafia do Notatek (wybrany, okno się zamyka)", czekaj(r"K1-WYBOR: wybrano plik") and
            czekaj(r"K1-NOTATKI: okno wyboru pliku: plik wybrany") and czekaj(r"K1-NOTATKI: otwarto notatkę \(13 B\)", od=n_o))
    # again (a new window: ekran puts it a bit lower and to the right), and "Anuluj"
    n_w = q.ile(OKNO.format("wybor"))
    po(r"K1-WYBOR: prośba: aplikacja 2", lambda: klik(n0[0] + int(un.group(1)), n0[1] + int(un.group(2))))
    m = czekaj(OKNO.format("wybor"), od=n_w)
    anuluj = (int(m.group(4)) + int(uw.group(9)), int(m.group(5)) + int(uw.group(10))) if m else anuluj
    sprawdz("Anuluj: Notatki dowiadują się, że nic nie wybrano", po(r"K1-NOTATKI: okno wyboru pliku: anulowano", lambda: klik(*anuluj)))

# Pliki: Dokumenty › Notatki — the notes are files there
p0 = okno("pliki", lambda: klik(*pasek(5)))
u = czekaj(r"K1-PLIKI: układ: miejsca od (\d+),(\d+) co (\d+), Kosz (\d+),(\d+); wiersze od (\d+),(\d+) co (\d+); "
           r"w górę (\d+),(\d+)")
if not sprawdz("Pliki otwarte", p0 and u and czekaj(r"K1-PLIKI: pliki wczytane")): sys.exit(1)
mx, my, mk, kx, ky, wx, wy, wk, gx, gy = (int(u.group(i)) for i in range(1, 11))
wiersz = lambda i: (p0[0] + wx, p0[1] + wy + i * wk)
sprawdz("Dokumenty: folder Notatki", widok(lambda: klik(p0[0] + mx, p0[1] + my + mk), "Dokumenty", 1))
sprawdz("Dokumenty › Notatki: 3 pliki", widok(lambda: q.dwuklik(*wiersz(0)), "folder", 3))
zrzut(f"{tryb}-4-pliki", gdy=lambda w, h, px: True)

if tryb == "notatki":
    klik(*wiersz(0))  # „Lista zakupy”
    p = przyciski(p0)
    wa = "Otwórz w…" in p and po(r"K1-PLIKI: przyciski: Notatki (\d+),(\d+); Anuluj", lambda: klik(*p["Otwórz w…"]))
    sprawdz("Otwórz w…: przyciski aplikacji, które otwierają tekst (Notatki)", wa, str(p))
    if wa:
        n_o = q.ile(r"K1-NOTATKI: otwarto notatkę \(20 B\)")
        klik(p0[0] + int(wa.group(1)), p0[1] + int(wa.group(2)))
        sprawdz("Otwórz w → Notatki: skarbiec daje plik, nadzorca przekazuje go Notatkom, okno na wierzch",
                czekaj(r"K1-SKARBIEC: pliki: plik \d+ otwarty w aplikacji 2") and
                czekaj(r"K1-NADZORCA: skarbiec: aplikacja 2 uruchomiona z plikiem") and
                czekaj(r"K1-NOTATKI: plik otwarty z Plików albo z pulpitu") and
                czekaj(r"K1-NOTATKI: otwarto notatkę \(20 B\)", od=n_o))
        zrzut("notatki-5-otworz-w", gdy=lambda w, h, px: True)
    okno("pliki", lambda: klik(*pasek(5)))
    n_o = q.ile(r"K1-NOTATKI: otwarto notatkę \(9 B\)")
    sprawdz("podwójne kliknięcie pliku: domyślna aplikacja (Notatki)",
            po(r"K1-PLIKI: plik otwierany w aplikacji Notatki", lambda: q.dwuklik(*wiersz(1))) and
            czekaj(r"K1-NOTATKI: otwarto notatkę \(9 B\)", od=n_o))
    # Ustawienia → Aplikacje: text files open in Pliki now
    u0 = okno("ustawienia", lambda: klik(*pasek(2)))
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    ap = u0 and sk and po(r"K1-USTAWIENIA: aplikacje: Dokument tekstowy — 2 do wyboru, przyciski od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\)",
                          lambda: klik(u0[0] + int(sk.group(1)), u0[1] + int(sk.group(2)) + 6 * int(sk.group(3))))
    sprawdz("Ustawienia → Aplikacje: Dokument tekstowy — Notatki albo Pliki (podgląd)", ap)
    if ap:
        bx, by, bw, bh = (int(ap.group(i)) for i in range(1, 5))
        zrzut("notatki-6-ustawienia", gdy=lambda w, h, px: True)
        n_z = q.ile(r"K1-SKARBIEC: zapisano obiekt 1 właściciela 2 ")
        sprawdz("wybór „Pliki (podgląd)”: ustawienie ekranu zapisane w skarbcu",
                po(r"K1-USTAWIENIA: zmiana ustawienia 7 → 7", lambda: klik(u0[0] + bx + 3 * bw // 4, u0[1] + by + bh // 2)) and
                czekaj(r"K1-SKARBIEC: zapisano obiekt 1 właściciela 2 ", od=n_z))
    okno("pliki", lambda: klik(*pasek(5)))

sprawdz("podwójne kliknięcie pliku tekstowego: teraz domyślnie Pliki (podgląd)" + (" — zapamiętane po ponownym starcie" if tryb != "notatki" else ""),
        po(r"K1-PLIKI: plik pokazany w Plikach \(domyślna aplikacja\)", lambda: q.dwuklik(*wiersz(2))))
t_ = tekst()
sprawdz("nazwy i treść notatek nie trafiają do logu", "Zażółć" not in t_ and "Lista" not in t_ and "Najnowsza" not in t_)
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
        "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test Pliki, krok 2 (ADR 0030): stare notatki → pliki, okno wyboru pliku, „Otwórz w…”, domyślna aplikacja"
STARE=$TESTY/pliki-stare.img
[ -x "$K1_BUDOWA/obj/stare_notatki" ] || make -C "$K1_REPO" -s "$K1_BUDOWA/obj/stare_notatki" >/dev/null
rm -f "$STARE.czesc"; truncate -s $((30000 * 512)) "$STARE.czesc"
"$K1_BUDOWA/obj/stare_notatki" "$STARE.czesc" "Najnowsza" "Lista: zakupy?
mleko" "Zażółć K1" >/dev/null || r=1
dd if=/dev/zero of="$STARE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$STARE" >/dev/null
dd if="$STARE.czesc" of="$STARE" bs=512 seek=2048 conv=notrunc status=none; rm -f "$STARE.czesc"
DYSK_STARY="-drive if=none,id=dane,file=$STARE,format=raw -device ide-hd,drive=dane,bus=ide.0"
CZAS_ZW=$CZAS; CZAS=$((CZAS * 2))
uruchom notatki --qemu "$DYSK_STARY"
sprawdz notatki
CZAS=$CZAS_ZW
python3 -I - "$STARE" <<'PY' || r=1
import sys
d = open(sys.argv[1], "rb").read()
jawne = [t for t in ("Zażółć".encode(), b"Najnowsza", b"Lista", b"Notatki", b"K1U2") if t in d]
print(f"  [{'OK' if not jawne else 'BŁĄD'}]    dysk danych: notatki jako pliki i ustawienia zaszyfrowane" + (f" — jawne: {jawne}" if jawne else ""))
sys.exit(1 if jawne else 0)
PY
echo "[k1] test Pliki, krok 2: ponowne uruchomienie z tym samym dyskiem"
uruchom notatki-ponownie --qemu "$DYSK_STARY"
sprawdz notatki-ponownie
if [ $r -eq 0 ]; then info "Pliki: wszystko zaliczone (zrzuty: $TESTY/pliki-*.png)"; else echo "[k1] Pliki: BŁĄD (logi: $TESTY/pliki-*.log)"; fi
exit $r

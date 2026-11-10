#!/bin/sh
# Test 0.8.0 Pliki (ADR 0029, powtarzalny): aplikacja Pliki i Kosz w działającym K1 (obraz produkcyjny) z dyskiem
# danych. Pierwsze uruchomienie: nowy folder i zmiana nazwy z klawiatury, zajęta nazwa, wejście do folderu i „w górę”,
# Kosz (usuń, przywróć, usuń na zawsze z pytaniem), folder na pulpicie (ikona, podwójne kliknięcie, „Usuń” z menu,
# „Opróżnij Kosz”); drugie — to samo po wyłączeniu. Dowody: log + zrzuty build/testy/pliki-*.png.
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
    n_m = q.ile(r"K1-GUI: menu kontekstowe \(ikona pliku\): (.*)")
    q.prawy_klik(*ikp[plik])
    mk_ = czekaj(r"K1-GUI: menu kontekstowe \(ikona pliku\): (.*)", od=n_m, czas=20)
    poz = {t.strip(): (int(x), int(y)) for t, x, y in re.findall(r"([^;:]+?) (\d+),(\d+)", mk_.group(1))} if mk_ else {}
    n_k = q.ile(r"K1-GUI: pliki na pulpicie: dodane 0, usunięte 1")
    if "Usuń" in poz:
        klik(*poz["Usuń"])
    sprawdz("menu pliku na pulpicie → „Usuń”: ikona znika, plik w Koszu",
            czekaj(r"K1-GUI: pliki na pulpicie: dodane 0, usunięte 1", od=n_k) and czekaj(r"K1-GUI: pliki na pulpicie: 0, w Koszu: 1"))
    w, h, px = zrzut("6-kosz-pelny")
    ik = ikony()
    n_m = q.ile(r"K1-GUI: menu kontekstowe \(ikona Kosz\): (.*)")
    q.prawy_klik(*ik["Kosz"])
    mk_ = czekaj(r"K1-GUI: menu kontekstowe \(ikona Kosz\): (.*)", od=n_m, czas=20)
    poz = {t.strip(): (int(x), int(y)) for t, x, y in re.findall(r"([^;:]+?) (\d+),(\d+)", mk_.group(1))} if mk_ else {}
    if "Opróżnij Kosz" in poz:
        klik(*poz["Opróżnij Kosz"])
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
if [ $r -eq 0 ]; then info "Pliki: wszystko zaliczone (zrzuty: $TESTY/pliki-*.png)"; else echo "[k1] Pliki: BŁĄD (logi: $TESTY/pliki-*.log)"; fi
exit $r

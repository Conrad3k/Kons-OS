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
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/m4.py" "$TESTY/m4-$1.log" "$TESTY/m4-$1.sock" "$TESTY" "$K1_REPO/scripts/zrzut-ekranu.sh" \
        "$CZAS" "$1" || r=1
    kill $QPID 2>/dev/null || true; wait $QPID 2>/dev/null || true
}

cat > "$K1_BUDOWA/m4.py" <<'PY'
import os, re, sys, time
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, zrzut_sh, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], float(sys.argv[5]), sys.argv[6]
q = Maszyna(log, mon, out, zrzut_sh, limit, "m4")
tekst, czekaj, monitor, zrzut, sprawdz = q.tekst, q.czekaj, q.monitor, q.zrzut, q.sprawdz
kursor_teraz, idz, klik, pisz, kursor, wyniki = q.kursor_teraz, q.idz, q.klik, q.pisz, q.kursor, q.wyniki

jasny = lambda c: min(c) > 200
ciemny = lambda c: max(c) < 70
akcent = lambda c: c[0] > 170 and c[1] < 150 and c[2] < 120 and c[0] > c[1] > c[2]

if tryb == "haslo":
    # M5e: the vault has a password now — the password screen instead of the desktop
    e = czekaj(r"K1-LOGOWANIE: ekran hasła gotowy", czas=90)
    sprawdz("magazyn z hasłem: zamiast pulpitu ekran hasła (wariant B „Zegar”)", e and "K1-GUI: pulpit gotowy" not in tekst())
    time.sleep(2)
    zrzut("haslo-1-ekran")
    pisz("z", "l", "e", "1", "ret")
    sprawdz("złe hasło odrzucone (skarbiec i ekran hasła)",
            czekaj(r"K1-SKARBIEC: złe hasło", czas=90) and czekaj(r"K1-LOGOWANIE: złe hasło", czas=30))
    time.sleep(1)
    zrzut("haslo-2-zle")
    pisz("t", "a", "j", "n", "e", "1", "ret")
    sprawdz("dobre hasło: magazyn odblokowany", czekaj(r"K1-SKARBIEC: magazyn odblokowany", czas=120))
    sprawdz("po odblokowaniu pulpit z zapisanymi ustawieniami (tapeta Zmierzch, ciemny motyw)",
            czekaj(r"K1-EKRAN: ustawienia wczytane ze skarbca \(tapeta 1, motyw 1", czas=90) and
            czekaj(r"K1-GUI: pulpit gotowy", czas=90))
    time.sleep(3)
    zrzut("haslo-3-pulpit")
    # 0.6.3: Start → Wyloguj: the apps close, the vault locks again, the password screen comes back
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
    time.sleep(2)
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
    time.sleep(2)
    k1 = list(re.finditer(r"K1-EKRAN: kursor (\d+),(\d+) ", tekst()))[-1]
    kursor[:] = [int(k1.group(1)), int(k1.group(2))]
    ax, ay, krok = int(g.group(3)), int(g.group(4)), int(g.group(5))
    klik(ax + krok, ay)  # Notatki
    sprawdz("notatki wróciły: 2, na górze ostatnio zmieniona „Druga” (5 B)",
            czekaj(r"K1-NOTATKI: notatki wczytane: 2 \(pierwsza: 5 B\)", czas=40))
    o = czekaj(r"K1-EKRAN: okno (\d+) \(notatki\) (\d+)x(\d+) w (\d+),(\d+)", czas=20)
    u_n = czekaj(r"K1-NOTATKI: układ: nowa (\d+),(\d+); lista od (\d+),(\d+) co (\d+); usuń (\d+),(\d+), potwierdź (\d+)")
    time.sleep(2)
    zrzut("ponownie-notatki")
    if o and u_n:
        nx, ny = int(o.group(4)), int(o.group(5))
        lx, ly, lk = int(u_n.group(3)), int(u_n.group(4)), int(u_n.group(5))
        klik(nx + lx, ny + ly + lk)  # the second row: „Zażółć K1”
        sprawdz("Notatki: druga na liście to „Zażółć K1” (13 B)", czekaj(r"K1-NOTATKI: otwarto notatkę \(13 B\)"))
        klik(nx + int(u_n.group(6)), ny + int(u_n.group(7)))   # Usuń notatkę
        time.sleep(0.5)
        klik(nx + int(u_n.group(8)), ny + int(u_n.group(7)))   # Usuń (potwierdzenie)
        sprawdz("Notatki: usunięcie z potwierdzeniem (została 1)", czekaj(r"K1-NOTATKI: usunięto notatkę \(zostało: 1\)"))
    # M5e: Ustawienia → Hasło: set the password "tajne1" (the next boot asks for it)
    klik(ax + 2 * krok, ay)
    uo = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    if uo and sk:
        ux, uy = int(uo.group(4)), int(uo.group(5))
        time.sleep(1)
        klik(ux + int(sk.group(1)), uy + int(sk.group(2)) + 5 * int(sk.group(3)))
        hp = czekaj(r"K1-USTAWIENIA: hasło: przełącznik (\d+),(\d+);", czas=20)
        time.sleep(2)
        if hp:
            klik(ux + int(hp.group(1)), uy + int(hp.group(2)))
            time.sleep(1)
            pisz("t", "a", "j", "n", "e", "1", "tab", "t", "a", "j", "n", "e", "1", "ret")
    sprawdz("Ustawienia → Hasło: hasło ustawione (skarbiec)", czekaj(r"K1-SKARBIEC: hasło ustawione", czas=90))
    time.sleep(2)
    zrzut("ponownie-haslo")
    sprawdz("brak 'Kernel panic', błędów i zablokowanych wywołań", "Kernel panic" not in tekst() and "BŁĄD" not in tekst()
            and "zabronione wywołanie" not in tekst())
    sys.exit(0 if all(wyniki) else 1)
W, H = int(t.group(1)), int(t.group(2))
sx, sy, ax, ay, krok = (int(g.group(i)) for i in range(1, 6))
kursor[:] = [int(k0.group(1)), int(k0.group(2))]
time.sleep(1.5)
w, h, px = zrzut(f"{tryb}-1-pulpit")
sprawdz(f"zrzut: rozmiar {w}x{h} = tryb {W}x{H}", (w, h) == (W, H))
sprawdz("zrzut: górny pasek ciemny, dolny pasek jasny (motyw jasny)", max(px(W // 2, 6)) < 120 and jasny(px(W // 2, ay - 26)),
        f"{px(W // 2, 6)} {px(W // 2, ay - 26)}")
sprawdz("zrzut: kolorowa tapeta (nie czarna, nie jednolita)", px(W // 4, H // 2) != px(W - 20, H // 3) and max(px(W // 3, H // 2)) > 80,
        f"{px(W // 4, H // 2)} {px(W - 20, H // 3)}")
if tryb != "pulpit":
    sprawdz("brak 'Kernel panic', błędów i zablokowanych wywołań", "Kernel panic" not in tekst() and "BŁĄD" not in tekst()
            and "zabronione wywołanie" not in tekst())
    sys.exit(0 if all(wyniki) else 1)

# menu Start → Notatki
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);")
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
time.sleep(2)
# "Zażółć K1" (AltGr = polski układ programisty)
pisz("shift-z", "a", "alt_r-z", "alt_r-o", "alt_r-l", "alt_r-c", "spc", "shift-k", "1")
sprawdz("Notatki: polskie znaki wpisane i zapisane automatycznie (9 znaków)", czekaj(r"K1-NOTATKI: zapisano notatkę \(9 znaków\)"))
w, h, px = zrzut("3-notatki")
ciemne = sum(1 for x in range(nx + 256, nx + 372) for y in range(ny + 58, ny + 76) if max(px(x, y)) < 110)
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
idz(nx + 200, ny + 20); time.sleep(0.3); monitor("mouse_button 1"); time.sleep(0.3)
idz(nx + 320, ny + 80); time.sleep(0.5); monitor("mouse_button 0")
sprawdz("okno przeciągnięte za pasek tytułu", czekaj(rf"K1-EKRAN: okno {nid} przesunięte do {nx + 120},{ny + 60}"))

# Zegar z dolnego paska
klik(ax, ay)
z = czekaj(r"K1-EKRAN: okno (\d+) \(zegar\) (\d+)x(\d+) w (\d+),(\d+)")
sprawdz("Zegar: uruchomiony z dolnego paska", z)
if z:
    zx, zy = int(z.group(4)), int(z.group(5)); time.sleep(2.5)
    w, h, px = zrzut("4-zegar")
    sprawdz("zrzut: tarcza zegara (środek w kolorze akcentu)", akcent(px(zx + 140, zy + 172)), str(px(zx + 140, zy + 172)))

# Ustawienia: ciemny motyw i inna tapeta
klik(ax + 2 * krok, ay)
u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)")
l = czekaj(r"przyciski od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\); tapety od (\d+),(\d+) co (\d+)")
sprawdz("Ustawienia: uruchomione z dolnego paska", u and l)
if u and l:
    uid_, ux, uy = int(u.group(1)), int(u.group(4)), int(u.group(5))
    bx, by, bw, bh, tx, ty, tk = (int(l.group(i)) for i in range(1, 8))
    time.sleep(1.5)
    klik(ux + bx + bw * 3 // 4, uy + by + bh // 2)
    sprawdz("ciemny motyw: ekran przyjmuje ustawienie tylko od Ustawień", czekaj(r"K1-EKRAN: ustawienie 1=1"))
    time.sleep(2.5)
    w, h, px = zrzut("5-ciemny")
    sprawdz("zrzut: okno Ustawień w ciemnym motywie", ciemny(px(ux + 600, uy + 400)), str(px(ux + 600, uy + 400)))
    klik(ux + tx + tk, uy + ty)
    sprawdz("tapeta „Zmierzch” ustawiona", czekaj(r"K1-EKRAN: tapeta 1 "))
    time.sleep(2.5)
    w, h, px = zrzut("6-tapeta")
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
            time.sleep(2.5)
            w, h, px = zrzut("7-rozdzielczosc")
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
    zx, zy = int(zs.group(4)), int(zs.group(5)); time.sleep(8)
    w, h, px = zrzut("8-zasoby")
    # wykres pamięci: linia w kolorze akcentu (motyw ciemny); po 8 s ma ok. 7 próbek (≈ 50 px
    # szerokości przy prawej krawędzi) — liczymy każdą kolumnę, bo linia jest płaska i cienka
    linia = sum(1 for x in range(zx + 32, zx + 536) for y in range(zy + 200, zy + 276) if akcent(px(x, y)))
    sprawdz("zrzut: wykres pamięci w oknie Zasobów", linia >= 30, f"pikseli linii: {linia}")

# 0.7.1: Kalkulator z dolnego paska (piąta ikona): klawiatura, mysz, dzielenie przez zero
klik(ax + 4 * krok, ay)
ko = czekaj(r"K1-EKRAN: okno (\d+) \(kalkulator\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
ku = czekaj(r"K1-KALKULATOR: układ: 7 (\d+),(\d+) co (\d+) w poziomie, (\d+) w pionie", czas=30)
sprawdz("Kalkulator: uruchomiony z dolnego paska", ko and ku)
if ko and ku:
    kx, ky = int(ko.group(4)) + int(ku.group(1)), int(ko.group(5)) + int(ku.group(2))
    dx, dy = int(ku.group(3)), int(ku.group(4))
    time.sleep(1)
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
    time.sleep(1)
    zrzut("9-kalkulator")
    pisz("5", "kp_divide", "0", "ret")
    sprawdz("Kalkulator: dzielenie przez zero → komunikat, nie awaria",
            czekaj(r"K1-KALKULATOR: klawisz = → Nie można dzielić przez zero", czas=20))

tx_ = tekst()
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów",
        "Kernel panic" not in tx_ and "BŁĄD" not in tx_ and "zabronione wywołanie" not in tx_ and "odrzucono" not in tx_)
# Start → Wyłącz: kontrolowane wyłączenie
klik(sx, sy)
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);", od=1)
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
uruchom pulpit --qemu "$DYSK_DANYCH"
sprawdz pulpit
python3 -I - "$DANE" <<'PY' || r=1
import sys
d = open(sys.argv[1], "rb").read()
jawne = [t for t in ("Zażółć".encode(), b"K1U1") if t in d]
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

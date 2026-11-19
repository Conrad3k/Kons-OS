#!/bin/sh
# Test strefy próbnej (0.10.0 krok 1, ADR 0042; powtarzalny): obraz testowy z małym dyskiem o numerze seryjnym
# „k1-test-strefy” (znak dla programu testowego: /proc/cmdline go nie widzi) — program testowy prosi
# nadzorcę o strefę próbną (mikro-maszyna z jądrem strefy i programem gosc), a ten skrypt patrzy z zewnątrz:
#   1. jądro K1 ma KVM (w emulacji QEMU udaje wirtualizację AMD — ADR 0042 T10), strefa startuje, okno w ramce
#   2. ramkę (pasek tytułu nad obrazem strefy i obwódka 3 px — krok 3, D1: szkic A; od 0.10.7 krok 4, ADR 0048: w tle okna
#      K1, kolor strefy tylko w kropce i linii pod paskiem) rysuje ekran K1;
#      w środku wzór gościa (zrzut)
#   3. kliknięcie i klawisz docierają do gościa (licznik w logu gościa, zmiana wzoru); bez fokusu klawisz nie dociera;
#      kółko (0.10.0 krok 4) dociera — z fokusem i bez niego (okno pod kursorem)
#   (0.10.1 krok 1) linia liczników strefy co kilka sekund i przy końcu, szczyt pamięci klatki u nadzorcy
#   (0.10.1 krok 2) BALON: złośliwi goście balonu (strona poza pamięcią, blok nie z całych stron, za długa lista) — koniec
#      tylko strefy; uczciwy gość (tryb 9): górna granica ponad pamięć na start, zwolnione 64 MiB wracają do K1, cel
#      balonu 32 MiB i z powrotem 0 (prośby programu testowego przez nadzorcę), nagły skok prawie do granicy — żyje
#   (0.10.1 krok 3) ROZDZIELACZ: K1 (1 GiB) poniżej rezerwy — nadzorca sam bierze balonem wolną pamięć strefy (tryb 10),
#      fokus od gui, Zasoby z przydziałem sesji, okienko „Mało pamięci” z „Zamknij” — zamyka strefę
#   4. kursor na pasku: przyciski K1 (zrzut); przeciągnięcie paska przesuwa okno; „minimalizuj” chowa okno
#      (0.10.5 krok 4, D8) ZMIANA ROZMIARU za róg ramki (ekran → ROZMIAR → nowa powierzchnia VMM → nowy wyświetlacz gościa),
#      najmniejszy 640×480, MAKSYMALIZACJA przyciskiem (obszar roboczy z paneli, oba paski widoczne — zrzut), zmaksymalizowane
#      nie przesuwa się, podwójne kliknięcie paska przywraca; rozmiar zapamiętany dla sesji (następne okno strefy)
#   5. okno K1 (Zegar) na wierzchu zasłania strefę, ramka strefy zostaje tam, gdzie jej nie zasłania; ciemny motyw
#   6. „zamknij” na pasku: gość dostaje „zamknij” (vsock), kończy się, maszyna znika, okno też
#   (krok 3, S26: gość włącza pamięć kompresowaną — zram w jądrze strefy; wpis w logu gościa)
#   7. NEGATYWNE: złośliwy gość (deskryptor poza pamięcią, pętla deskryptorów, odczyt ponad limit, zapis do obrazu
#      tylko do odczytu) — kończy się tylko ta strefa, z powodem w logu; K1 działa dalej
#   8. NEGATYWNE: procesor bez wirtualizacji — nadzorca mówi „brak wirtualizacji”, nic nie startuje, nadzorca żyje
#   9. obraz produkcyjny nie ma strefy próbnej (tylko jądro strefy)
#  (0.10.2 krok 1) EKRAN STARTU do okna programu strefy (gość z „oknem” — tryb 11), pasek w ruchu; ZAMYKANIE W TLE:
#      okno znika od razu, maszyna kończy się potem; otwarcie w trakcie zamykania — nadzorca czeka i otwiera sam;
#      WYŁĄCZANIE przy działającej strefie: najpierw strefa się zamyka i zapisuje dysk, potem reszta K1
#  (0.10.2 krok 3) ZRZUT WZORCOWY ramki strefy w obu motywach (testy/wzorce/ramka-strefy-*.png, obraz gościa pominięty)
#  10. (0.10.0 krok 5) strefa pamięta: K1 z dyskiem danych (K1-DANE), strefa próbna dostaje dysk w skarbcu (/dev/vdb,
#      ext4 z pustego dysku strefy) — gość liczy na nim swoje uruchomienia: pierwsze 1, po złośliwych gościach 2; po
#      „Wyczyść dane sesji” znów 1
# Dowody: build/testy/strefa.log, strefa-bez-wirt.log, zrzuty build/testy/strefa-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1200}
mkdir -p "$TESTY"; rm -f "$TESTY"/strefa*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
ZNAK=$TESTY/strefa-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
# 0.10.0 step 5: an empty K1-DANE (skarbiec makes a vault on it): a zone remembers only with the data on a disk
DANE=$TESTY/strefa-dane.img
rm -f "$DANE"; truncate -s 260M "$DANE"
printf 'label: gpt\nstart=2048, size=524288, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
# CI with KVM: K1 needs nested virtualization from the runner (S7); without it the zone test runs emulated
EMULACJA=
if [ -w /dev/kvm ]; then
    grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested || EMULACJA=--emulacja
fi

cat > "$K1_BUDOWA/strefa.py" <<'PY'
import os, re, subprocess, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, znak, emulacja = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6:]
ZNAK = (f"-drive if=none,id=znak,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-strefy,"
        f"addr=0x1e -drive if=none,id=dane,file={znak.replace('-znak', '-dane')},format=raw -device ide-hd,drive=dane,bus=ide.0")
log, mon = f"{out}/strefa.log", f"{out}/strefa.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)),
                         "--pamiec", "1024", "--qemu", ZNAK] + emulacja)
q = Maszyna(log, mon, out, limit, "strefa")
czekaj, sprawdz = q.czekaj, q.sprawdz
PIASEK = (0xA8, 0x83, 0x4A)   # K1_STREFY: the test zone's colour (0.10.7 step 4: the dot and the line under the bar)
TLO_OKNA = {"jasny": (0xFA, 0xF9, 0xF6), "ciemny": (0x26, 0x25, 0x23)}  # k1_motyw tlo: the frame around the picture
TLO_GOSCIA = (0x20, 0x30, 0x4A)
ZIELONY, POMARANCZOWY, BIALY = (0x2E, 0x8B, 0x57), (0xD2, 0x69, 0x1E), (0xFF, 0xFF, 0xFF)


def blisko(c, w, tol=24):
    return all(abs(a - b) <= tol for a, b in zip(c, w))


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=limit * 0.4)
# The proof of KVM is the zone's machine running (KVM_CREATE_VM, KVM_RUN), not a kernel line: kvm_amd prints
# "Nested Paging enabled", but kvm_intel prints nothing when VMX works — CI runners are AMD or Intel (runs 77, 79:
# Intel Xeon with nested VMX). The kernel lines only name the kind, from the raw bytes of the log (q.tekst() drops a
# kernel message printed in the middle of a K1 line).
dziala = czekaj(r"K1-STREFA: strefa 1 \(Strefa próbna\): maszyna działa", czas=240)
surowy = open(log, "rb").read()
rodzaj = ("AMD z zagnieżdżonym stronicowaniem" if b"kvm_amd: Nested Paging enabled" in surowy else
          "Intel VMX" if b"kvm_intel" not in surowy else "?")
sprawdz(f"jądro K1 z KVM: maszyna strefy działa ({rodzaj}{', emulacja' if emulacja else ''})", dziala and rodzaj != "?",
        rodzaj)
ax, ay, krok = (int(g.group(i)) for i in (3, 4, 5)) if g else (0, 0, 0)
o = czekaj(r"K1-EKRAN: okno (\d+) \(strefa\) (\d+)x(\d+) w (\d+),(\d+)", czas=180)
nr, w_, h_, x, y = (int(o.group(i)) for i in range(1, 6)) if o else (0, 0, 0, 0, 0)
sprawdz("nadzorca uruchamia strefę próbną (VMM w klatce), ekran pokazuje jej okno",
        czekaj(r"K1-NADZORCA: strefa 1 \(Strefa próbna\): pid \d+, uid 300, pamięć 128 MiB na start, granica (\d+) MiB", czas=60) and o)
sprawdz("gość startuje: jądro strefy, gosc, kanał sterujący (vsock) „gotowa”, pierwszy obraz",
        czekaj(r"K1-STREFA: gość gotowy \(kanał sterujący\)", czas=240) and
        czekaj(r"K1-STREFA: pierwszy obraz gościa", czas=120))
start = czekaj(r"K1-STREFA: strefa 1 \(Strefa próbna\): maszyna działa")
gotowy = czekaj(r"K1-GOSC: gotowa: urządzenia wejścia 2, kanał sterujący jest", czas=30)
sprawdz("gość: dwa urządzenia wejścia (klawiatura, wskaźnik) i kanał sterujący", gotowy)
sprawdz("gość: pamięć kompresowana (zram w jądrze strefy, krok 3 — S26)",
        czekaj(r"K1-GOSC: pamięć kompresowana: \d+ MiB", czas=30))
TYTUL = 40  # K1_STREFA_TYTUL (k1_okno.h): the bar ekran draws above the zone's picture, inside the 3 px outline


def ramka_i_wzor(w, h, px):
    return (o and blisko(px(x - 2, y + h_ // 2), TLO_OKNA["jasny"], 8) and
            blisko(px(x + w_ + 1, y + h_ // 2), TLO_OKNA["jasny"], 8) and blisko(px(x + w_ // 2, y - 1), PIASEK) and
            blisko(px(x + w_ // 2, y - TYTUL - 2), TLO_OKNA["jasny"], 8) and
            not blisko(px(x + w_ // 2, y - TYTUL // 2), TLO_GOSCIA)
            and blisko(px(x + w_ // 2, y + h_ // 6), TLO_GOSCIA) and blisko(px(x + w_ // 2, y + h_ // 2), ZIELONY) and
            blisko(px(x + w_ - 6, y + 6), (0xFF, 0xD7, 0x00)))


w, h, px = q.zrzut("1-okno", gdy=ramka_i_wzor)
sprawdz("okno strefy: ramka okna K1 (tło okna wokół obrazu, 0.10.7 krok 4) i linia w kolorze strefy pod paskiem "
        "tytułu, rysowane przez ekran, w środku wzór gościa "
        "(zrzut strefa-1-okno)", ramka_i_wzor(w, h, px),
        str((px(x - 2, y + h_ // 2), px(x + w_ // 2, y - 1), px(x + w_ // 2, y + h_ // 2))) if o else "brak okna")
zx, zy = x + w_ // 2, y - TYTUL // 2  # the bar, between the title and the buttons


def wzorzec_ramki(motyw):
    """0.10.2 step 3 (D4): the frame ekran draws — the title bar and the 3 px outline — pixel by pixel as in
    testy/wzorce/ramka-strefy-<motyw>.png; the guest's picture inside is left out (the pointer goes outside)."""
    q.wzorzec(f"ramka-strefy-{motyw}", prostokat=(x - 3, y - TYTUL - 3, w_ + 6, h_ + TYTUL + 6),
              pomin=[(3, TYTUL + 3, w_, h_)])


if o:
    wzorzec_ramki("jasny")

kx, ky = x + w_ // 3, y + h_ // 4
q.klik(kx, ky)
sprawdz("kliknięcie w okno strefy dociera do gościa (wskaźnik virtio-input)",
        czekaj(r"K1-GOSC: wejście: klawisze 0, kliknięcia 1", czas=60))
w, h, px = q.zrzut("2-klikniecie", gdy=lambda w, h, px: blisko(px(kx - 7, ky - 7), BIALY))  # beside the cursor's tip
sprawdz("gość rysuje biały kwadrat tam, gdzie kliknięto (zrzut strefa-2-klikniecie)", blisko(px(kx - 7, ky - 7), BIALY))
q.pisz("a")
sprawdz("klawisz dociera do gościa przy fokusie (licznik, nigdy kod klawisza w logu)",
        czekaj(r"K1-GOSC: wejście: klawisze 1, kliknięcia 1", czas=60))
w, h, px = q.zrzut("3-klawisz", gdy=lambda w, h, px: blisko(px(x + w_ // 2, y + h_ // 2), POMARANCZOWY))
sprawdz("gość zmienia pas po klawiszu (zrzut strefa-3-klawisz)", blisko(px(x + w_ // 2, y + h_ // 2), POMARANCZOWY))
q.pisz("shift-b")  # CI runs 88-96 of PR #119: without Shift itself the browser got ";" for ":" and "l" for Ctrl+L
sprawdz("Shift dociera do gościa jako klawisz (Shift i litera: licznik rośnie o 2)",
        czekaj(r"K1-GOSC: wejście: klawisze 3, kliknięcia 1", czas=60))
q.kolko(kx, ky, w_dol=2)  # 0.10.0 step 4: the pointer's wheel (virtio-input REL_WHEEL and REL_WHEEL_HI_RES)
sprawdz("kółko dociera do gościa: 2 ząbki do siebie = REL_WHEEL −2 i REL_WHEEL_HI_RES −240",
        czekaj(r"K1-GOSC: wejście: kółko -2 ząbków, -240/120", czas=60))

PASEK = (r"K1-EKRAN: strefa: pasek okna {} \(minimalizuj (\d+),(\d+), maksymalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)"
         .format(nr))


def na_pasek():  # the pointer into the zone's picture, then onto its bar: ekran logs where the bar's buttons are
    n = q.ile(PASEK)
    q.idz(zx, y + 60)
    q.idz(zx, zy)
    return czekaj(PASEK, od=n, czas=30)


p = na_pasek()
if p:
    q.idz(int(p.group(5)), int(p.group(6)))  # "zamknij" lights up (not clicked)
w, h, px = q.zrzut("4-pasek", gdy=lambda w, h, px: p and not blisko(px(zx, zy), TLO_GOSCIA))
sprawdz("kursor na pasku: przyciski K1 (minimalizuj, maksymalizuj, zamknij) w pasku nad obrazem strefy (zrzut "
        "strefa-4-pasek)", p and int(p.group(6)) < y and int(p.group(5)) > int(p.group(3)) > int(p.group(1)) and
        not blisko(px(zx, zy), TLO_GOSCIA), p.group(0) if p else "")
PRZES = r"K1-EKRAN: okno {} przesunięte do (\d+),(\d+)".format(nr)
q.przeciagnij(zx, zy, zx + 60, zy + 40)
m = czekaj(PRZES, czas=30)
sprawdz("przeciągnięcie paska tytułu przesuwa okno strefy", m and int(m.group(1)) == x + 60 and int(m.group(2)) == y + 40,
        m.group(0) if m else "")
if m:
    x, y = int(m.group(1)), int(m.group(2))
    zx, zy = x + w_ // 2, y - TYTUL // 2

# 0.10.5 step 4 (D8, T12): the size of the window — only on K1's frame; the zone gets the final size and draws it
ROZ = r"K1-EKRAN: okno {}: nowy rozmiar (\d+)x(\d+)( \(zmaksymalizowane\))?".format(nr)
POW = r"K1-EKRAN: okno {}: powierzchnia (\d+)x(\d+) w (\d+),(\d+)".format(nr)
GOSC = r"K1-GOSC: ekran gościa: nowy rozmiar (\d+)x(\d+)"


def rozmiar(opis, ruch, oczekiwany, maks=False):
    """One change: `ruch` does it; then ekran's request, the VMM's surface, the guest's display — all of that size."""
    n = (q.ile(ROZ), q.ile(POW), q.ile(GOSC))
    ruch()
    r = czekaj(ROZ, od=n[0], czas=30)
    pw = czekaj(POW, od=n[1], czas=60)
    g = czekaj(GOSC, od=n[2], czas=120)
    oczekiwany = oczekiwany() if callable(oczekiwany) else oczekiwany
    jest = tuple(int(v) for v in r.groups()[:2]) if r else None
    sprawdz(opis, r and jest == oczekiwany and bool(r.group(3)) == maks and pw and g and
            (int(g.group(1)), int(g.group(2))) == oczekiwany == (int(pw.group(1)), int(pw.group(2))),
            f"{r.group(0) if r else 'brak'} / {pw.group(0) if pw else 'brak'} / {g.group(0) if g else 'brak'}")
    return (int(pw.group(3)), int(pw.group(4))) if pw else (x, y)


w0, h0 = w_, h_
rozmiar("zmiana rozmiaru: prawy dolny róg przeciągnięty o 64×24 — ekran prosi strefę o 1088×632 (szerokość co 8), VMM "
        "daje okno tego rozmiaru, gość dostaje nowy wyświetlacz",
        lambda: q.przeciagnij(x + w_ + 1, y + h_ + 1, x + w_ + 65, y + h_ + 25), ((w0 + 64) & ~7, h0 + 24))
w_, h_ = (w0 + 64) & ~7, h0 + 24


def wzor_caly(w, h, px):  # the guest's pattern on its whole new display: the yellow corners at the new size
    return all(blisko(px(a, b), (0xFF, 0xD7, 0x00)) for a, b in ((x + w_ - 6, y + 6), (x + w_ - 6, y + h_ - 6)))


w, h, px = q.zrzut("4b-rozmiar", gdy=wzor_caly)
sprawdz("po zmianie rozmiaru: ramka K1 i wzór gościa w nowym rozmiarze (zrzut strefa-4b-rozmiar)", wzor_caly(w, h, px))
rozmiar("NEGATYWNY: ciągnięcie poniżej najmniejszego rozmiaru — 640×480",
        lambda: q.przeciagnij(x + w_ + 1, y + h_ + 1, x + 400, y + 300), (640, 480))
w_, h_ = 640, 480
p = na_pasek()
OBSZAR = r"K1-EKRAN: obszar roboczy (\d+),(\d+) (\d+)x(\d+)"
n_ob = q.ile(OBSZAR)


def obszar():
    ob = czekaj(OBSZAR, od=n_ob, czas=30)
    return tuple(int(v) for v in ob.groups()) if ob else (0, 0, 0, 0)


rozmiar("„maksymalizuj” na pasku: okno na cały obszar roboczy (szerokość ekranu, od górnego do dolnego paska)",
        lambda: p and q.klik(int(p.group(3)), int(p.group(4))), lambda: (obszar()[2] & ~7, obszar()[3] - TYTUL), maks=True)
ax_, ay_, aw_, ah_ = obszar()
sprawdz("obszar roboczy z paneli powłoki: pod górnym paskiem (32 px), nad dolnym paskiem, cała szerokość ekranu",
        ay_ == 32 and aw_ == 1280 and 600 < ay_ + ah_ < 800, str((ax_, ay_, aw_, ah_)))


def maks_widac(w, h, px):  # the top bar and the bottom bar seen, the zone's bar right under the top one, its pattern
    return (not blisko(px(640, 12), PIASEK) and blisko(px(320, 32 + TYTUL - 1), PIASEK) and
            blisko(px(640, 32 + TYTUL + 30), TLO_GOSCIA) and not blisko(px(640, 790), TLO_GOSCIA) and
            blisko(px(1280 - 6, 32 + TYTUL + 6), (0xFF, 0xD7, 0x00)))


w, h, px = q.zrzut("4c-maksymalizacja", gdy=maks_widac)
sprawdz("zmaksymalizowane: górny i dolny pasek widoczne, pasek tytułu strefy tuż pod górnym, wzór między paskami "
        "(zrzut strefa-4c-maksymalizacja)", maks_widac(w, h, px))
n = q.ile(PRZES)
q.przeciagnij(640, 32 + TYTUL // 2, 700, 32 + TYTUL // 2 + 60)
sprawdz("NEGATYWNY: zmaksymalizowanego okna nie da się przesunąć za pasek", q.cisza(PRZES, 5) and q.ile(PRZES) == n)
rozmiar("podwójne kliknięcie paska tytułu: okno wraca do poprzedniego rozmiaru i miejsca",
        lambda: q.dwuklik(640, 32 + TYTUL // 2), (640, 480))
rozmiar("i z powrotem do pierwszego rozmiaru (lewy górny róg tam, gdzie był)",
        lambda: q.przeciagnij(x + w_ + 1, y + h_ + 1, x + w0 + 1, y + h0 + 1), (w0, h0))
w_, h_ = w0, h0
zx, zy = x + w_ // 2, y - TYTUL // 2

# the bottom bar: the zone's window among the running ones; minimise from the title bar, back from the bottom bar
pk = czekaj(r"K1-GUI: pasek zmieniony: Start \d+,\d+, aplikacje od (\d+),(\d+) co (\d+), dźwięk \d+,\d+ \(pasek: ([^)]*)\)",
            czas=30)
sprawdz("dolny pasek: okno strefy wśród działających (ikona strefy)", pk and pk.group(4).endswith("Strefa próbna"),
        pk.group(4) if pk else "")
p = na_pasek()
if p:
    q.klik(int(p.group(1)), int(p.group(2)))
sprawdz("„minimalizuj” na pasku chowa okno strefy", p and czekaj(r"K1-EKRAN: okno {} zminimalizowane".format(nr), czas=30))
if pk:
    elementy = pk.group(4).split(", ")
    przypiete = next((i for i, e in enumerate(elementy) if e.startswith("| ")), len(elementy))
    q.klik(int(pk.group(1)) + (len(elementy) - 1) * int(pk.group(3)) + (9 if przypiete < len(elementy) else 0),
           int(pk.group(2)))
sprawdz("kliknięcie strefy na pasku przywraca jej okno", czekaj(r"K1-EKRAN: okno {} przywrócone".format(nr), czas=30))

# a K1 window on top: Zegar (the first application of the bottom bar — where it is now: the zone's item moved the
# centred bar)
pk = czekaj(r"K1-GUI: (?:pasek zmieniony|pulpit gotowy): Start \d+,\d+, aplikacje od (\d+),(\d+) co (\d+)", czas=10)
if pk:
    ax, ay, krok = (int(pk.group(i)) for i in (1, 2, 3))
q.klik(ax, ay)
zg = czekaj(r"K1-EKRAN: okno (\d+) \(zegar\) (\d+)x(\d+) w (\d+),(\d+)", czas=120)
if zg:
    gw, gh, gx, gy = (int(zg.group(i)) for i in (2, 3, 4, 5))
    px_x, px_y = max(x, gx) + 20, max(y, gy) + 60  # inside both rectangles


def zegar_na_wierzchu(w, h, px):
    return zg and not blisko(px(px_x, px_y), TLO_GOSCIA) and not blisko(px(px_x, px_y), ZIELONY) and \
        not blisko(px(px_x, px_y), POMARANCZOWY)


w, h, px = q.zrzut("5-zegar-na-wierzchu", gdy=zegar_na_wierzchu)
sprawdz("okno K1 (Zegar) na wierzchu zasłania treść strefy (zrzut strefa-5-zegar-na-wierzchu)", zegar_na_wierzchu(w, h, px))
n = q.ile(r"K1-GOSC: wejście: klawisze")
q.pisz("b")
sprawdz("NEGATYWNY: klawisz przy fokusie Zegara nie trafia do strefy", q.cisza(r"K1-GOSC: wejście: klawisze", 15))
# 0.10.0 step 4: scrolling goes to the window under the pointer — the zone's, though Zegar has the focus
wid = [(px_, py_) for px_, py_ in ((x + 30, y + h_ - 30), (x + w_ - 30, y + h_ - 30), (x + 30, y + 30), (x + w_ - 30, y + 30))
       if not (zg and gx <= px_ < gx + gw and gy - TYTUL <= py_ < gy + gh)]
if wid:
    q.kolko(*wid[0], w_dol=-1)
sprawdz("kółko nad widoczną częścią strefy dociera do niej bez fokusu (okno pod kursorem)",
        wid and czekaj(r"K1-GOSC: wejście: kółko -1 ząbków, -120/120", czas=60), str(wid[:1]))

# 0.10.5 step 6 (T16, S43): Alt+Tab between a K1 window and the zone's. ekran gives Tab with Alt held to the shell — never
# to the zone; a zone with the focus gets Alt going up first, so its guest does not keep Alt held ("wciśnięte": the keys
# the guest holds when it counts a key going down).
POKAZ = r"K1-GUI: przełącznik okien: pokazuję "
n = q.ile(POKAZ + "Strefa próbna")  # `od`: earlier matches of the same pattern
nk = q.ile(r"K1-GOSC: wejście: klawisze")
q.pisz("alt-tab")
sprawdz("Alt+Tab z Zegara: okno strefy (używane przed chwilą) na wierzchu; gość nie dostaje nic z Alt+Tab",
        czekaj(POKAZ + "Strefa próbna", od=n, czas=60) and q.cisza(r"K1-GOSC: wejście: klawisze", 8) and
        q.ile(r"K1-GOSC: wejście: klawisze") == nk)
n = q.ile(POKAZ + "Zegar")
q.pisz("alt-tab")
sprawdz("Alt+Tab ze strefy do Zegara: ekran puszcza strefie Alt, Tab do niej nie trafia (gość: jeden klawisz — Alt)",
        czekaj(POKAZ + "Zegar", od=n, czas=60) and
        czekaj(r"K1-EKRAN: Alt\+Tab: przełączanie okien w powłoce \(strefa z fokusem: puszczony Alt\)", czas=10) and
        czekaj(r"K1-GOSC: wejście: klawisze 4, kliknięcia 1", czas=60) and
        q.cisza(r"K1-GOSC: wejście: klawisze 5", 8))
n = q.ile(POKAZ + "Strefa próbna")
q.pisz("alt-tab")
sprawdz("i z powrotem do strefy", czekaj(POKAZ + "Strefa próbna", od=n, czas=60))
q.pisz("b")
sprawdz("po Alt+Tab gość nie trzyma Alt: przy wciśnięciu litery wciśnięty jest tylko ten klawisz",
        czekaj(r"K1-GOSC: wejście: klawisze 5, kliknięcia 1, wciśnięte 1\b", czas=60),
        (re.findall(r"K1-GOSC: wejście: klawisze[^\n]*", q.tekst()) or ["brak"])[-1])

# the dark theme: Ustawienia → Wygląd → Ciemny (as in the other tests)
q.klik(ax + 2 * krok, ay)
u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=60)
sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+); przyciski od (\d+),(\d+) \(szer\. (\d+)", czas=30)
if u and sk:
    ux, uy = int(u.group(4)), int(u.group(5))
    q.klik(ux + int(sk.group(1)), uy + int(sk.group(2)))
    q.klik(ux + int(sk.group(4)) + int(sk.group(6)) * 3 // 4, uy + int(sk.group(5)) + 10)
sprawdz("ciemny motyw", czekaj(r"K1-EKRAN: ustawienie 1=1", czas=60))
# the zone's window to the front: a click on its content (focus), the frame then in the full colour
n = q.ile(r"K1-GOSC: wejście: klawisze 5, kliknięcia 2")
q.klik(x + w_ - 40, y + h_ - 40)
czekaj(r"K1-GOSC: wejście: klawisze 5, kliknięcia 2", czas=60)
ciemna_ramka = lambda w, h, px: blisko(px(x - 2, y + h_ // 2), TLO_OKNA["ciemny"], 8) and blisko(px(x + w_ // 2, y - 1), PIASEK)
w, h, px = q.zrzut("6-ciemny", gdy=ciemna_ramka)
sprawdz("ciemny motyw: ramka w tle okna, linia pod paskiem w kolorze strefy, pasek tytułu nad treścią (zrzut strefa-6-ciemny)",
        ciemna_ramka(w, h, px) and not blisko(px(zx, zy), TLO_GOSCIA))
if o:
    wzorzec_ramki("ciemny")

# closing from the title bar
p = na_pasek()
if p:
    q.klik(int(p.group(5)), int(p.group(6)))
n_ses = q.ile(r"K1-EKRAN: okno \d+: rozmiar sesji")
sprawdz("„zamknij” na pasku: VMM prosi gościa (vsock), gość się zamyka, maszyna kończy się, okno znika",
        p and czekaj(r"K1-STREFA: zamykam strefę na prośbę ekranu", czas=30) and
        czekaj(r"K1-STREFA: gość się zamyka", czas=60) and
        czekaj(r"K1-STREFA: strefa 1: koniec — gość zakończył pracę \(reset\)", czas=60) and
        czekaj(r"K1-EKRAN: okno {} zamknięte".format(nr), czas=30) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 0\)", czas=30))

ses = czekaj(r"K1-EKRAN: okno \d+: rozmiar sesji (\d+)x(\d+)", od=n_ses, czas=600)
sprawdz("rozmiar okna zapamiętany dla sesji: następne okno strefy próbnej dostaje ostatni rozmiar (skarbiec)",
        ses and (int(ses.group(1)), int(ses.group(2))) == (w0, h0), ses.group(0) if ses else "brak")
sprawdz("LICZNIKI (0.10.1 krok 1): co kilka sekund linia liczników — pamięć gościa, zram, grafika, połączenia; przy końcu "
        "jeszcze raz; nadzorca: szczyt pamięci klatki i zdarzenia jej limitu",
        czekaj(r"K1-STREFA: liczniki: gość — dostępna [1-9]\d* MiB, zram \d+ MiB, zabite 0; pamięć w K1 [1-9]\d* MiB, "
               r"oddane \d+ MiB, balon 0 MiB; grafika: zasoby [1-9]",
               czas=10) and czekaj(r"K1-STREFA: liczniki przy końcu: gość — dostępna", czas=10) and
        czekaj(r"K1-NADZORCA: strefa 1: pamięć klatki — szczyt [1-9]\d* MiB \(od startu K1\), limit osiągnięty 0, zabite 0",
               czas=10))
for l_ in re.findall(r"K1-STREFA: liczniki[^\n]*", q.tekst())[-2:]:
    print("            | " + l_[:240], flush=True)

POWODY = {1: "deskryptor poza pamięcią gościa", 2: r"pętla deskryptorów \(łańcuch dłuższy niż kolejka\)",
          3: "virtio-blk: odczyt dłuższy niż limit", 4: "virtio-blk: zapis do obrazu tylko do odczytu"}
OPISY = {1: "deskryptor poza pamięcią", 2: "pętla deskryptorów", 3: "odczyt ponad limit", 4: "zapis do obrazu tylko do odczytu"}
for t in range(1, 5):
    sprawdz(f"NEGATYWNY: złośliwy gość ({OPISY[t]}) — kończy się tylko ta strefa, powód w logu",
            czekaj(rf"K1-TEST: strefy: strefa próbna uruchomiona \(tryb {t}\)", czas=120) and
            czekaj(rf"K1-STREFA: strefa 1: koniec — {POWODY[t]}", czas=180) and
            czekaj(rf"K1-TEST: strefy: strefa skończyła się \(tryb {t}\)", czas=60))
# 0.10.1 step 2 (ADR 0044): guests hostile to the balloon (the VMM puts one in the slot the guest drives itself)
POWODY.update({6: "balon: strona poza pamięcią gościa", 7: "balon: zgłoszony blok nie z całych stron",
               8: "balon: lista stron nie do odczytu albo za długa"})
OPISY.update({6: "balon: strona poza pamięcią", 7: "balon: blok nie z całych stron", 8: "balon: za długa lista stron"})
for t in range(6, 9):
    sprawdz(f"NEGATYWNY: złośliwy gość ({OPISY[t]}) — kończy się tylko ta strefa, powód w logu",
            czekaj(rf"K1-TEST: strefy: strefa próbna uruchomiona \(tryb {t}\)", czas=120) and
            czekaj(rf"K1-STREFA: strefa 1: koniec — {POWODY[t]}", czas=180) and
            czekaj(rf"K1-TEST: strefy: strefa skończyła się \(tryb {t}\)", czas=60))


def oddane():  # the VMM's counters: MiB handed back to K1, in order
    return [int(m) for m in re.findall(r"K1-STREFA: liczniki: gość — [^;]*; pamięć w K1 \d+ MiB, oddane (\d+) MiB", q.tekst())]


n_l, g = len(oddane()), None
sprawdz("BALON (0.10.1 krok 2): gość ma górną granicę — pamięć komputera minus rezerwa K1 (więcej niż 128 MiB na start)",
        czekaj(r"K1-TEST: strefy: strefa próbna uruchomiona \(tryb 9\)", czas=120) and
        (g := czekaj(r"K1-NADZORCA: strefa 1 \(Strefa próbna\): pid \d+, uid 300, pamięć 128 MiB na start, granica (\d+) MiB",
                     od=q.ile(r"strefa 1 \(Strefa próbna\): pid")-1, czas=30)) and int(g.group(1)) > 128,
        g.group(0) if g else "")
sprawdz("BALON: gość zajmuje 64 MiB i zwalnia — wolne bloki wracają do K1 (zgłaszanie wolnych stron: oddane ≥ 48 MiB)",
        czekaj(r"K1-GOSC: pamięć: zajęte i zwolnione 64 MiB: tak", czas=120) and
        q.czekaj_na(lambda: any(v >= 48 for v in oddane()[n_l:]), czas=60), str(oddane()[n_l:]))
sprawdz("BALON: nadzorca prosi o 32 MiB w balonie — gość je oddaje (cel osiągnięty), potem 0 — bierze z powrotem",
        czekaj(r"K1-NADZORCA: strefa 1: cel balonu 32 MiB", czas=60) and
        czekaj(r"K1-STREFA: balon: gość ma w nim 32 MiB \(cel\)", czas=60) and
        czekaj(r"K1-NADZORCA: strefa 1: cel balonu 0 MiB", czas=60) and
        czekaj(r"K1-STREFA: balon: gość ma w nim 0 MiB \(cel\)", czas=60))
sprawdz("BALON: nagły skok — gość bierze naraz prawie całą swoją pamięć, bez czekania na K1, i żyje; potem kończy się sam",
        czekaj(r"K1-GOSC: pamięć: skok [1-9]\d* MiB naraz: żyję", czas=120) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 9\)", czas=120) and
        czekaj(r"K1-NADZORCA: strefa 1: pamięć na końcu — w K1 \d+ MiB, wolne w gościu -?\d+ MiB, balon 0 MiB \(cel 0\), oddane [1-9]\d* MiB",
               czas=30))
for l_ in re.findall(r"K1-(?:STREFA: (?:liczniki|balon)|NADZORCA: strefa 1: (?:pamięć|cel))[^\n]*", q.tekst())[-8:]:
    print("            | " + l_[:240], flush=True)

DYSK = r"K1-GOSC: dysk: uruchomienie (\d+)"
liczniki = lambda: [int(m) for m in re.findall(DYSK, q.tekst())]
sprawdz("STREFA PAMIĘTA (krok 5): z danymi na dysku nadzorca daje strefie dysk w skarbcu — pierwsze uruchomienie 1",
        czekaj(r"K1-NADZORCA: strefa 1 \(Strefa próbna\): pid \d+, .*pamięta \(dysk w skarbcu\)", czas=30) and
        liczniki()[:1] == [1], str(liczniki()))
sprawdz("…ponowne otwarcie: gość czyta licznik z dysku — 2 (dane w skarbcu, kawałki po 64 KiB)",
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 5\)", czas=300) and liczniki()[1:2] == [2], str(liczniki()))
sprawdz("„Wyczyść dane sesji”: skarbiec usuwa kawałki dysku strefy, następne otwarcie liczy od 1",
        czekaj(r"K1-NADZORCA: strefa 1: dane sesji usunięte \(\d+ kawałków\)", czas=60) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 5\)", od=1, czas=300) and liczniki()[2:3] == [1],
        str(liczniki()))
z = re.findall(r"K1-STREFA: dysk strefy: kawałki wczytane (\d+), zapisane (\d+), błędy (\d+)", q.tekst())
print(f"  [info]  dysk strefy na końcu każdego uruchomienia (wczytane, zapisane, błędy): {z}", flush=True)
sprawdz("dysk strefy bez błędów zapisu i odczytu", z and all(b == "0" for _, _, b in z), str(z))
# 0.10.1 step 3 (T6, D3): the rozdzielacz — K1 here has 1 GiB, below its reserve (768 MiB): nadzorca takes the guest's
# free memory by its balloon itself; when no more can be taken, gui shows "Mało pamięci" with the zone and "Zamknij"
sprawdz("ROZDZIELACZ (0.10.1 krok 3): K1 poniżej rezerwy — nadzorca sam bierze balonem wolną pamięć strefy (cel od "
        "rozdzielacza, gość oddaje strony)",
        czekaj(r"K1-TEST: strefy: strefa próbna uruchomiona \(tryb 10\)", czas=300) and
        czekaj(r"K1-NADZORCA: rozdzielacz: K1 ma -?\d+ MiB \(rezerwa \d+\); cel balonu: strefa 1 [1-9]\d* MiB", czas=120)
        and czekaj(r"K1-STREFA: balon: gość ma w nim [1-9]\d* MiB \(cel\)", czas=120),
        "; ".join(re.findall(r"K1-NADZORCA: rozdzielacz:[^\n]*", q.tekst())[-3:]))
sprawdz("fokus: okno strefy aktywne — gui mówi nadzorcy, która strefa ma fokus (STREFA_FOKUS)",
        czekaj(r"K1-NADZORCA: fokus: strefa 1", czas=60))
sprawdz("Zasoby: sesja ze swoim przydziałem (używa … z …)", czekaj(r"K1-ZASOBY: strefa 1: używa \d+ MiB z [1-9]\d* MiB", czas=120))
mp = czekaj(r"K1-GUI: okienko \(mało pamięci\): sesja 1 \(\d+ MiB\) Zamknij (\d+),(\d+); Później \d+,\d+", czas=180)
sprawdz("MAŁO PAMIĘCI (D3): balon nie może wziąć więcej — okienko z sesją, jej pamięcią i „Zamknij”",
        mp and czekaj(r"K1-NADZORCA: mało pamięci: K1 ma -?\d+ MiB \(rezerwa \d+\), balony nie mogą wziąć więcej — okienko"))
if mp:
    q.zrzut("9-malo-pamieci")
    q.klik(int(mp.group(1)), int(mp.group(2)))
sprawdz("„Zamknij” w okienku zamyka sesję: okno strefy, maszyna kończy się (bez okienka „zamknęła się”)",
        mp and czekaj(r"K1-GUI: okienko \(mało pamięci\): zamykam sesję 1", czas=30) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 10\)", czas=300) and
        not re.search(r"K1-GUI: strefa 1 zamknęła się", q.tekst()))
# 0.10.2 step 1 (S1, S2, T1; the owner's D1): the start screen stays until the guest's program shows its window — the
# test guest (mode 11) says "etap start", shows its first picture at once and its "window" 8 s later
OKNO_STREFY = r"K1-EKRAN: okno (\d+) \(strefa\) (\d+)x(\d+) w (\d+),(\d+)"
sprawdz("EKRAN STARTU (0.10.2 krok 1): gość z „oknem” (tryb 11) mówi „etap start”; jego pierwszy obraz przychodzi, a ekran "
        "startu zostaje",
        czekaj(r"K1-TEST: strefy: strefa próbna uruchomiona \(tryb 11\)", czas=300) and
        czekaj(r"K1-STREFA: gość: program strefy startuje — ekran startu do jego okna", czas=240) and
        czekaj(r"K1-STREFA: pierwszy obraz gościa \(\d+x\d+\) — ekran startu zostaje do okna programu strefy", czas=120))
t_ = q.tekst()
o2 = czekaj(OKNO_STREFY, od=len(re.findall(OKNO_STREFY, t_[:t_.find("strefa skończyła się (tryb 10)")])), czas=30)
nr2, w2, h2, x2, y2 = (int(o2.group(i)) for i in range(1, 6)) if o2 else (0, 0, 0, 0, 0)


def odcinek(px):  # where the segment in the zone's colour is on the start screen's bar (-1: not there)
    by = y2 + h2 // 2 + 24
    return next((dx for dx in range(-80, 80) if blisko(px(x2 + w2 // 2 + dx, by), PIASEK, 40)), -1)


def ekran_startu(w, h, px):
    return o2 and not blisko(px(x2 + w2 // 2, y2 + h2 // 6), TLO_GOSCIA) and odcinek(px) >= -80


w, h, px = q.zrzut("10-ekran-startu", gdy=ekran_startu)
d1 = odcinek(px)
sprawdz("…obraz gościa jest, a okno pokazuje „Uruchamiam…” z paskiem w kolorze strefy (zrzut strefa-10-ekran-startu)",
        ekran_startu(w, h, px) and not re.search(r"K1-STREFA: ekran startu zniknął", q.tekst()), f"odcinek {d1}")
w, h, px = q.zrzut("11-pasek-w-ruchu", gdy=lambda w, h, px: ekran_startu(w, h, px) and odcinek(px) not in (-1, d1),
                   czas=5)
sprawdz("…pasek w ruchu: odcinek przesuwa się (dwa zrzuty)", odcinek(px) not in (-1, d1), f"{d1} → {odcinek(px)}")
sprawdz("…ekran startu znika dopiero przy „etap okno” (8 s po pierwszym obrazie), potem obraz gościa (zrzut strefa-12-okno)",
        czekaj(r"K1-STREFA: gość: okno przeglądarki po", czas=120) and
        czekaj(r"K1-STREFA: ekran startu zniknął: okno programu strefy po [\d.]+ s", czas=30) and
        blisko(q.zrzut("12-okno", gdy=lambda w, h, px: blisko(px(x2 + w2 // 2, y2 + h2 // 6), TLO_GOSCIA))[2](
            x2 + w2 // 2, y2 + h2 // 6), TLO_GOSCIA))
# 0.10.2 step 1 (S3, T2): "zamknij" — the window goes at once, the machine ends in the background (the guest takes 6 s,
# as the browser saving its session); meanwhile the test program asks for the same zone again (D2: it opens after)
PASEK2 = (r"K1-EKRAN: strefa: pasek okna {} \(minimalizuj (\d+),(\d+), maksymalizuj (\d+),(\d+), zamknij (\d+),(\d+)\)"
          .format(nr2))
n_ = q.ile(PASEK2)
q.idz(x2 + w2 // 2, y2 + 60)
q.idz(x2 + w2 // 2, y2 - TYTUL // 2)
p2 = czekaj(PASEK2, od=n_, czas=30)
KONIEC1 = r"K1-STREFA: strefa 1: koniec — gość zakończył pracę"
n_kon = q.ile(KONIEC1)
if p2:
    q.klik(int(p2.group(5)), int(p2.group(6)))
zamk = p2 and czekaj(r"K1-EKRAN: okno {} zamknięte".format(nr2), czas=30)
sprawdz("ZAMYKANIE W TLE: po „zamknij” okno strefy znika od razu — maszyna jeszcze działa (gość zapisuje sesję)",
        zamk and q.ile(KONIEC1) == n_kon and
        czekaj(r"K1-STREFA: okno strefy zamknięte — maszyna kończy się w tle", czas=10) and
        czekaj(r"K1-NADZORCA: strefa 1 zamyka się w tle \(okno zamknięte\)", czas=10))
sprawdz("…potem gość kończy (6 s), maszyna kończy się; okno zniknęło przed jej końcem",
        czekaj(r"K1-GOSC: zamknięty po 6 s", czas=120) and czekaj(KONIEC1, od=n_kon, czas=60))
sprawdz("OTWARCIE W TRAKCIE ZAMYKANIA (D2): nadzorca czeka na starą maszynę i sam otwiera strefę ponownie",
        czekaj(r"K1-NADZORCA: strefa 1 jeszcze się zamyka — otworzę ją, gdy skończy", czas=10) and
        czekaj(r"K1-NADZORCA: strefa 1 skończyła się — otwieram ją ponownie", czas=60) and
        czekaj(r"K1-TEST: strefy: strefa otwarta ponownie po zamknięciu \(tryb 5\)", czas=60) and
        czekaj(r"K1-TEST: strefy: strefa skończyła się \(tryb 5\)", od=2, czas=300))
sprawdz("K1 działa dalej po złośliwych gościach: nadzorca, ekran i pulpit bez awarii",
        czekaj(r"K1-TEST: strefy: koniec scenariusza", czas=60) and
        not re.search(r"K1-NADZORCA: (ekran|gui|nadzorca).*zakończon", q.tekst()))
t_ = q.tekst()
sprawdz("brak zabronionych wywołań i paniki; strefa nie otwiera plików ani gniazd (klatka: jej log bez SIGSYS)",
        "zabronione wywołanie" not in t_ and "Kernel panic" not in t_)
if start and gotowy:
    print(f"  [info]  czas startu strefy (od maszyny do „gotowa”): w logu {start.group(0)[:60]}", flush=True)
# 0.10.2 step 1 (S5): Start → Wyłącz while a zone that remembers runs — nadzorca asks it to close and waits; the guest
# writes its disk on closing (as the browser saves its session), then K1 powers off
GOTOWA = r"K1-GOSC: gotowa: urządzenia wejścia 2, kanał sterujący jest"
ost = czekaj(r"K1-TEST: strefy: strefa próbna uruchomiona \(tryb 0\)", od=1, czas=60)
t_ = q.tekst()
n_g = len(re.findall(GOTOWA, t_[:t_.rfind("strefa próbna uruchomiona (tryb 0)")]))
sprawdz("WYŁĄCZANIE: strefa (tryb 0, pamięta) działa, gdy K1 się wyłącza", ost and czekaj(GOTOWA, od=n_g, czas=240))
KAWALKI = r"K1-STREFA: dysk strefy: kawałki wczytane \d+, zapisane [1-9]\d*, błędy 0"
n_kaw = q.ile(KAWALKI)
m = czekaj(r"K1-GUI: (?:pasek zmieniony|pulpit gotowy): Start (\d+),(\d+)", czas=10)
nm = q.ile(r"menu Start otwarte")
if m:
    q.klik(int(m.group(1)), int(m.group(2)))
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+); Wyłącz (\d+),(\d+);", od=nm, czas=30)
if m:
    q.klik(int(m.group(4)), int(m.group(5)))
wyl = m and czekaj(r"K1-NADZORCA: wyłączanie: najpierw zamykam strefy \(1, najwyżej 30 s\)", czas=30)
sprawdz("…nadzorca najpierw prosi strefę o zamknięcie (jak „zamknij” na pasku), gość zapisuje dysk i kończy się",
        wyl and czekaj(r"K1-STREFA: zamykam strefę na prośbę nadzorcy \(wyłączanie K1\)", czas=30) and
        czekaj(r"K1-GOSC: dysk: zamknięcie zapisane: tak", czas=60) and
        czekaj(KAWALKI, od=n_kaw, czas=60))
sprawdz("…dopiero potem K1 wyłącza resztę (skarbiec przyjął zapis) i komputer się wyłącza",
        wyl and czekaj(r"K1-NADZORCA: strefy zamknięte — wyłączam resztę", czas=60) and
        czekaj(r"K1: wyłączanie zasilania", czas=60) and czekaj(r"reboot: Power down", czas=60))
if not re.search(r"reboot: Power down", q.tekst()):
    q.monitor("quit")
qemu.wait()

# ---- a processor without virtualization ----
log2, mon2 = f"{out}/strefa-bez-wirt.log", f"{out}/strefa-bez-wirt.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log2, "--monitor", mon2, "--czas", str(int(limit)),
                         "--pamiec", "1024", "--qemu", ZNAK, "--bez-wirtualizacji"] + emulacja)
q2 = Maszyna(log2, mon2, out, limit, "strefa-bez-wirt")
q2.wyniki = q.wyniki
q2.sprawdz("NEGATYWNY: bez wirtualizacji nadzorca mówi „brak wirtualizacji”, strefa nie startuje, nadzorca żyje",
           q2.czekaj(r"K1-NADZORCA: strefa 1: brak wirtualizacji", czas=limit * 0.5) and
           q2.czekaj(r"K1-TEST: strefy: brak wirtualizacji — strefa nie ruszyła", czas=60))
ok = q2.czekaj(r"K1-GUI: strefa 1 nie ruszyła \(powód 1\): okienko", czas=30)
q2.zrzut("7-bez-wirtualizacji")
q2.sprawdz("pulpit mówi, co zrobić: okienko „Strefa próbna nie może ruszyć” z radą o BIOS-ie (zrzut "
           "strefa-bez-wirt-7-bez-wirtualizacji)", ok)
q2.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test strefy próbnej (0.10.0): KVM, VMM w klatce, okno w zaufanej ramce, wejście, złośliwy gość, brak wirtualizacji"
r=0
if unsquashfs -l "$K1_WYNIK/rdzen.squashfs" 2>/dev/null | grep -q 'strefy/probna'; then
    echo "  [BŁĄD]  obraz produkcyjny ma strefę próbną"; r=1
else
    echo "  [OK]    obraz produkcyjny bez strefy próbnej (jest tylko jądro strefy)"
fi
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/strefa.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$ZNAK" $EMULACJA || r=1
if [ $r -eq 0 ]; then info "strefa: wszystko zaliczone"; else echo "[k1] strefa: BŁĄD (log: $TESTY/strefa.log)"; fi
exit $r

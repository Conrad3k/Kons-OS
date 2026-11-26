#!/bin/sh
# Test 0.10.2 krok 3 (powtarzalny): ZRZUTY WZORCOWE — wygląd okien K1 co do piksela z zatwierdzonymi obrazami
# (testy/wzorce/*.png, testy/qemu/k1wzorce.py). Obraz testowy z opisem ekranu (dysk „k1-test-opis”, ADR 0045) i pustym
# dyskiem danych; okna z decyzji właściciela D4, w obu motywach: dolny i górny pasek, ikony pulpitu, menu Start, okienko
# „Którą sesję otworzyć?”, Ustawienia (każda sekcja), Pliki (pusty folder, zaznaczony folder, Kosz — 0.10.7 krok 3), Zasoby,
# Kalkulator, przełącznik okien (0.10.5 krok 6), okienko „Dźwięk” (0.12.0 krok 1, z kartą dźwiękową QEMU; od 0.12.3 krok 2 z wierszem „Korektor”), Ustawienia → Dźwięk → Korektor (0.12.3 krok 2); ekran „Wyłączanie” (0.10.7 krok 6, na hoście: k1_wylaczanie); okienko
# baterii (0.11.8 krok 3) — w osobnym przebiegu z tablicą ACPI baterii (z baterią Ustawienia i Zasoby wyglądają inaczej).
# Ramkę strefy sprawdza test-strefa.
# Każde okno wycięte według opisu ekranu; miejsca zmienne (zegar, liczby, wykresy, wersja) się nie liczą; kursor poza
# oknem. Różnica: build/testy/wzorce/<nazwa>-przed/-po/-roznica.png (w CI: artefakt z logami). Zmianę wyglądu
# zatwierdza właściciel (D3): dopiero po jego „OK” scripts/przyjmij-wzorce.sh przenosi obrazy „po” do testy/wzorce/.
# Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1500}
mkdir -p "$TESTY"; rm -f "$TESTY"/wzorce-* "$TESTY"/wzorce.log  # build/testy/wzorce/: each check clears its own pictures
r=0

cat > "$K1_BUDOWA/wzorce.py" <<'PY'
import os, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
BATERIA = sys.argv[5:] == ["bateria"]  # 0.11.8 step 3: the pass with a battery — its popover only
q = Maszyna(log, mon, out, limit, "wzorce")
# QEMU has no battery: Bateria is not on the list (0.10.7 step 2; with a battery: test-m5)
SEKCJE = {"Wygląd": "wyglad", "Ekran": "ekran", "Mysz": "mysz", "Data i czas": "czas",
          "Hasło": "haslo", "Aplikacje": "aplikacje", "Miejsce": "miejsce", "Sieć": "siec",
          "Przeglądarka": "przegladarka", "Aktualizacje": "aktualizacje", "Dźwięk": "dzwiek", "Informacje": "informacje"}

if not q.sprawdz("pulpit gotowy, opis ekranu: pasek z „Start” i ikony pulpitu",
                 q.czekaj(r"K1-GUI: pulpit gotowy", czas=limit * 0.5) and q.czekaj_na_element("Pasek", "Start", czas=120)
                 and q.czekaj_na_element("Pulpit", "Pliki", czas=60) and q.czekaj_na_element("Pasek górny", czas=60)):
    q.koniec()


def otworz(nazwa):
    """An application from the bottom bar, its window in the description (and the focus: the newest window)."""
    e = q.klik_w("Pasek", nazwa, "ikona")
    return e and q.czekaj_na_element(nazwa, "Zamknij", "przycisk", czas=120)


def zamknij(nazwa):
    q.klik_w(nazwa, "Zamknij", "przycisk")
    q.czekaj_na(lambda: q.element(nazwa) is None, czas=60)


def pusty_pulpit():
    """A click on the desktop where nothing is (it closes a menu)."""
    W, H = q.tryb_ekranu()
    q.klik(W * 2 // 3, H // 3)


def ustawienia(motyw):
    """Every section of Ustawienia (opened when it is not: a click on a running application's icon minimises it)."""
    if not q.element("Ustawienia") and not otworz("Ustawienia"):
        return q.sprawdz("Ustawienia otwarte", False)
    q.sprawdz("NEGATYWNY: komputer bez baterii, sekcji „Bateria” nie ma na liście Ustawień",
              q.czekaj(r"K1-USTAWIENIA: sekcje na liście: 0 1 3 4 ", czas=30) and  # the first frame may come before it
              q.czekaj_na(lambda: q.element("Ustawienia", "Wygląd", "zakladka") is not None
                          and q.element("Ustawienia", "Bateria", "zakladka") is None, czas=30))
    for sekcja, krotko in SEKCJE.items():
        stan_akt = r"K1-USTAWIENIA: aktualizacje: (?!sprawdzam)"
        n = q.ile(stan_akt)
        q.klik_w("Ustawienia", sekcja, "zakladka")
        q.czekaj_na(lambda: (q.element("Ustawienia", sekcja, "zakladka") or {}).get("stan", 0) & 1, czas=30)
        if sekcja == "Aktualizacje":  # 0.10.5 step 1: the disks came from nadzorca (not "Sprawdzam dyski…")
            q.czekaj(stan_akt, od=n, czas=30)
        q.wzorzec(f"ustawienia-{krotko}-{motyw}", "Ustawienia")
        if sekcja == "Dźwięk":  # 0.12.3 step 2 (D2: A): its tab Korektor — flat, the speakers; then back to Głośność
            q.klik_w("Ustawienia", "Korektor", "zakladka")
            if q.sprawdz(f"Ustawienia → Dźwięk → Korektor ({motyw})", q.czekaj_na_element("Ustawienia", "Pasmo 16k", czas=30)):
                q.wzorzec(f"ustawienia-korektor-{motyw}", "Ustawienia")
            q.klik_w("Ustawienia", "Głośność", "zakladka")
            q.czekaj_na_element("Ustawienia", "Odtwórz próbę", czas=30)
    q.klik_w("Ustawienia", "Wygląd", "zakladka")


def pliki_z_folderem(motyw):
    """0.10.7 step 3: "Nowy folder" from the middle of the empty Dokumenty; selected (the buttons that come with a
    selection, the details); in Kosz, selected; then deleted for good (Dokumenty empty again)."""
    n = q.ile(r"K1-PLIKI: zmiana nazwy: pole otwarte")
    q.klik_w("Pliki", "Nowy folder", "przycisk")
    if not q.sprawdz("Pliki: „Nowy folder” w środku pustego folderu", q.czekaj(r"K1-PLIKI: zmiana nazwy: pole otwarte", od=n, czas=30)):
        return
    q.pisz("ret")
    q.klik_w("Pliki", "wiersz 1", "wiersz")
    if q.czekaj_na_element("Pliki", "Usuń", "przycisk", czas=30):
        q.wzorzec(f"pliki-zaznaczony-{motyw}", "Pliki")
        q.klik_w("Pliki", "Usuń", "przycisk")  # one folder: into Kosz without a question
    q.czekaj_na(lambda: q.element("Pliki", "wiersz 1") is None, czas=30)
    n = q.ile(r"K1-PLIKI: widok: Kosz, 0 elementów")
    q.klik_w("Pliki", "Kosz", "zakladka")
    q.klik_w("Pliki", "wiersz 1", "wiersz")
    if q.czekaj_na_element("Pliki", "Usuń na zawsze", "przycisk", czas=30):
        q.wzorzec(f"pliki-kosz-{motyw}", "Pliki")
        q.klik_w("Pliki", "Opróżnij Kosz", "przycisk")
        q.klik_w("Pliki", "Tak, usuń", "przycisk")
    q.sprawdz("Pliki: Kosz opróżniony", q.czekaj(r"K1-PLIKI: widok: Kosz, 0 elementów", od=n, czas=30))


def okienko_dzwieku(motyw):
    """0.12.0 step 1 (D2: B): the popover "Dźwięk" with the card of this pass (QEMU's HDA, no sound out), 50 %."""
    q.sprawdz(f"usługa dźwięku ma kartę ({motyw})", q.czekaj(r"K1-GUI: dźwięk: karta 2,", czas=60))
    q.klik_w("Pasek górny", "Dźwięk", "przycisk")
    if q.sprawdz(f"okienko „Dźwięk” otwarte ({motyw})", q.czekaj_na_element("Okienko paska", "Odtwórz próbę", czas=30)):
        q.wzorzec(f"okienko-dzwieku-{motyw}", "Okienko paska", pomin=[(0, 0, 332, 12)])  # its top: the bar (the clock)
    q.klik_w("Pasek górny", "Dźwięk", "przycisk")
    q.czekaj_na(lambda: q.element("Okienko paska") is None, czas=30)


def pulpit(motyw):
    """The bars, the desktop's icons, the Start menu, the sessions' window, Pliki, Zasoby, Kalkulator."""
    q.wzorzec(f"pasek-dolny-{motyw}", "Pasek")
    q.wzorzec(f"pasek-gorny-{motyw}", "Pasek górny")
    okienko_dzwieku(motyw)
    q.wzorzec(f"pulpit-ikony-{motyw}", "Pulpit", elementy=True)
    q.klik_w("Pasek", "Start", "ikona")
    if q.czekaj_na_element("Menu Start", "Wyłącz", czas=30):
        q.wzorzec(f"menu-start-{motyw}", "Menu Start")
        q.klik_w("Menu Start", "Przeglądarka", "wiersz")
        if q.czekaj_na_element("Okienko", "Anuluj", "przycisk", czas=30):
            q.wzorzec(f"okienko-sesji-{motyw}", "Okienko")
            q.klik_w("Okienko", "Anuluj", "przycisk")
            q.czekaj_na(lambda: q.element("Okienko") is None, czas=30)
    if q.element("Menu Start"):
        pusty_pulpit()
    for apl in ("Pliki", "Zasoby", "Kalkulator"):
        if otworz(apl):
            q.wzorzec(f"{apl.lower()}-{motyw}", apl)
            if apl == "Pliki":
                pliki_z_folderem(motyw)
            if apl == "Zasoby":  # 0.10.10 step 2: the tab "Karta graficzna" (QEMU: no i915 card — "nie podaje")
                n = q.ile(r"K1-ZASOBY: zakładka: Karta graficzna")
                q.klik_w("Zasoby", "Karta graficzna", "zakladka")
                if q.czekaj(r"K1-ZASOBY: zakładka: Karta graficzna", od=n, czas=30):
                    q.wzorzec(f"zasoby-karta-{motyw}", "Zasoby")
            zamknij(apl)
        else:
            q.sprawdz(f"{apl} otwarte", False)
    # 0.10.5 step 6 (D11: sketch A): the window switcher — Alt held, Tab: tiles of Kalkulator and Pliki, Pliki chosen
    if otworz("Pliki") and otworz("Kalkulator"):
        q.klawisze(("alt", 1), ("tab", 1), ("tab", 0))
        if q.czekaj_na_element("Przełącznik okien", czas=30):
            q.wzorzec(f"przelacznik-okien-{motyw}", "Przełącznik okien", elementy=True)
        q.klawisze(("esc", 1), ("esc", 0), ("alt", 0))
        q.czekaj_na(lambda: q.element("Przełącznik okien") is None, czas=30)
    else:
        q.sprawdz("Pliki i Kalkulator otwarte (przełącznik okien)", False)
    for apl in ("Kalkulator", "Pliki"):
        if q.element(apl):
            zamknij(apl)


def okienko_baterii():
    """0.11.8 step 3 (D4: A): the battery's popover in both themes (its numbers are changing places)."""
    for motyw in ("jasny", "ciemny"):
        if motyw == "ciemny" and otworz("Ustawienia"):
            q.klik_w("Ustawienia", "Ciemny", "zakladka")
            q.sprawdz("ciemny motyw (Ustawienia → Wygląd)", q.czekaj(r"K1-EKRAN: ustawienie 1=1", czas=60))
            zamknij("Ustawienia")
        q.klik_w("Pasek górny", "Bateria", "przycisk")
        if q.sprawdz(f"okienko baterii otwarte ({motyw})",
                     q.czekaj_na_element("Okienko paska", "Ustawienia baterii", "przycisk", czas=30)):
            # the panel's surface starts 16 px (its shadow) above the popover, which stands 4 px under the 32 px top bar:
            # its top 12 rows are the bar with the clock — left out
            q.wzorzec(f"okienko-baterii-{motyw}", "Okienko paska", pomin=[(0, 0, 332, 12)])
        q.klik_w("Pasek górny", "Bateria", "przycisk")
        q.czekaj_na(lambda: q.element("Okienko paska") is None, czas=30)


if BATERIA:
    okienko_baterii()
    q.czysty_log()
    q.koniec()
pulpit("jasny")
ustawienia("jasny")
q.klik_w("Ustawienia", "Ciemny", "zakladka")
q.sprawdz("ciemny motyw (Ustawienia → Wygląd)", q.czekaj(r"K1-EKRAN: ustawienie 1=1", czas=60))
ustawienia("ciemny")
zamknij("Ustawienia")
pulpit("ciemny")
q.czysty_log()
q.koniec()
PY

# 0.10.7 step 6 (T8): the screen "Wyłączanie" lasts too short in QEMU for a picture (without a zone K1 powers off in a
# moment) — the same function ekran draws it with (k1_wylaczanie) on the host, 1280 × 800, its still frame
info "test wzorców: ekran „Wyłączanie” (k1_wylaczanie na hoście)"
make -C "$K1_REPO" -s "$K1_BUDOWA/obj/podglad_wylaczania" >/dev/null || r=1
for motyw in jasny ciemny; do
    "$K1_BUDOWA/obj/podglad_wylaczania" 1280 800 "$([ $motyw = ciemny ] && echo 1 || echo 0)" 0 0 \
        > "$TESTY/.wylaczanie-$motyw.ppm" || r=1
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$TESTY/.wylaczanie-$motyw.ppm" "wylaczanie-$motyw" "$TESTY/wzorce" <<'PY' || r=1
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
import k1wzorce
ppm, nazwa, katalog = sys.argv[1:4]
b = open(ppm, "rb").read()
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", b)
w, h, teraz = int(m.group(1)), int(m.group(2)), b[m.end():]
os.makedirs(katalog, exist_ok=True)
for p in ("-przed", "-po", "-roznica"):
    if os.path.exists(os.path.join(katalog, nazwa + p + ".png")):
        os.unlink(os.path.join(katalog, nazwa + p + ".png"))
wzor = k1wzorce.wczytaj(os.path.join(k1wzorce.KATALOG, nazwa + ".png"))
if wzor and wzor[:2] == (w, h) and k1wzorce.rozne(w, h, wzor[2], teraz)[0] == 0:
    print(f"  [OK]    wzorzec {nazwa}: zgodny co do piksela ({w}x{h})")
    sys.exit(0)
k1wzorce.zapisz(os.path.join(katalog, nazwa + "-po.png"), w, h, teraz)
if wzor and wzor[:2] == (w, h):
    k1wzorce.zapisz(os.path.join(katalog, nazwa + "-przed.png"), *wzor)
    k1wzorce.zapisz(os.path.join(katalog, nazwa + "-roznica.png"), w, h, k1wzorce.obraz_roznicy(w, h, wzor[2], teraz))
print(f"  [BŁĄD]  wzorzec {nazwa}: " + ("różny" if wzor else "brak obrazu wzorcowego") + f" — wzorce/{nazwa}-po.png")
sys.exit(1)
PY
    rm -f "$TESTY/.wylaczanie-$motyw.ppm"
done

info "test wzorców: wygląd okien K1 co do piksela (obraz testowy z opisem ekranu)"
DANE=$TESTY/wzorce-dane.img
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
ZNAK=$TESTY/wzorce-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
DYSKI="-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0"
DYSKI="$DYSKI -drive if=none,id=znak,file=$ZNAK,format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-opis,addr=0x1e"
# 0.12.0 step 2: one date — the top bar's date ("śr. 25 lis" / "czw. 26 lis") moves its icons, and the battery's popover
# stands under its icon: on another day its picture (the wallpaper through it) differed (CI run 350). Wednesday, 11:00 in
# Poland; the clock's digits keep their width.
DYSKI="$DYSKI -rtc base=2026-11-25T10:00:00"
# 0.12.0 step 1: a sound card (its sound goes nowhere) — the popover "Dźwięk" with its controls
KARTA="-audiodev none,id=dz -device ich9-intel-hda,id=hda -device hda-output,audiodev=dz,bus=hda.0"
rm -f "$TESTY/wzorce.log" "$TESTY/wzorce.sock"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/wzorce.log" --monitor "$TESTY/wzorce.sock" --czas "$CZAS" \
    --obraz "$K1_WYNIK/rdzen-test.squashfs" --qemu "$DYSKI $KARTA" &
QPID=$!
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/wzorce.py" "$TESTY/wzorce.log" "$TESTY/wzorce.sock" "$TESTY" "$CZAS" || r=1
zamknij_qemu "$TESTY/wzorce.sock" $QPID

info "test wzorców: okienko baterii (osobny przebieg z tablicą ACPI baterii: 73 %, ładowanie)"
bateria_aml 2 1 "$TESTY/wzorce-bateria.aml"
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none  # anew: the first pass left the dark theme in it
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
rm -f "$TESTY/wzorce-bateria.log" "$TESTY/wzorce-bateria.sock"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/wzorce-bateria.log" --monitor "$TESTY/wzorce-bateria.sock" --czas "$CZAS" \
    --obraz "$K1_WYNIK/rdzen-test.squashfs" --qemu "$DYSKI -acpitable file=$TESTY/wzorce-bateria.aml" &
QPID=$!
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/wzorce.py" "$TESTY/wzorce-bateria.log" "$TESTY/wzorce-bateria.sock" \
    "$TESTY" "$CZAS" bateria || r=1
zamknij_qemu "$TESTY/wzorce-bateria.sock" $QPID
if [ $r -eq 0 ]; then
    info "wzorce: wszystko zgodne co do piksela"
else
    echo "[k1] wzorce: BŁĄD — obrazy przed / po / różnica: $TESTY/wzorce/ (zatwierdzenie zmiany: scripts/przyjmij-wzorce.sh)"
fi
exit $r

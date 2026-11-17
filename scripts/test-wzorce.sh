#!/bin/sh
# Test 0.10.2 krok 3 (powtarzalny): ZRZUTY WZORCOWE — wygląd okien K1 co do piksela z zatwierdzonymi obrazami
# (testy/wzorce/*.png, testy/qemu/k1wzorce.py). Obraz testowy z opisem ekranu (dysk „k1-test-opis”, ADR 0045) i pustym
# dyskiem danych; okna z decyzji właściciela D4, w obu motywach: dolny i górny pasek, ikony pulpitu, menu Start, okienko
# „Którą sesję otworzyć?”, Ustawienia (każda sekcja), Pliki, Zasoby, Kalkulator. Ramkę strefy sprawdza test-strefa.
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
q = Maszyna(log, mon, out, limit, "wzorce")
SEKCJE = {"Wygląd": "wyglad", "Ekran": "ekran", "Bateria": "bateria", "Mysz": "mysz", "Data i czas": "czas",
          "Hasło": "haslo", "Aplikacje": "aplikacje", "Miejsce": "miejsce", "Sieć": "siec",
          "Przeglądarka": "przegladarka", "Aktualizacje": "aktualizacje", "Informacje": "informacje"}

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
    for sekcja, krotko in SEKCJE.items():
        stan_akt = r"K1-USTAWIENIA: aktualizacje: (?!sprawdzam)"
        n = q.ile(stan_akt)
        q.klik_w("Ustawienia", sekcja, "zakladka")
        q.czekaj_na(lambda: (q.element("Ustawienia", sekcja, "zakladka") or {}).get("stan", 0) & 1, czas=30)
        if sekcja == "Aktualizacje":  # 0.10.5 step 1: the disks came from nadzorca (not "Sprawdzam dyski…")
            q.czekaj(stan_akt, od=n, czas=30)
        q.wzorzec(f"ustawienia-{krotko}-{motyw}", "Ustawienia")
    q.klik_w("Ustawienia", "Wygląd", "zakladka")


def pulpit(motyw):
    """The bars, the desktop's icons, the Start menu, the sessions' window, Pliki, Zasoby, Kalkulator."""
    q.wzorzec(f"pasek-dolny-{motyw}", "Pasek")
    q.wzorzec(f"pasek-gorny-{motyw}", "Pasek górny")
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
            zamknij(apl)
        else:
            q.sprawdz(f"{apl} otwarte", False)


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

info "test wzorców: wygląd okien K1 co do piksela (obraz testowy z opisem ekranu)"
DANE=$TESTY/wzorce-dane.img
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
ZNAK=$TESTY/wzorce-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
DYSKI="-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0"
DYSKI="$DYSKI -drive if=none,id=znak,file=$ZNAK,format=raw,readonly=on -device virtio-blk-pci,drive=znak,serial=k1-test-opis,addr=0x1e"
rm -f "$TESTY/wzorce.log" "$TESTY/wzorce.sock"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/wzorce.log" --monitor "$TESTY/wzorce.sock" --czas "$CZAS" \
    --obraz "$K1_WYNIK/rdzen-test.squashfs" --qemu "$DYSKI" &
QPID=$!
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/wzorce.py" "$TESTY/wzorce.log" "$TESTY/wzorce.sock" "$TESTY" "$CZAS" || r=1
zamknij_qemu "$TESTY/wzorce.sock" $QPID
if [ $r -eq 0 ]; then
    info "wzorce: wszystko zgodne co do piksela"
else
    echo "[k1] wzorce: BŁĄD — obrazy przed / po / różnica: $TESTY/wzorce/ (zatwierdzenie zmiany: scripts/przyjmij-wzorce.sh)"
fi
exit $r

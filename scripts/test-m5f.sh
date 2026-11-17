#!/bin/sh
# Test etapu M5f (ADR 0018): instalator — K1 z pendrive'a na pusty dysk SATA i start z dysku.
# 1. pendrive (obraz USB) + pusty dysk 4 GiB: Instalator z menu Start → dysk → „USUŃ” → instalacja;
#    na dysku GPT z „K1-EFI” i „K1-DANE” (sfdisk), program zapisujący w klatce (UID 105);
# 2. start z samego dysku (UEFI, bez pendrive'a): pulpit, nowy magazyn na K1-DANE dysku;
#    (0.8.5 krok 3) z dysku: Ustawienia → Miejsce — „nośnik: dysk”, bez przycisku powiększania pendrive'a.
# 3. znów pendrive + dysk: dane z dysku wewnętrznego (partycja pendrive'a pominięta), Ustawienia → Aktualizacje
#    (0.10.5 krok 1; wcześniej Instalator) rozpoznają starszy K1 na dysku → aktualizacja (tylko K1-EFI), dane nietknięte;
#    (0.9.0 krok 3, ADR 0038) dysk ma „stare” pliki startowe (inny grub.cfg i .disk/info, jeden plik więcej):
#    pendrive ze zmienionym plikiem startowym — odmowa, dysk nietknięty; aktualizacja przerwana (QEMU wyłączone
#    w trakcie zapisu plików startowych) — dysk dalej startuje; aktualizacja — pliki startowe dysku = pendrive'a
#    (mtools), fsck.fat bez błędów;
# 4. start z dysku po aktualizacji: nowe miejsce (B) wystartowało na próbę i K1 je potwierdził (0.7.0, A/B),
#    magazyn z kroku 2 otwarty (dane przetrwały);
# 5. wycofanie (A/B): „nowe” miejsce A jest uszkodzone — GRUB wraca do B, K1 zgłasza nieudaną aktualizację: okienko na
#    pulpicie (0.10.5 krok 1), „Pokaż w Ustawieniach” otwiera Ustawienia → Aktualizacje z „nieudaną próbą”;
# 6. (M6a) na dysku nowsza wersja niż na pendrivie — Ustawienia → Aktualizacje bez przycisku, nic się nie zaczyna.
# Z dysku (bez pendrive'a) Instalator nie ma ekranu aktualizacji: karta z linkiem do Ustawień → Aktualizacje.
# Zrzuty: build/testy/m5f-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-300}
USBIMG=$K1_WYNIK/k1-$K1_WERSJA-usb.img
DYSK=$TESTY/m5f-dysk.img PEN=$TESTY/m5f-pendrive.img
mkdir -p "$TESTY"; rm -f "$TESTY"/m5f-*
[ -f "$USBIMG" ] || blad "brak obrazu USB $USBIMG (scripts/zbuduj-usb.sh)"
cp "$USBIMG" "$PEN"
truncate -s 4G "$DYSK"
r=0

echo "[k1] test M5f: instalator — pendrive → dysk, start z dysku, aktualizacja z zachowaniem danych"

# $1 = scenario, $2 = log name; the rest: arguments of uruchom-qemu.sh
uruchom() {
    tryb=$1 nazwa=$2; shift 2
    LOG=$TESTY/m5f-$nazwa.log MON=$TESTY/m5f-$nazwa.sock
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" "$@" &
    QPID=$!
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$tryb" "$LOG" "$MON" "$TESTY" "$CZAS" <<'PY' || r=1
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
tryb = sys.argv[1]
q = Maszyna(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], "m5f")
czekaj, sprawdz, tekst, monitor, klik, cisza = q.czekaj, q.sprawdz, q.tekst, q.monitor, q.klik, q.cisza
def zrzut(nazwa): q.zrzut(nazwa)
def koniec():
    q.czysty_log()
    q.koniec()

def aktualizacje(g):
    """0.10.5 step 1: Ustawienia → Aktualizacje from the Start menu (3rd row, section 11); (window x, y, state line)."""
    klik(int(g.group(1)), int(g.group(2)))
    m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+)", czas=20)
    if not m:
        return None
    klik(int(m.group(1)), int(m.group(2)) + 2 * int(m.group(3)))
    o = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    if not (o and sk):
        return None
    ox, oy = int(o.group(4)), int(o.group(5))
    klik(ox + int(sk.group(1)), oy + int(sk.group(2)) + 10 * int(sk.group(3)))
    return ox, oy, czekaj(STAN_AKT, czas=60)
STAN_AKT = r"K1-USTAWIENIA: aktualizacje: (?!sprawdzam)([^(]+?) \(dysk: ([^,]*), pendrive: ([^)]*)\); przycisk (-?\d+),(-?\d+)"

g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=200)
sprawdz("pulpit gotowy", g)
if not g: koniec()

if tryb == "z-iso":
    # 0.10.0: also the checks of the former test-m4 start "iso-uefi" (the same ISO through UEFI with virtio): one start
    s_ = czekaj(r"K1-GUI: (K1 zainstalowany na tym komputerze|sesja z pendrive'a)", czas=10)
    sprawdz("iso-uefi: Wyloguj w menu Start ukryte (K1 nie jest zainstalowany)",
            s_ and not s_.group(1).startswith("K1 zainst"), s_.group(0) if s_ else "brak")
    t = czekaj(r"K1-EKRAN: tryb (\d+)x(\d+)")
    W, H, ay = int(t.group(1)), int(t.group(2)), int(g.group(4))
    jasny = lambda c: min(c) > 200
    w, h, px = q.zrzut("0-iso-uefi-pulpit", gdy=lambda w, h, px: (w, h) == (W, H) and max(px(W // 2, 6)) < 120
                       and jasny(px(W // 2, ay - 26)))
    sprawdz(f"iso-uefi: zrzut {w}x{h} = tryb {W}x{H}", (w, h) == (W, H))
    sprawdz("iso-uefi: górny pasek ciemny, dolny pasek jasny (motyw jasny)",
            max(px(W // 2, 6)) < 120 and jasny(px(W // 2, ay - 26)), f"{px(W // 2, 6)} {px(W // 2, ay - 26)}")
    sprawdz("iso-uefi: kolorowa tapeta (nie czarna, nie jednolita)",
            px(W // 4, H // 2) != px(W - 20, H // 3) and max(px(W // 3, H // 2)) > 80, f"{px(W // 4, H // 2)}")
    sprawdz("iso-uefi: rdzeń wbudowany w ISO (loop), simpledrm",
            "obraz rdzenia zamontowany z /dev/loop" in tekst() and "simpledrm" in tekst())

if tryb == "z-dysku":
    sprawdz("start z dysku SATA przez UEFI (bez pendrive'a): K1-DANE dysku otwarta",
            czekaj(r"K1-NADZORCA: partycja danych K1-DANE otwarta dla skarbca", czas=10))
    sprawdz("skarbiec: nowy zaszyfrowany magazyn na K1-DANE dysku",
            czekaj(r"K1-SKARBIEC: utworzono nowy magazyn na pustej partycji K1-DANE", czas=10))
    sprawdz("start z dysku: pulpit wie, że K1 jest zainstalowany (Start → Wyloguj dostępne)",
            czekaj(r"K1-GUI: K1 zainstalowany na tym komputerze: Wyloguj dostępne", czas=10))
    zrzut("2-z-dysku")
    # 0.8.5 step 3: Ustawienia → Miejsce knows K1 runs from the disk — no stick, no "Użyj całego pendrive'a"
    klik(int(g.group(3)) + 2 * int(g.group(5)), int(g.group(4)))
    u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    if u and sk:
        klik(int(u.group(4)) + int(sk.group(1)), int(u.group(5)) + int(sk.group(2)) + 7 * int(sk.group(3)))
    c = czekaj(r"K1-USTAWIENIA: miejsce: części: .*; nośnik: (\w+)", czas=30)
    mj = czekaj(r"K1-USTAWIENIA: miejsce: zajęte \d+ z (\d+) MiB, .* pendrive ma więcej (\d+) MiB; przycisk (-?\d+),", czas=10)
    sprawdz("(0.8.5) start z dysku: Ustawienia → Miejsce — nośnik: dysk, bez przycisku „Użyj całego pendrive'a”",
            c and c.group(1) == "dysk" and mj and mj.group(2) == "0" and mj.group(3) == "-1",
            (c.group(0) if c else "brak części") + " / " + (mj.group(0) if mj else "brak miejsca"))
    zrzut("2-miejsce-z-dysku")
    if u:
        klik(int(u.group(4)) + 720 - 8 - 14, int(u.group(5)) + 22)  # × of Ustawienia (720 wide since 0.10.5)
        czekaj(r"K1-USTAWIENIA: okno zamknięte przez użytkownika", czas=20)
if tryb == "po-przerwaniu":
    sprawdz("po przerwanej aktualizacji dysk dalej startuje (pulpit)", g)
    koniec()
if tryb == "po-aktualizacji":
    sprawdz("start z dysku po aktualizacji (pendrive też podłączony): magazyn dysku otwarty — dane przetrwały",
            czekaj(r"K1-SKARBIEC: magazyn otwarty", czas=10) and "utworzono nowy magazyn" not in tekst()
            and czekaj(r"K1-NADZORCA: K1-DANE na dysku wewnętrznym \(sda2\) — partycja na pendrivie pominięta|"
                       r"K1-NADZORCA: partycja danych K1-DANE otwarta dla skarbca \(sda2\)", czas=5))
    sprawdz("A/B: GRUB uruchomił nowe miejsce B na próbę, K1 je potwierdził po udanym starcie",
            czekaj(r"K1-NADZORCA: A/B: nowa wersja [0-9.]+ wystartowała — potwierdzona \(sda1, miejsce b\)", czas=60))
    koniec()
if tryb == "wycofanie":
    sprawdz("A/B: uszkodzone nowe miejsce A — GRUB wrócił do B, K1 zgłasza nieudaną aktualizację",
            czekaj(r"K1-NADZORCA: A/B: aktualizacja \(miejsce a\) nie wystartowała — działa poprzednia wersja \(sda1, miejsce b\)",
                   czas=60))
    ok_ = czekaj(r"K1-GUI: okienko \(nieudana próba nowej wersji\): Rozumiem (\d+),(\d+); Pokaż w Ustawieniach (\d+),(\d+)", czas=30)
    sprawdz("(0.10.5) okienko na pulpicie: „Nowa wersja K1 nie uruchomiła się”", ok_)
    if ok_:
        zrzut("5-okienko")
        klik(int(ok_.group(3)), int(ok_.group(4)))
        a = czekaj(STAN_AKT, czas=60)
        sprawdz("„Pokaż w Ustawieniach”: Ustawienia → Aktualizacje — nieudana próba, działa poprzednia (miejsce B)",
                czekaj(r"K1-USTAWIENIA: aktualizacje: otwarte na prośbę nadzorcy", czas=30) and a
                and a.group(1) == "nieudana próba", a.group(0) if a else "brak")
        zrzut("5-ustawienia")
    koniec()

if tryb in ("aktualizuj", "pliki-zle", "przerwij", "starsza"):  # 0.10.5 step 1: Ustawienia → Aktualizacje
    if tryb == "aktualizuj":
        sprawdz("pendrive + dysk z K1: skarbiec dostaje K1-DANE dysku wewnętrznego (sda2), nie pendrive'a",
                czekaj(r"K1-NADZORCA: partycja danych K1-DANE otwarta dla skarbca \(sda2\)", czas=5)
                and czekaj(r"K1-SKARBIEC: magazyn otwarty", czas=5))
    u = aktualizacje(g)
    a = u and u[2]
    if tryb == "starsza":  # M6a: the disk says 99.0.0 — the stick is older
        sprawdz("Ustawienia → Aktualizacje: na dysku nowsza wersja (99.0.0) niż na pendrivie — bez przycisku",
                a and a.group(1) == "dysk nowszy" and a.group(2) == "99.0.0" and a.group(4) == "-1", a.group(0) if a else "brak")
        zrzut("6-starsza")
        if u:
            klik(u[0] + 216 + 100, u[1] + 318)  # where the button would be: nothing may start
        sprawdz("powrót do starszej wersji zablokowany (nic się nie zaczyna)", cisza(r"proszę o aktualizację|instalator: aktualizacja", 3))
        koniec()
    # the test stick has the disk's version (0.10.x of this build): "ta sama wersja" with "Zapisz K1 na dysku jeszcze raz"
    sprawdz("Ustawienia → Aktualizacje: na dysku starsza albo ta sama wersja, przycisk zapisu na dysk",
            a and a.group(1) in ("dysk starszy", "ta sama wersja") and a.group(4) != "-1", a.group(0) if a else "brak")
    if not a or a.group(4) == "-1":
        koniec()
    zrzut("3-aktualizacje" if tryb == "aktualizuj" else "3a-aktualizacje")
    klik(u[0] + int(a.group(4)), u[1] + int(a.group(5)))
    sprawdz("aktualizacja: tylko partycja K1-EFI (sda1), na prośbę okna Ustawień",
            czekaj(r"K1-NADZORCA: instalator: aktualizacja — sda1 \(z sd\w+, okno: Ustawienia\)", czas=10))
    if tryb == "aktualizuj":
        sprawdz("(0.9.0) pliki startowe dysku zamienione na pendrive'a (po przerwanej próbie: już nowe)",
                czekaj(r"K1-INSTALATOR: pliki startowe: wymienione na wersję [0-9.]+|K1-INSTALATOR: pliki startowe na dysku aktualne",
                       czas=120))
        sprawdz("instalator (A/B): nowy system w wolnym miejscu B, działający i dane nietknięte",
                czekaj(r"K1-INSTALATOR: aktualizacja A/B: system [0-9.]+ \(\d+ MiB\) do miejsca b", czas=60)
                and czekaj(r"K1-INSTALATOR: gotowe: nowy system w miejscu b", czas=200)
                and czekaj(r"K1-USTAWIENIA: aktualizacje: zaktualizowano \(nowe miejsce A/B\)", czas=20))
        k_ = czekaj(r"K1-USTAWIENIA: aktualizacje: gotowe \(dysk: [^)]*\); przycisk (\d+),(\d+)", czas=20)
        sprawdz("Ustawienia: „Gotowe” i „Uruchom ponownie”", k_)
        zrzut("3-gotowe")
        koniec()
    if tryb == "pliki-zle":
        sprawdz("zmieniony plik startowy na pendrivie: odmowa przed zapisem (niezgodny z manifestem plików startowych)",
                czekaj(r"K1-INSTALATOR: źródło [0-9.]+: pliki startowe nie zgadzają się z manifestem", czas=120)
                and czekaj(r"K1-USTAWIENIA: aktualizacje: błąd aktualizacji \(kod -8\)", czas=20))
        zrzut("3a-pliki-zle")
        koniec()
    z = czekaj(r"K1-INSTALATOR: pliki startowe: zapis obok starych", czas=120)
    monitor("quit")  # a power cut while the new boot files are written (the disk writes slowly in this run)
    sprawdz("aktualizacja przerwana w trakcie zapisu plików startowych (QEMU wyłączone, przed przełączeniem)",
            z and "K1-INSTALATOR: pliki startowe: przełączone" not in tekst() and "K1-INSTALATOR: gotowe" not in tekst())
    q.koniec(wylacz=False)

# Instalator from the Start menu (5th row)
klik(int(g.group(1)), int(g.group(2)))
m = czekaj(r"menu Start otwarte \(pierwszy wiersz (\d+),(\d+) co (\d+)", czas=20)
sprawdz("menu Start otwarte", m)
if not m: koniec()
klik(int(m.group(1)), int(m.group(2)) + 4 * int(m.group(3)))
o = czekaj(r"K1-EKRAN: okno (\d+) \(instalacja\) (\d+)x(\d+) w (\d+),(\d+)", czas=30)
d = czekaj(r"K1-INSTALACJA: dyski: (\d+) \(system do skopiowania: ([\w' ]+)(?:; K1 działa z dysku)?\); dysk 1 (\d+),(\d+) co (\d+); przycisk (\d+),(\d+)", czas=30)
bez_zrodla = tryb in ("z-iso", "z-dysku")
sprawdz("Instalator: okno z listą dysków (1 dysk" + (")" if bez_zrodla else ", system do skopiowania z pendrive'a)"),
        o and d and d.group(1) == "1" and (bez_zrodla or d.group(2) == "jest"), d.group(0) if d else "brak listy")
if not (o and d): koniec()
if tryb == "z-dysku":  # 0.7.2: not "run K1 from a stick" — K1 runs from this disk
    sprawdz("start z dysku: Instalator wie, że K1 jest zainstalowany (nie prosi o pendrive)",
            d.group(2) == "brak" and "K1 działa z dysku" in d.group(0), d.group(0))
    l_ = czekaj(r"K1-INSTALACJA: aktualizacja jest w Ustawieniach; link (\d+),(\d+)", czas=10)
    sprawdz("(0.10.5) Instalator bez aktualizacji: karta „Aktualizacja jest teraz w Ustawieniach” z linkiem", l_)
    zrzut("2-instalator-z-dysku")
    if l_:
        klik(int(o.group(4)) + int(l_.group(1)), int(o.group(5)) + int(l_.group(2)))
        a = czekaj(STAN_AKT, czas=60)
        sprawdz("link otwiera Ustawienia → Aktualizacje: K1 na dysku (miejsce A)",
                czekaj(r"K1-NADZORCA: otwieram Ustawienia → Aktualizacje", czas=10)
                and czekaj(r"K1-USTAWIENIA: aktualizacje: otwarte na prośbę nadzorcy", czas=30) and a and a.group(1) == "na dysku",
                a.group(0) if a else "brak")
        zrzut("2-aktualizacje-z-dysku")
    koniec()
ox, oy = int(o.group(4)), int(o.group(5))
px, py = ox + int(d.group(6)), oy + int(d.group(7))

if tryb == "z-iso":
    sprawdz("start z obrazu .iso: Instalator mówi, że nie ma skąd kopiować (0.6.1)",
            d.group(2) == "brak", d.group(0))
    zrzut("0-z-iso")
    klik(px, py)  # where the button would be: nothing may start
    sprawdz("bez kopii systemu nie ma przycisku „Zainstaluj” (nic się nie zaczyna)",
            cisza(r"potwierdzono|instalator: odmowa", 3))
    koniec()

if tryb == "uszkodzony":  # M6a: one byte of the system on the stick changed
    klik(px, py)
    monitor("sendkey u", "sendkey s", "sendkey u", "sendkey n")
    klik(px, py)
    sprawdz("uszkodzony system na pendrivie: instalator odmawia przed zapisem (skrót niezgodny z manifestem)",
            czekaj(r"K1-INSTALATOR: źródło [0-9.]+: skrót nie zgadza się z manifestem", czas=120)
            and czekaj(r"K1-INSTALACJA: kod -8", czas=20))
    zrzut("0-uszkodzony")
    koniec()

if tryb == "instaluj":
    sprawdz("dysk pusty: bez Kons K1", czekaj(r"K1-INSTALACJA: dysk 1: \d+ MiB, rodzaj 1, Kons K1: nie", czas=5))
    sprawdz("sesja z pendrive'a: w menu Start nie ma „Wyloguj”", "K1-GUI: sesja z pendrive'a: bez Wyloguj" in tekst())
    zrzut("1-lista")
    klik(ox + int(d.group(3)), oy + int(d.group(4)))  # the disk
    klik(px, py)                                      # Zainstaluj
    klik(px, py)                                      # without „USUŃ”: nothing happens
    sprawdz("bez wpisania „USUŃ” instalacja się nie zaczyna", cisza(r"potwierdzono", 3))
    monitor("sendkey u", "sendkey s", "sendkey u", "sendkey alt_r-n")  # usuń
    zrzut("1-potwierdzenie")
    klik(px, py)
    sprawdz("potwierdzone po wpisaniu „usuń”", czekaj(r"K1-INSTALACJA: potwierdzono: instalacja na dysku 1", czas=10))
    sprawdz("nadzorca: program zapisujący w klatce (UID 105), źródło = K1-EFI pendrive'a",
            czekaj(r"K1-NADZORCA: instalator: instalacja na dysku — sda \(z sd\w+, okno: Instalator\), pid \d+, uid 105", czas=10))
    sprawdz("instalator: system i pliki startowe pendrive'a zgodne z podpisanymi manifestami",
            czekaj(r"K1-INSTALATOR: źródło: wersja [0-9.]+, .*, pliki startowe zgodne z manifestem", czas=60))
    sprawdz("instalator: kopia, sprawdzenie, tablica partycji", czekaj(r"K1-INSTALATOR: gotowe: K1 zainstalowany", czas=200))
    sprawdz("okno: „Gotowe”", czekaj(r"K1-INSTALACJA: zainstalowano", czas=20))
    zrzut("1-gotowe")
    klik(px, py)  # Wyłącz komputer
    sprawdz("„Wyłącz komputer” wyłącza system", czekaj(r"K1: wyłączanie zasilania|reboot: Power down", czas=60))
    q.czysty_log()
    q.koniec(wylacz=False)

PY
    zamknij_qemu "$MON" $QPID
}

SATA="-drive if=none,id=dysk,file=$DYSK,format=raw -device ide-hd,bus=ide.0,drive=dysk"
uruchom z-iso 0-z-iso --iso "$K1_WYNIK/k1-$K1_WERSJA.iso" --uefi --vga virtio --qemu "$SATA"
# M6a: a stick whose system was changed (one byte of slot A that the checksum covers, in the free space of its
# inner FAT after k1.efi — the stick itself still starts) — refused before any write.
# The layout block (k1_uklad.h) at 1 KiB of K1-EFI (which starts at 1 MiB) says where the slots are.
uklad() { # $1 = image, $2 = field offset in the block (32: slot A, 40: slot B, 16: grubenv) → bytes from the image start
    python3 -I -c 'import sys; f=open(sys.argv[1],"rb"); f.seek(1048576+1024+int(sys.argv[2])); print(1048576+int.from_bytes(f.read(8),"little"))' "$1" "$2"
}
cp "$PEN" "$TESTY/m5f-uszkodzony.img"
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); a=int(sys.argv[2]); f.seek(a+127*1048576+16); n=int.from_bytes(f.read(8),"little"); f.seek(a+n-100); b=f.read(1); f.seek(-1,1); f.write(bytes([b[0]^0xff])); f.close()' \
    "$TESTY/m5f-uszkodzony.img" "$(uklad "$TESTY/m5f-uszkodzony.img" 32)"
uruchom uszkodzony 0-uszkodzony --pendrive "$TESTY/m5f-uszkodzony.img" --qemu "$SATA"
if sfdisk -d "$DYSK" >/dev/null 2>&1; then echo "  [BŁĄD]  po odmowie na dysku jest tablica partycji"; r=1
else echo "  [OK]    po odmowie dysk nietknięty (bez tablicy partycji)"; fi
rm -f "$TESTY/m5f-uszkodzony.img"
uruchom instaluj 1-instalacja --pendrive "$PEN" --qemu "$SATA"
if sfdisk -d "$DYSK" 2>/dev/null | grep -q 'name="K1-EFI"' && sfdisk -d "$DYSK" | grep -q 'name="K1-DANE"' \
    && sfdisk --verify "$DYSK" >/dev/null 2>&1; then
    echo "  [OK]    dysk: GPT z K1-EFI i K1-DANE (sfdisk --verify bez błędów)"
else
    echo "  [BŁĄD]  dysk: brak poprawnej tablicy GPT z K1-EFI i K1-DANE"; r=1
fi
# 0.9.0 step 3 (ADR 0038): the boot files of the disk (its K1-EFI at 1 MiB) and of the stick, through mtools
export MTOOLS_SKIP_CHECK=1
EFI_DYSK="$DYSK@@1M" EFI_PEN="$PEN@@1M"
PLIKI_STARTOWE="EFI/BOOT/BOOTX64.EFI EFI/BOOT/grubx64.efi EFI/BOOT/mmx64.efi boot/grub/grub.cfg boot/grub/start.png .disk/info k1/klucz.cer"
pliki_startowe() { # 0: the disk's boot files are the stick's (and nothing else), fsck.fat finds nothing wrong
    for p in $PLIKI_STARTOWE; do
        rm -f "$TESTY/m5f-p1" "$TESTY/m5f-p2"
        mcopy -n -i "$EFI_DYSK" "::/$p" "$TESTY/m5f-p1" 2>/dev/null && mcopy -n -i "$EFI_PEN" "::/$p" "$TESTY/m5f-p2" \
            && cmp -s "$TESTY/m5f-p1" "$TESTY/m5f-p2" || return 1
    done
    ! mcopy -n -i "$EFI_DYSK" ::/EFI/BOOT/stary.txt "$TESTY/m5f-p1" 2>/dev/null || return 1
    dd if="$DYSK" of="$TESTY/m5f-efi.img" bs=1M skip=1 count=280 status=none && fsck.fat -n "$TESTY/m5f-efi.img" >/dev/null 2>&1
}
# every file of the disk's K1-EFI with its content, but NvVars: OVMF without a variables file keeps its variables there
# and writes it at every start (a real laptop does not)
efi_suma() {
    mdir -/ -b -i "$EFI_DYSK" :: | grep -v '/NvVars$' | while IFS= read -r p; do
        echo "$p"; mtype -i "$EFI_DYSK" "$p" 2>/dev/null | sha256sum
    done | sha256sum | cut -c1-64
}
if pliki_startowe; then echo "  [OK]    instalacja: pliki startowe dysku = pendrive'a (fsck.fat bez błędów)"
else echo "  [BŁĄD]  instalacja: pliki startowe dysku inne niż pendrive'a albo fsck.fat znalazł błędy"; r=1; fi
uruchom z-dysku 2-z-dysku --dysk-uefi "$DYSK"
# the disk as from an earlier release: another grub.cfg (a comment more) and .disk/info, a file more
mcopy -n -i "$EFI_DYSK" ::/boot/grub/grub.cfg "$TESTY/m5f-grub.cfg" && printf '# K1 0.8.7\n' >> "$TESTY/m5f-grub.cfg"
printf 'Kons K1 0.8.7\n' > "$TESTY/m5f-info"
mcopy -o -i "$EFI_DYSK" "$TESTY/m5f-grub.cfg" ::/boot/grub/grub.cfg && mcopy -o -i "$EFI_DYSK" "$TESTY/m5f-info" ::/.disk/info \
    && mcopy -i "$EFI_DYSK" "$TESTY/m5f-info" ::/EFI/BOOT/stary.txt || blad "mtools: stare pliki startowe na dysku"
if pliki_startowe; then echo "  [BŁĄD]  „stare” pliki startowe na dysku nie różnią się od pendrive'a"; r=1; fi
# a stick whose grub.cfg was changed (it still starts): its boot files no longer match their manifest
cp "$PEN" "$TESTY/m5f-zly.img"
mcopy -n -i "$PEN@@1M" ::/boot/grub/grub.cfg "$TESTY/m5f-grub.cfg" && printf '# zmieniony\n' >> "$TESTY/m5f-grub.cfg" \
    && mcopy -o -i "$TESTY/m5f-zly.img@@1M" "$TESTY/m5f-grub.cfg" ::/boot/grub/grub.cfg || blad "mtools: zmieniony pendrive"
przed=$(efi_suma)
uruchom pliki-zle 3a-pliki-zle --pendrive "$TESTY/m5f-zly.img" --qemu "$SATA"
if [ "$(efi_suma)" = "$przed" ]; then echo "  [OK]    po odmowie pliki K1-EFI dysku nietknięte (także miejsca i grubenv)"
else echo "  [BŁĄD]  po odmowie K1-EFI dysku się zmieniła"; r=1; fi
rm -f "$TESTY/m5f-zly.img"
# The power cut must land in the middle of writing the boot files: with KVM (CI) the installer finished the whole update
# before the monitor's "quit" took effect (0.9.0 CI). In this run the disk writes at 256 KiB/s — 4 MiB of boot files take
# about 16 s.
SATA_WOLNY="-drive if=none,id=dysk,file=$DYSK,format=raw,throttling.bps-write=262144 -device ide-hd,bus=ide.0,drive=dysk"
uruchom przerwij 3b-przerwana --pendrive "$PEN" --qemu "$SATA_WOLNY"
uruchom po-przerwaniu 3c-po-przerwaniu --dysk-uefi "$DYSK"
PEN_USB="-device qemu-xhci,id=xhci -drive if=none,id=pendrive,file=$PEN,format=raw -device usb-storage,bus=xhci.0,drive=pendrive"
uruchom aktualizuj 3-aktualizacja --pendrive "$PEN" --qemu "$SATA"
if pliki_startowe; then echo "  [OK]    aktualizacja: pliki startowe dysku = pendrive'a, starego pliku nie ma (fsck.fat bez błędów)"
else echo "  [BŁĄD]  aktualizacja: pliki startowe dysku inne niż pendrive'a albo fsck.fat znalazł błędy"; r=1; fi
uruchom po-aktualizacji 4-po-aktualizacji --dysk-uefi "$DYSK" --qemu "$PEN_USB"
env_dysku() { python3 -I -c 'import sys; f=open(sys.argv[1],"rb"); f.seek(int(sys.argv[2])); print(" ".join(l.decode() for l in f.read(1024).split(b"\n") if l.startswith(b"k1_")))' "$DYSK" "$(uklad "$DYSK" 16)"; }
if env_dysku | grep -q 'k1_slot=b k1_start=b k1_nowy= k1_proba=$'; then echo "  [OK]    grubenv dysku: potwierdzone miejsce B, nic do próby"
else echo "  [BŁĄD]  grubenv dysku po potwierdzeniu: $(env_dysku)"; r=1; fi
# A/B rollback: "a new system in A" that cannot even be opened (its inner FAT zeroed), tried once
python3 -I - "$DYSK" "$(uklad "$DYSK" 16)" "$(uklad "$DYSK" 32)" <<'PY'
import sys
f = open(sys.argv[1], "r+b"); env, a = int(sys.argv[2]), int(sys.argv[3])
f.seek(a); f.write(bytes(65536))
blok = b"# GRUB Environment Block\n# WARNING: Do not edit this file by tools other than grub-editenv!!!\n" \
       b"k1_slot=b\nk1_start=b\nk1_nowy=a\nk1_proba=\n"
f.seek(env); f.write(blok + b"#" * (1024 - len(blok))); f.close()
PY
uruchom wycofanie 5-wycofanie --dysk-uefi "$DYSK"
if env_dysku | grep -q 'k1_slot=b k1_start=b k1_nowy= k1_proba=$'; then echo "  [OK]    grubenv dysku po wycofaniu: działa B, próba wyczyszczona"
else echo "  [BŁĄD]  grubenv dysku po wycofaniu: $(env_dysku)"; r=1; fi
# M6a: pretend the disk runs 99.0.0 (the manifest of its confirmed slot B, 127 MiB into the slot)
python3 -I -c 'import sys; f=open(sys.argv[1],"r+b"); o=int(sys.argv[2])+127*1048576; f.seek(o+8); f.write((99<<16).to_bytes(4,"little")); f.seek(o+24); f.write(b"99.0.0".ljust(24,b"\0")); f.close()' "$DYSK" "$(uklad "$DYSK" 40)"
uruchom starsza 6-starsza --pendrive "$PEN" --qemu "$SATA"
rm -f "$DYSK" "$PEN" "$TESTY"/m5f-p1 "$TESTY"/m5f-p2 "$TESTY"/m5f-efi.img "$TESTY"/m5f-grub.cfg "$TESTY"/m5f-info
if [ $r -eq 0 ]; then info "M5f: wszystko zaliczone (zrzuty: $TESTY/m5f-*.png)"; else echo "[k1] M5f: BŁĄD (logi: $TESTY/m5f-*.log)"; fi
exit $r

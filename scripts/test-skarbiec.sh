#!/bin/sh
# Test 0.8.1 krok 2 (ADR 0034, powtarzalny) i 0.8.2 krok 1c (ADR 0036): skarbiec z trudnym dyskiem danych w działającym
# K1 (obraz produkcyjny). Dyski robi testy/host/magazyn_testowy.c (kod skarbca) w formacie 1 (jak zapisywały 0.5.6–0.8.1)
# albo 2; po teście ten sam program sprawdza na hoście format i treść każdego obiektu.
#   1. „dziennik”: K1-DANE 190 MiB, format 1 z długim dziennikiem (jeden obiekt zapisany 20 000 razy — 78 MiB starych
#      wersji) → skarbiec kompaktuje przy otwarciu, pulpit startuje; po potwierdzeniu startu (15 s) — przejście na format 2;
#      ponowny start — format 2, bez kompaktowania; na hoście: ostatnia wersja obiektu
#   2. „przejście”: K1-DANE 64 MiB z 0.5.6–0.8.1 prawie pełna (format 1, obszar 31 MiB, 6 MiB wolne) → przejście na
#      format 2 dopiero po potwierdzeniu startu, w przerwach → Ustawienia → Miejsce: miejsce na pliki 23 → 50 MiB;
#      na hoście: format 2 i każdy obiekt (25 × 1 MiB) bez zmian
#   3. „pełny”: K1-DANE 64 MiB w formacie 2, zajęta prawie do końca (6 MiB wolne) → Pliki: „Nowy folder” odrzucony (brak
#      miejsca na pliki, wynik −28), a ustawienie systemu (Ustawienia → Wygląd: ciemny motyw) się zapisuje
#   4. (0.8.1 krok 3, ADR 0033) „powiększenie”: ta sama pełna K1-DANE na pendrivie USB o 512 MiB większym → Ustawienia →
#      Miejsce: zajęte, pliki i foldery, przycisk „Użyj całego pendrive'a na dane” → nadzorca pisze nową tablicę,
#      skarbiec bierze całą partycję (dane na miejscu) → z drugiego pendrive'a (FAT32) import 16 zdjęć po 2,8 MB
#      (próba T6 planu): 16 z 16; ponowny start: magazyn otwarty, nic już nie rośnie; tablica poprawna (sfdisk)
#   5. „dokończenie”: tablica już powiększona, magazyn jeszcze nie (przerwa prądu w środku) → skarbiec kończy przy starcie
# Dowody: build/testy/skarbiec-*.log, zrzuty build/testy/skarbiec-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
mkdir -p "$TESTY"; rm -f "$TESTY"/skarbiec-*
r=0

uruchom() {
    n=$1; shift
    rm -f "$TESTY/skarbiec-$n.log" "$TESTY/skarbiec-$n.sock"
    "$K1_REPO/scripts/uruchom-qemu.sh" --log "$TESTY/skarbiec-$n.log" --monitor "$TESTY/skarbiec-$n.sock" --czas "$CZAS" "$@" &
    QPID=$!
}
sprawdz() {
    K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/skarbiec.py" "$TESTY/skarbiec-$1.log" "$TESTY/skarbiec-$1.sock" \
        "$TESTY" "$CZAS" "$1" || r=1
    zamknij_qemu "$TESTY/skarbiec-$1.sock" $QPID
}
# dysk OBRAZ SEKTORY WERSJI ZOSTAW_MIB FORMAT — a GPT disk with one K1-DANE partition holding magazyn_testowy's vault
dysk() {
    rm -f "$1" "$1.czesc"
    truncate -s $(( ($2 + 4096) * 512 )) "$1"
    printf 'label: gpt\nstart=2048, size=%s, name="K1-DANE"\n' "$2" | sfdisk -q "$1" >/dev/null
    truncate -s $(( $2 * 512 )) "$1.czesc"
    wynik=$("$K1_BUDOWA/obj/magazyn_testowy" "$1.czesc" "$3" "$4" "$5") || r=1
    echo "  $wynik"
    dd if="$1.czesc" of="$1" bs=512 seek=2048 conv=notrunc,sparse status=none; rm -f "$1.czesc"
}
# tresc OBRAZ SEKTORY WERSJI FORMAT OPIS — after the run: the vault on the disk has FORMAT and every object as written
tresc() {
    dd if="$1" of="$1.czesc" bs=512 skip=2048 count="$2" conv=sparse status=none
    if wynik=$("$K1_BUDOWA/obj/magazyn_testowy" --sprawdz "$1.czesc" "$3" "$4"); then
        echo "  [OK]    $5 ($wynik)"
    else
        echo "  [BŁĄD]  $5 ($wynik)"; r=1
    fi
    rm -f "$1.czesc"
}

cat > "$K1_BUDOWA/skarbiec.py" <<'PY'
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, tryb = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), sys.argv[5]
q = Maszyna(log, mon, out, limit, "skarbiec")
tekst, czekaj, zrzut, sprawdz, klik = q.tekst, q.czekaj, q.zrzut, q.sprawdz, q.klik
KOMP = r"K1-SKARBIEC: kompaktowanie \(przy otwarciu\): dziennik (\d+) → (\d+) MiB"


def po(wzor, akcja, czas=30):
    n = q.ile(wzor)
    akcja()
    return czekaj(wzor, od=n, czas=czas)


def okno(nazwa, akcja):
    m = po(r"K1-EKRAN: okno (\d+) \(" + nazwa + r"\) (\d+)x(\d+) w (\d+),(\d+)", akcja)
    return (int(m.group(4)), int(m.group(5))) if m else None


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=limit * 0.7)
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
if not sprawdz(f"{tryb}: pulpit gotowy", g and k0): sys.exit(1)
q.kursor[:] = [int(k0.group(1)), int(k0.group(2))]
ax, ay, krok = int(g.group(3)), int(g.group(4)), int(g.group(5))
pasek = lambda i: (ax + i * krok, ay)  # 0 Zegar, 1 Notatki, 2 Ustawienia, 3 Zasoby, 4 Kalkulator, 5 Pliki

MIEJSCE = r"K1-USTAWIENIA: miejsce: zajęte (\d+) z (\d+) MiB, pliki i foldery (\d+) z 16384, pendrive ma więcej (\d+) MiB; przycisk (-?\d+),(-?\d+)"
PRZEJSCIE = (r"K1-SKARBIEC: przejście na format 2: gotowe — (\d+) obiektów, (\d+) kroków w [\d.]+ s, najdłuższy krok ([\d.]+) s, "
             r"najdłuższa przerwa w odpowiedziach ([\d.]+) s; miejsce na dane (\d+) → (\d+) MiB")


def przejscie(opis, przed, po):
    """The transition to format 2 (0.8.2 step 1c): only after the start was confirmed, then done in steps."""
    p = czekaj(PRZEJSCIE, czas=limit * 0.6)
    t_ = tekst()
    i_potw, i_start = t_.find("start tej wersji potwierdzony"), t_.find("przejście na format 2 (ADR 0036): kopiuję")
    sprawdz(f"{opis}: przejście na format 2 dopiero po potwierdzeniu startu (nadzorca, 15 s pulpitu)",
            0 <= i_potw < i_start and "K1-NADZORCA: start" in t_[:i_potw])
    sprawdz(f"{opis}: przejście gotowe w przerwach — miejsce na dane {przed} → {po} MiB, najdłuższy krok < 1 s",
            p and int(p.group(5)) == przed and int(p.group(6)) == po and float(p.group(3)) < 1.0, p.group(0) if p else "brak")
    return p


def informacje(u0=None):
    """Ustawienia → Miejsce (0.8.5; Informacje until 0.8.3): the room for files line."""
    u0 = u0 or okno("ustawienia", lambda: klik(*pasek(2)))
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+)", czas=30)
    m = u0 and sk and po(MIEJSCE, lambda: klik(u0[0] + int(sk.group(1)), u0[1] + int(sk.group(2)) + 7 * int(sk.group(3))))
    return u0, m


if tryb == "dziennik":
    m = czekaj(KOMP)
    sprawdz("długi dziennik (78 MiB starych wersji): skarbiec kompaktuje przy otwarciu",
            m and int(m.group(1)) >= 78 and int(m.group(2)) <= 1, m.group(0) if m else "brak")
    przejscie("K1-DANE 190 MiB", 94, 168)
elif tryb == "dziennik-ponownie":
    sprawdz("ponownie: magazyn otwarty w formacie 2, bez kompaktowania i bez ponownego przejścia",
            czekaj(r"K1-SKARBIEC: magazyn otwarty: \d+ obiektów \(format 2, ") and not re.search(KOMP, tekst()) and
            "przejście na format 2" not in tekst())
elif tryb == "przejscie":
    przejscie("K1-DANE 64 MiB z 0.8.1, prawie pełna", 31, 58)
    u0, m = informacje()
    sprawdz("Ustawienia → Miejsce po przejściu: miejsce na pliki 50 MiB (w formacie 1: 23), dane zostały",
            m and int(m.group(2)) == 50 and 25 <= int(m.group(1)) <= 30, m.group(0) if m else "brak")
    zrzut("przejscie-informacje", gdy=lambda w, h, px: True)
elif tryb == "dokonczenie":
    sprawdz("tablica powiększona, magazyn nie (przerwa prądu): skarbiec bierze całą partycję przy starcie",
            (m := czekaj(r"K1-SKARBIEC: magazyn powiększony do całej partycji K1-DANE \(dokończenie po starcie\): miejsce na dane 58 → (\d+) MiB"))
            and int(m.group(1)) >= 556, m.group(0) if m else "brak")
elif tryb == "powiekszenie-ponownie":
    t_ = tekst()
    sprawdz("ponownie: magazyn otwarty, nic już nie rośnie (ani partycja, ani magazyn)",
            czekaj(r"K1-SKARBIEC: magazyn otwarty") and "powiększon" not in tekst())
elif tryb == "powiekszenie":
    u0, m = informacje()
    if not sprawdz("Ustawienia → Miejsce: miejsce na pliki (zajęte prawie wszystko z 50 MiB), pendrive ma 512 MiB więcej, "
                   "przycisk „Użyj całego pendrive'a na dane”",
                   m and int(m.group(2)) == 50 and int(m.group(1)) >= 45 and int(m.group(4)) >= 512 and int(m.group(5)) > 0,
                   m.group(0) if m else "brak"): sys.exit(1)
    c = czekaj(r"K1-USTAWIENIA: miejsce: części: .*; nośnik: (\w+)", czas=30)
    sprawdz("(0.8.5) Miejsce wie, że K1-DANE jest na pendrivie (opis, przycisk tylko tu)", c and c.group(1) == "pendrive",
            c.group(0) if c else "brak")
    zrzut("powiekszenie-1-miejsce", gdy=lambda w, h, px: True)
    n = q.ile(MIEJSCE)
    klik(u0[0] + int(m.group(5)), u0[1] + int(m.group(6)))
    sprawdz("klik = zgoda: nadzorca pisze nową tablicę (K1-DANE 64 → 576 MiB), dane zostają",
            czekaj(r"K1-NADZORCA: K1-DANE: powiększenie na prośbę z Ustawień") and
            czekaj(r"K1-NADZORCA: K1-DANE powiększona do końca pendrive'a: 64 → 576 MiB", czas=60))
    sprawdz("skarbiec bierze całą partycję: miejsce na dane 58 → 556 MiB (nowe odcinki), Ustawienia: gotowe",
            czekaj(r"K1-SKARBIEC: magazyn powiększony do całej partycji K1-DANE \(na prośbę\): miejsce na dane 58 → 556 MiB") and
            czekaj(r"K1-USTAWIENIA: powiększenie K1-DANE: gotowe \(wynik 0\)"))
    m2 = czekaj(MIEJSCE, od=n, czas=30)
    sprawdz("Miejsce po powiększeniu: miejsce na pliki 545 MiB, przycisku już nie ma, liczba plików ta sama",
            m2 and int(m2.group(2)) == 545 and int(m2.group(4)) == 0 and int(m2.group(5)) == -1 and m2.group(3) == m.group(3),
            m2.group(0) if m2 else "brak")
    zrzut("powiekszenie-2-gotowe", gdy=lambda w, h, px: True)
    # T6 of the plan: 16 photos of 2.8 MB from a FAT32 stick (Pliki opened now: its window on top)
    p0 = okno("pliki", lambda: klik(*pasek(5)))
    u = czekaj(r"K1-PLIKI: układ: miejsca od (\d+),(\d+) co (\d+), Kosz (\d+),(\d+); wiersze od (\d+),(\d+) co (\d+); "
               r"w górę (\d+),(\d+); Pendrive (\d+),(\d+)")
    if not sprawdz("Pliki otwarte, miejsca wystarczy", p0 and u and czekaj(r"K1-PLIKI: pliki wczytane") and
                   czekaj(r"K1-PLIKI: miejsce na pliki: wystarczy")): sys.exit(1)
    wx, wy, px_, py_ = (int(u.group(i)) for i in (6, 7, 11, 12))
    widok = r"K1-PLIKI: widok: Pendrive, (\d+) elementów"
    ok = po(widok, lambda: klik(p0[0] + px_, p0[1] + py_), czas=60)
    ok = ok and po(widok, lambda: q.dwuklik(p0[0] + wx, p0[1] + wy))
    ok = ok and po(widok, lambda: q.dwuklik(p0[0] + wx, p0[1] + wy))
    pm = list(re.finditer(r"K1-PLIKI: przyciski: (.*)", tekst()))
    p = {t.strip(): (p0[0] + int(x), p0[1] + int(y)) for t, x, y in re.findall(r"([^;]+?) (\d+),(\d+)", pm[-1].group(1))} if pm else {}
    sprawdz("pendrive ze zdjęciami: DCIM › 100K1, 16 plików", ok and ok.group(1) == "16" and "Importuj wszystkie" in p,
            ok.group(0) if ok else "brak")
    sprawdz("„Importuj wszystkie”: 16 zdjęć po 2,8 MB zapisanych — 16 z 16 (przed powiększeniem: nic)",
            "Importuj wszystkie" in p and
            po(r"K1-PLIKI: import: koniec — zapisane 16 \(do Obrazów 16\), za duże 0, błędy 0", lambda: klik(*p["Importuj wszystkie"]),
               czas=limit * 0.6))
    zrzut("powiekszenie-3-import", gdy=lambda w, h, px: True)
else:  # pelny
    p0 = okno("pliki", lambda: klik(*pasek(5)))
    u = czekaj(r"K1-PLIKI: układ: miejsca od (\d+),(\d+) co (\d+), Kosz")
    if not sprawdz("Pliki otwarte", p0 and u and czekaj(r"K1-PLIKI: pliki wczytane")): sys.exit(1)
    mx, my, mk = (int(u.group(i)) for i in range(1, 4))
    po(r"K1-PLIKI: widok: Dokumenty", lambda: klik(p0[0] + mx, p0[1] + my + mk))
    pm = list(re.finditer(r"K1-PLIKI: przyciski: (.*)", tekst()))
    p = {t.strip(): (p0[0] + int(x), p0[1] + int(y)) for t, x, y in re.findall(r"([^;]+?) (\d+),(\d+)", pm[-1].group(1))} if pm else {}
    sprawdz("prawie pełny skarbiec: Pliki mówią, że mało miejsca (0.8.1 krok 3)", czekaj(r"K1-PLIKI: miejsce na pliki: mało"))
    zrzut("pelny-0-malo-miejsca", gdy=lambda w, h, px: True)
    sprawdz("prawie pełny skarbiec: „Nowy folder” odrzucony — brak miejsca na pliki (−28)",
            "Nowy folder" in p and po(r"K1-PLIKI: skarbiec odmówił \(komunikat 86, wynik -28\)", lambda: klik(*p["Nowy folder"])),
            str(p))
    zrzut("pelny-1-brak-miejsca", gdy=lambda w, h, px: True)
    u0 = okno("ustawienia", lambda: klik(*pasek(2)))
    # a setting of the system (0.8.2 step 5: Aplikacje have no choice any more — the dark theme of Wygląd, the same object)
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od \d+,\d+ co \d+; przyciski od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\)", czas=30)
    if u0 and sk:
        bx, by, bw, bh = (int(sk.group(i)) for i in range(1, 5))
        sprawdz("…a ustawienie systemu (ciemny motyw) się zapisuje (rezerwa na system)",
                po(r"K1-SKARBIEC: zapisano obiekt 1 właściciela 2 ", lambda: klik(u0[0] + bx + 3 * bw // 4, u0[1] + by + bh // 2)))
    else:
        sprawdz("Ustawienia: układ", False)
t_ = tekst()
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
        "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test skarbca (0.8.1 krok 2, 3; 0.8.2 krok 1c): długi dziennik, przejście na format 2, prawie pełny dysk danych, powiększenie"
make -C "$K1_REPO" -s "$K1_BUDOWA/obj/magazyn_testowy" >/dev/null
DZ=$TESTY/skarbiec-dziennik.img
dysk "$DZ" 389120 20000 0 1
uruchom dziennik --qemu "-drive if=none,id=dane,file=$DZ,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz dziennik
uruchom dziennik-ponownie --qemu "-drive if=none,id=dane,file=$DZ,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz dziennik-ponownie
tresc "$DZ" 389120 20000 2 "dziennik na hoście: format 2, ostatnia z 20 000 wersji"
PR=$TESTY/skarbiec-przejscie.img
dysk "$PR" 131072 0 6 1
uruchom przejscie --qemu "-drive if=none,id=dane,file=$PR,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz przejscie
tresc "$PR" 131072 0 2 "przejście na hoście: format 2, każdy obiekt (1 MiB) bez zmian"
PE=$TESTY/skarbiec-pelny.img
dysk "$PE" 131072 0 6 2
uruchom pelny --qemu "-drive if=none,id=dane,file=$PE,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz pelny
# 3. the same full K1-DANE on a USB stick 512 MiB larger, a second stick (FAT32) with 16 photos of 2.8 MB
PW=$TESTY/skarbiec-powiekszenie.img FOT=$TESTY/skarbiec-zdjecia.img
dysk "$PW" 131072 0 6 2
truncate -s $(( (131072 + 4096) * 512 + (512 << 20) )) "$PW" # as a small image written to a larger stick
Z=$TESTY/skarbiec-zdjecia
rm -rf "$Z" "$FOT"; mkdir -p "$Z"
python3 -I - "$Z" <<'ZDJ' || r=1
import random, sys
from PIL import Image
for i in range(16):  # noise compresses badly: about 2.8 MB each, as a phone's photo
    g = random.Random(i)
    Image.frombytes("RGB", (2000, 1500), g.randbytes(2000 * 1500 * 3)).save(f"{sys.argv[1]}/IMG_{i:04d}.JPG", "JPEG", quality=90)
ZDJ
export MTOOLS_SKIP_CHECK=1
mformat -C -i "$FOT.czesc" -F -c 1 -T 196608 :: >/dev/null && mmd -i "$FOT.czesc" ::/DCIM ::/DCIM/100K1 &&
    mcopy -i "$FOT.czesc" "$Z"/*.JPG ::/DCIM/100K1/ || r=1
echo "  zdjęcia do importu: $(ls "$Z" | wc -l), razem $(du -sm "$Z" | cut -f1) MiB"
truncate -s 100M "$FOT"
printf 'label: dos\nstart=2048, size=196608, type=c\n' | sfdisk -q "$FOT" >/dev/null
dd if="$FOT.czesc" of="$FOT" bs=512 seek=2048 conv=notrunc status=none; rm -rf "$FOT.czesc" "$Z"
USB="-device qemu-xhci,id=xhci -drive if=none,id=dane,file=$PW,format=raw -device usb-storage,bus=xhci.0,drive=dane"
uruchom powiekszenie --qemu "$USB -drive if=none,id=fot,file=$FOT,format=raw -device usb-storage,bus=xhci.0,drive=fot"
sprawdz powiekszenie
uruchom powiekszenie-ponownie --qemu "$USB"
sprawdz powiekszenie-ponownie
# the table K1 wrote, read by another program: K1-DANE from 1 MiB up to the last full MiB of the stick
if sfdisk -d "$PW" 2>/dev/null | grep -q 'start=        2048, size=     1179648, .*name="K1-DANE"'; then
    echo "  [OK]    tablica partycji po powiększeniu poprawna (sfdisk): K1-DANE do ostatniego MiB pendrive'a"
else
    echo "  [BŁĄD]  tablica partycji po powiększeniu:"; sfdisk -d "$PW"; r=1
fi
# 4. a power cut between the new table and the vault's header: the table grown by another program
DK=$TESTY/skarbiec-dokonczenie.img
dysk "$DK" 131072 0 6 2
truncate -s $(( (131072 + 4096) * 512 + (512 << 20) )) "$DK"
sfdisk -q --relocate gpt-bak-std "$DK" >/dev/null 2>&1
echo ", +" | sfdisk -q -N 1 "$DK" >/dev/null 2>&1 || r=1
uruchom dokonczenie --qemu "-drive if=none,id=dane,file=$DK,format=raw -device ide-hd,drive=dane,bus=ide.0"
sprawdz dokonczenie
rm -f "$DZ" "$PR" "$PE" "$PW" "$FOT" "$DK"
if [ $r -eq 0 ]; then info "skarbiec: wszystko zaliczone"; else echo "[k1] skarbiec: BŁĄD (logi: $TESTY/skarbiec-*.log)"; fi
exit $r

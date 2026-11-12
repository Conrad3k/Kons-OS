#!/bin/sh
# Test 0.8.0 krok 3 i 4 (ADR 0031, 0032, powtarzalny): zdjęcia z pendrive'a w działającym K1 (obraz produkcyjny) — bez
# prawdziwego sprzętu: wirtualny pendrive USB (xHCI) z FAT32 w partycji MBR, zrobiony jak przez inny system (mtools),
# z prawdziwymi zdjęciami JPEG i PNG (Pillow), uszkodzonym JPEG-iem, polską długą nazwą, plikiem ukrytym, śmieciem
# macOS, plikiem 17 MB i (0.8.1 krok 4) filmem 300 MB.
#   1. Pliki pokazują miejsce „Pendrive”; po kliknięciu nadzorca uruchamia czytnik (UID 106, pendrive tylko do
#      odczytu), czytnik rozpoznaje FAT32, Pliki pokazują foldery (bez ukrytych plików)
#   2. DCIM › 100K1 → „Importuj wszystkie”: trzy pliki (dwa zdjęcia, uszkodzony JPEG) trafiają do Obrazów, skarbiec
#      rozpoznaje je jako obrazy
#   3. pojedynczo: plik 17 MB (do 0.8.0 „za duży”) i film 300 MB → Dokumenty kawałkami po 1 MiB (0.8.1 krok 4: czytnik
#      ma cgroup 32 MiB, a Pliki i skarbiec dostają najwyżej kawałek), zdjęcie z polską nazwą → Obrazy, notatka → Dokumenty
#   4. Obrazy: 4 pliki „z pendrive'a”; Dokumenty: 3
#   4a. (krok 4) zaznaczone zdjęcie → dekoder w klatce (UID 107) → podgląd w Plikach (kolor na zrzucie); PNG też;
#      uszkodzony JPEG → „Podgląd niedostępny”; „Ustaw jako tapetę” → ekran rysuje zdjęcie na całym ekranie
#   5. wyjęcie pendrive'a (device_del): czytnik zatrzymany, miejsce znika; włożenie pendrive'a z NTFS: Pliki mówią,
#      że K1 go jeszcze nie odczyta
#   6. nazwy plików i ich treść nie trafiają do logu; brak zabronionych wywołań i odrzuconych komunikatów
#   7. ponowny start z tym samym dyskiem danych: tapeta ze zdjęcia wraca ze skarbca; magazyn z filmem 300 MB otwiera się
#      od razu (0.8.2 krok 1c, format 2: spis zamiast czytania wszystkiego) — ekran dostaje zapisane ustawienia przy
#      starcie, nie później (w 0.8.1: „skarbiec jeszcze otwiera magazyn”, R58)
# Dowody: build/testy/pendrive.log, zrzuty build/testy/pendrive-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
CZAS=$((CZAS * 2))
LOG=$TESTY/pendrive.log MON=$TESTY/pendrive.sock
PEN=$TESTY/pendrive-fat32.img NTFS=$TESTY/pendrive-ntfs.img DANE=$TESTY/pendrive-dane.img
mkdir -p "$TESTY"; rm -f "$LOG" "$MON" "$TESTY"/pendrive-*.png
export LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1

echo "[k1] test pendrive (0.8.0 krok 3, 4): zdjęcia z wirtualnego pendrive'a FAT32 → Pliki, podgląd, tapeta, wyjęcie, NTFS"
# the stick: FAT32 in an MBR partition from 1 MiB
Z=$TESTY/pendrive-zrodla
rm -rf "$Z" "$PEN" "$TESTY/pendrive-czesc.img"; mkdir -p "$Z"
python3 -I - "$Z" <<'PY' || blad "zdjęcia do testu (python3-pil)"
import random, sys
from PIL import Image
z = sys.argv[1]
for nazwa, kolor, rozm in (("IMG_0001.JPG", (196, 110, 80), (64, 48)), ("Wakacje nad morzem — Łódź.jpg", (60, 120, 180), (80, 60))):
    Image.new("RGB", rozm, kolor).save(f"{z}/{nazwa}", "JPEG", quality=90)
Image.new("RGB", (32, 32), (90, 160, 90)).save(f"{z}/IMG_0002.PNG", "PNG")
open(f"{z}/USZKODZONE.JPG", "wb").write(b"\xff\xd8\xff\xe0" + bytes(range(256)) * 4)  # a JPEG start, then rubbish
open(f"{z}/notatka.txt", "w", encoding="utf-8").write("Zakupy: mleko, chleb\n")
open(f"{z}/film.mp4", "wb").write(b"\0\0\0\x18ftypmp42" + bytes(17 * 1024 * 1024))
with open(f"{z}/wideo.mp4", "wb") as f:  # 300 MiB (0.8.1 step 4): a piece of 1 MiB of noise, repeated
    blok = random.Random(7).randbytes(1 << 20)
    f.write(b"\0\0\0\x18ftypmp42")
    for _ in range(300):
        f.write(blok)
open(f"{z}/ukryty.txt", "w").write("ukryty")
open(f"{z}/._IMG_0001.JPG", "wb").write(b"\0\5\26\7")
PY
mformat -C -i "$TESTY/pendrive-czesc.img" -F -c 8 -T 720896 :: || blad "mformat"
mmd -i "$TESTY/pendrive-czesc.img" ::/DCIM ::/DCIM/100K1
mcopy -i "$TESTY/pendrive-czesc.img" "$Z/IMG_0001.JPG" "$Z/IMG_0002.PNG" "$Z/USZKODZONE.JPG" "$Z/._IMG_0001.JPG" ::/DCIM/100K1/
mcopy -i "$TESTY/pendrive-czesc.img" "$Z/Wakacje nad morzem — Łódź.jpg" "$Z/notatka.txt" "$Z/film.mp4" "$Z/wideo.mp4" \
    "$Z/ukryty.txt" ::
mattrib -i "$TESTY/pendrive-czesc.img" +h ::/ukryty.txt
truncate -s 360M "$PEN"
printf 'label: dos\nstart=2048, size=720896, type=c\n' | sfdisk -q "$PEN" >/dev/null || blad "sfdisk (pendrive)"
dd if="$TESTY/pendrive-czesc.img" of="$PEN" bs=512 seek=2048 conv=notrunc status=none
rm -f "$TESTY/pendrive-czesc.img"
OCZEKIWANE=$(cat "$Z/IMG_0001.JPG" "$Z/IMG_0002.PNG" "$Z/USZKODZONE.JPG" "$Z/Wakacje nad morzem — Łódź.jpg" "$Z/notatka.txt" \
    "$Z/film.mp4" "$Z/wideo.mp4" | wc -c)
rm -rf "$Z"
# a stick with NTFS (only its signature): K1 cannot read it yet
rm -f "$NTFS"; truncate -s 16M "$NTFS"
printf 'label: dos\nstart=2048, type=7\n' | sfdisk -q "$NTFS" >/dev/null
printf '\353R\220NTFS    ' | dd of="$NTFS" bs=512 seek=2048 conv=notrunc status=none
# the data partition (K1-DANE) on a SATA disk: 768 MiB, sparse (format 2 of the vault since 0.8.2: about 90 % for files)
rm -f "$DANE"; truncate -s 768M "$DANE"
printf 'label: gpt\nstart=2048, size=1568768, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null

"$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" \
    --qemu "-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0
            -device qemu-xhci,id=xhci -drive if=none,id=pendrive,file=$PEN,format=raw
            -device usb-storage,id=pen,bus=xhci.0,drive=pendrive" &
QPID=$!
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$LOG" "$MON" "$TESTY" "$CZAS" "$NTFS" "$OCZEKIWANE" <<'PY' || r=1
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, ntfs, oczekiwane = sys.argv[1:7]
q = Maszyna(log, mon, out, limit, "pendrive")
tekst, czekaj, zrzut, sprawdz, klik = q.tekst, q.czekaj, q.zrzut, q.sprawdz, q.klik
IKONY = r"K1-GUI: ikony na pulpicie \(siatka \d+x\d+, co \d+,\d+\): (.*)"
WIDOK = r"K1-PLIKI: widok: ([^,]+), (\d+) elementów"


def po(wzor, akcja, czas=30):
    n = q.ile(wzor)
    akcja()
    return czekaj(wzor, od=n, czas=czas)


def widok(akcja, miejsce, ile, czas=40):
    """`akcja`, then Pliki show `miejsce` with `ile` entries (one of the next view lines)."""
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


g = czekaj(r"K1-GUI: pulpit gotowy")
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
if not sprawdz("pulpit gotowy", g and k0): sys.exit(1)
q.kursor[:] = [int(k0.group(1)), int(k0.group(2))]
m = czekaj(IKONY)
ik = {c.rsplit(" ", 1)[0]: tuple(int(v) for v in c.rsplit(" ", 1)[1].split(",")) for c in m.group(1).split("; ")} if m else {}
o = po(r"K1-EKRAN: okno (\d+) \(pliki\) (\d+)x(\d+) w (\d+),(\d+)", lambda: q.dwuklik(*ik["Pliki"])) if "Pliki" in ik else None
u = czekaj(r"K1-PLIKI: układ: miejsca od (\d+),(\d+) co (\d+), Kosz (\d+),(\d+); wiersze od (\d+),(\d+) co (\d+); "
           r"w górę (\d+),(\d+); Pendrive (\d+),(\d+)")
if not sprawdz("Pliki otwarte", o and u and czekaj(r"K1-PLIKI: pliki wczytane")): sys.exit(1)
ox, oy = int(o.group(4)), int(o.group(5))
mx, my, mk, kx, ky, wx, wy, wk, gx, gy, px_, py_ = (int(u.group(i)) for i in range(1, 13))
miejsce = lambda i: (ox + mx, oy + my + i * mk)
wiersz = lambda i: (ox + wx, oy + wy + i * wk)
pendrive, w_gore = (ox + px_, oy + py_), (ox + gx, oy + gy)


def przyciski():
    m = list(re.finditer(r"K1-PLIKI: przyciski: (.*)", tekst()))
    return {t.strip(): (ox + int(x), oy + int(y)) for t, x, y in re.findall(r"([^;]+?) (\d+),(\d+)", m[-1].group(1))} if m else {}


def importuj(akcja, zapisane, obrazy, duze, bledy=0):
    wz = (rf"K1-PLIKI: import: koniec — zapisane {zapisane} \(do Obrazów {obrazy}\), za duże {duze}, błędy {bledy}")
    return po(wz, akcja, czas=90)


# 1. the place, czytnik, the main folder
sprawdz("nadzorca: pendrive podłączony — Pliki pokazują miejsce „Pendrive”", czekaj(r"K1-PLIKI: pendrive: jest \(miejsce Pendrive\)"))
zrzut("1-miejsce")
sprawdz("„Pendrive”: czytnik w klatce (UID 106) dostaje pendrive tylko do odczytu, rozpoznaje FAT32; Pliki: 5 elementów "
        "(DCIM, 4 pliki — bez ukrytego)",
        widok(lambda: klik(*pendrive), "Pendrive", 5, czas=60) and
        czekaj(r"K1-NADZORCA: pendrive: czytnik dla sd[a-z]+ \(tylko odczyt\), pid \d+, uid 106") and
        czekaj(r"K1-CZYTNIK: pendrive: FAT32, klaster 4096 B"))
zrzut("2-pendrive", gdy=lambda w, h, px: True)
p = przyciski()
sprawdz("przyciski pendrive'a: Importuj, Importuj wszystkie (bez zmiany nazwy i usuwania)",
        {"Importuj", "Importuj wszystkie"} == set(p), str(p))
# 2. DCIM › 100K1 → "Importuj wszystkie"
sprawdz("DCIM (1 folder)", widok(lambda: q.dwuklik(*wiersz(0)), "Pendrive", 1))
sprawdz("DCIM › 100K1: 3 pliki (bez śmieci macOS „._…”)", widok(lambda: q.dwuklik(*wiersz(0)), "Pendrive", 3))
zrzut("3-100k1", gdy=lambda w, h, px: True)
sprawdz("„Importuj wszystkie”: 3 pliki → Obrazy",
        "Importuj wszystkie" in p and importuj(lambda: klik(*p["Importuj wszystkie"]), 3, 3, 0))
sprawdz("skarbiec: 3 pliki z pendrive'a, wszystkie rozpoznane z treści jako obrazy",
        czekaj(r"K1-PLIKI: pliki z pendrive'a: 3 \(obrazy PNG/JPEG: 3\)"))
zrzut("4-zaimportowane", gdy=lambda w, h, px: True)
# 3. one by one, from the main folder (rows: DCIM, film.mp4, notatka.txt, Wakacje…, wideo.mp4)
sprawdz("„w górę” dwa razy: główny folder pendrive'a",
        widok(lambda: klik(*w_gore), "Pendrive", 1) and widok(lambda: klik(*w_gore), "Pendrive", 5))
klik(*wiersz(1))
sprawdz("plik 17 MB (do 0.8.0: „za duży”) → Dokumenty, w 18 kawałkach", importuj(lambda: klik(*p["Importuj"]), 1, 0, 0) and
        czekaj(r"K1-CZYTNIK: plik przeczytany \(17825804 B, kawałków 18\)"))
klik(*wiersz(4))
sprawdz("film 300 MB → Dokumenty kawałkami po 1 MiB (czytnik: cgroup 32 MiB, memfd najwyżej kawałek)",
        po(r"K1-PLIKI: import: koniec — zapisane 1 \(do Obrazów 0\), za duże 0, błędy 0", lambda: klik(*p["Importuj"]),
           czas=float(limit) * 0.5) and czekaj(r"K1-CZYTNIK: plik przeczytany \(314572812 B, kawałków 301\)"))
zrzut("5-duzy", gdy=lambda w, h, px: True)
klik(*wiersz(3))
sprawdz("zdjęcie z polską długą nazwą → Obrazy", importuj(lambda: klik(*p["Importuj"]), 1, 1, 0))
klik(*wiersz(2))
sprawdz("notatka → Dokumenty", importuj(lambda: klik(*p["Importuj"]), 1, 0, 0))
sprawdz(f"skarbiec: 7 plików z pendrive'a (4 obrazy), razem {oczekiwane} B — tyle, ile na pendrivie",
        czekaj(rf"K1-PLIKI: pliki z pendrive'a: 7 \(obrazy PNG/JPEG: 4\), razem {oczekiwane} B"))
# 4. where they went (Obrazy by name: IMG_0001.JPG, IMG_0002.PNG, USZKODZONE.JPG, Wakacje…)
sprawdz("Obrazy: 4 pliki", widok(lambda: klik(*miejsce(2)), "Obrazy", 4))
# 4a. pictures (step 4): the preview through a dekoder, the wallpaper
NIEBIESKI, ZIELONY = (60, 120, 180), (90, 160, 90)
blisko = lambda a, b, t=12: all(abs(x - y) <= t for x, y in zip(a, b))
miniatura = (ox + 920 - 232 - 12 + 16 + 100, oy + 44 + 10 + 16 + 75)  # the middle of the picture in the preview


def podglad(i, wzor, kolor, nazwa):
    n = q.ile(wzor)
    klik(*wiersz(i))
    m = czekaj(wzor, od=n, czas=90)
    w, h, px = zrzut(nazwa, gdy=lambda w, h, px: kolor is None or blisko(px(*miniatura), kolor))
    return m and (kolor is None or blisko(px(*miniatura), kolor)), px(*miniatura)


n_d = q.ile(r"K1-NADZORCA: dekoder dla Plików")
ok, kol = podglad(3, r"K1-PLIKI: podgląd obrazu: 80x60", NIEBIESKI, "6-podglad-jpeg")
sprawdz("zdjęcie JPEG: nadzorca uruchamia dekoder w klatce (UID 107), dekoder odsyła 80×60, Pliki pokazują je w podglądzie",
        ok and czekaj(r"K1-NADZORCA: dekoder dla Plików: pid \d+, uid 107", od=n_d) and czekaj(r"K1-DEKODER: obraz JPEG → 80x60"),
        f"kolor w podglądzie {kol}")
ok, kol = podglad(1, r"K1-PLIKI: podgląd obrazu: 32x32", ZIELONY, "6-podglad-png")
sprawdz("zdjęcie PNG: podgląd 32×32", ok and czekaj(r"K1-DEKODER: obraz PNG → 32x32"), f"kolor w podglądzie {kol}")
ok, _ = podglad(2, r"K1-PLIKI: obraz: dekoder nie odczytał pliku \(-42\)", None, "6-podglad-uszkodzony")
sprawdz("NEGATYWNY: uszkodzony JPEG — dekoder odpowiada błędem, Pliki: „Podgląd niedostępny”, nic więcej się nie dzieje",
        ok and czekaj(r"K1-DEKODER: obraz JPEG: nie da się odczytać \(-42\)"))
klik(*wiersz(3))
t_ = czekaj(r"K1-PLIKI: przycisk tapety: (\d+),(\d+)")
tlo = lambda px, w, h: px(w - 12, h // 2)
n_t = q.ile(r"K1-EKRAN: zdjęcie na tapetę od Plików")
if t_:
    klik(ox + int(t_.group(1)), oy + int(t_.group(2)))
ok = czekaj(r"K1-EKRAN: zdjęcie na tapetę od Plików: 80x60", od=n_t, czas=90)
w, h, px = zrzut("6-tapeta", gdy=lambda w, h, px: blisko(tlo(px, w, h), NIEBIESKI))
sprawdz("„Ustaw jako tapetę”: Pliki → dekoder → ekran (tylko od Plików); zdjęcie na całym ekranie, zapisane w skarbcu",
        t_ and ok and czekaj(r"K1-EKRAN: tapeta 31") and czekaj(r"K1-PLIKI: tapeta ze zdjęcia: 80x60 wysłana do ekranu") and
        blisko(tlo(px, w, h), NIEBIESKI), f"tło przy prawej krawędzi {tlo(px, w, h)}")
sprawdz("Dokumenty: 3 pliki (notatka, plik 17 MB, film 300 MB)", widok(lambda: klik(*miejsce(1)), "Dokumenty", 3))
# 5. out and in
n_b = q.ile(r"K1-PLIKI: pendrive: brak")
widok(lambda: klik(*pendrive), "Pendrive", 5)
q.monitor("device_del pen")
sprawdz("wyjęcie pendrive'a: nadzorca zatrzymuje czytnik, Pliki wracają do Dokumentów",
        czekaj(r"K1-PLIKI: pendrive: brak", od=n_b, czas=60) and czekaj(r"K1-NADZORCA: pendrive: odłączony — zatrzymuję czytnik") and
        czekaj(r"K1-NADZORCA: czytnik zakończony sygnałem 9"))
zrzut("7-wyjety", gdy=lambda w, h, px: True)
n_j = q.ile(r"K1-PLIKI: pendrive: jest")
q.monitor(f"drive_add 0 if=none,id=ntfs,file={ntfs},format=raw", "device_add usb-storage,id=pen2,drive=ntfs,bus=xhci.0")
sprawdz("pendrive z NTFS włożony: miejsce „Pendrive” wraca", czekaj(r"K1-PLIKI: pendrive: jest", od=n_j, czas=60))
n_c = q.ile(r"K1-NADZORCA: pendrive: czytnik dla")
klik(*pendrive)
sprawdz("NEGATYWNY: NTFS — czytnik odmawia, Pliki mówią, że K1 go jeszcze nie odczyta",
        czekaj(r"K1-NADZORCA: pendrive: czytnik dla", od=n_c, czas=60) and czekaj(r"K1-CZYTNIK: pendrive: nie FAT") and
        czekaj(r"K1-PLIKI: pendrive: nie do odczytu"))
zrzut("8-ntfs", gdy=lambda w, h, px: True)
# 6. the log
t_ = tekst()
sprawdz("nazwy i treść plików z pendrive'a nie trafiają do logu",
        all(s not in t_ for s in ("IMG_0001", "IMG_0002", "USZKODZONE", "Wakacje", "Łódź", "notatka", "film", "wideo", "ukryty", "Zakupy",
                                  "DCIM", "100K1")))
sprawdz("brak 'Kernel panic', błędów, zablokowanych wywołań i odrzuconych komunikatów", "Kernel panic" not in t_ and
        "BŁĄD" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_)
sys.exit(0 if all(q.wyniki) else 1)
PY
zamknij_qemu "$MON" $QPID

# 7. the same data disk again (no stick): the wallpaper photo comes back from skarbiec
LOG2=$TESTY/pendrive-ponownie.log MON2=$TESTY/pendrive-ponownie.sock
rm -f "$LOG2" "$MON2"
"$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG2" --monitor "$MON2" --czas "$CZAS" \
    --qemu "-drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0" &
QPID=$!
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$LOG2" "$MON2" "$TESTY" "$CZAS" <<'PY' || r=1
import os, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit = sys.argv[1:5]
q = Maszyna(log, mon, out, limit, "pendrive")
blisko = lambda a, b, t=12: all(abs(x - y) <= t for x, y in zip(a, b))
ok = q.czekaj(r"K1-EKRAN: zdjęcie na tapetę wczytane ze skarbca \(80x60\)") and q.czekaj(r"K1-GUI: pulpit gotowy")
m = q.czekaj(r"K1-SKARBIEC: magazyn otwarty: (\d+) obiektów \(format 2, ([\d.]+) s\)")
q.sprawdz("ponowny start: magazyn z filmem 300 MB otwarty od razu (format 2: spis, nie cała treść; 0.8.2 krok 1c)",
          m and float(m.group(2)) < 1.0 and "skarbiec jeszcze otwiera magazyn" not in q.tekst(), m.group(0) if m else "brak")
w, h, px = q.zrzut("9-ponownie", gdy=lambda w, h, px: blisko(px(w - 12, h // 2), (60, 120, 180)))
q.sprawdz("ponowny start: tapeta ze zdjęcia wczytana ze skarbca i narysowana", ok and blisko(px(w - 12, h // 2), (60, 120, 180)),
          f"tło przy prawej krawędzi {px(w - 12, h // 2)}")
t_ = q.tekst()
q.sprawdz("ponowny start: brak błędów i zablokowanych wywołań", "Kernel panic" not in t_ and "BŁĄD" not in t_ and
          "zabronione wywołanie" not in t_)
sys.exit(0 if all(q.wyniki) else 1)
PY
zamknij_qemu "$MON2" $QPID
if [ $r -eq 0 ]; then info "pendrive: wszystko zaliczone (zrzuty: $TESTY/pendrive-*.png)"; else echo "[k1] pendrive: BŁĄD (log: $LOG)"; fi
exit $r

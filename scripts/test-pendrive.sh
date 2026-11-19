#!/bin/sh
# Test 0.8.0 krok 3 i 4 (ADR 0031, 0032, powtarzalny): zdjęcia z pendrive'a w działającym K1 (obraz produkcyjny) — bez
# prawdziwego sprzętu: wirtualny pendrive USB (xHCI) z FAT32 w partycji MBR, zrobiony jak przez inny system (mtools),
# z prawdziwymi zdjęciami JPEG i PNG (Pillow), uszkodzonym JPEG-iem, polską długą nazwą, plikiem ukrytym, śmieciem
# macOS, plikiem 17 MB i (0.8.1 krok 4) filmem 300 MB.
#   1. Pliki pokazują miejsce „Pendrive”; po kliknięciu nadzorca uruchamia czytnik (UID 106, pendrive tylko do
#      odczytu), czytnik rozpoznaje FAT32, Pliki pokazują foldery (bez ukrytych plików)
#   2. DCIM › 100K1 → „Importuj wszystkie”: dziewięć plików (zdjęcia, uszkodzony JPEG, od 0.8.2 duże zdjęcia, GIF, BMP,
#      WebP) trafia do Obrazów, skarbiec rozpoznaje je jako obrazy; drugi raz: nic nie przybywa, „Pominięto 9, już
#      zaimportowane” (0.8.2 krok 2, D3)
#   3. pojedynczo: plik 17 MB (do 0.8.0 „za duży”) i film 300 MB → Dokumenty kawałkami po 1 MiB (0.8.1 krok 4: czytnik
#      ma cgroup 32 MiB, a Pliki i skarbiec dostają najwyżej kawałek), zdjęcie z polską nazwą → Obrazy, notatka → Dokumenty
#   4. Obrazy: 10 plików „z pendrive'a”; Dokumenty: 3; (0.8.5 krok 3) Ustawienia → Miejsce przed importem i po nim:
#      części od skarbca (Obrazy, Dokumenty rosną), zrzuty w jasnym i ciemnym motywie
#   4c. (0.8.5 krok 2) Podgląd: 20 × „+” i 20 × „−” bez czekania, przeciąganie wieloma ruchami — okno nadąża i dalej
#      odpowiada (← / ‹ potem), w ruchu szybka droga, po zatrzymaniu raz dokładna (log „ruch: klatek…”)
#   4a. (krok 4) zaznaczone zdjęcie → dekoder w klatce (UID 107) → podgląd w Plikach (kolor na zrzucie); PNG też;
#      uszkodzony JPEG → „Podgląd niedostępny”; „Ustaw jako tapetę” → ekran rysuje zdjęcie na całym ekranie
#   4b. (0.8.2 krok 3; maszyna ma 8 GB jak X230) zdjęcia 48 i 100 Mpx (kilka MB: plik idzie do dekodera kawałkami po
#      1 MiB) → podgląd; nagłówek 101 Mpx → „Zdjęcie ma 101 Mpx — K1 pokazuje do 100 Mpx”; GIF, BMP, WebP → podgląd
#   5. wyjęcie pendrive'a (device_del): czytnik zatrzymany, miejsce znika; włożenie pendrive'a z NTFS: Pliki mówią,
#      że K1 go jeszcze nie odczyta
#   5a. (0.8.2 krok 2) pendrive z „małym” FAT32 (mformat -F -c 8, mniej niż 65 525 klastrów — do 0.8.1 „nie FAT”)
#      i folderem 1100 zdjęć (więcej niż jedna lista czytnika): widać wszystkie (0.10.0 krok 4: kółko przewija listę po
#      3 wiersze na ząbek, nad miejscami po lewej — nie), „Importuj wszystkie” bierze wszystkie,
#      czas z pendrive'a zostaje przy pliku, drugi import pomija wszystkie
#   6. nazwy plików i ich treść nie trafiają do logu; brak zabronionych wywołań i odrzuconych komunikatów
#   7. ponowny start z tym samym dyskiem danych: tapeta ze zdjęcia wraca ze skarbca; magazyn z filmem 300 MB otwiera się
#      od razu (0.8.2 krok 1c, format 2: spis zamiast czytania wszystkiego) — ekran dostaje zapisane ustawienia przy
#      starcie, nie później (w 0.8.1: „skarbiec jeszcze otwiera magazyn”, R58)
# Dowody: build/testy/pendrive.log, zrzuty build/testy/pendrive-*.png. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-240}
CZAS=$((CZAS * 3))  # 0.8.2 step 3: + photos of 48 and 100 Mpx (in TCG 10 and 20 s each)
# 100 Mpx only with KVM (CI, a real computer: ~1.5 s): in TCG it takes ~20 s of processor, as much as dekoder's limit
KVM=0; [ -w /dev/kvm ] && KVM=1
LOG=$TESTY/pendrive.log MON=$TESTY/pendrive.sock
PEN=$TESTY/pendrive-fat32.img NTFS=$TESTY/pendrive-ntfs.img DANE=$TESTY/pendrive-dane.img MALY=$TESTY/pendrive-maly.img
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
# 0.8.2 step 3: photos of 48 and 100 Mpx — one colour with a band of noise at the bottom (a few MB: more than one piece)
szum = random.Random(3)
for nazwa, kolor, rozm in (("ZDJ_48MPX.JPG", (220, 140, 40), (8000, 6000)), ("ZDJ_100MPX.JPG", (30, 140, 120), (10000, 10000))):
    im = Image.new("RGB", rozm, kolor)
    pas = Image.frombytes("RGB", (rozm[0], 160), szum.randbytes(rozm[0] * 160 * 3))
    im.paste(pas, (0, rozm[1] - 160))
    im.save(f"{z}/{nazwa}", "JPEG", quality=90)
    del im
d = bytearray(open(f"{z}/IMG_0001.JPG", "rb").read())  # 101 Mpx in the header (SOF0), the data of 64 × 48
i = 2
while d[i + 1] != 0xC0:
    i += 2 + (d[i + 2] << 8 | d[i + 3])
d[i + 5:i + 9] = (10000).to_bytes(2, "big") + (10050).to_bytes(2, "big")
open(f"{z}/ZDJ_101MPX.JPG", "wb").write(bytes(d))
Image.new("RGB", (64, 48), (150, 60, 170)).save(f"{z}/ZDJ_GIF.GIF")
Image.new("RGB", (64, 48), (200, 200, 60)).save(f"{z}/ZDJ_BMP.BMP")
Image.new("RGB", (64, 48), (230, 90, 90)).save(f"{z}/ZDJ_WEBP.WEBP", quality=95)
PY
mformat -C -i "$TESTY/pendrive-czesc.img" -F -c 8 -T 720896 :: || blad "mformat"
mmd -i "$TESTY/pendrive-czesc.img" ::/DCIM ::/DCIM/100K1
ZDJ="$Z/ZDJ_48MPX.JPG $Z/ZDJ_100MPX.JPG $Z/ZDJ_101MPX.JPG $Z/ZDJ_GIF.GIF $Z/ZDJ_BMP.BMP $Z/ZDJ_WEBP.WEBP"
# shellcheck disable=SC2086
mcopy -i "$TESTY/pendrive-czesc.img" "$Z/IMG_0001.JPG" "$Z/IMG_0002.PNG" "$Z/USZKODZONE.JPG" "$Z/._IMG_0001.JPG" $ZDJ \
    ::/DCIM/100K1/
mcopy -i "$TESTY/pendrive-czesc.img" "$Z/Wakacje nad morzem — Łódź.jpg" "$Z/notatka.txt" "$Z/film.mp4" "$Z/wideo.mp4" \
    "$Z/ukryty.txt" ::
mattrib -i "$TESTY/pendrive-czesc.img" +h ::/ukryty.txt
truncate -s 360M "$PEN"
printf 'label: dos\nstart=2048, size=720896, type=c\n' | sfdisk -q "$PEN" >/dev/null || blad "sfdisk (pendrive)"
dd if="$TESTY/pendrive-czesc.img" of="$PEN" bs=512 seek=2048 conv=notrunc status=none
rm -f "$TESTY/pendrive-czesc.img"
# shellcheck disable=SC2086
OCZEKIWANE=$(cat "$Z/IMG_0001.JPG" "$Z/IMG_0002.PNG" "$Z/USZKODZONE.JPG" "$Z/Wakacje nad morzem — Łódź.jpg" "$Z/notatka.txt" \
    "$Z/film.mp4" "$Z/wideo.mp4" $ZDJ | wc -c)
ls -l $ZDJ
rm -rf "$Z"
# a stick with NTFS (only its signature): K1 cannot read it yet
rm -f "$NTFS"; truncate -s 16M "$NTFS"
printf 'label: dos\nstart=2048, type=7\n' | sfdisk -q "$NTFS" >/dev/null
printf '\353R\220NTFS    ' | dd of="$NTFS" bs=512 seek=2048 conv=notrunc status=none
# a "small" FAT32 (0.8.2 step 2, L13 and L14): fewer than 65 525 clusters, in an MBR partition; a folder of 1100 pictures
# (a JPEG start each — the type comes from the content), more than one list of czytnik (1024). The names differ in
# their first characters: names sharing them make mcopy search the whole folder for a free short name (~20 s here).
rm -rf "$Z" "$MALY" "$TESTY/pendrive-czesc.img"; mkdir -p "$Z"
python3 -I -c 'import sys
for i in range(1100): open(f"{sys.argv[1]}/{i:04d}_telefon.jpg", "wb").write(b"\xff\xd8\xff\xe0" + str(i).encode())' "$Z"
mformat -C -i "$TESTY/pendrive-czesc.img" -F -c 8 -T 253952 :: || blad "mformat (mały FAT32)"
mmd -i "$TESTY/pendrive-czesc.img" ::/Telefon
mcopy -i "$TESTY/pendrive-czesc.img" "$Z"/* ::/Telefon/
truncate -s 128M "$MALY"
printf 'label: dos\nstart=2048, size=253952, type=c\n' | sfdisk -q "$MALY" >/dev/null || blad "sfdisk (mały FAT32)"
dd if="$TESTY/pendrive-czesc.img" of="$MALY" bs=512 seek=2048 conv=notrunc status=none
rm -rf "$Z" "$TESTY/pendrive-czesc.img"
# the data partition (K1-DANE) on a SATA disk: 768 MiB, sparse (format 2 of the vault since 0.8.2: about 90 % for files)
rm -f "$DANE"; truncate -s 768M "$DANE"
printf 'label: gpt\nstart=2048, size=1568768, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null

"$K1_REPO/scripts/uruchom-qemu.sh" --log "$LOG" --monitor "$MON" --czas "$CZAS" \
    --qemu "-m 8G -drive if=none,id=dane,file=$DANE,format=raw -device ide-hd,drive=dane,bus=ide.0
            -device qemu-xhci,id=xhci -drive if=none,id=pendrive,file=$PEN,format=raw
            -device usb-storage,id=pen,bus=xhci.0,drive=pendrive" &
QPID=$!
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I - "$LOG" "$MON" "$TESTY" "$CZAS" "$NTFS" "$OCZEKIWANE" "$MALY" "$KVM" <<'PY' || r=1
import os, re, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
log, mon, out, limit, ntfs, oczekiwane, maly, kvm = sys.argv[1:9]
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


g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)")
k0 = czekaj(r"K1-EKRAN: kursor (\d+),(\d+) ")
if not sprawdz("pulpit gotowy", g and k0): sys.exit(1)
q.kursor[:] = [int(k0.group(1)), int(k0.group(2))]
CZESCI = r"K1-USTAWIENIA: miejsce: części: Pulpit (\d+) KiB, Dokumenty (\d+) KiB, Obrazy (\d+) KiB, Muzyka \d+ KiB, " \
         r"Wideo \d+ KiB, Pobrane \d+ KiB, Kosz (\d+) KiB, Strefy \d+ KiB, System (\d+) KiB; zapas systemu (\d+) KiB; " \
         r"nośnik: (\w+)"


def miejsce_w_ustawieniach(nazwa, ciemny=False):
    """0.8.5 step 3: Ustawienia → Miejsce (the taskbar: 0 Zegar, 1 Notatki, 2 Ustawienia), the parts from skarbiec;
    a screenshot (and one in the dark theme), then × closes the window. Returns the line of the parts."""
    u = po(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)",
           lambda: klik(int(g.group(3)) + 2 * int(g.group(5)), int(g.group(4))))
    sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+); przyciski od (\d+),(\d+) \(szer\. (\d+)")
    if not (u and sk):
        return None
    ux, uy = int(u.group(4)), int(u.group(5))
    sekcja = lambda i: klik(ux + int(sk.group(1)), uy + int(sk.group(2)) + q.wiersz_sekcji(i) * int(sk.group(3)))
    c = po(CZESCI, lambda: sekcja(7), czas=30)
    zrzut(nazwa, gdy=lambda w, h, px: True)
    if c and ciemny:  # Wygląd → "Ciemny", Miejsce again, back to "Jasny"
        motyw = lambda i: klik(ux + int(sk.group(4)) + int(sk.group(6)) * (2 * i + 1) // 4, uy + int(sk.group(5)) + 18)
        sekcja(0)
        po(r"K1-EKRAN: ustawienie 1=1", lambda: motyw(1))
        sekcja(7)
        zrzut(nazwa + "-ciemny", gdy=lambda w, h, px: True)
        sekcja(0)
        po(r"K1-EKRAN: ustawienie 1=0", lambda: motyw(0))
    po(r"K1-USTAWIENIA: okno zamknięte przez użytkownika", lambda: klik(ux + 720 - 8 - 14, uy + 22))
    return c


# 0. (0.8.5 step 3) Ustawienia → Miejsce before the import: Obrazy empty
m0 = miejsce_w_ustawieniach("0-miejsce")
# (K1-DANE of this machine is on a SATA disk — the stick holds only the photos: "dysk"; a K1-DANE on a stick: test-skarbiec)
sprawdz("(0.8.5) Ustawienia → Miejsce: części od skarbca (tylko liczby), Obrazy puste, zapas systemu, nośnik: dysk",
        m0 and int(m0.group(3)) == 0 and int(m0.group(6)) > 0 and m0.group(7) == "dysk", m0.group(0) if m0 else "brak")
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


def przycisk(nazwa, czas=20):
    """0.10.7 step 3: "Importuj" comes only with a selection — its centre as soon as Pliki logs it."""
    return przyciski()[nazwa] if q.czekaj_na(lambda: nazwa in przyciski(), czas) else (0, 0)


def importuj(akcja, zapisane, obrazy, duze, bledy=0, pominiete=0, czas=90):
    wz = (rf"K1-PLIKI: import: koniec — zapisane {zapisane} \(do Obrazów {obrazy}\), za duże {duze}, błędy {bledy}, "
          rf"pominięte {pominiete}")
    return po(wz, akcja, czas=czas)


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
sprawdz("przyciski pendrive'a bez zaznaczenia: tylko Importuj wszystkie (bez zmiany nazwy i usuwania; 0.10.7 krok 3)",
        {"Importuj wszystkie"} == set(p), str(p))
# 2. DCIM › 100K1 → "Importuj wszystkie"
sprawdz("DCIM (1 folder)", widok(lambda: q.dwuklik(*wiersz(0)), "Pendrive", 1))
sprawdz("DCIM › 100K1: 9 plików (bez śmieci macOS „._…”)", widok(lambda: q.dwuklik(*wiersz(0)), "Pendrive", 9))
zrzut("3-100k1", gdy=lambda w, h, px: True)
sprawdz("„Importuj wszystkie”: 9 plików → Obrazy (GIF, BMP i WebP też — 0.8.2)",
        "Importuj wszystkie" in p and importuj(lambda: klik(*p["Importuj wszystkie"]), 9, 9, 0))
sprawdz("skarbiec: 9 plików z pendrive'a, wszystkie rozpoznane z treści jako obrazy",
        czekaj(r"K1-PLIKI: pliki z pendrive'a: 9 \(obrazy: 9\)"))
zrzut("4-zaimportowane", gdy=lambda w, h, px: True)
sprawdz("(0.8.2, D3) „Importuj wszystkie” drugi raz: nic nowego — „Pominięto 9, już zaimportowane” (ta sama nazwa, "
        "rozmiar i czas z pendrive'a)", importuj(lambda: klik(*p["Importuj wszystkie"]), 0, 0, 0, pominiete=9))
zrzut("4-pominiete", gdy=lambda w, h, px: True)
# 3. one by one, from the main folder (rows: DCIM, film.mp4, notatka.txt, Wakacje…, wideo.mp4)
sprawdz("„w górę” dwa razy: główny folder pendrive'a",
        widok(lambda: klik(*w_gore), "Pendrive", 1) and widok(lambda: klik(*w_gore), "Pendrive", 5))
klik(*wiersz(1))
sprawdz("plik 17 MB (do 0.8.0: „za duży”) → Dokumenty, w 18 kawałkach", importuj(lambda: klik(*przycisk("Importuj")), 1, 0, 0) and
        czekaj(r"K1-CZYTNIK: plik przeczytany \(17825804 B, kawałków 18\)"))
klik(*wiersz(4))
sprawdz("film 300 MB → Dokumenty kawałkami po 1 MiB (czytnik: cgroup 32 MiB, memfd najwyżej kawałek)",
        po(r"K1-PLIKI: import: koniec — zapisane 1 \(do Obrazów 0\), za duże 0, błędy 0", lambda: klik(*przycisk("Importuj")),
           czas=float(limit) * 0.5) and czekaj(r"K1-CZYTNIK: plik przeczytany \(314572812 B, kawałków 301\)"))
zrzut("5-duzy", gdy=lambda w, h, px: True)
p = przyciski()
# 0.8.2 step 6: dragging from the stick — onto Kosz: "Możesz tylko…"; onto a folder of K1: an import into it
m = po(r"K1-PLIKI: okienko \(odmowa\): (.*)", lambda: q.przeciagnij(*wiersz(3), ox + kx, oy + ky))
ok_ = dict((t.strip(), (ox + int(x), oy + int(y))) for t, x, y in re.findall(r"([^;:]+?) (\d+),(\d+)", m.group(1))) if m else {}
sprawdz("NEGATYWNY (krok 6): plik z pendrive'a na Kosz — okienko „Z pendrive'a możesz tylko skopiować…” i „OK”",
        list(ok_) == ["OK"] and czekaj(r"cel Kosz, działanie: odmowa"), str(ok_))
zrzut("5a-tylko-import", gdy=lambda w, h, px: True)
if "OK" in ok_:
    klik(*ok_["OK"])
sprawdz("(krok 6) zdjęcie z polską długą nazwą przeciągnięte na „Obrazy” → kopia tam (import)",
        importuj(lambda: q.przeciagnij(*wiersz(3), *miejsce(2)), 1, 1, 0) and czekaj(r"cel folder, działanie: import"))
klik(*wiersz(2))
sprawdz("notatka → Dokumenty", importuj(lambda: klik(*przycisk("Importuj")), 1, 0, 0))
sprawdz(f"skarbiec: 13 plików z pendrive'a (10 obrazów), razem {oczekiwane} B — tyle, ile na pendrivie",
        czekaj(rf"K1-PLIKI: pliki z pendrive'a: 13 \(obrazy: 10\), razem {oczekiwane} B"))
# 4. where they went (Obrazy by name: IMG_0001.JPG, IMG_0002.PNG, USZKODZONE.JPG, Wakacje…, ZDJ_100MPX.JPG,
# ZDJ_101MPX.JPG, ZDJ_48MPX.JPG, ZDJ_BMP.BMP, ZDJ_GIF.GIF, ZDJ_WEBP.WEBP)
sprawdz("Obrazy: 10 plików", widok(lambda: klik(*miejsce(2)), "Obrazy", 10))
# 4. (0.8.5 step 3) Ustawienia → Miejsce after the import: Obrazy and Dokumenty grew (17 MB and 300 MB in Dokumenty)
m1 = miejsce_w_ustawieniach("4b-miejsce", ciemny=True)
sprawdz("(0.8.5) Ustawienia → Miejsce po imporcie: Obrazy urosły, Dokumenty ≥ 317 MB, Kosz pusty (zrzuty w obu motywach)",
        m0 and m1 and int(m1.group(3)) > int(m0.group(3)) + 1000 and int(m1.group(2)) >= 317 * 1024 and int(m1.group(4)) == 0,
        m1.group(0) if m1 else "brak")
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


n_d = q.ile(r"K1-NADZORCA: dekoder dla aplikacji Pliki")
ok, kol = podglad(3, r"K1-PLIKI: miniatura: 80x60", NIEBIESKI, "6-podglad-jpeg")
sprawdz("zdjęcie JPEG: nadzorca uruchamia dekoder w klatce (UID 107), dekoder odsyła 80×60, Pliki pokazują je w podglądzie",
        ok and czekaj(r"K1-NADZORCA: dekoder dla aplikacji Pliki: pid \d+, uid 107, pamięć 1152 MiB", od=n_d) and
        czekaj(r"K1-DEKODER: Obraz JPEG → 80x60"),
        f"kolor w podglądzie {kol}")
ok, kol = podglad(1, r"K1-PLIKI: miniatura: 32x32", ZIELONY, "6-podglad-png")
sprawdz("zdjęcie PNG: podgląd 32×32", ok and czekaj(r"K1-DEKODER: Obraz PNG → 32x32"), f"kolor w podglądzie {kol}")
ok, _ = podglad(2, r"K1-PLIKI: obraz: dekoder nie odczytał pliku \(-42\)", None, "6-podglad-uszkodzony")
sprawdz("NEGATYWNY: uszkodzony JPEG — dekoder odpowiada błędem, Pliki: „Podgląd niedostępny”, nic więcej się nie dzieje",
        ok and czekaj(r"K1-DEKODER: Obraz JPEG: nie da się odczytać \(-42;"))
# 4b. (0.8.2 step 3) large photos and new formats (100 Mpx: with KVM only — see KVM above)
if kvm != "1":
    print("  [uwaga] bez KVM: zdjęcie 100 Mpx nie jest dekodowane w tym teście (TCG: ok. 20 s procesora, tyle co limit "
          "dekodera); sprawdzają je test_dekoder na hoście i ten test w CI")
for i, nazwa, wzor, kolor, opis in (
        (6, "48mpx", r"K1-DEKODER: Obraz JPEG → 200x150 \(([\d.]+) s\)", (220, 140, 40), "zdjęcie 48 Mpx (8000×6000)"),
        *([(4, "100mpx", r"K1-DEKODER: Obraz JPEG → 150x150 \(([\d.]+) s\)", (30, 140, 120), "zdjęcie 100 Mpx (10000×10000)")]
          if kvm == "1" else []),
        (8, "gif", r"K1-DEKODER: Obraz GIF → 64x48", (150, 60, 170), "GIF"),
        (7, "bmp", r"K1-DEKODER: Obraz BMP → 64x48", (200, 200, 60), "BMP"),
        (9, "webp", r"K1-DEKODER: Obraz WebP → 64x48", (230, 90, 90), "WebP (stratny, VP8)")):
    n_k = q.ile(wzor)
    ok, kol = podglad(i, r"K1-PLIKI: miniatura: \d+x\d+", kolor, f"6-podglad-{nazwa}")
    m = czekaj(wzor, od=n_k, czas=120)
    sprawdz(f"{opis}: podgląd w Plikach" + (" — plik do dekodera kawałkami po 1 MiB" if "Mpx" in opis else ""),
            ok and m, f"kolor {kol}, {m.group(0) if m else 'brak odpowiedzi dekodera'}")
ok, kol = podglad(5, r"K1-PLIKI: obraz: dekoder nie odczytał pliku \(-41\): 101 Mpx", None, "6-podglad-101mpx")
sprawdz("NEGATYWNY: nagłówek 101 Mpx — dekoder odmawia, zanim weźmie pamięć; Pliki: „Zdjęcie ma 101 Mpx — K1 pokazuje "
        "do 100 Mpx”", ok and czekaj(r"K1-DEKODER: Obraz JPEG: nie da się odczytać \(-41; w pliku 10050x10000\)"))
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
# 4c. (0.8.2 step 4, ADR 0037) Podgląd: a double click on a picture opens it in its own window (Obrazy by name: row 1
# IMG_0002.PNG green, row 2 USZKODZONE.JPG, row 3 Wakacje… blue 80×60)
PUKLAD = (r"K1-PODGLAD: układ: poprzednie (\d+),(\d+) następne (\d+),(\d+) otwórz (\d+),(\d+) obróć (\d+),(\d+) "
          r"tapeta (\d+),(\d+) kosz (\d+),(\d+) zdjęcie (\d+),(\d+) mniej (\d+),(\d+) więcej (\d+),(\d+)")
ZDJ = r"K1-PODGLAD: zdjęcie (\d+) z (\d+)"
n_d, n_z, n_u = q.ile(r"K1-NADZORCA: dekoder dla aplikacji Podgląd"), q.ile(ZDJ), q.ile(PUKLAD)
op = po(r"K1-EKRAN: okno (\d+) \(podglad\) (\d+)x(\d+) w (\d+),(\d+)", lambda: q.dwuklik(*wiersz(3)), czas=60)
up = czekaj(PUKLAD, od=n_u)
pz = czekaj(ZDJ, od=n_z, czas=60)
pw = czekaj(r"K1-PODGLAD: obraz: 80x60 \(zdjęcie 80x60\)", czas=90)
if not sprawdz("Podgląd otwarty", op and up and pz): sys.exit(1)
pox, poy = int(op.group(4)), int(op.group(5))
pp = {k: (pox + int(up.group(2 * i + 1)), poy + int(up.group(2 * i + 2)))
      for i, k in enumerate(("poprzednie", "nastepne", "otworz", "obroc", "tapeta", "kosz", "zdjecie", "mniej", "wiecej"))}
sz, sy = pp["zdjecie"]
w, h, px = zrzut("6b-podglad", gdy=lambda w, h, px: blisko(px(sz, sy), NIEBIESKI))
sprawdz("Podgląd (UID 208): dwuklik zdjęcia w Plikach — zdjęcie 4 z 10 w osobnym oknie, 80×60 (nie powiększone ponad "
        "100 %), z dekodera w klatce uruchomionego dla Podglądu",
        pw and pz.group(1, 2) == ("4", "10") and czekaj(r"K1-NADZORCA: uruchomiono podglad \(pid \d+, uid 208\)") and
        czekaj(r"K1-NADZORCA: dekoder dla aplikacji Podgląd: pid \d+, uid 107", od=n_d) and blisko(px(sz, sy), NIEBIESKI),
        f"kolor w środku {px(sz, sy)}")
# the owner's wish: − + zoom the picture, with a short animation (its end is logged)
PL = r"K1-PODGLAD: powiększenie (\d+)%: widok (\d+)x(\d+)"
m = po(PL, lambda: q.pisz("kp_add"))
w, h, px = zrzut("6b-podglad-plus", gdy=lambda w, h, px: blisko(px(sz + 50, sy), NIEBIESKI))
sprawdz("+ (klawisz): przybliżenie z animacją — 150 %, widok 120×90 (zrzut: niebieski dalej od środka)",
        m and m.group(1, 2, 3) == ("150", "120", "90") and blisko(px(sz + 50, sy), NIEBIESKI), f"{m and m.group(0)} {px(sz + 50, sy)}")
m = po(PL, lambda: klik(*pp["mniej"]))
w, h, px = zrzut("6b-podglad-minus", gdy=lambda w, h, px: not blisko(px(sz + 50, sy), NIEBIESKI))
sprawdz("przycisk −: z powrotem dopasowane — 100 %, 80×60", m and m.group(1, 2, 3) == ("100", "80", "60") and
        not blisko(px(sz + 50, sy), NIEBIESKI), f"{m and m.group(0)}")
# 0.8.5 step 2: quick zooming and dragging — the window keeps up and answers (a frozen window in 0.8.3: L1, L3)
n_pl, n_r = q.ile(r"K1-PODGLAD: powiększenie 400%: widok 320x240"), q.ile(r"K1-PODGLAD: ruch: klatek")
q.pisz(*["kp_add"] * 20)
ok = czekaj(r"K1-PODGLAD: powiększenie 400%: widok 320x240", od=n_pl, czas=60)
q.przeciagnij(sz, sy, sz + 120, sy + 70, krokow=20)
n_pl = q.ile(r"K1-PODGLAD: powiększenie 100%: widok 80x60")
q.pisz(*["kp_subtract"] * 20)
ok2 = czekaj(r"K1-PODGLAD: powiększenie 100%: widok 80x60", od=n_pl, czas=60)
r_ = czekaj(r"K1-PODGLAD: ruch: klatek (\d+) w (\d+) ms, najdłuższa ([\d.]+) ms, ostra ([\d.]+) ms", od=n_r, czas=30)
w, h, px = zrzut("6b-podglad-szybko", gdy=lambda w, h, px: blisko(px(sz, sy), NIEBIESKI) and not blisko(px(sz + 50, sy), NIEBIESKI))
sprawdz("(0.8.5) 20 × „+” bez czekania → 400 % (320×240), przeciąganie (20 ruchów), 20 × „−” → 100 %: okno nadąża, "
        "w ruchu szybko, potem raz ostro (log „ruch: klatek…”), zrzut: zdjęcie dopasowane",
        ok and ok2 and r_ and int(r_.group(1)) >= 1 and blisko(px(sz, sy), NIEBIESKI) and not blisko(px(sz + 50, sy), NIEBIESKI),
        r_.group(0) if r_ else "brak")
ok = po(r"K1-PODGLAD: obrót 90°: widok 60x80", lambda: q.pisz("r"))
w, h, px = zrzut("6b-podglad-obrot", gdy=lambda w, h, px: not blisko(px(sz + 36, sy), NIEBIESKI))
sprawdz("R: widok obrócony o 90° (60×80; plik bez zmian)", ok and blisko(px(sz, sy + 36), NIEBIESKI) and
        not blisko(px(sz + 36, sy), NIEBIESKI), f"{px(sz, sy + 36)} {px(sz + 36, sy)}")
n_z = q.ile(ZDJ)
q.pisz("left")
m = czekaj(ZDJ, od=n_z)
sprawdz("NEGATYWNY: ← — zdjęcie 3 z 10, uszkodzone: dekoder odmawia, Podgląd mówi „Tego zdjęcia nie da się odczytać.”",
        m and m.group(1) == "3" and czekaj(r"K1-PODGLAD: obraz: dekoder nie odczytał pliku \(-42\)", czas=60))
zrzut("6b-podglad-uszkodzone", gdy=lambda w, h, px: True)
n_z, n_o = q.ile(ZDJ), q.ile(r"K1-PODGLAD: obraz: 32x32")
klik(*pp["poprzednie"])
m = czekaj(ZDJ, od=n_z)
ok = czekaj(r"K1-PODGLAD: obraz: 32x32 \(zdjęcie 32x32\)", od=n_o, czas=60)
w, h, px = zrzut("6b-podglad-png", gdy=lambda w, h, px: blisko(px(sz, sy), ZIELONY))
sprawdz("przycisk ‹: zdjęcie 2 z 10 (PNG, zielone)", m and m.group(1) == "2" and ok and blisko(px(sz, sy), ZIELONY),
        f"kolor {px(sz, sy)}")
KOSZ, PO, O9 = (r"K1-PODGLAD: do Kosza: zdjęcie 2 z 10", r"K1-PODGLAD: zdjęcie 2 z 9",
                r"K1-PLIKI: widok: Obrazy, 9 elementów")
n_k, n_p9, n_o9 = q.ile(KOSZ), q.ile(PO), q.ile(O9)  # "od" counts lines of the same pattern
q.pisz("delete")
ok = czekaj(KOSZ, od=n_k, czas=30) and czekaj(PO, od=n_p9, czas=30)
sprawdz("Delete (D2): zdjęcie do Kosza bez pytania, Podgląd pokazuje następne (2 z 9); Pliki: Obrazy — 9",
        ok and czekaj(O9, od=n_o9, czas=30))
n_p = q.ile(r"K1-WYBOR: prośba: aplikacja 9")
q.pisz("ctrl-o")
ok = czekaj(r"K1-WYBOR: prośba: aplikacja 9, typ 0, rodzina 1", od=n_p, czas=60)
zrzut("6b-podglad-otworz", gdy=lambda w, h, px: True)
q.pisz("esc")
sprawdz("Ctrl+O: okno wyboru pliku z samymi zdjęciami (rodzina „obrazy”); Esc — anulowano",
        ok and czekaj(r"K1-PODGLAD: okno wyboru pliku: anulowano", czas=30))
n_z, n_t, n_o = q.ile(ZDJ), q.ile(r"K1-EKRAN: zdjęcie na tapetę od Podglądu"), q.ile(r"K1-PODGLAD: obraz: 80x60")
klik(*pp["nastepne"])
m = czekaj(ZDJ, od=n_z)
ok = czekaj(r"K1-PODGLAD: obraz: 80x60", od=n_o, czas=60)
klik(*pp["tapeta"])
sprawdz("› (3 z 9) i „Ustaw jako tapetę” w Podglądzie: dekoder → ekran (tabela nadawców: Pliki, Podgląd)",
        m and m.group(1, 2) == ("3", "9") and ok and
        czekaj(r"K1-EKRAN: zdjęcie na tapetę od Podglądu: 80x60", od=n_t, czas=90) and
        czekaj(r"K1-PODGLAD: tapeta ze zdjęcia: 80x60 wysłana do ekranu"))
klik(pox + 960 - 8 - 14, poy + 22)  # × of the window (k1_okno.c: 28 px buttons, 8 px from the edge)
sprawdz("Podgląd zamknięty (×)", czekaj(r"K1-PODGLAD: okno zamknięte przez użytkownika", czas=30))
sprawdz("Dokumenty: 3 pliki (notatka, plik 17 MB, film 300 MB)", widok(lambda: klik(*miejsce(1)), "Dokumenty", 3))
# 4d. (0.8.2 step 5) a file no application opens (film.mp4 — "Plik"; Dokumenty by name: film.mp4, notatka.txt, wideo.mp4)
n_a = q.ile(r"otwarty w aplikacji")
sprawdz("NEGATYWNY (krok 5): podwójne kliknięcie pliku bez aplikacji (film) — stopka „K1 nie ma jeszcze aplikacji…”, "
        "żadna aplikacja nie dostaje pliku", po(r"K1-PLIKI: plik bez aplikacji \(typ 1\): komunikat w stopce",
                                               lambda: q.dwuklik(*wiersz(0))) and q.ile(r"otwarty w aplikacji") == n_a)
zrzut("6c-bez-aplikacji", gdy=lambda w, h, px: True)
n_m = q.ile(r"K1-PLIKI: menu kontekstowe \(pliku\): (.*)")
q.prawy_klik(*wiersz(0))
mm = czekaj(r"K1-PLIKI: menu kontekstowe \(pliku\): (.*)", od=n_m, czas=20)
wm = [t.strip() for t in re.findall(r"([^;:]+?) \d+,\d+", mm.group(1))] if mm else []
w, h, px = zrzut("6c-menu-bez-aplikacji", gdy=lambda w, h, px: True)
sprawdz("menu pliku bez aplikacji: „Otwórz” i „Otwórz za pomocą” wyszarzone (nie do kliknięcia), zostają „Zmień nazwę” "
        "i „Usuń”", wm == ["Zmień nazwę", "Usuń"], str(wm))
q.pisz("esc")  # closes the menu (a click elsewhere would only close it, too)
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
# 5a. (0.8.2 step 2) a small FAT32, a folder of 1100 pictures
n_b = q.ile(r"K1-PLIKI: pendrive: brak")
q.monitor("device_del pen2")
czekaj(r"K1-PLIKI: pendrive: brak", od=n_b, czas=60)
n_j = q.ile(r"K1-PLIKI: pendrive: jest")
q.monitor(f"drive_add 0 if=none,id=maly,file={maly},format=raw", "device_add usb-storage,id=pen3,drive=maly,bus=xhci.0")
sprawdz("pendrive z małym FAT32 (mniej niż 65 525 klastrów; do 0.8.1: „nie FAT”) czytany jako FAT32",
        czekaj(r"K1-PLIKI: pendrive: jest", od=n_j, czas=60) and widok(lambda: klik(*pendrive), "Pendrive", 1, czas=60) and
        czekaj(r"K1-CZYTNIK: pendrive: FAT32, klaster 4096 B, 128 MiB"))
sprawdz("folder 1100 zdjęć: czytnik wysyła dwie listy, Pliki pokazują wszystkie 1100 (do 0.8.1: pierwsze 1024)",
        widok(lambda: q.dwuklik(*wiersz(0)), "Pendrive", 1100, czas=120) and czekaj(r"K1-CZYTNIK: folder: 1100 elementów \(list: 2\)"))
zrzut("9-folder-1100", gdy=lambda w, h, px: True)
# 5b. (0.10.0 step 4) the wheel over the long list: a notch = 3 rows; NEGATIVE: over the places on the left — nothing
LISTA = r"K1-PLIKI: lista przewinięta: od wiersza (\d+) z 1100"
od_wiersza = lambda: ([None] + [int(v) for v in re.findall(LISTA, tekst())])[-1]
q.kolko(*wiersz(2), w_dol=2)
sprawdz("kółko nad listą: 2 ząbki w dół — lista od wiersza 7 (3 wiersze na ząbek)",
        q.czekaj_na(lambda: od_wiersza() == 7, czas=30))
q.kolko(*wiersz(2), w_dol=-1)
sprawdz("kółko w górę: ząbek — od wiersza 4", q.czekaj_na(lambda: od_wiersza() == 4, czas=30))
zrzut("9-przewinieta", gdy=lambda w, h, px: True)
q.kolko(*miejsce(0), w_dol=3)
sprawdz("NEGATYWNY: kółko nad miejscami po lewej nie przewija listy", q.cisza(LISTA, 5))
q.kolko(*wiersz(2), w_dol=-5)
sprawdz("kółko w górę ponad początek: lista od wiersza 1 (nie mniej)", q.czekaj_na(lambda: od_wiersza() == 1, czas=30))
p = przyciski()
sprawdz("„Importuj wszystkie”: 1100 plików → Obrazy (każdy z czasem z pendrive'a)",
        "Importuj wszystkie" in p and importuj(lambda: klik(*p["Importuj wszystkie"]), 1100, 1100, 0, czas=float(limit) * 0.4))
zrzut("9-zaimportowane-1100", gdy=lambda w, h, px: True)
sprawdz("drugi raz: „Pominięto 1100, już zaimportowane”, nic nie przybywa",
        importuj(lambda: klik(*p["Importuj wszystkie"]), 0, 0, 0, pominiete=1100, czas=120) and
        widok(lambda: klik(*miejsce(2)), "Obrazy", 1109))  # 10 + 1100, one in Kosz (Podgląd, 4c)
# 6. the log
t_ = tekst()
sprawdz("nazwy i treść plików z pendrive'a nie trafiają do logu",
        all(s not in t_ for s in ("IMG_0001", "IMG_0002", "USZKODZONE", "Wakacje", "Łódź", "notatka", "film", "wideo", "ukryty", "Zakupy",
                                  "DCIM", "100K1", "telefon", "Telefon", "ZDJ_")))
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

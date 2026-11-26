#!/bin/sh
# Test aktualizacji przez sieć (0.10.5 krok 2, ADR 0046; powtarzalny): obraz testowy z dyskiem, na którym „jest
# zainstalowany” K1 (K1-EFI z budowy — jej miejsce A udaje starszą wersję 0.0.1 — i pusta K1-DANE), z małym dyskiem
# o numerze seryjnym „k1-test-aktualizacji” (test aktualizacji: strefa pobierania łączy się z serwerem testowym,
# wersja deweloperska przyjmuje pliki bez podpisu; z kluczem w CI podpis sprawdzany zawsze) i kartą w sieci QEMU
# (-netdev user: router 10.0.2.2 = ten komputer). Na „routerze” serwer HTTPS w Pythonie (certyfikat robiony tu, na
# chwilę) podaje jak GitHub: latest/download/k1-ogloszenie.bin → przekierowanie → ogłoszenie tej budowy, i paczkę.
#   1. po starcie pulpitu: program zapisujący czyta dysk (działa 0.0.1, nic nie czeka, poprzedniej nie ma)
#   2. 30 s po sieci K1 sam sprawdza („Sprawdzaj sam” domyślnie włączone): strefa pobierania (bez okna), curl przez
#      przekaźnik i łącznik (tylko adres serwera testowego), ogłoszenie przyjęte — jest nowsza wersja
#   3. paczka do poczekalni (druga strefa pobierania), potem okienko „Jest nowa wersja K1” na pulpicie (zrzut)
#   4. „Zainstaluj” → program zapisujący: pliki startowe, system do miejsca B, k1_nowy → ponowne uruchomienie
#   5. dysk po wszystkim: grubenv „spróbuj B”, w miejscu B manifest wersji tej budowy, znak poczekalni wyczyszczony
#   6. NEGATYWNE: serwer podaje paczkę ze zmienionym bajtem — odrzucona, nic nie czeka, okienka nie ma, K1 żyje
#   7. w logu K1 żadnego adresu (niezmiennik 8) — tylko kody i liczby
#   8. (0.10.7 krok 5) na dysku działa już ta wersja: po sprawdzeniu „masz najnowszą wersję”; Ustawienia → Aktualizacje
#      otwarte, zamknięte i otwarte jeszcze raz, potem „Sprawdź teraz” — okno Ustawień żyje cały czas (do 0.10.7 widok
#      „Sprawdzono o …” kończył Ustawienia kodem 159: musl czytał strefę czasową z /etc/localtime, a klatka nie ma open);
#      opis ekranu (dysk „k1-test-opis”) — test klika po nazwach
# 0.13.0 (ADR 0056; D1: A, T1): K1 czyta k1-ogloszenie2.bin (format 2 z czasem podpisu), strefa dokleja stopkę z czasem
#   z nagłówka Date serwera. Serwer testowy przesuwa swój Date: po podaniu paczki o 25 min naprzód — zwłoka wydania
#   (20 min) mija bez czekania: K1 sprawdza jeszcze raz (w teście po 20 s), wydanie wciąż jest — dopiero wtedy okienko
#   „Jest nowa wersja K1” (nie wcześniej); w przebiegu 3 najpierw Date o 8 dni naprzód — „ogłoszenie za stare”
#   (Ustawienia: „Nie mogę sprawdzić, czy masz najnowszą wersję”), potem bez przesunięcia „Sprawdź teraz” — najnowsza.
# Bez KVM (emulacja QEMU, kontener) — ten sam pełny scenariusz (2026-11-18: ok. 8 min, paczka 130 MB też).
# Dowody: build/testy/akt-*.log, akt-*.png.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1500}
mkdir -p "$TESTY"; rm -f "$TESTY"/akt-*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
ESP=$K1_BUDOWA/usb/esp.img
PACZKA=$K1_WYNIK/k1-$K1_WERSJA.k1a
OGL=$K1_WYNIK/k1-ogloszenie2.bin # 0.13.0 (ADR 0056): the file K1 0.13+ reads
for f in "$ESP" "$PACZKA" "$OGL" "$K1_WYNIK/rdzen-test.squashfs"; do [ -f "$f" ] || blad "brak $f — najpierw scripts/zbuduj.sh"; done
command -v openssl >/dev/null || blad "brak openssl (certyfikat serwera testowego)"
EMULACJA=--emulacja
if [ -w /dev/kvm ] && grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested; then
    EMULACJA=
fi

# The disk: GPT as the Instalator writes it (src/instalator/gpt.c): K1-EFI 3 GiB at 1 MiB (since 0.10.9 step 4b), K1-DANE
# after it (sparse: only what is written takes room)
DYSK=$TESTY/akt-dysk.img
rm -f "$DYSK"; truncate -s 3700M "$DYSK"
sfdisk -q "$DYSK" >/dev/null <<TAB || blad "sfdisk"
label: gpt
start=2048, size=6291456, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="K1-EFI"
start=6293504, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="K1-DANE"
TAB
dd if="$ESP" of="$DYSK" bs=1M seek=1 conv=notrunc,sparse status=none
ZNAK=$TESTY/akt-znak.img
dd if=/dev/zero of="$ZNAK" bs=1M count=1 status=none
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -subj /CN=k1-test -days 2 \
    -keyout "$TESTY/akt-klucz.pem" -out "$TESTY/akt-cert.pem" 2>/dev/null || blad "certyfikat testowy"

cat > "$K1_BUDOWA/aktualizacja.py" <<'PY'
import http.server, os, re, ssl, struct, subprocess, sys, threading
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, dysk, znak, paczka, ogl, wersja = sys.argv[1:10]
limit = float(limit)
emulacja = sys.argv[10:]
MIB = 1 << 20

def czytaj(f, od, n):
    with open(f, "rb") as p:
        p.seek(od)
        return p.read(n)

def uklad():  # the K1-EFI partition's layout block (k1_uklad.h): FAT bytes, grubenv, slot bytes, slot A, slot B
    b = czytaj(dysk, MIB + 1024, 512)
    return [struct.unpack_from("<Q", b, o)[0] for o in (8, 16, 24, 32, 40)]

FAT, GENV, SLOT, SLOT_A, SLOT_B = uklad()
MAN = SLOT - MIB  # the slot manifest: in the slot's last MiB (k1_slot_man_off)
POCZ = (FAT + MIB - 1) // MIB * MIB  # the waiting room: right after the FAT (k1_pocz_od)
GENV_PRZED = czytaj(dysk, MIB + GENV, 1024)  # for the negative run: the disk as before the update
MANIFEST_A = czytaj(dysk, MIB + SLOT_A + MAN, 48)  # for the last run: slot A runs this build's version
# the running slot A says 0.0.1 (k1_manifest.h: version at 8, its text at 24) — the build's version is newer
with open(dysk, "r+b") as p:
    p.seek(MIB + SLOT_A + MAN + 8); p.write(struct.pack("<I", 1))
    p.seek(MIB + SLOT_A + MAN + 24); p.write(b"0.0.1".ljust(24, b"\0"))

dane = {"zla": False, "zapytania": [], "przesun": 0}
class Serwer(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass
    def date_time_string(self, timestamp=None):  # 0.13.0: the header Date — the time K1 counts by, moved by the test
        import email.utils, time
        return email.utils.formatdate(time.time() + dane["przesun"], usegmt=True)
    def do_GET(self):
        dane["zapytania"].append(self.path)
        if self.path == "/latest/download/k1-ogloszenie2.bin":  # as GitHub: to the newest release
            self.send_response(302)
            self.send_header("Location", f"/download/k1-v{wersja}/k1-ogloszenie2.bin")
            self.end_headers()
            return
        pliki = {f"/download/k1-v{wersja}/k1-ogloszenie2.bin": ogl, f"/download/k1-v{wersja}/k1-{wersja}.k1a": paczka}
        if self.path not in pliki:
            self.send_response(404); self.end_headers(); return
        b = open(pliki[self.path], "rb").read()
        if dane["zla"] and self.path.endswith(".k1a"):
            b = bytearray(b); b[8192 + 4096] ^= 0x40; b = bytes(b)  # a byte of the system: the signed digest fails
        self.send_response(200)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)
        if self.path.endswith(".k1a") and dane["przesun"] == 0:
            dane["przesun"] = 25 * 60  # 0.13.0 (D1: A): from now on the server is 25 minutes later — the waiting is over

serwer = http.server.ThreadingHTTPServer(("127.0.0.1", 8443), Serwer)
kontekst = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
kontekst.load_cert_chain(f"{out}/akt-cert.pem", f"{out}/akt-klucz.pem")
serwer.socket = kontekst.wrap_socket(serwer.socket, server_side=True)
threading.Thread(target=serwer.serve_forever, daemon=True).start()

SIEC = "-netdev user,id=n0 -device e1000e,netdev=n0"
DYSKI = (f"-drive if=none,id=znak,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=znak,"
         f"serial=k1-test-aktualizacji,addr=0x1e -drive if=none,id=dysk,file={dysk},format=raw -device ide-hd,drive=dysk,bus=ide.0")

def uruchom(nazwa, opis=False):
    log, mon = f"{out}/akt-{nazwa}.log", f"{out}/akt-{nazwa}.sock"
    for f in (log, mon):
        if os.path.exists(f):
            os.remove(f)
    subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                      "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit) // 2),
                      "--pamiec", "2048", "--qemu", SIEC + " " + DYSKI + (OPIS if opis else "")] + emulacja)
    return Maszyna(log, mon, out, limit // 2, f"akt-{nazwa}")

# 0.10.7 step 5: the screen's description too (the test program's scenario; the disk of the test of updates stays)
OPIS = (f" -drive if=none,id=opis,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=opis,"
        "serial=k1-test-opis,addr=0x1d")

STAN = r"K1-INSTALATOR: aktualizacja przez sieć: działa (\S+), czeka (\S+), do pobrania (\S+)"
q = uruchom("1")
czekaj, sprawdz = q.czekaj, q.sprawdz
sprawdz("obraz testowy w trybie testu aktualizacji (dysk „k1-test-aktualizacji”)",
        czekaj(r"K1-NADZORCA: aktualizacja: test aktualizacji", czas=600))
s = czekaj(STAN, czas=300)
sprawdz("po starcie: program zapisujący czyta dysk — działa 0.0.1, nic nie czeka", s and s.group(1) == "0.0.1" and
        s.group(2) == "0.0.0", s.group(0) if s else "brak")
sprawdz("K1 sam sprawdza po połączeniu z siecią (strefa pobierania, curl, łącznik)",
        czekaj(r"K1-NADZORCA: aktualizacja: sprawdzam", czas=300))
o = czekaj(r"K1-INSTALATOR: ogłoszenie wydania: \d+ B, podpis i format poprawne", czas=900)
sprawdz("ogłoszenie z serwera testowego przyjęte (przekierowanie jak na GitHubie)", o)
s = czekaj(STAN, od=1, czas=60)
sprawdz(f"jest nowsza wersja do pobrania: {wersja}", s and s.group(3) == wersja, s.group(0) if s else "brak")
sprawdz("strefa pobierania nie ma okna (ekran jej nie zna)", "strefa bez okna" in q.tekst() or
        "bez kanału do ekranu" in q.tekst())
p = czekaj(r"K1-INSTALATOR: gotowe: (\S+) czeka w poczekalni", czas=1200)
sprawdz("paczka pobrana do poczekalni (skrót i podpisy sprawdzone)", p and p.group(1) == wersja, p.group(0) if p else "brak")
sprawdz("0.13.0 (D1: A): nowa wersja czeka 20 minut (zwłoka wydania) — K1 sprawdzi wydanie jeszcze raz",
        czekaj(r"K1-INSTALATOR: aktualizacja przez sieć: .* \(zwłoka wydania\)", czas=120) and
        czekaj(r"K1-NADZORCA: aktualizacja: nowa wersja czeka — sprawdzę wydanie jeszcze raz", czas=60))
pt = czekaj(r"K1-INSTALATOR: (\S+): wydanie wciąż jest najnowsze (\d+) min po pierwszym zobaczeniu — gotowe do instalacji",
            czas=900)
# the minutes come from the server's Date (25 min ahead after the package) through the zone's footer: the footer works
sprawdz("koniec zwłoki: K1 sprawdza jeszcze raz, wydanie wciąż jest (czas ze stopki strefy: serwer 25 min później)",
        pt and pt.group(1) == wersja and int(pt.group(2)) >= 20 and
        "koniec zwłoki wydania — sprawdzam" in q.tekst(), pt.group(0) if pt else "brak")
b = czekaj(r"K1-GUI: okienko \(jest nowa wersja\): Zainstaluj (\d+),(\d+); Później (\d+),(\d+)", czas=120)
sprawdz("okienko „Jest nowa wersja K1” na pulpicie — dopiero po zwłoce (NEGATYWNE: nie wcześniej)", b and pt and
        q.tekst().find("okienko (jest nowa wersja)") > q.tekst().find("wydanie wciąż jest najnowsze"))
if b:
    q.zrzut("2-okienko")
    q.klik(int(b.group(1)), int(b.group(2)))
sprawdz("„Zainstaluj”: program zapisujący zapisuje nową wersję do miejsca B",
        czekaj(r"K1-INSTALATOR: gotowe: \S+ w miejscu b — wystartuje na próbę", czas=600))
sprawdz("po zapisie K1 uruchamia się ponownie", czekaj(r"K1-NADZORCA: aktualizacja: nowa wersja zapisana — uruchamiam ponownie",
                                                       czas=60))
t = q.tekst()
sprawdz("w logu K1 żadnego adresu (niezmiennik 8)", "10.0.2.2" not in t and "github" not in t.lower())
q.czysty_log()
q.czekaj(r"K1-INIT: (wyłączam|uruchamiam ponownie)", czas=120)
# the disk after it all
env = czytaj(dysk, MIB + GENV, 1024).decode("ascii", "replace")
man_b = czytaj(dysk, MIB + SLOT_B + MAN, 48)
znak_p = czytaj(dysk, MIB + POCZ, 4)
sprawdz("dysk: grubenv „spróbuj miejsca B raz”", "k1_nowy=b" in env, env.split("\n")[1:5])
sprawdz(f"dysk: w miejscu B manifest wersji {wersja}", man_b[:4] == b"K1M1" and man_b[24:24 + len(wersja)] == wersja.encode(),
        man_b[24:40])
sprawdz("dysk: znak poczekalni wyczyszczony (nic już nie czeka)", znak_p != b"K1W1")
sprawdz("serwer: pytano tylko o ogłoszenie i paczkę", all(z.startswith(("/latest/download/", "/download/")) for z in dane["zapytania"]),
        dane["zapytania"])

# NEGATIVE: a changed byte in the package — refused, nothing waits, no window
with open(dysk, "r+b") as p:  # back to "0.0.1 runs, nothing tried"
    p.seek(MIB + GENV); p.write(GENV_PRZED)
    p.seek(MIB + SLOT_B + MAN); p.write(b"\0" * 1024)
dane["zla"] = True
q2 = uruchom("2")
q2.sprawdz("NEGATYWNE: paczka ze zmienionym bajtem odrzucona", q2.czekaj(
    r"K1-INSTALATOR: pobieranie: skrót paczki albo systemu niezgodny z podpisanym — odrzucona", czas=1800))
q2.sprawdz("NEGATYWNE: nic nie czeka, okienka nie ma", q2.cisza(r"okienko \(jest nowa wersja\)", 30) and
           "czeka w poczekalni" not in q2.tekst())
q2.czysty_log()
q.wyniki += q2.wyniki
q2.monitor("quit")

# 0.10.7 step 5: the disk runs this build's version — "masz najnowszą wersję"; Ustawienia → Aktualizacje twice and
# "Sprawdź teraz" (before 0.10.7 the view "Sprawdzono o …" ended Ustawienia: localtime_r opened /etc/localtime, code 159)
with open(dysk, "r+b") as p:
    p.seek(MIB + SLOT_A + MAN); p.write(MANIFEST_A)
dane["zla"] = False
dane["przesun"] = 8 * 86400  # 0.13.0 (T1): the server 8 days later — the announcement signed at the build is too old
q3 = uruchom("3", opis=True)
NAJNOWSZA = r"K1-USTAWIENIA: aktualizacje: sieć — najnowsza"
STARE = r"K1-USTAWIENIA: aktualizacje: sieć — ogłoszenie za stare"
KONIEC_UST = r"K1-NADZORCA: ustawienia zakończ"

def ustawienia_aktualizacje(raz, wzor=NAJNOWSZA, opis="„masz najnowszą wersję”"):
    n = q3.ile(wzor)
    q3.klik(*q3.srodek(q3.czekaj_na_element("Pasek", "Start", czas=60) or {"x": 0, "y": 0, "w": 0, "h": 0}))
    q3.klik_w("Menu Start", "Ustawienia")
    for _ in range(3):  # the list may still change under the click (Bateria goes when nadzorca says there is none)
        q3.klik_w("Ustawienia", "Aktualizacje", "zakladka", czas=60)
        if q3.czekaj_na(lambda: (q3.element("Ustawienia", "Aktualizacje", "zakladka") or {"stan": 0})["stan"] & 1, 10):
            break
    q3.sprawdz(f"Ustawienia → Aktualizacje ({raz}): {opis}", q3.czekaj(wzor, od=n, czas=60))
    q3.sprawdz(f"…i okno Ustawień żyje ({raz})", q3.cisza(KONIEC_UST, 10) and q3.element("Ustawienia") is not None,
               "\n".join(l for l in q3.tekst().splitlines() if "ustawienia zakończ" in l)[-300:])

q3.sprawdz("0.13.0 (T1), NEGATYWNE: serwer 8 dni później — ogłoszenie za stare, nic do pobrania", q3.czekaj(
    r"K1-INSTALATOR: zegar K1 różni się od czasu serwera HTTPS o \d+ min — terminy liczę według serwera", czas=1200) and
    q3.czekaj(r"K1-INSTALATOR: ogłoszenie wydania: podpisane \d+ dni temu — za stare \(termin 7 dni\)", czas=120) and
    q3.czekaj(r"K1-INSTALATOR: aktualizacja przez sieć: działa \S+, czeka \S+, do pobrania 0\.0\.0", od=1, czas=120))
ustawienia_aktualizacje("pierwsze otwarcie", STARE, "„Nie mogę sprawdzić, czy masz najnowszą wersję”")
q3.zrzut("3-za-stare")
dane["przesun"] = 0
n, ns = q3.ile(NAJNOWSZA), q3.ile(r"K1-NADZORCA: aktualizacja: sprawdzam")
q3.klik_w("Ustawienia", "Sprawdź teraz", "przycisk")
q3.sprawdz("serwer znów na czas, „Sprawdź teraz”: ta sama wersja na dysku — nic do pobrania, „masz najnowszą wersję”",
           q3.czekaj(r"K1-NADZORCA: aktualizacja: sprawdzam", od=ns, czas=60) and q3.czekaj(
               r"K1-INSTALATOR: ogłoszenie wydania: \d+ B, podpis i format poprawne", czas=900) and
           q3.czekaj(NAJNOWSZA, od=n, czas=900))
q3.klik_w("Ustawienia", "Zamknij", "przycisk")
q3.sprawdz("Ustawienia zamknięte przyciskiem", q3.czekaj(KONIEC_UST + r"\S* pracę, kod 0", czas=30))
ustawienia_aktualizacje("drugie otwarcie")
n, ns = q3.ile(NAJNOWSZA), q3.ile(r"K1-NADZORCA: aktualizacja: sprawdzam")
q3.klik_w("Ustawienia", "Sprawdź teraz", "przycisk")
q3.sprawdz("„Sprawdź teraz”: K1 sprawdza jeszcze raz", q3.czekaj(r"K1-NADZORCA: aktualizacja: sprawdzam", od=ns, czas=60))
q3.sprawdz("…potem znów „masz najnowszą wersję”, okno Ustawień żyje", q3.czekaj(NAJNOWSZA, od=n, czas=900) and
           q3.cisza(KONIEC_UST, 10) and q3.element("Ustawienia") is not None)
q3.zrzut("3-najnowsza")
q3.czysty_log()
q.wyniki += q3.wyniki
q3.monitor("quit")
q.koniec(wylacz=False)
PY

K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/aktualizacja.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$DYSK" "$ZNAK" \
    "$PACZKA" "$OGL" "$K1_WERSJA" $EMULACJA
r=$?
rm -f "$DYSK" "$TESTY"/akt-klucz.pem
if [ $r -eq 0 ]; then info "aktualizacja przez sieć: wszystko zaliczone (logi: $TESTY/akt-*.log)"; else echo "[k1] aktualizacja przez sieć: BŁĄD (logi: $TESTY/akt-*.log)"; fi
exit $r

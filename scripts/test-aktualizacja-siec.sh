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
# Bez KVM (emulacja QEMU, kontener) — ten sam pełny scenariusz (2026-11-18: ok. 8 min, paczka 130 MB też).
# Dowody: build/testy/akt-*.log, akt-*.png.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1500}
mkdir -p "$TESTY"; rm -f "$TESTY"/akt-*
CMDLINE="$(cat "$K1_REPO/jadro/cmdline")"
ESP=$K1_BUDOWA/usb/esp.img
PACZKA=$K1_WYNIK/k1-$K1_WERSJA.k1a
OGL=$K1_WYNIK/k1-ogloszenie.bin
for f in "$ESP" "$PACZKA" "$OGL" "$K1_WYNIK/rdzen-test.squashfs"; do [ -f "$f" ] || blad "brak $f — najpierw scripts/zbuduj.sh"; done
command -v openssl >/dev/null || blad "brak openssl (certyfikat serwera testowego)"
EMULACJA=--emulacja
if [ -w /dev/kvm ] && grep -qsE '^(1|Y)$' /sys/module/kvm_intel/parameters/nested /sys/module/kvm_amd/parameters/nested; then
    EMULACJA=
fi

# The disk: GPT as the Instalator writes it (src/instalator/gpt.c): K1-EFI 512 MiB at 1 MiB, K1-DANE after it
DYSK=$TESTY/akt-dysk.img
rm -f "$DYSK"; truncate -s 1100M "$DYSK"
sfdisk -q "$DYSK" >/dev/null <<TAB || blad "sfdisk"
label: gpt
start=2048, size=1048576, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B, name="K1-EFI"
start=1050624, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="K1-DANE"
TAB
dd if="$ESP" of="$DYSK" bs=1M seek=1 conv=notrunc status=none
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

def uklad():  # the K1-EFI partition's layout block (k1_uklad.h): grubenv, slot A, slot B
    b = czytaj(dysk, MIB + 1024, 512)
    return struct.unpack_from("<Q", b, 16)[0], struct.unpack_from("<Q", b, 32)[0], struct.unpack_from("<Q", b, 40)[0]

GENV, SLOT_A, SLOT_B = uklad()
GENV_PRZED = czytaj(dysk, MIB + GENV, 1024)  # for the negative run: the disk as before the update
# the running slot A says 0.0.1 (k1_manifest.h: version at 8, its text at 24) — the build's version is newer
with open(dysk, "r+b") as p:
    p.seek(MIB + SLOT_A + 127 * MIB + 8); p.write(struct.pack("<I", 1))
    p.seek(MIB + SLOT_A + 127 * MIB + 24); p.write(b"0.0.1".ljust(24, b"\0"))

dane = {"zla": False, "zapytania": []}
class Serwer(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass
    def do_GET(self):
        dane["zapytania"].append(self.path)
        if self.path == "/latest/download/k1-ogloszenie.bin":  # as GitHub: to the newest release
            self.send_response(302)
            self.send_header("Location", f"/download/k1-v{wersja}/k1-ogloszenie.bin")
            self.end_headers()
            return
        pliki = {f"/download/k1-v{wersja}/k1-ogloszenie.bin": ogl, f"/download/k1-v{wersja}/k1-{wersja}.k1a": paczka}
        if self.path not in pliki:
            self.send_response(404); self.end_headers(); return
        b = open(pliki[self.path], "rb").read()
        if dane["zla"] and self.path.endswith(".k1a"):
            b = bytearray(b); b[8192 + 4096] ^= 0x40; b = bytes(b)  # a byte of the system: the signed digest fails
        self.send_response(200)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

serwer = http.server.ThreadingHTTPServer(("127.0.0.1", 8443), Serwer)
kontekst = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
kontekst.load_cert_chain(f"{out}/akt-cert.pem", f"{out}/akt-klucz.pem")
serwer.socket = kontekst.wrap_socket(serwer.socket, server_side=True)
threading.Thread(target=serwer.serve_forever, daemon=True).start()

SIEC = "-netdev user,id=n0 -device e1000e,netdev=n0"
DYSKI = (f"-drive if=none,id=znak,file={znak},format=raw,readonly=on -device virtio-blk-pci,drive=znak,"
         f"serial=k1-test-aktualizacji,addr=0x1e -drive if=none,id=dysk,file={dysk},format=raw -device ide-hd,drive=dysk,bus=ide.0")

def uruchom(nazwa):
    log, mon = f"{out}/akt-{nazwa}.log", f"{out}/akt-{nazwa}.sock"
    for f in (log, mon):
        if os.path.exists(f):
            os.remove(f)
    subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                      "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit) // 2),
                      "--pamiec", "2048", "--qemu", SIEC + " " + DYSKI] + emulacja)
    return Maszyna(log, mon, out, limit // 2, f"akt-{nazwa}")

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
b = czekaj(r"K1-GUI: okienko \(jest nowa wersja\): Zainstaluj (\d+),(\d+); Później (\d+),(\d+)", czas=120)
sprawdz("okienko „Jest nowa wersja K1” na pulpicie", b)
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
man_b = czytaj(dysk, MIB + SLOT_B + 127 * MIB, 48)
znak_p = czytaj(dysk, MIB + 280 * MIB, 4)
sprawdz("dysk: grubenv „spróbuj miejsca B raz”", "k1_nowy=b" in env, env.split("\n")[1:5])
sprawdz(f"dysk: w miejscu B manifest wersji {wersja}", man_b[:4] == b"K1M1" and man_b[24:24 + len(wersja)] == wersja.encode(),
        man_b[24:40])
sprawdz("dysk: znak poczekalni wyczyszczony (nic już nie czeka)", znak_p != b"K1W1")
sprawdz("serwer: pytano tylko o ogłoszenie i paczkę", all(z.startswith(("/latest/download/", "/download/")) for z in dane["zapytania"]),
        dane["zapytania"])

# NEGATIVE: a changed byte in the package — refused, nothing waits, no window
with open(dysk, "r+b") as p:  # back to "0.0.1 runs, nothing tried"
    p.seek(MIB + GENV); p.write(GENV_PRZED)
    p.seek(MIB + SLOT_B + 127 * MIB); p.write(b"\0" * 1024)
dane["zla"] = True
q2 = uruchom("2")
q2.sprawdz("NEGATYWNE: paczka ze zmienionym bajtem odrzucona", q2.czekaj(
    r"K1-INSTALATOR: pobieranie: skrót paczki albo systemu niezgodny z podpisanym — odrzucona", czas=1800))
q2.sprawdz("NEGATYWNE: nic nie czeka, okienka nie ma", q2.cisza(r"okienko \(jest nowa wersja\)", 30) and
           "czeka w poczekalni" not in q2.tekst())
q2.czysty_log()
q.wyniki += q2.wyniki
q2.monitor("quit")
q.koniec(wylacz=False)
PY

K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/aktualizacja.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$DYSK" "$ZNAK" \
    "$PACZKA" "$OGL" "$K1_WERSJA" $EMULACJA
r=$?
rm -f "$DYSK" "$TESTY"/akt-klucz.pem
if [ $r -eq 0 ]; then info "aktualizacja przez sieć: wszystko zaliczone (logi: $TESTY/akt-*.log)"; else echo "[k1] aktualizacja przez sieć: BŁĄD (logi: $TESTY/akt-*.log)"; fi
exit $r

#!/bin/sh
# Test Wi-Fi (0.9.1 krok 1, ADR 0041; powtarzalny): QEMU nie ma kart Wi-Fi, więc jądro startuje z udawanymi kartami
# (mac80211_hwsim.radios=7 — wydanie ma 0) i z obrazem testowym: pierwsza karta jest „kartą komputera” dla nadzorcy, na
# sześciu pozostałych program testowy robi punkty dostępu testowe (testy/system/t_wifi.c), każdy ogłasza jedną sieć:
# „K1-TEST” (WPA2), „Kawiarnia” (otwarta), nazwę ze znakiem nowej linii i sekwencją sterującą (WPA2/WPA3), nazwę
# 32-bajtową (WPA3), nazwę ze złym UTF-8 i znakiem odwracającym kierunek (WPA2) i sieć z zepsutym opisem zabezpieczeń.
#   1. jądro: baza przepisów radiowych wbudowana i sprawdzona podpisem; nadzorca znajduje kartę Wi-Fi i włącza radio
#   2. nadzorca szuka sieci (nl80211), opisy z powietrza idą do usługi siec (w klatce): 5 sieci, 1 opis odrzucony —
#      usługa działa dalej, żadna nazwa sieci nie trafia do logu
#   3. górny pasek: ikona Wi-Fi „nie połączono” (wachlarz z krzyżykiem); Ustawienia → Sieć → Wi-Fi: lista 5 sieci
#      (4 z hasłem, 1 otwarta), zrzuty w jasnym i ciemnym motywie: build/testy/wifi-*.png
#   4. przełącznik w Ustawieniach wyłącza radio (rfkill): lista znika, ikona „Wi-Fi wyłączone”; włącza — lista wraca
#   5. zakładka „Karty sieciowe”: kafelek karty Wi-Fi z plakietką „Włączone”
# 0.9.1 krok 2 (łączenie; „K1-TEST” i „Kawiarnia” przyjmują komputer — punkt dostępu testowy, część 2: t_wifi_ap.c):
#   6. „Kawiarnia” (otwarta): kliknięcie → „Połącz” → adres od routera testowego (DHCP przez Wi-Fi), ikona z kreskami
#   7. „K1-TEST” (WPA2): złe hasło → router nie przyjmuje wiadomości 2, „Złe hasło — spróbuj jeszcze raz”
#   8. dobre hasło → logowanie WPA2, klucze po obu stronach, adres od routera (dane płyną zaszyfrowane), „Połączono”;
#      ramka EAPOL za krótka i podrobiona wiadomość 3 (zły MIC) od routera testowego — odrzucone, siec działa dalej
#   9. „Kawiarnia” → „Zapomnij tę sieć”
#  10. ponowny start z tym samym dyskiem (K1-DANE): K1 łączy się sam z zapamiętaną „K1-TEST”
#  hasła i nazwy sieci nie ma w żadnym logu
# Dowody: build/testy/wifi.log i wifi-2.log (logi K1), zrzuty. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1500}
mkdir -p "$TESTY"; rm -f "$TESTY"/wifi*
DANE=$TESTY/wifi-dane.img  # 0.9.1 step 2: K1-DANE, so that the remembered network survives a restart
dd if=/dev/zero of="$DANE" bs=1M count=16 status=none
printf 'label: gpt\nstart=2048, size=30000, name="K1-DANE"\n' | sfdisk -q "$DANE" >/dev/null
CMDLINE="$(sed 's/mac80211_hwsim\.radios=0/mac80211_hwsim.radios=7/' "$K1_REPO/jadro/cmdline")"
echo "$CMDLINE" | grep -q 'radios=7' || blad "jadro/cmdline: brak mac80211_hwsim.radios=0"

cat > "$K1_BUDOWA/wifi.py" <<'PY'
import os, re, subprocess, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline, dane = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], sys.argv[5]
log, mon = f"{out}/wifi.log", f"{out}/wifi.sock"
DYSK = f"-drive if=none,id=dane,file={dane},format=raw -device ide-hd,drive=dane,bus=ide.0"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit)), "--qemu", DYSK])
q = Maszyna(log, mon, out, limit, "wifi")
czekaj, sprawdz = q.czekaj, q.sprawdz
LISTA = r"K1-USTAWIENIA: wifi: {stan}; sieci {n} \(z hasłem {h}, otwarte {o}, inne 0\)(?:; szukam)?; " \
        r"wiersze od (\d+),(\d+) co (\d+); przełącznik (\d+),(\d+)"


def jasny(c):
    return min(c) > 200


def ikona(stan, n=0, czas=60):
    m = czekaj(rf"K1-GUI: sieć: {re.escape(stan)} \(ikona w (\d+),(\d+)\)", od=n, czas=czas)
    return (int(m.group(1)), int(m.group(2))) if m else None


def w_ikonie(px, x, y, warunek):
    return any(warunek(px(x + i, y + j)) for i in range(16) for j in range(16))


sprawdz("jądro: baza przepisów radiowych wbudowana i sprawdzona kluczem z jądra",
        czekaj(r"Loaded X\.509 cert 'sforshee", czas=limit * 0.3) and "regulatory.db" not in
        "".join(l for l in q.tekst().splitlines() if "failed" in l.lower() or "error" in l.lower()))
sprawdz("punkt dostępu testowy: 6 sieci na udawanych kartach",
        czekaj(r"K1-TEST: punkt dostępu testowy gotowy \(6 sieci z 6\)", czas=limit * 0.5))
sprawdz("nadzorca: karta Wi-Fi (pierwsza udawana) i jej radio włączone",
        czekaj(r"K1-NADZORCA: wifi: karta Wi-Fi wlan0 \(wirtualna 0000:0000\)", czas=60) and
        czekaj(r"K1-NADZORCA: wifi: radio włączone \(wlan0\)", czas=30))
sprawdz("usługa siec: 5 sieci w pobliżu, opis z zepsutym zabezpieczeniem odrzucony",
        czekaj(r"K1-SIEC: sieci Wi-Fi w pobliżu: 5 \(punktów dostępu 6, ukryte 0, odrzucone opisy 1\)", czas=240))
g = czekaj(r"K1-GUI: pulpit gotowy: Start (\d+),(\d+), aplikacje od (\d+),(\d+) co (\d+)", czas=60)
ax, ay, krok = (int(g.group(i)) for i in (3, 4, 5)) if g else (0, 0, 0)
i = ikona("Wi-Fi nie połączono")
w, h, px = q.zrzut("1-ikona-nie-polaczono", gdy=lambda w, h, px: i and jasny(px(i[0] + 13, i[1] + 13))) if i else (0, 0, None)
sprawdz("górny pasek: ikona Wi-Fi „nie połączono” — przygaszony wachlarz z małym krzyżykiem (zrzut wifi-1-ikona-nie-polaczono)",
        i and jasny(px(i[0] + 13, i[1] + 13)), str(i))

q.klik(ax + 2 * krok, ay)
u = czekaj(r"K1-EKRAN: okno (\d+) \(ustawienia\) (\d+)x(\d+) w (\d+),(\d+)", czas=60)
sk = czekaj(r"K1-USTAWIENIA: układ: sekcje od (\d+),(\d+) co (\d+); przyciski od (\d+),(\d+) \(szer\. (\d+)", czas=30)
ux, uy = (int(u.group(4)), int(u.group(5))) if u else (0, 0)
SIEC_NR, WYGLAD_NR = 8, 0


def sekcja(nr):
    q.klik(ux + int(sk.group(1)), uy + int(sk.group(2)) + nr * int(sk.group(3)))


if u and sk:
    sekcja(SIEC_NR)
z = czekaj(r"K1-USTAWIENIA: sieć: zakładki od (\d+),(\d+) \(szer\. (\d+), wys\. (\d+)\)", czas=30)
m = czekaj(LISTA.format(stan="włączone", n=5, h=4, o=1), czas=120)
sprawdz("Ustawienia → Sieć: zakładka Wi-Fi (pierwsza, bo jest karta Wi-Fi), lista 5 sieci: 4 z hasłem, 1 otwarta",
        z and m)
wx, wy = (ux + int(m.group(1)) + 6, uy + int(m.group(2))) if m else (0, 0)
w, h, px = q.zrzut("2-ustawienia-jasny", gdy=lambda w, h, px: m and min(px(wx, wy)) > 240)
sprawdz("Ustawienia → Sieć → Wi-Fi w jasnym motywie (zrzut wifi-2-ustawienia-jasny)", m and min(px(wx, wy)) > 240)

WYL = (r"K1-NADZORCA: wifi: radio wyłączone w Ustawieniach \(wlan0\)", r"K1-USTAWIENIA: wifi: wyłączone; sieci 0",
       r"K1-GUI: sieć: Wi-Fi wyłączone")
n = [q.ile(w) for w in WYL]
if m:
    q.klik(ux + int(m.group(4)), uy + int(m.group(5)))
sprawdz("przełącznik wyłącza radio: nadzorca (rfkill), lista znika, ikona „Wi-Fi wyłączone”",
        all(czekaj(w, od=k, czas=30) for w, k in zip(WYL, n)))
q.zrzut("3-wylaczone")
WL = (r"K1-NADZORCA: wifi: radio włączone \(wlan0\)", LISTA.format(stan="włączone", n=5, h=4, o=1),
      r"K1-GUI: sieć: Wi-Fi nie połączono")
n = [q.ile(w) for w in WL]
if m:
    q.klik(ux + int(m.group(4)), uy + int(m.group(5)))
sprawdz("przełącznik włącza radio: K1 od razu szuka sieci, lista wraca",
        all(czekaj(w, od=k, czas=180) for w, k in zip(WL, n)))

if u and sk:  # the dark theme: Wygląd → Ciemny, back to Sieć
    sekcja(WYGLAD_NR)
    q.klik(ux + int(sk.group(4)) + int(sk.group(6)) * 3 // 4, uy + int(sk.group(5)) + 10)
    czekaj(r"K1-EKRAN: ustawienie 1=1", czas=30)
    sekcja(SIEC_NR)
w, h, px = q.zrzut("4-ustawienia-ciemny", gdy=lambda w, h, px: m and max(px(wx, wy)) < 70)
sprawdz("Ustawienia → Sieć → Wi-Fi w ciemnym motywie (zrzut wifi-4-ustawienia-ciemny)", m and max(px(wx, wy)) < 70)

KARTY = r"K1-USTAWIENIA: sieć: Karta Wi-Fi \(wirtualna\): Włączone \(kafelki od"
n = q.ile(KARTY)
if z:
    q.klik(ux + int(z.group(1)) + int(z.group(3)) * 3 // 4, uy + int(z.group(2)) + int(z.group(4)) // 2)
sprawdz("zakładka „Karty sieciowe”: kafelek karty Wi-Fi z plakietką „Włączone”",
        czekaj(KARTY, od=n, czas=30))
q.zrzut("5-karty")

# ---- 0.9.1 step 2: joining ----
PELNA = r"K1-USTAWIENIA: wifi: włączone; sieci 5 \(z hasłem 4, otwarte 1, inne 0\)(?:; szukam)?; wiersze od (\d+),(\d+) " \
        r"co (\d+); przełącznik (\d+),(\d+); rodzaje ([\d,]+); połączenie (\d) \(rząd (-?\d+)\)"
ROZW = r"K1-USTAWIENIA: wifi: rząd {} otwarty \(rodzaj {}\); pole (\d+),(\d+); Połącz (\d+),(\d+); Zapomnij (\d+),(\d+)"
OTWARTA, WPA2 = 1, 2                                # K1_WIFI_* of the rows
R_HASLO, R_OTWARTA, R_ZAPAMIETANA = 0, 1, 2          # what an open row offers (wifi_haslo.c)


def lista():
    ms = list(re.finditer(PELNA, q.tekst()))
    return ms[-1] if ms else None


def rzad(rodzaj):
    """Where the row of the network of this kind is now (the list may reorder): its index and centre."""
    L = lista()
    if not L or str(rodzaj) not in L.group(6).split(","):
        return None, 0, 0
    r = L.group(6).split(",").index(str(rodzaj))
    return r, ux + int(L.group(1)) + 90, uy + int(L.group(2)) + r * int(L.group(3))


def otworz(rodzaj, co):
    q.pisz("esc")  # an open row closes first (with it open the list may be scrolled)
    r, x, y = rzad(rodzaj)
    if r is None:
        return None
    n = q.ile(ROZW.format(r, co))
    q.klik(x, y)
    return czekaj(ROZW.format(r, co), od=n, czas=30)


def polaczenie(stan, n=0, czas=120):
    return czekaj(PELNA.replace(r"połączenie (\d)", f"połączenie {stan}"), od=n, czas=czas)


if z:  # back to the Wi-Fi tab
    q.klik(ux + int(z.group(1)) + int(z.group(3)) // 4, uy + int(z.group(2)) + int(z.group(4)) // 2)
POL = (r"K1-TEST: punkt dostępu testowy: sieć 2 — stacja połączona", r"K1-SIEC: połączono: adres od routera ustawiony",
       r"K1-GUI: sieć: Wi-Fi połączono, kreski [1-4]")
n = [q.ile(w) for w in POL] + [q.ile(PELNA.replace(r"połączenie (\d)", "połączenie 2"))]
o = otworz(OTWARTA, R_OTWARTA)
if o:
    q.klik(ux + int(o.group(3)), uy + int(o.group(4)))
sprawdz("„Kawiarnia” (otwarta): wiersz z „Połącz” i napisem o sieci niezabezpieczonej; po kliknięciu — połączenie, adres "
        "od routera testowego (DHCP przez Wi-Fi), ikona Wi-Fi z kreskami, w Ustawieniach „Połączono”",
        o and all(czekaj(w, od=k, czas=180) for w, k in zip(POL, n)) and polaczenie(2, n[3], 60))
q.zrzut("6-polaczono-otwarta")

ZLE = (r"K1-TEST: punkt dostępu testowy: sieć 1 — wiadomość 2 ze złym MIC \(złe hasło\)",
       r"K1-TEST: punkt dostępu testowy: sieć 1 — stacja odłączona", r"K1-SIEC: wifi: sieć nie przyjęła hasła")
n = [q.ile(w) for w in ZLE] + [q.ile(PELNA.replace(r"połączenie (\d)", "połączenie 3"))]
h = otworz(WPA2, R_HASLO)
if h:
    q.pisz("z", "l", "e", "h", "a", "s", "l", "o", "1", "ret")
sprawdz("„K1-TEST” (WPA2), złe hasło: router nie przyjmuje wiadomości 2 i odłącza komputer; Ustawienia: „Złe hasło — "
        "spróbuj jeszcze raz” (zrzut wifi-7-zle-haslo)",
        h and all(czekaj(w, od=k, czas=180) for w, k in zip(ZLE, n)) and polaczenie(3, n[3], 60))
q.zrzut("7-zle-haslo")

DOBRE = (r"K1-TEST: punkt dostępu testowy: sieć 1 — logowanie WPA2 udane, klucze wpisane",
         r"K1-NADZORCA: wifi: połączono — klucze wpisane, dane płyną",
         r"K1-TEST: punkt dostępu testowy: sieć 1 — DHCP: adres dla stacji", r"K1-SIEC: połączono: adres od routera ustawiony",
         r"K1-GUI: sieć: Wi-Fi połączono, kreski [1-4]", r"K1-SIEC: wifi: sieć zapamiętana \(razem 2\)",
         r"K1-SIEC: wifi: odrzucone ramki EAPOL: [1-9]")
n = [q.ile(w) for w in DOBRE] + [q.ile(PELNA.replace(r"połączenie (\d)", "połączenie 2"))]
if h:
    q.pisz("h", "a", "s", "l", "o", "minus", "t", "e", "s", "t", "o", "w", "e", "ret")
ok = h and all(czekaj(w, od=k, czas=240) for w, k in zip(DOBRE, n)) and polaczenie(2, n[-1], 60)
L = lista()
sprawdz("dobre hasło: logowanie WPA2 (wymiana czterech wiadomości), klucze po obu stronach, adres od routera — dane płyną "
        "zaszyfrowane w obie strony; sieć zapamiętana; „Połączono” przy „K1-TEST”; ramka za krótka i podrobiona wiadomość 3 "
        "(zły MIC) odrzucone, siec żyje (zrzut wifi-8-polaczono)",
        ok and L and L.group(8) == str(rzad(WPA2)[0]))
q.zrzut("8-polaczono")

ZAP = r"K1-SIEC: wifi: sieć zapomniana \(zostało 1\)"
n = q.ile(ZAP)
o = otworz(OTWARTA, R_ZAPAMIETANA)
if o:
    q.klik(ux + int(o.group(5)), uy + int(o.group(6)))
sprawdz("„Kawiarnia” → „Zapomnij tę sieć”: zapomniana (została zapamiętana „K1-TEST”)", o and czekaj(ZAP, od=n, czas=60))

t_ = q.tekst()
sprawdz("w logu nie ma nazw sieci (to sieci sąsiadów) ani hasła",
        "Kawiarnia" not in t_ and "nazwa-z-32" not in t_ and "31mnazwa" not in t_ and "haslo-testowe" not in t_ and
        "zlehaslo" not in t_)
sprawdz("brak 'Kernel panic', zablokowanych wywołań, odrzuconych komunikatów i restartu usługi sieci",
        "Kernel panic" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_ and
        "siec zakończył" not in t_ and "BŁĄD" not in t_)
q.monitor("quit")
qemu.wait()

# ---- the second start with the same K1-DANE: the remembered network by itself ----
log2, mon2 = f"{out}/wifi-2.log", f"{out}/wifi-2.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log2, "--monitor", mon2, "--czas", str(int(limit)), "--qemu", DYSK])
q2 = Maszyna(log2, mon2, out, limit, "wifi-2")
q2.wyniki = q.wyniki
sprawdz = q2.sprawdz
sprawdz("ponowny start: zapamiętana sieć wczytana ze skarbca, K1 łączy się sam z „K1-TEST” (logowanie WPA2, adres, ikona)",
        q2.czekaj(r"K1-SIEC: wifi: zapamiętane sieci: 1", czas=limit * 0.5) and
        q2.czekaj(r"K1-SIEC: wifi: zapamiętana sieć w zasięgu — łączę sam", czas=300) and
        q2.czekaj(r"K1-TEST: punkt dostępu testowy: sieć 1 — logowanie WPA2 udane", czas=180) and
        q2.czekaj(r"K1-SIEC: połączono: adres od routera ustawiony", czas=180) and
        q2.czekaj(r"K1-GUI: sieć: Wi-Fi połączono, kreski [1-4]", czas=60))
q2.zrzut("9-sam-po-starcie")
t_ = q2.tekst()
sprawdz("po ponownym starcie: w logu nie ma hasła ani nazw sieci; brak zablokowanych wywołań",
        "haslo-testowe" not in t_ and "Kawiarnia" not in t_ and "zabronione wywołanie" not in t_ and "BŁĄD" not in t_)
q2.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test Wi-Fi (0.9.1): udawane karty, punkty dostępu testowe, lista sieci, przełącznik, łączenie, hasło, zapamiętane"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/wifi.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" "$DANE" || r=1
if [ $r -eq 0 ]; then info "Wi-Fi: wszystko zaliczone"; else echo "[k1] Wi-Fi: BŁĄD (log: $TESTY/wifi.log)"; fi
exit $r

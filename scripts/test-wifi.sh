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
# Dowody: build/testy/wifi.log (log K1), zrzuty. Wymaga: scripts/zbuduj.sh.
. "$(dirname "$0")/wspolne.sh"
TESTY=$K1_BUDOWA/testy
CZAS=${K1_TEST_CZAS:-1500}
mkdir -p "$TESTY"; rm -f "$TESTY"/wifi*
CMDLINE="$(sed 's/mac80211_hwsim\.radios=0/mac80211_hwsim.radios=7/' "$K1_REPO/jadro/cmdline")"
echo "$CMDLINE" | grep -q 'radios=7' || blad "jadro/cmdline: brak mac80211_hwsim.radios=0"

cat > "$K1_BUDOWA/wifi.py" <<'PY'
import os, re, subprocess, sys
sys.path.insert(0, os.environ["K1_TESTY_PY"])
from k1qemu import Maszyna
repo, out, limit, cmdline = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
log, mon = f"{out}/wifi.log", f"{out}/wifi.sock"
qemu = subprocess.Popen([f"{repo}/scripts/uruchom-qemu.sh", "--obraz", f"{repo}/build/wynik/rdzen-test.squashfs",
                         "--cmdline", cmdline, "--log", log, "--monitor", mon, "--czas", str(int(limit))])
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

t_ = q.tekst()
sprawdz("w logu nie ma nazw sieci (to sieci sąsiadów)",
        "Kawiarnia" not in t_ and "nazwa-z-32" not in t_ and "31mnazwa" not in t_)
sprawdz("brak 'Kernel panic', zablokowanych wywołań, odrzuconych komunikatów i restartu usługi sieci",
        "Kernel panic" not in t_ and "zabronione wywołanie" not in t_ and "odrzucono" not in t_ and
        "siec zakończył" not in t_ and "BŁĄD" not in t_)
q.monitor("quit")
qemu.wait()
sys.exit(0 if all(q.wyniki) else 1)
PY

echo "[k1] test Wi-Fi (0.9.1): udawane karty, punkty dostępu testowe, lista sieci, przełącznik"
r=0
K1_TESTY_PY="$K1_REPO/testy/qemu" python3 -I "$K1_BUDOWA/wifi.py" "$K1_REPO" "$TESTY" "$CZAS" "$CMDLINE" || r=1
if [ $r -eq 0 ]; then info "Wi-Fi: wszystko zaliczone"; else echo "[k1] Wi-Fi: BŁĄD (log: $TESTY/wifi.log)"; fi
exit $r
